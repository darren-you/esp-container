from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import counter_guest  # noqa: E402
import product_package  # noqa: E402


def section_offset(data: bytes, section_id: int) -> int:
    reader = counter_guest.Reader(data)
    reader.take(8)
    while reader.offset < len(data):
        current = reader.byte()
        length = reader.u32()
        offset = reader.offset
        reader.take(length)
        if current == section_id:
            return offset
    raise AssertionError(f"缺少 section {section_id}")


class CounterGuestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        sdk_path = os.environ.get("WASI_SDK_ROOT")
        if not sdk_path:
            raise unittest.SkipTest("设置 WASI_SDK_ROOT 后运行真实 wasi-sdk-33 编译检查")
        cls.sdk = Path(sdk_path)
        cls.temp = tempfile.TemporaryDirectory()
        cls.output = Path(cls.temp.name) / "one" / "counter.wasm"
        counter_guest.build(cls.sdk, cls.output)
        cls.wasm = cls.output.read_bytes()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp.cleanup()

    def test_compiler_output_is_deterministic_and_accepted_by_package_scanner(self) -> None:
        second = Path(self.temp.name) / "two" / "other-name.wasm"
        counter_guest.build(self.sdk, second)
        self.assertEqual(self.wasm, second.read_bytes())
        counter_guest.check_wasm(self.wasm)
        product_package._wasm(self.wasm)

    def test_rejects_wrong_guest_signature(self) -> None:
        changed = bytearray(self.wasm)
        exports = section_offset(changed, 7)
        marker = b"econtainer_on_event\x00\x01"
        index = changed.find(marker, exports)
        self.assertGreaterEqual(index, exports)
        changed[index + len(marker) - 1] = 0
        with self.assertRaisesRegex(counter_guest.GuestError, "函数签名"):
            counter_guest.check_wasm(changed)

    def test_rejects_shared_or_unbounded_memory(self) -> None:
        changed = bytearray(self.wasm)
        offset = section_offset(changed, 5)
        self.assertEqual(changed[offset:offset + 4], b"\x01\x01\x01\x01")
        changed[offset + 1] = 3
        with self.assertRaisesRegex(counter_guest.GuestError, "固定 64 KiB"):
            counter_guest.check_wasm(changed)
        changed[offset + 1] = 0
        with self.assertRaisesRegex(counter_guest.GuestError, "固定 64 KiB"):
            counter_guest.check_wasm(changed)

    def test_rejects_old_two_page_guest(self) -> None:
        changed = bytearray(self.wasm)
        offset = section_offset(changed, 5)
        changed[offset + 2:offset + 4] = b"\x02\x02"
        with self.assertRaisesRegex(counter_guest.GuestError, "固定 64 KiB"):
            counter_guest.check_wasm(changed)

    def test_rejects_feature_section_and_automatic_start(self) -> None:
        feature_name = b"target_features"
        custom = b"\x00" + bytes((len(feature_name) + 1, len(feature_name))) + feature_name
        with self.assertRaisesRegex(counter_guest.GuestError, "目标特性"):
            counter_guest.check_wasm(self.wasm + custom)
        with self.assertRaisesRegex(counter_guest.GuestError, "Classic profile"):
            counter_guest.check_wasm(self.wasm + b"\x08\x01\x00")


if __name__ == "__main__":
    unittest.main()
