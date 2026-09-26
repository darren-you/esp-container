#!/usr/bin/env python3
"""从 C3 REGISTERED/Pong 冻结输入派生仓外 32BIT 分配能力实验。"""

import argparse
import hashlib
from pathlib import Path
import shutil
import tempfile


APP = Path("firmware/apps/esp_base/main")
EXPECTED = {
    Path("firmware/sdkconfig"):
        "a66858cf817841457a8557a46ec18e5757e4889a5e31dffc0978adf8a277fb52",
    APP / "frps_session_qemu_probe.c":
        "601f69047acdca637bbab49dd76f5a29168577705a9b5e8352281b8a334e2ddd",
    Path("firmware/components/esp_frp/include/esp_frp_yamux.h"):
        "f1c2fb9b6e2b77975bd4fcb6e5f2a9de16c9ffc25e92dd69506cf894b571f5e6",
    Path("firmware/components/esp_frp/src/yamux.c"):
        "9da9f54d8659b2053b60afcd1cbb0dd0acbfcf8df872c812f551f4a9e0dac2f3",
    Path("firmware/components/esp_frp/src/session.c"):
        "48810478d39819bbffe61cd7a533675aa75696fe7f32d998c11827e615319a68",
    Path("firmware/components/esp_frp/src/work.c"):
        "f0da8a9852e339012c82180e8ac73451a3bc4347b881ff17a1141be40f445f7b",
    Path("firmware/components/esp_frp/src/work_internal.h"):
        "3b5db34f163aad4771d89ee7cc9b23a5cb778831220f1f4f8e45e56961da7b28",
}


def digest(path: Path) -> str:
    hash_value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hash_value.update(chunk)
    return hash_value.hexdigest()


def replace_once(path: Path, before: str, after: str) -> None:
    source = path.read_text(encoding="utf-8")
    if source.count(before) != 1:
        raise ValueError(f"探针接线锚点应恰好出现一次：{path}: {before!r}")
    path.write_text(source.replace(before, after, 1), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="C3 已到 REGISTERED/Pong 的冻结仓外 probe")
    parser.add_argument("destination", type=Path, help="尚不存在的仓外输出目录")
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    destination = args.destination.resolve()
    if destination.exists():
        parser.error(f"输出目录已存在：{destination}")
    for relative, expected in EXPECTED.items():
        actual = digest(source / relative)
        if actual != expected:
            parser.error(f"冻结输入不符：{relative} {actual} != {expected}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="c3-registered-32bit-", dir=destination.parent) as tmp:
        prepared = Path(tmp) / "probe"
        shutil.copytree(source, prepared, ignore=shutil.ignore_patterns("build"))
        local_app = prepared / APP
        shutil.copy2(Path(__file__).with_name("c3_registered_32bit_qemu_probe.c"),
                     local_app / "c3_registered_32bit_qemu_probe.c")
        replace_once(local_app / "CMakeLists.txt", '"frps_session_qemu_probe.c"',
                     '"frps_session_qemu_probe.c" "c3_registered_32bit_qemu_probe.c"')
        frps = local_app / "frps_session_qemu_probe.c"
        replace_once(frps, "void capacity_openeth_report_heap(const char *phase, int error);",
                     "void capacity_openeth_report_heap(const char *phase, int error);\n"
                     "bool capacity_registered_32bit_probe(efrp_client_t *client);")
        replace_once(frps,
                     '    report_status("result", result, &status);\n'
                     '    const efrp_result_t destroyed = efrp_destroy(&client, 5000);',
                     '    report_status("result", result, &status);\n'
                     '    const bool capacity_stable = ready && capacity_registered_32bit_probe(client);\n'
                     '    if (client) {\n'
                     '        (void)efrp_get_status(client, &status);\n'
                     '        report_status("after_32bit", result, &status);\n'
                     '    }\n'
                     '    const efrp_result_t destroyed = efrp_destroy(&client, 5000);')
        replace_once(frps,
                     '    return ready && destroyed == EFRP_OK && client == NULL;',
                     '    return ready && capacity_stable && destroyed == EFRP_OK && client == NULL;')
        prepared.rename(destination)

    print(f"C3_REGISTERED_32BIT_READY={destination}")
    print(f"PROBE_SHA256={digest(Path(__file__).with_name('c3_registered_32bit_qemu_probe.c'))}")


if __name__ == "__main__":
    main()
