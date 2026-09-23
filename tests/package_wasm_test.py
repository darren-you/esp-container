"""Cross-check signed host packages with the bounded device Wasm ABI reader."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import tempfile
from io import BytesIO
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import product_package as pkg  # noqa: E402


ROOT = Path(__file__).resolve().parents[1]
SPEC = json.loads((ROOT / "examples/counter/spec.example.json").read_text())
HEADER = b"\0asm\x01\0\0\0"
TYPES = (b"\x05\x60\x00\x01\x7f\x60\x02\x7f\x7f\x01\x7f"
         b"\x60\x00\x01\x7e\x60\x02\x7f\x7f\x01\x7e"
         b"\x60\x01\x7e\x01\x7f")


def leb(number: int) -> bytes:
    output = bytearray()
    while True:
        octet = number & 0x7F
        number >>= 7
        output.append(octet | (0x80 if number else 0))
        if number == 0:
            return bytes(output)


def section(kind: int, content: bytes) -> bytes:
    return bytes((kind,)) + leb(len(content)) + content


def name(value: str) -> bytes:
    encoded = value.encode("ascii")
    return leb(len(encoded)) + encoded


def module(imports: tuple[str, ...] = (), *, event_type: int = 1,
           memory_flags: int = 1, memory_max: int = 2,
           duplicate_export: bool = False, code_count: int = 3,
           extra: bytes = b"") -> bytes:
    type_by_name = {"monotonic_ms": 2, "log": 1,
                    "timer_start": 3, "timer_cancel": 4}
    imported = (leb(len(imports)) + b"".join(
        name("econtainer") + name(field) + b"\0" + leb(type_by_name.get(field, 2))
        for field in imports)) if imports else b""
    function_types = b"\x03\0" + leb(event_type) + b"\0"
    memory = b"\x01" + leb(memory_flags) + b"\x02" + (
        leb(memory_max) if memory_flags & 1 else b"")
    exported = [("econtainer_init", 0, len(imports)),
                ("econtainer_on_event", 0, len(imports) + 1),
                ("econtainer_stop", 0, len(imports) + 2),
                ("memory", 2, 0)]
    if duplicate_export:
        exported[1] = ("econtainer_init", 0, len(imports) + 1)
    exports = leb(len(exported)) + b"".join(
        name(field) + bytes((kind,)) + leb(index) for field, kind, index in exported)
    body = b"\0\x41\0\x0b"
    code = leb(code_count) + (leb(len(body)) + body) * code_count
    return (HEADER + section(1, TYPES) +
            (section(2, imported) if imports else b"") +
            section(3, function_types) + section(5, memory) +
            section(7, exports) + section(10, code) + extra)


def signed_package(private: Path, wasm: bytes,
                   spec: dict[str, object]) -> bytes:
    record = copy.deepcopy(spec)
    record.update(package_format_version=1, signature_algorithm=pkg.SIGNATURE_ALGORITHM,
                  payload={"path": "app.wasm", "size_bytes": len(wasm),
                           "sha256": hashlib.sha256(wasm).hexdigest()})
    manifest = pkg._json_bytes(record)
    pkg._manifest(manifest)
    signature = pkg.sign_manifest(manifest, private, "test-key")
    output = BytesIO()
    with tarfile.open(fileobj=output, mode="w:", format=tarfile.USTAR_FORMAT) as archive:
        for filename, content in zip(pkg.MEMBERS, (manifest, signature, wasm), strict=True):
            archive.addfile(pkg._member_info(filename, len(content)), BytesIO(content))
    return output.getvalue()


def main() -> None:
    binary = Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        private = root / "private.pem"
        private.write_bytes(key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption()))
        public = root / "public.der"
        public.write_bytes(key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.PKCS1))
        package_path = root / "product.pkg"

        def run(wasm: bytes, spec: dict[str, object] = SPEC, *, grant: int = 3,
                max_memory: int = 131072, expected: int = 0,
                fail_at: int | None = None, flip_at: int | None = None) -> None:
            package_path.write_bytes(signed_package(private, wasm, spec))
            args = [str(binary), str(package_path), str(public), "test-key",
                    "524288", str(grant), str(max_memory)]
            if fail_at is not None or flip_at is not None:
                args.append(str(fail_at if fail_at is not None else 4294967295))
            if flip_at is not None:
                args.append(str(flip_at))
            result = subprocess.run(args, capture_output=True, text=True, check=False)
            assert result.returncode == (0 if expected == 0 else 2), result.stderr + result.stdout
            assert f"wasm_result={expected} " in result.stdout, result.stdout
            parts = dict(part.split("=", 1) for part in result.stdout.split())
            assert int(parts["verify_max_read"]) <= 512, result.stdout
            assert int(parts["wasm_max_read"]) <= 512, result.stdout

        none = module()
        assert pkg._wasm(none) == frozenset()
        pkg.create_manifest(SPEC, none)
        run(none)
        large_custom = module(extra=section(0, name("name") + bytes(65536)))
        assert pkg._wasm(large_custom) == frozenset()
        run(large_custom)
        clock = module(("monotonic_ms",))
        clock_spec = copy.deepcopy(SPEC)
        clock_spec["required_capabilities"] = ["monotonic-time"]
        assert pkg._wasm(clock) == frozenset({"monotonic-time"})
        pkg.create_manifest(clock_spec, clock)
        run(clock, clock_spec)
        log = module(("log",))
        log_spec = copy.deepcopy(SPEC)
        log_spec["required_capabilities"] = ["log"]
        assert pkg._wasm(log) == frozenset({"log"})
        pkg.create_manifest(log_spec, log)
        run(log, log_spec)
        both = module(("monotonic_ms", "log"))
        both_spec = copy.deepcopy(SPEC)
        both_spec["required_capabilities"] = ["log", "monotonic-time"]
        assert pkg._wasm(both) == frozenset({"log", "monotonic-time"})
        pkg.create_manifest(both_spec, both)
        run(both, both_spec)
        run(both, both_spec, grant=1, expected=3)
        run(both, both_spec, max_memory=65536, expected=3)
        run(both, SPEC, expected=1)
        timer = module(("timer_start", "timer_cancel"))
        timer_spec = copy.deepcopy(SPEC)
        timer_spec["required_capabilities"] = ["timer"]
        assert pkg._wasm(timer) == frozenset({"timer"})
        pkg.create_manifest(timer_spec, timer)
        run(timer, timer_spec, grant=4)
        run(module(("timer_start",)), timer_spec, grant=4)
        run(module(("timer_cancel",)), timer_spec, grant=4)
        run(timer, timer_spec, grant=0, expected=3)
        run(timer, SPEC, grant=4, expected=1)
        wrong_timer = timer.replace(name("timer_start") + b"\0\x03",
                                    name("timer_start") + b"\0\x04")
        try:
            pkg._wasm(wrong_timer)
            raise AssertionError("host accepted a wrong timer signature")
        except pkg.PackageError:
            pass
        run(wrong_timer, timer_spec, grant=4, expected=2)
        too_little_memory = copy.deepcopy(SPEC)
        too_little_memory["limits"]["memory_limit_bytes"] = 65536
        run(none, too_little_memory, expected=1)
        too_much_stack = copy.deepcopy(SPEC)
        too_much_stack["limits"]["stack_limit_bytes"] = 8192
        run(none, too_much_stack, expected=3)
        unknown_spec = copy.deepcopy(SPEC)
        unknown_spec["required_capabilities"] = ["gpio"]
        run(none, unknown_spec, expected=2)
        bad_abi = copy.deepcopy(SPEC)
        bad_abi["guest_abi_version"] = 2
        run(none, bad_abi, expected=2)
        bad_profile = copy.deepcopy(SPEC)
        bad_profile["runtime_profile"] = "aot-v1"
        run(none, bad_profile, expected=2)

        malformed = [
            module(event_type=0), module(memory_flags=3),
            module(memory_flags=0), module(duplicate_export=True),
            module(("log", "log")),
            module(("timer_start", "timer_start")),
            module(extra=b"\x08\x01\0"),
            module(extra=b"\x0a\x01\0"),
            module(extra=b"\x01\x80"),
            module(extra=b"\x01\xff\xff\xff\xff\x10"),
            module(extra=b"\x01\x05\0"),
            module(extra=section(0, name("target_features"))),
            module(extra=section(4, b"\0")),
            HEADER + section(1, b"\x80") + module()[8 + len(section(1, TYPES)):],
            module(("other",)),
            module(code_count=2),
        ]
        for index, wasm in enumerate(malformed):
            try:
                pkg._wasm(wasm)
            except pkg.PackageError:
                pass
            with_index = f"{index}: {wasm.hex()[:40]}"
            try:
                run(wasm, expected=2 if index in (3, 4, 5, 6, 11, 12, 14) else 1)
            except AssertionError as exc:
                raise AssertionError(with_index) from exc

        # The independently verified bytes must still match the signed digest.
        valid = signed_package(private, none, SPEC)
        package_path.write_bytes(valid)
        with tarfile.open(fileobj=BytesIO(valid), mode="r:") as archive:
            wasm_offset = archive.getmember("app.wasm").offset_data
        run(none, fail_at=wasm_offset, expected=4)
        run(none, flip_at=wasm_offset + len(none) - 2, expected=1)

        sdk = os.environ.get("WASI_SDK_ROOT")
        if sdk:
            fixtures = root / "real-guests"
            subprocess.run([sys.executable, str(ROOT / "tests/build_runtime_guests.py"),
                            "--wasi-sdk", sdk, "--output-dir", str(fixtures)], check=True)
            counter = (fixtures / "counter.wasm").read_bytes()
            host_api = (fixtures / "host-api.wasm").read_bytes()
            timer_guest = (fixtures / "timer.wasm").read_bytes()
            assert pkg._wasm(counter) == frozenset()
            assert pkg._wasm(host_api) == frozenset({"log", "monotonic-time"})
            assert pkg._wasm(timer_guest) == frozenset({"timer"})
            run(counter)
            run(host_api, both_spec)
            run(timer_guest, timer_spec, grant=4)
    print("package_wasm: host/device import matrix, ABI, malformed structure, grant and read faults passed")


if __name__ == "__main__":
    main()
