import json
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

from tools.ui_compiler import rcc


TEMP_ROOT = Path("build/ui_compiler_tests")


class RccTests(unittest.TestCase):
    def temp_dir(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        path = TEMP_ROOT / f"case_{uuid.uuid4().hex}"
        path.mkdir(parents=True)
        return path

    def test_generates_image_resource_header(self):
        tmp_path = self.temp_dir()
        res_path = tmp_path / "ai_home.res.json"
        out_dir = tmp_path / "generated"
        res_path.write_text(
            json.dumps(
                {
                    "name": "ai_home",
                    "images": {
                        "robot": {
                            "symbol": "img_icon_robot_4bit",
                            "type": "lv_image_dsc_t",
                        }
                    },
                }
            ),
            encoding="utf-8",
        )

        header_path = rcc.compile_resource_file(res_path, out_dir)

        self.assertEqual(header_path, out_dir / "ai_home_res.h")
        text = header_path.read_text(encoding="utf-8")
        self.assertIn("#ifndef AI_HOME_RES_H", text)
        self.assertIn("extern lv_image_dsc_t img_icon_robot_4bit;", text)
        self.assertIn("#define AI_HOME_RES_IMAGE_ROBOT (&img_icon_robot_4bit)", text)

    def test_generates_file_image_resource_header(self):
        tmp_path = self.temp_dir()
        res_path = tmp_path / "ui.res.json"
        out_dir = tmp_path / "generated"
        res_path.write_text(
            json.dumps(
                {
                    "name": "ui",
                    "images": {
                        "airpods": {
                            "path": "/romfs/system/images/airpods.jpg",
                        }
                    },
                }
            ),
            encoding="utf-8",
        )

        header_path = rcc.compile_resource_file(res_path, out_dir)

        text = header_path.read_text(encoding="utf-8")
        self.assertNotIn("extern", text)
        self.assertIn('#define UI_RES_IMAGE_AIRPODS "/romfs/system/images/airpods.jpg"', text)

    def test_rejects_missing_name(self):
        tmp_path = self.temp_dir()
        res_path = tmp_path / "broken.res.json"
        res_path.write_text(json.dumps({"images": {}}), encoding="utf-8")

        with self.assertRaisesRegex(ValueError, "name"):
            rcc.compile_resource_file(res_path, tmp_path / "generated")

    def test_rejects_image_without_symbol(self):
        tmp_path = self.temp_dir()
        res_path = tmp_path / "broken.res.json"
        res_path.write_text(
            json.dumps({"name": "bad", "images": {"robot": {"type": "lv_image_dsc_t"}}}),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(ValueError, "images.robot.symbol"):
            rcc.compile_resource_file(res_path, tmp_path / "generated")

    def test_cli_reports_validation_errors_without_traceback(self):
        tmp_path = self.temp_dir()
        res_path = tmp_path / "broken.res.json"
        res_path.write_text(json.dumps({"images": {}}), encoding="utf-8")

        result = subprocess.run(
            [
                sys.executable,
                "tools/ui_compiler/rcc.py",
                str(res_path),
                "--out-dir",
                str(tmp_path / "generated"),
            ],
            cwd=Path.cwd(),
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
