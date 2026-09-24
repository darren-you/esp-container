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

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import product_package as pkg  # noqa: E402
from wasm_fixture import HEADER, TYPES, leb, module, name, section  # noqa: E402


ROOT = Path(__file__).resolve().parents[1]
SPEC = json.loads((ROOT / "examples/counter/spec.example.json").read_text())
def signed_package(private: Path, wasm: bytes,
                   spec: dict[str, object], *, validate_host: bool = True) -> bytes:
    record = copy.deepcopy(spec)
    record.update(package_format_version=1, signature_algorithm=pkg.SIGNATURE_ALGORITHM,
                  payload={"path": "app.wasm", "size_bytes": len(wasm),
                           "sha256": hashlib.sha256(wasm).hexdigest()})
    manifest = pkg._json_bytes(record)
    if validate_host:
        pkg._manifest(manifest)
        signature = pkg.sign_manifest(manifest, private, "test-key")
    else:
        # Test a cryptographically valid manifest that the host now refuses.
        key = serialization.load_pem_private_key(private.read_bytes(), password=None)
        signature = key.sign(pkg.DOMAIN + manifest,
                             padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
                             hashes.SHA256())
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
                max_memory: int = 65536, expected: int = 0,
                fail_at: int | None = None, flip_at: int | None = None,
                validate_host: bool = True) -> None:
            package_path.write_bytes(signed_package(
                private, wasm, spec, validate_host=validate_host))
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
        # ABI 2 accepts the last complete 4 KiB region and valid padded LEB.
        run(module(buffer_global=b"\x7f\0\x41\x80\xe0\x03\x0b"))
        run(module(buffer_global=b"\x7f\0\x41\x80\x88\x80\x80\0\x0b"))
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
        run(both, both_spec, max_memory=65535, expected=1)
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
        too_little_memory["limits"]["memory_limit_bytes"] = 65535
        try:
            pkg.create_manifest(too_little_memory, none)
            raise AssertionError("host accepted a Wasm memory maximum above the signed limit")
        except pkg.PackageError:
            pass
        run(none, too_little_memory, expected=1, validate_host=False)
        old_manifest_memory = copy.deepcopy(SPEC)
        old_manifest_memory["limits"]["memory_limit_bytes"] = 131072
        try:
            pkg.create_manifest(old_manifest_memory, none)
            raise AssertionError("host accepted the old two-page signed limit")
        except pkg.PackageError:
            pass
        run(none, old_manifest_memory, expected=1, validate_host=False)
        too_much_stack = copy.deepcopy(SPEC)
        too_much_stack["limits"]["stack_limit_bytes"] = 8192
        run(none, too_much_stack, expected=3)
        unknown_spec = copy.deepcopy(SPEC)
        unknown_spec["required_capabilities"] = ["gpio"]
        try:
            pkg.create_manifest(unknown_spec, none)
            raise AssertionError("host accepted a device-unsupported capability")
        except pkg.PackageError:
            pass
        run(none, unknown_spec, expected=2)
        bad_abi = copy.deepcopy(SPEC)
        bad_abi["guest_abi_version"] = 1
        try:
            pkg.create_manifest(bad_abi, none)
            raise AssertionError("host accepted a device-unsupported ABI")
        except pkg.PackageError:
            pass
        run(none, bad_abi, expected=2)
        bad_profile = copy.deepcopy(SPEC)
        bad_profile["runtime_profile"] = "aot-v1"
        try:
            pkg.create_manifest(bad_profile, none)
            raise AssertionError("host accepted a device-unsupported runtime profile")
        except pkg.PackageError:
            pass
        run(none, bad_profile, expected=2)

        # These bytes used to pass the old host shape check, but the signed
        # device scanner rejects the same modules before activation.
        memory_section = section(5, b"\x01\x01\x01\x01")
        code_section = section(10, b"\x03" + b"\x04\0\x41\0\x0b" * 3)
        deterministic_rejections = (
            ("target_features", module(extra=section(0, name("target_features"))), 2),
            ("missing_abi_sections", HEADER, 1),
            ("table", none.replace(memory_section, section(4, b"\0") + memory_section, 1), 2),
            ("element", none.replace(code_section, section(9, b"\0") + code_section, 1), 2),
            ("empty_imports", none.replace(section(3, b"\x03\0\x01\0"),
                                           section(2, b"") + section(3, b"\x03\0\x01\0"), 1), 1),
            ("entry_signature", module(event_type=0), 1),
            ("code_count", module(code_count=2), 1),
            ("old_two_page_guest", module(memory_pages=2, memory_max=2), 1),
            ("old_four_exports", module(export_buffer=False), 2),
            ("buffer_index", module(buffer_index=1), 1),
            ("buffer_mutable", module(buffer_global=b"\x7f\x01\x41\x80\x08\x0b"), 1),
            ("buffer_i64", module(buffer_global=b"\x7e\0\x42\x80\x08\x0b"), 1),
            ("buffer_null", module(buffer_global=b"\x7f\0\x41\0\x0b"), 1),
            ("buffer_negative", module(buffer_global=b"\x7f\0\x41\x7f\x0b"), 1),
            ("buffer_cross_page", module(buffer_global=b"\x7f\0\x41\x81\xe0\x03\x0b"), 1),
            ("buffer_overflow_leb", module(buffer_global=b"\x7f\0\x41\x80\x80\x80\x80\x08\x0b"), 1),
        )
        for label, wasm, result in deterministic_rejections:
            try:
                pkg.create_manifest(SPEC, wasm)
            except pkg.PackageError:
                pass
            else:
                raise AssertionError(f"host accepted device-rejected Wasm: {label}")
            run(wasm, expected=result)

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
            else:
                raise AssertionError(f"host accepted malformed Wasm {index}")
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
