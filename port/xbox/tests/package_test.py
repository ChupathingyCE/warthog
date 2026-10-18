"""tools/xbox_package.py's devkit memory flag: the XBE header's init flags
(offset 0x124) get XINIT_LIMIT_DEVKIT_MEMORY (0x4) set or cleared, and nothing
else changes.

    python3 port/xbox/tests/package_test.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import xbox_package  # noqa: E402


def image(flags: int) -> bytes:
    data = bytearray(b"XBEH" + bytes(0x200))
    data[0x124:0x128] = flags.to_bytes(4, "little")
    data[0x300 - 4:0x300] = b"tail"
    return bytes(data)


class DevkitMemoryLimit(unittest.TestCase):
    def flags(self, data: bytes) -> int:
        return int.from_bytes(data[0x124:0x128], "little")

    def test_set_keeps_the_other_flags(self):
        patched = xbox_package.set_devkit_memory_limit(image(0x1), True)
        self.assertEqual(self.flags(patched), 0x5)

    def test_clear(self):
        patched = xbox_package.set_devkit_memory_limit(image(0x5), False)
        self.assertEqual(self.flags(patched), 0x1)

    def test_only_the_flags_change(self):
        before = image(0x1)
        after = xbox_package.set_devkit_memory_limit(before, True)
        self.assertEqual(len(before), len(after))
        self.assertEqual(before[:0x124], after[:0x124])
        self.assertEqual(before[0x128:], after[0x128:])

    def test_not_an_xbe(self):
        with self.assertRaises(ValueError):
            xbox_package.set_devkit_memory_limit(b"MZ" + bytes(0x200), True)
        with self.assertRaises(ValueError):
            xbox_package.set_devkit_memory_limit(b"XBEH", True)


if __name__ == "__main__":
    unittest.main()
