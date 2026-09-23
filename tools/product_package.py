#!/usr/bin/env python3
"""Build and verify the deliberately small product.pkg v1 host format."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tarfile
from io import BytesIO
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa


DOMAIN = b"ESP-CONTAINER-PRODUCT-V1\x00"
FORMAT_VERSION = 1
SIGNATURE_ALGORITHM = "rsa-3072-pss-sha256"
MEMBERS = ("manifest.json", "signature.bin", "app.wasm")
MANIFEST_MAX_BYTES = 4096
SIGNATURE_BYTES = 384
DEFAULT_MAX_WASM_BYTES = 512 * 1024  # Host prototype only; C3 limit needs P6-03.
IDENTIFIER = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
LIMIT_KEYS = {
    "memory_limit_bytes",
    "stack_limit_bytes",
    "event_queue_limit",
    "instruction_budget",
    "host_call_timeout_ms",
    "storage_limit_bytes",
}
MANIFEST_KEYS = {
    "package_format_version",
    "product_id",
    "product_version",
    "guest_abi_version",
    "required_capabilities",
    "runtime_profile",
    "limits",
    "data_schema_version",
    "payload",
    "signature_algorithm",
    "signing_key_id",
}


class PackageError(ValueError):
    pass


def _unique_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"重复 JSON 键：{key}")
        result[key] = value
    return result


def _json_bytes(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
                      allow_nan=False).encode("utf-8")


def _manifest(data: bytes) -> dict[str, object]:
    if not 0 < len(data) <= MANIFEST_MAX_BYTES:
        raise PackageError("manifest 大小超限")
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_unique_pairs,
                           parse_constant=lambda _: (_ for _ in ()).throw(PackageError("非法数值")))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise PackageError("manifest 不是合法 UTF-8 JSON") from exc
    if not isinstance(value, dict) or set(value) != MANIFEST_KEYS:
        raise PackageError("manifest 字段不完整或含未知字段")
    if data != _json_bytes(value):
        raise PackageError("manifest 必须采用唯一规范编码")
    if type(value["package_format_version"]) is not int or value["package_format_version"] != FORMAT_VERSION:
        raise PackageError("不支持的包格式")
    for key in ("product_id", "product_version", "runtime_profile", "signing_key_id"):
        if not isinstance(value[key], str) or not IDENTIFIER.fullmatch(value[key]):
            raise PackageError(f"非法 {key}")
    for key in ("guest_abi_version", "data_schema_version"):
        if type(value[key]) is not int or not 0 < value[key] <= 0xFFFFFFFF:
            raise PackageError(f"非法 {key}")
    caps = value["required_capabilities"]
    if (not isinstance(caps, list) or len(caps) > 16 or
            any(not isinstance(cap, str) or not IDENTIFIER.fullmatch(cap) for cap in caps) or
            caps != sorted(set(caps))):
        raise PackageError("非法 required_capabilities")
    limits = value["limits"]
    if not isinstance(limits, dict) or set(limits) != LIMIT_KEYS:
        raise PackageError("非法 limits 字段")
    for key, number in limits.items():
        lower = 0 if key == "storage_limit_bytes" else 1
        if type(number) is not int or not lower <= number <= 0xFFFFFFFF:
            raise PackageError(f"非法限制：{key}")
    payload = value["payload"]
    if not isinstance(payload, dict) or set(payload) != {"path", "size_bytes", "sha256"}:
        raise PackageError("非法 payload 字段")
    if payload["path"] != "app.wasm":
        raise PackageError("非法 payload 路径")
    if type(payload["size_bytes"]) is not int or payload["size_bytes"] < 8:
        raise PackageError("非法 payload 长度")
    if not isinstance(payload["sha256"], str) or not SHA256.fullmatch(payload["sha256"]):
        raise PackageError("非法 payload 摘要")
    if value["signature_algorithm"] != SIGNATURE_ALGORITHM:
        raise PackageError("不支持的签名算法")
    return value


def _wasm_u32(data: bytes, offset: int, end: int) -> tuple[int, int]:
    result = 0
    for index in range(5):
        if offset >= end:
            raise PackageError("截断的 Wasm 整数")
        octet = data[offset]
        offset += 1
        if index == 4 and octet & 0xF0:
            raise PackageError("Wasm 整数溢出")
        result |= (octet & 0x7F) << (7 * index)
        if not octet & 0x80:
            return result, offset
    raise PackageError("Wasm 整数溢出")


def _wasm(data: bytes) -> None:
    if not data.startswith(b"\x00asm\x01\x00\x00\x00"):
        raise PackageError("不是标准 Wasm v1 模块")
    offset = 8
    last_section = 0
    while offset < len(data):
        section = data[offset]
        offset += 1
        size, offset = _wasm_u32(data, offset, len(data))
        if section > 12 or section == 8:
            raise PackageError("不允许 Wasm start 或未知 section")
        if section:
            if section <= last_section:
                raise PackageError("Wasm section 重复或乱序")
            last_section = section
        if size > len(data) - offset:
            raise PackageError("截断的 Wasm section")
        end = offset + size
        if section == 2:
            count, offset = _wasm_u32(data, offset, end)
            if count:
                raise PackageError("当前 profile 不允许 Wasm imports")
            if offset != end:
                raise PackageError("非法 Wasm import section")
        elif section == 7:
            count, offset = _wasm_u32(data, offset, end)
            for _ in range(count):
                length, offset = _wasm_u32(data, offset, end)
                if length > end - offset:
                    raise PackageError("截断的 Wasm export")
                name = data[offset:offset + length]
                if name in (b"__post_instantiate", b"__wasm_call_ctors", b"_initialize"):
                    raise PackageError("当前 profile 不允许自动构造入口")
                offset += length
                if offset >= end or data[offset] > 3:
                    raise PackageError("非法 Wasm export 类型")
                offset += 1
                _, offset = _wasm_u32(data, offset, end)
            if offset != end:
                raise PackageError("非法 Wasm export section")
        offset = end


def create_manifest(spec: dict[str, object], wasm: bytes) -> bytes:
    _wasm(wasm)
    value = dict(spec)
    value["package_format_version"] = FORMAT_VERSION
    value["signature_algorithm"] = SIGNATURE_ALGORITHM
    value["payload"] = {
        "path": "app.wasm",
        "size_bytes": len(wasm),
        "sha256": hashlib.sha256(wasm).hexdigest(),
    }
    data = _json_bytes(value)
    _manifest(data)
    return data


def _private_key(path: Path) -> rsa.RSAPrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size != 3072:
        raise PackageError("签名私钥必须为 RSA-3072")
    return key


def _public_key(path: Path) -> rsa.RSAPublicKey:
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, rsa.RSAPublicKey) or key.key_size != 3072:
        raise PackageError("验证公钥必须为 RSA-3072")
    return key


def sign_manifest(manifest: bytes, private_key: Path, expected_key_id: str) -> bytes:
    if _manifest(manifest)["signing_key_id"] != expected_key_id:
        raise PackageError("manifest 签名 key ID 与受控输入不一致")
    return _private_key(private_key).sign(
        DOMAIN + manifest,
        padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
        hashes.SHA256(),
    )


def verify_signature(manifest: bytes, signature: bytes, public_key: Path) -> None:
    if len(signature) != SIGNATURE_BYTES:
        raise PackageError("签名长度错误")
    try:
        _public_key(public_key).verify(
            signature, DOMAIN + manifest,
            padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
            hashes.SHA256(),
        )
    except InvalidSignature as exc:
        raise PackageError("签名验证失败") from exc


def _member_info(name: str, size: int) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.size = size
    info.mode = 0o644
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


def pack(manifest: bytes, signature: bytes, wasm: bytes, *, max_wasm_bytes: int) -> bytes:
    record = _manifest(manifest)
    if len(signature) != SIGNATURE_BYTES or len(wasm) > max_wasm_bytes:
        raise PackageError("签名或 Wasm 长度超限")
    if record["payload"]["size_bytes"] != len(wasm) or record["payload"]["sha256"] != hashlib.sha256(wasm).hexdigest():
        raise PackageError("Wasm 与 manifest 不匹配")
    _wasm(wasm)
    output = BytesIO()
    with tarfile.open(fileobj=output, mode="w:", format=tarfile.USTAR_FORMAT) as archive:
        for name, data in zip(MEMBERS, (manifest, signature, wasm), strict=True):
            archive.addfile(_member_info(name, len(data)), BytesIO(data))
    return output.getvalue()


def _octal(raw: bytes) -> int:
    if not raw or not re.fullmatch(rb"[0-7]*\x00[ \x00]*", raw):
        raise PackageError("非法 ustar 数值字段")
    digits = raw.split(b"\x00", 1)[0]
    return int(digits or b"0", 8)


def unpack(package: bytes, *, max_wasm_bytes: int) -> tuple[bytes, bytes, bytes]:
    if len(package) % 512 or len(package) > max_wasm_bytes + MANIFEST_MAX_BYTES + 8192:
        raise PackageError("包长度或对齐错误")
    cursor = 0
    extracted: list[bytes] = []
    for expected in MEMBERS:
        if cursor + 512 > len(package):
            raise PackageError("截断的 ustar header")
        header = package[cursor:cursor + 512]
        cursor += 512
        if header[257:263] != b"ustar\x00" or header[263:265] != b"00":
            raise PackageError("只接受 POSIX ustar")
        name = header[:100].split(b"\x00", 1)[0]
        if name != expected.encode("ascii") or header[156:157] not in (b"0", b"\x00"):
            raise PackageError("成员名称、顺序或类型错误")
        if any(header[start:end].strip(b"\x00 ") for start, end in ((157, 257), (345, 500))):
            raise PackageError("不接受链接、前缀或扩展字段")
        if (any(_octal(header[start:end]) != required for start, end, required in
                ((100, 108, 0o644), (108, 116, 0), (116, 124, 0), (136, 148, 0))) or
                header[265:329].strip(b"\x00 ") or
                any(_octal(header[start:end]) != 0 for start, end in ((329, 337), (337, 345)))):
            raise PackageError("ustar 元数据超出固定子集")
        checksum = _octal(header[148:156])
        actual = sum(header[:148]) + 8 * 32 + sum(header[156:])
        if checksum != actual:
            raise PackageError("ustar header 校验和错误")
        size = _octal(header[124:136])
        cap = MANIFEST_MAX_BYTES if expected == MEMBERS[0] else SIGNATURE_BYTES if expected == MEMBERS[1] else max_wasm_bytes
        if size > cap or cursor + size > len(package):
            raise PackageError("成员长度超限或截断")
        if header != _member_info(expected, size).tobuf(format=tarfile.USTAR_FORMAT):
            raise PackageError("ustar header 非固定规范编码")
        extracted.append(package[cursor:cursor + size])
        cursor += size
        padding_bytes = (-size) % 512
        if package[cursor:cursor + padding_bytes] != bytes(padding_bytes):
            raise PackageError("成员填充非零")
        cursor += padding_bytes
    # tarfile 写入两个结束块后补齐至完整 ustar record。额外零块会让同一份
    # 已签名 payload 对应多个可接受包字节及不同包摘要，必须拒绝。
    end = cursor + 1024
    canonical_size = ((end + tarfile.RECORDSIZE - 1) // tarfile.RECORDSIZE) * tarfile.RECORDSIZE
    if len(package) != canonical_size or package[cursor:] != bytes(len(package) - cursor):
        raise PackageError("归档结束块或尾随字节错误")
    return tuple(extracted)  # type: ignore[return-value]


def verify_package(package: bytes, public_key: Path, expected_key_id: str,
                   *, max_wasm_bytes: int) -> dict[str, object]:
    manifest, signature, wasm = unpack(package, max_wasm_bytes=max_wasm_bytes)
    record = _manifest(manifest)
    if record["signing_key_id"] != expected_key_id:
        raise PackageError("manifest 签名 key ID 与信任锚不一致")
    verify_signature(manifest, signature, public_key)
    if record["payload"]["size_bytes"] != len(wasm) or record["payload"]["sha256"] != hashlib.sha256(wasm).hexdigest():
        raise PackageError("Wasm 摘要或长度错误")
    _wasm(wasm)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description="product.pkg v1 主机打包与验证")
    parser.add_argument("--max-wasm-bytes", type=int, default=DEFAULT_MAX_WASM_BYTES)
    commands = parser.add_subparsers(dest="command", required=True)
    manifest_cmd = commands.add_parser("manifest")
    manifest_cmd.add_argument("--spec", type=Path, required=True)
    manifest_cmd.add_argument("--wasm", type=Path, required=True)
    manifest_cmd.add_argument("--output", type=Path, required=True)
    sign_cmd = commands.add_parser("sign")
    sign_cmd.add_argument("--manifest", type=Path, required=True)
    sign_cmd.add_argument("--private-key", type=Path, required=True)
    sign_cmd.add_argument("--key-id", required=True)
    sign_cmd.add_argument("--output", type=Path, required=True)
    pack_cmd = commands.add_parser("pack")
    pack_cmd.add_argument("--manifest", type=Path, required=True)
    pack_cmd.add_argument("--signature", type=Path, required=True)
    pack_cmd.add_argument("--wasm", type=Path, required=True)
    pack_cmd.add_argument("--output", type=Path, required=True)
    verify_cmd = commands.add_parser("verify")
    verify_cmd.add_argument("--package", type=Path, required=True)
    verify_cmd.add_argument("--public-key", type=Path, required=True)
    verify_cmd.add_argument("--key-id", required=True)
    args = parser.parse_args()
    try:
        if args.max_wasm_bytes < 8:
            raise PackageError("max-wasm-bytes 必须至少为 8")
        if args.command == "manifest":
            spec = json.loads(args.spec.read_text(encoding="utf-8"), object_pairs_hook=_unique_pairs)
            args.output.write_bytes(create_manifest(spec, args.wasm.read_bytes()))
        elif args.command == "sign":
            args.output.write_bytes(sign_manifest(args.manifest.read_bytes(), args.private_key,
                                                  args.key_id))
        elif args.command == "pack":
            args.output.write_bytes(pack(args.manifest.read_bytes(), args.signature.read_bytes(),
                                         args.wasm.read_bytes(), max_wasm_bytes=args.max_wasm_bytes))
        else:
            record = verify_package(args.package.read_bytes(), args.public_key, args.key_id,
                                    max_wasm_bytes=args.max_wasm_bytes)
            print(json.dumps({"product_id": record["product_id"],
                              "product_version": record["product_version"],
                              "package_sha256": hashlib.sha256(args.package.read_bytes()).hexdigest()},
                             sort_keys=True, ensure_ascii=False))
    except (PackageError, OSError, ValueError, TypeError) as exc:
        print(f"product.pkg：{exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
