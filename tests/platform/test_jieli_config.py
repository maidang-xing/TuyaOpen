import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class JieliConfigTest(unittest.TestCase):
    def test_platform_registry_contains_jieli(self):
        config = (ROOT / "platform" / "platform_config.yaml").read_text()
        self.assertIn("name: JIELI", config)
        self.assertIn("repo: local", config)

    def test_board_catalog_contains_ac7916a(self):
        board_kconfig = (ROOT / "boards" / "Kconfig").read_text()
        self.assertIn("BOARD_ENABLE_JIELI", board_kconfig)
        self.assertIn('rsource "./JIELI/Kconfig"', board_kconfig)

        platform_kconfig = (ROOT / "platform" / "JIELI" / "Kconfig")
        self.assertTrue(platform_kconfig.is_file())
        self.assertIn("PLATFORM_JIELI", platform_kconfig.read_text())

        platform_config = ROOT / "platform" / "JIELI" / "platform_config.cmake"
        self.assertIn("PLATFORM_SKIP_DEFAULT_COMPONENTS ON", platform_config.read_text())

        root_cmake = ROOT / "CMakeLists.txt"
        self.assertIn("PLATFORM_SKIP_DEFAULT_COMPONENTS", root_cmake.read_text())

        board_config = ROOT / "boards" / "JIELI" / "AC7916A" / "Kconfig"
        self.assertTrue(board_config.is_file())
        self.assertIn("CHIP_AC7916A", board_config.read_text())


if __name__ == "__main__":
    unittest.main()
