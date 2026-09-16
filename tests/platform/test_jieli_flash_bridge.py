import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform" / "JIELI"))

import platform_flash_bridge


class _Logger:
    def __init__(self):
        self.messages = []

    def info(self, message):
        self.messages.append(message)


class JieliFlashBridgeTest(unittest.TestCase):
    def test_missing_uploader_is_reported(self):
        logger = _Logger()
        result = platform_flash_bridge.platform_flash(
            using_data={},
            binfile="/missing/jieli.bin",
            port="/dev/ttyUSB0",
            baud=115200,
            boards_root="/boards",
            logger=logger,
        )
        self.assertFalse(result["success"])
        self.assertIn("firmware image not found", result["message"])


if __name__ == "__main__":
    unittest.main()
