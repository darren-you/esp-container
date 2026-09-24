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
DEVICE_MAX_WASM_BYTES = 512 * 1024  # Device scanner bound; C3 slot capacity needs P6-03.
DEVICE_MEMORY_BYTES = 65536  # C3 single-page guest profile; includes signed limit.
DEFAULT_MAX_WASM_BYTES = DEVICE_MAX_WASM_BYTES
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
    if limits["memory_limit_bytes"] != DEVICE_MEMORY_BYTES:
        raise PackageError("C3 guest 清单内存限额必须为固定 64 KiB")
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


class _WasmReader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def u32(self) -> int:
        value, self.offset = _wasm_u32(self.data, self.offset, len(self.data))
        return value

    def take(self, size: int) -> bytes:
        if size > len(self.data) - self.offset:
            raise PackageError("截断的 Wasm section")
        value = self.data[self.offset:self.offset + size]
        self.offset += size
        return value

    def byte(self) -> int:
        return self.take(1)[0]

    def name(self) -> bytes:
        return self.take(self.u32())

    def finish(self) -> None:
        if self.offset != len(self.data):
            raise PackageError("Wasm section 有尾随数据")


def _wasm_types(data: bytes) -> list[tuple[bytes, bytes]]:
    reader = _WasmReader(data)
    count = reader.u32()
    if count > len(data) - reader.offset:
        raise PackageError("非法 Wasm 类型数量")
    types = []
    for _ in range(count):
        if reader.byte() != 0x60:
            raise PackageError("只允许 Wasm 函数类型")
        params = reader.take(reader.u32())
        results = reader.take(reader.u32())
        if any(value not in (0x7f, 0x7e, 0x7d, 0x7c) for value in params + results):
            raise PackageError("不允许的 Wasm 值类型")
        types.append((params, results))
    reader.finish()
    return types


def _wasm_imports(types: list[tuple[bytes, bytes]], data: bytes | None) -> tuple[frozenset[str], int]:
    if data is None:
        return frozenset(), 0
    reader = _WasmReader(data)
    count = reader.u32()
    if count > 4:
        raise PackageError("不允许的 Wasm imports 数量")
    expected = {
        b"monotonic_ms": (b"", b"\x7e", "monotonic-time"),
        b"log": (b"\x7f\x7f", b"\x7f", "log"),
        b"timer_start": (b"\x7f\x7f", b"\x7e", "timer"),
        b"timer_cancel": (b"\x7e", b"\x7f", "timer"),
    }
    seen: set[bytes] = set()
    required: set[str] = set()
    for _ in range(count):
        module = reader.name()
        field = reader.name()
        kind = reader.byte()
        type_index = reader.u32()
        if module != b"econtainer" or kind != 0 or field not in expected:
            raise PackageError("不允许的 Wasm imports")
        params, results, capability = expected[field]
        if field in seen or type_index >= len(types) or types[type_index] != (params, results):
            raise PackageError("Wasm imports 签名或重复项错误")
        seen.add(field)
        required.add(capability)
    reader.finish()
    return frozenset(required), count


def _wasm_functions(data: bytes, type_count: int) -> list[int]:
    reader = _WasmReader(data)
    count = reader.u32()
    if count > len(data) - reader.offset:
        raise PackageError("非法 Wasm 函数数量")
    functions = [reader.u32() for _ in range(count)]
    reader.finish()
    if any(index >= type_count for index in functions):
        raise PackageError("Wasm 函数类型索引越界")
    return functions


def _wasm_exports(data: bytes) -> tuple[int, int, int]:
    reader = _WasmReader(data)
    if reader.u32() != 4:
        raise PackageError("Wasm 导出集合与 ABI 不符")
    expected = {b"econtainer_init": 0, b"econtainer_on_event": 0,
                b"econtainer_stop": 0, b"memory": 2}
    exports: dict[bytes, int] = {}
    for _ in range(4):
        name = reader.name()
        kind = reader.byte()
        index = reader.u32()
        if name not in expected or name in exports or kind != expected[name] or (
                name == b"memory" and index != 0):
            raise PackageError("Wasm 导出集合与 ABI 不符")
        exports[name] = index
    reader.finish()
    if set(exports) != set(expected):
        raise PackageError("Wasm 导出集合与 ABI 不符")
    return (exports[b"econtainer_init"], exports[b"econtainer_on_event"],
            exports[b"econtainer_stop"])


def _wasm_memory(data: bytes, max_memory_bytes: int) -> None:
    reader = _WasmReader(data)
    count, flags, minimum, maximum = (reader.u32() for _ in range(4))
    reader.finish()
    if count != 1 or flags != 1 or minimum == 0 or minimum > maximum or (
            maximum > min(max_memory_bytes, DEVICE_MEMORY_BYTES) // 65536):
        raise PackageError("Wasm 内存超出当前 profile 或清单限额")


def _wasm_code(data: bytes, function_count: int) -> None:
    reader = _WasmReader(data)
    if reader.u32() != function_count:
        raise PackageError("Wasm 函数与代码数量不符")
    for _ in range(function_count):
        size = reader.u32()
        body = reader.take(size)
        if size < 2 or body[-1] != 0x0b:
            raise PackageError("Wasm 函数体缺少结束指令")
    reader.finish()


def _wasm(data: bytes, *, max_memory_bytes: int = 0xffffffff) -> frozenset[str]:
    """Mirror the device package scanner's byte-determined Classic/ABI checks."""
    if len(data) > DEVICE_MAX_WASM_BYTES:
        raise PackageError("Wasm 超出设备扫描上限")
    if not data.startswith(b"\x00asm\x01\x00\x00\x00"):
        raise PackageError("不是标准 Wasm v1 模块")
    reader = _WasmReader(data[8:])
    sections: dict[int, bytes] = {}
    last_section = 0
    while reader.offset < len(reader.data):
        section = reader.byte()
        content = reader.take(reader.u32())
        if section in (4, 8, 9) or section > 12:
            raise PackageError("不允许 Wasm start、table、element 或未知 section")
        if section == 0:
            if _WasmReader(content).name() == b"target_features":
                raise PackageError("不允许 Wasm target_features")
            continue
        if section <= last_section:
            raise PackageError("Wasm section 重复或乱序")
        sections[section] = content
        last_section = section
    if not {1, 3, 5, 7, 10}.issubset(sections):
        raise PackageError("Wasm 缺少设备 ABI 必需 section")
    types = _wasm_types(sections[1])
    required, imported_count = _wasm_imports(types, sections.get(2))
    functions = _wasm_functions(sections[3], len(types))
    exports = _wasm_exports(sections[7])
    if len(set(exports)) != 3:
        raise PackageError("Wasm 三个入口不能复用同一个函数")
    signatures = ((b"", b"\x7f"), (b"\x7f\x7f", b"\x7f"), (b"", b"\x7f"))
    for index, signature in zip(exports, signatures, strict=True):
        if index < imported_count or index - imported_count >= len(functions) or (
                types[functions[index - imported_count]] != signature):
            raise PackageError("Wasm 入口签名与 ABI 不符")
    _wasm_memory(sections[5], max_memory_bytes)
    _wasm_code(sections[10], len(functions))
    return required


def _check_declared_capabilities(wasm: bytes, record: dict[str, object]) -> None:
    if record["guest_abi_version"] != 1 or record["runtime_profile"] != "wamr-classic-v1":
        raise PackageError("不支持的 Wasm ABI 或运行 profile")
    if not set(record["required_capabilities"]).issubset({"monotonic-time", "log", "timer"}):
        raise PackageError("不支持的 Wasm 能力声明")
    required = _wasm(wasm, max_memory_bytes=record["limits"]["memory_limit_bytes"])
    if not required.issubset(record["required_capabilities"]):
        raise PackageError("Wasm imports 缺少 manifest required_capabilities 授权")


def create_manifest(spec: dict[str, object], wasm: bytes) -> bytes:
    value = dict(spec)
    value["package_format_version"] = FORMAT_VERSION
    value["signature_algorithm"] = SIGNATURE_ALGORITHM
    value["payload"] = {
        "path": "app.wasm",
        "size_bytes": len(wasm),
        "sha256": hashlib.sha256(wasm).hexdigest(),
    }
    data = _json_bytes(value)
    record = _manifest(data)
    _check_declared_capabilities(wasm, record)
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
    _check_declared_capabilities(wasm, record)
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
    _check_declared_capabilities(wasm, record)
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
