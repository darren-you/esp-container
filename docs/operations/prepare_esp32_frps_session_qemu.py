#!/usr/bin/env python3
"""从已签名的 ESP32 字宽 AEAD 五仓镜像派生仓外 FRPS 会话 QEMU 工程。"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import ipaddress
from pathlib import Path
import shutil

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


SOURCE_SHA256 = {
    "firmware/build/esp_base.bin": "e54493c022c2e5b7e8ee3ef7906708d3dfe39e6dfe0d9f7975dabddfdb89700e",
    "firmware/sdkconfig": "b1f4e090370e5ac7b20a05cf84faf941f39c847df7b18eccc1e777e90586d6ff",
    "firmware/apps/esp_base/main/capacity_runtime_probe.c": "4bbd09168e8ae501d87f93b2d4414b89855bca2b22ba231fc5686dad75131576",
    "firmware/apps/esp_base/main/CMakeLists.txt": "1cddf5061c510cb40750ce4ff6a494ba91037af942eb27a62890538f0b35504f",
    "firmware/components/esp_frp/src/aead.c": "b20cde4a481b6cd7264fa1f84234e11fe55b7754cb006ed266825679cb4723d5",
    "firmware/components/esp_frp/src/session.c": "51dc11977f0e6a6d8fae5a3b089f2ab35c70c29bdd1fe9e10de4adbaed5500ac",
    "firmware/components/esp_frp/src/word_storage.h": "6b51d18873126d9550b8bee4952798499ae9ed0c8d0f3ffcd78ced31b26ff1bd",
    "firmware/dependencies.lock.esp32": "82bba31d16c277f5defe4c4950da9a533a62330d9bb2ac05110f4640d1879541",
}
COMPONENTS = ("esp_container", "esp_frp", "esp_ota", "mqtt")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(path: Path, before: str, after: str) -> None:
    source = path.read_text()
    if source.count(before) != 1:
        raise ValueError(f"{path}: expected exactly one insertion marker: {before!r}")
    path.write_text(source.replace(before, after, 1))


def write_private(path: Path, data: bytes) -> None:
    with path.open("xb") as output:
        output.write(data)
    path.chmod(0o600)


def create_test_certificate(directory: Path) -> None:
    ca_key = ec.generate_private_key(ec.SECP256R1())
    ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "QEMU FRPS Test CA")])
    ca = (x509.CertificateBuilder().subject_name(ca_name).issuer_name(ca_name)
          .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
          .not_valid_before(datetime(2025, 1, 1, tzinfo=timezone.utc))
          .not_valid_after(datetime(2030, 1, 1, tzinfo=timezone.utc))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                       key_encipherment=False, data_encipherment=False,
                                       key_agreement=False, key_cert_sign=True, crl_sign=True,
                                       encipher_only=False, decipher_only=False), critical=True)
          .sign(ca_key, hashes.SHA256()))
    leaf_key = ec.generate_private_key(ec.SECP256R1())
    leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "10.0.2.2")])
    leaf = (x509.CertificateBuilder().subject_name(leaf_name).issuer_name(ca_name)
            .public_key(leaf_key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(datetime(2025, 1, 1, tzinfo=timezone.utc))
            .not_valid_after(datetime(2030, 1, 1, tzinfo=timezone.utc))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName(
                [x509.IPAddress(ipaddress.IPv4Address("10.0.2.2"))]), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                         key_encipherment=False, data_encipherment=False,
                                         key_agreement=False, key_cert_sign=False, crl_sign=False,
                                         encipher_only=False, decipher_only=False), critical=True)
            .sign(ca_key, hashes.SHA256()))
    write_private(directory / "frps_ca.pem", ca.public_bytes(serialization.Encoding.PEM))
    write_private(directory / "frps_server.pem", leaf.public_bytes(serialization.Encoding.PEM))
    write_private(directory / "frps_server_key.pem", leaf_key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="冻结的 fbf5ec0 字宽 AEAD 签名探针")
    parser.add_argument("destination", type=Path, help="尚不存在的仓外 QEMU 工程")
    parser.add_argument("--port", type=int, required=True, help="未使用的本机端口，1024..65535")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("port must be 1024..65535")
    source, destination = args.source.resolve(), args.destination.resolve()
    if destination.exists() or destination.is_relative_to(source):
        parser.error("destination must be absent and outside source")
    for relative, expected in SOURCE_SHA256.items():
        if digest(source / relative) != expected:
            parser.error(f"frozen input mismatch: {relative}")
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("build", "*.log"))
    firmware = destination / "firmware"
    main_dir = firmware / "apps/esp_base/main"
    fixture = destination / "fixture"
    fixture.mkdir(mode=0o700)
    create_test_certificate(fixture)
    shutil.copy2(fixture / "frps_ca.pem", main_dir / "frps_ca.pem")
    (main_dir / "qemu_frps_config.h").write_text(
        "// QEMU-only generated test inputs. Never flash.\n"
        f"#define QEMU_FRPS_PORT {args.port}U\n")
    for name in ("openeth_qemu_probe.c", "esp32_frps_session_qemu_probe.c"):
        shutil.copy2(Path(__file__).with_name(name), main_dir / name)
    shutil.copy2(Path(__file__).with_name("qemu_frps_fixture.go"),
                 fixture / "qemu_frps_fixture.go")
    config = firmware / "sdkconfig"
    replace_once(config, "# CONFIG_ETH_USE_OPENETH is not set", "CONFIG_ETH_USE_OPENETH=y")
    cmake = main_dir / "CMakeLists.txt"
    replace_once(cmake, '"word_capacity_probe.c" EMBED_FILES',
                 '"word_capacity_probe.c" "openeth_qemu_probe.c" '
                 '"esp32_frps_session_qemu_probe.c" EMBED_FILES')
    replace_once(cmake, 'EMBED_FILES "aead_4096.bin" "aead_65536.bin"',
                 'EMBED_FILES "aead_4096.bin" "aead_65536.bin" '
                 'EMBED_TXTFILES "frps_ca.pem"')
    replace_once(cmake, "REQUIRES esp_hw_support esp_frp",
                 "REQUIRES esp_eth esp_event esp_netif esp_hw_support esp_frp")
    runtime = main_dir / "capacity_runtime_probe.c"
    replace_once(runtime, "void capacity_word_probe(const uint8_t *, size_t, const uint8_t *, size_t);",
                 "void capacity_word_probe(const uint8_t *, size_t, const uint8_t *, size_t);\n"
                 "bool capacity_openeth_start(void);\n"
                 "void capacity_openeth_stop(void);\n"
                 "bool capacity_frps_session_probe(void);")
    replace_once(runtime,
                 "                    (size_t)(aead_65536_bin_end - aead_65536_bin_start));",
                 "                    (size_t)(aead_65536_bin_end - aead_65536_bin_start));\n"
                 "                if (capacity_openeth_start()) {\n"
                 "                    if (!capacity_frps_session_probe()) ++s_failures;\n"
                 "                    capacity_openeth_stop();\n"
                 "                } else ++s_failures;")
    lock = firmware / "dependencies.lock.esp32"
    old_prefix = "../../../../private/private/tmp/esp32-frp-iram-aead-qemu-20260927/probe/firmware/components/"
    content = lock.read_text()
    for component in COMPONENTS:
        before = old_prefix + component
        if content.count(before) != 1:
            parser.error(f"component lock mismatch: {component}")
        content = content.replace(before, str(firmware / "components" / component), 1)
    lock.write_text(content)
    print(f"source_signed_app_sha256={SOURCE_SHA256['firmware/build/esp_base.bin']}")
    print(f"source_sdkconfig_sha256={SOURCE_SHA256['firmware/sdkconfig']}")
    print(f"candidate_sdkconfig_sha256={digest(config)}")
    print(f"candidate_runtime_sha256={digest(runtime)}")
    print(f"candidate_main_cmake_sha256={digest(cmake)}")
    print(f"destination={destination} port={args.port}")
    print("generated private test key stays outside Git; QEMU-only, never flash")


if __name__ == "__main__":
    main()
