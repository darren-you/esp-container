"""Exercise a real signed package through the reserved Flash slot readback path."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_wasm_test import module, signed_package  # noqa: E402


def main() -> None:
    binary = Path(sys.argv[1])
    root = Path(__file__).resolve().parents[1]
    spec = json.loads((root / "examples/counter/spec.example.json").read_text())
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        private = temporary / "private.pem"
        private.write_bytes(key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption()))
        public = temporary / "public.der"
        public.write_bytes(key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.PKCS1))
        package = temporary / "product.pkg"
        package.write_bytes(signed_package(private, module(), spec))
        for mode in ("valid", "reuse", "changed-copy", "product", "schema", "key-id", "memory", "queue",
                     "budget", "timeout", "read-fault", "wasm-read-fault"):
            result = subprocess.run([str(binary), str(package), str(public), mode],
                                    capture_output=True, text=True, check=False)
            assert result.returncode == 0, f"{mode}: {result.stdout} {result.stderr}"
            if mode in ("valid", "changed-copy"):
                expected = "result=0 phase=2"
            elif mode == "reuse":
                expected = "result=0 phase=5"
            elif mode in ("read-fault", "wasm-read-fault"):
                expected = "result=3 phase=1"
            else:
                expected = "result=8 phase=1"
            assert expected in result.stdout, f"{mode}: {result.stdout}"
    print("package_slot: signed Flash readback, policy, schema and read faults passed")


if __name__ == "__main__":
    main()
