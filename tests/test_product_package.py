from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import product_package as pkg  # noqa: E402


WASM = b"\x00asm\x01\x00\x00\x00"
SPEC = {
    "product_id": "counter",
    "product_version": "v0-1-0",
    "guest_abi_version": 1,
    "required_capabilities": [],
    "runtime_profile": "wamr-classic-v1",
    "limits": {
        "memory_limit_bytes": 65536,
        "stack_limit_bytes": 4096,
        "event_queue_limit": 8,
        "instruction_budget": 100000,
        "host_call_timeout_ms": 100,
        "storage_limit_bytes": 0,
    },
    "data_schema_version": 1,
    "signing_key_id": "test-key",
}


class ProductPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory()
        cls.private = Path(cls.temp.name) / "private.pem"
        cls.public = Path(cls.temp.name) / "public.pem"
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        cls.private.write_bytes(key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption()))
        cls.public.write_bytes(key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp.cleanup()

    def _package(self, wasm: bytes = WASM) -> tuple[bytes, bytes, bytes]:
        manifest = pkg.create_manifest(SPEC, wasm)
        signature = pkg.sign_manifest(manifest, self.private, "test-key")
        package = pkg.pack(manifest, signature, wasm, max_wasm_bytes=1024)
        return manifest, signature, package

    def test_signed_round_trip_and_deterministic_pack(self) -> None:
        manifest, signature, package = self._package()
        self.assertEqual(pkg.verify_package(package, self.public, "test-key", max_wasm_bytes=1024)["product_id"], "counter")
        self.assertEqual(package, pkg.pack(manifest, signature, WASM, max_wasm_bytes=1024))
        self.assertEqual(pkg.unpack(package, max_wasm_bytes=1024), (manifest, signature, WASM))

    def test_payload_mutation_rejected(self) -> None:
        _, _, package = self._package()
        changed = bytearray(package)
        changed[3072] ^= 1
        with self.assertRaisesRegex(pkg.PackageError, "摘要|Wasm"):
            pkg.verify_package(bytes(changed), self.public, "test-key", max_wasm_bytes=1024)

    def test_manifest_mutation_rejected_by_signature(self) -> None:
        manifest, signature, _ = self._package()
        changed = manifest.replace(b"counter", b"counted")
        package = pkg.pack(changed, signature, WASM, max_wasm_bytes=1024)
        with self.assertRaisesRegex(pkg.PackageError, "签名"):
            pkg.verify_package(package, self.public, "test-key", max_wasm_bytes=1024)

    def test_wrong_key_rejected(self) -> None:
        _, _, package = self._package()
        other = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        path = Path(self.temp.name) / "other.pem"
        path.write_bytes(other.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        with self.assertRaisesRegex(pkg.PackageError, "签名"):
            pkg.verify_package(package, path, "test-key", max_wasm_bytes=1024)
        with self.assertRaisesRegex(pkg.PackageError, "key ID"):
            pkg.verify_package(package, self.public, "other-key", max_wasm_bytes=1024)
        with self.assertRaisesRegex(pkg.PackageError, "key ID"):
            pkg.sign_manifest(pkg.create_manifest(SPEC, WASM), self.private, "other-key")

    def test_duplicate_json_key_and_unknown_field_rejected(self) -> None:
        manifest, _, _ = self._package()
        with self.assertRaisesRegex(pkg.PackageError, "重复"):
            pkg._manifest(manifest[:-1] + b',"product_id":"counter"}')
        value = json.loads(manifest)
        value["unknown"] = True
        with self.assertRaisesRegex(pkg.PackageError, "未知"):
            pkg._manifest(pkg._json_bytes(value))

    def test_start_and_truncated_wasm_rejected(self) -> None:
        with self.assertRaisesRegex(pkg.PackageError, "start"):
            pkg.create_manifest(SPEC, WASM + b"\x08\x01\x00")
        with self.assertRaisesRegex(pkg.PackageError, "截断"):
            pkg.create_manifest(SPEC, WASM + b"\x01\x05\x00")
        with self.assertRaisesRegex(pkg.PackageError, "imports"):
            pkg.create_manifest(SPEC, WASM + b"\x02\x01\x01")
        forbidden = b"__post_instantiate"
        export = b"\x01" + bytes((len(forbidden),)) + forbidden + b"\x00\x00"
        with self.assertRaisesRegex(pkg.PackageError, "自动构造"):
            pkg.create_manifest(SPEC, WASM + b"\x07" + bytes((len(export),)) + export)

    def test_tar_member_and_tail_rejected(self) -> None:
        _, _, package = self._package()
        changed = bytearray(package)
        changed[:8] = b"evil.bin"
        with self.assertRaises(pkg.PackageError):
            pkg.unpack(bytes(changed), max_wasm_bytes=1024)
        changed = bytearray(package)
        changed[-1] = 1
        with self.assertRaisesRegex(pkg.PackageError, "尾随"):
            pkg.unpack(bytes(changed), max_wasm_bytes=1024)

    def test_noncanonical_ustar_header_rejected_with_valid_checksum(self) -> None:
        _, _, package = self._package()
        changed = bytearray(package)
        changed[500] = 1
        changed[148:156] = b"        "
        checksum = sum(changed[:512])
        changed[148:156] = f"{checksum:06o}\0 ".encode("ascii")
        with self.assertRaisesRegex(pkg.PackageError, "非固定规范"):
            pkg.unpack(bytes(changed), max_wasm_bytes=1024)

    def test_package_limit_rejected(self) -> None:
        _, _, package = self._package()
        with self.assertRaises(pkg.PackageError):
            pkg.unpack(package, max_wasm_bytes=7)


if __name__ == "__main__":
    unittest.main()
