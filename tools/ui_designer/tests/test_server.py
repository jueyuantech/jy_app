import json
import shutil
import uuid
import unittest
from pathlib import Path

from tools.ui_designer import server


class UiDesignerServerTests(unittest.TestCase):
    def setUp(self):
        temp_root = Path("build/ui_designer_tests").resolve()
        temp_root.mkdir(parents=True, exist_ok=True)
        self.root = temp_root / f"case_{uuid.uuid4().hex}"
        self.root.mkdir(parents=True, exist_ok=True)
        (self.root / "system" / "popups").mkdir(parents=True)
        (self.root / "apps" / "demo").mkdir(parents=True)
        (self.root / "images").mkdir(parents=True)
        (self.root / "lfsd" / "system" / "font").mkdir(parents=True)
        (self.root / "lfsd" / "system" / "config.json").write_text(
            json.dumps({"fontinfo": {"weight": 26, "wordSpace": 1, "rowSpace": 2}}),
            encoding="utf-8",
        )
        (self.root / "lfsd" / "system" / "font" / "font.ttf").write_bytes(b"font")
        (self.root / "ui.res.json").write_text(
            json.dumps({"name": "ui", "images": {"robot": {"symbol": "img_robot", "type": "lv_image_dsc_t"}}}),
            encoding="utf-8",
        )
        (self.root / "StringPool.csv").write_text(
            "StringID,en-US,zh-CN\nHELLO,Hello,你好\nEMPTY,,\n",
            encoding="utf-8",
        )
        (self.root / "system" / "popups" / "demo.ui.json").write_text(
            json.dumps({"name": "demo", "root": {"type": "container", "id": "root"}}),
            encoding="utf-8",
        )
        (self.root / "apps" / "demo" / "screen.ui.json").write_text(
            json.dumps({"name": "screen", "root": {"type": "container", "id": "root"}}),
            encoding="utf-8",
        )
        (self.root / "images" / "img_robot.c").write_text(
            """
#include "img_robot.h"
static const uint8_t img_robot_map[] = {
  0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x01,
};
lv_image_dsc_t img_robot = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 2,
    .header.h = 1,
    .header.stride = 1,
    .header.cf = LV_COLOR_FORMAT_I1,
    .data = img_robot_map,
    .data_size = sizeof(img_robot_map),
};
""",
            encoding="utf-8",
        )

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_lists_ui_json_files_from_apps_and_system(self):
        files = server.list_ui_files(self.root)

        self.assertEqual(files, ["apps/demo/screen.ui.json", "system/popups/demo.ui.json"])

    def test_reads_root_resource_file(self):
        resources = server.read_resources(self.root)

        self.assertEqual(resources["name"], "ui")
        self.assertIn("robot", resources["images"])

    def test_reads_i18n_from_string_pool(self):
        i18n = server.read_i18n(self.root)

        self.assertEqual(i18n["default_locale"], "zh-CN")
        self.assertEqual(i18n["locales"], ["en-US", "zh-CN"])
        self.assertEqual(i18n["strings"]["zh-CN"]["HELLO"], "你好")

    def test_reads_lvgl_c_array_for_preview(self):
        resources = server.read_preview_resources(self.root)

        robot = resources["_decoded_images"]["robot"]
        self.assertEqual(robot["w"], 2)
        self.assertEqual(robot["h"], 1)
        self.assertEqual(robot["cf"], "LV_COLOR_FORMAT_I1")
        self.assertEqual(robot["data"][-1], 1)

    def test_reads_default_font_info(self):
        font = server.read_default_font_info(self.root)

        self.assertEqual(font, {"weight": 26, "wordSpace": 1, "rowSpace": 2})

    def test_resolves_system_font_file(self):
        self.assertEqual(
            server.resolve_system_font_file(self.root),
            self.root / "lfsd" / "system" / "font" / "font.ttf",
        )

    def test_rejects_paths_outside_supported_ui_roots(self):
        with self.assertRaises(ValueError):
            server.resolve_ui_path(self.root, "../system/popups/demo.ui.json")
        with self.assertRaises(ValueError):
            server.resolve_ui_path(self.root, "tools/ui_compiler/examples/ai_home.ui.json")

    def test_writes_pretty_json_with_trailing_newline(self):
        payload = {"name": "demo", "root": {"type": "container", "id": "root", "children": []}}

        target = server.write_ui_file(self.root, "system/popups/demo.ui.json", payload)

        data = target.read_text(encoding="utf-8")
        self.assertTrue(data.endswith("\n"))
        self.assertIn('  "root"', data)


if __name__ == "__main__":
    unittest.main()
