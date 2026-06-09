import json
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

from tools.ui_compiler import uic


TEMP_ROOT = Path("build/ui_compiler_tests")


class UicTests(unittest.TestCase):
    def temp_dir(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        path = TEMP_ROOT / f"case_{uuid.uuid4().hex}"
        path.mkdir(parents=True)
        return path

    def write_json(self, path, data):
        path.write_text(json.dumps(data), encoding="utf-8")

    def sample_ui(self):
        return {
            "name": "ai_home",
            "resources": "ai_home.res.json",
            "root": {
                "type": "container",
                "id": "root",
                "geometry": {"x": 0, "y": 0, "w": 360, "h": 320},
                "layout": {
                    "type": "vbox",
                    "spacing": 8,
                    "padding": [12, 12, 8, 8],
                    "main_align": "center",
                    "cross_align": "center",
                },
                "children": [
                    {
                        "type": "label",
                        "id": "title",
                        "text_key": "AI_HOME_TITLE",
                        "size": {"w": "content", "h": "content"},
                        "align": "center",
                        "overflow": "wrap",
                    },
                    {
                        "type": "img",
                        "id": "robot_icon",
                        "src": "@image/robot",
                        "size": {"w": 32, "h": 32},
                    },
                    {
                        "type": "button",
                        "id": "confirm_btn",
                        "text": "OK",
                        "size": {"w": 80, "h": 32},
                    },
                ],
            },
        }

    def test_generates_header_and_source_for_supported_widgets(self):
        tmp_path = self.temp_dir()
        ui_path = tmp_path / "ai_home.ui.json"
        out_dir = tmp_path / "generated"
        self.write_json(ui_path, self.sample_ui())

        generated = uic.compile_ui_file(ui_path, out_dir)

        self.assertEqual(generated.header, out_dir / "ai_home_ui.h")
        self.assertEqual(generated.source, out_dir / "ai_home_ui.c")
        header = generated.header.read_text(encoding="utf-8")
        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("typedef struct {", header)
        self.assertIn("container_t* root;", header)
        self.assertIn("label_t* title;", header)
        self.assertIn("img_t* robot_icon;", header)
        self.assertIn("button_t* confirm_btn;", header)
        self.assertIn("bool ai_home_init_ui(lv_obj_t* parent, ai_home_ui_t* ui);", header)
        self.assertIn('#include "ai_home_ui.h"', source)
        self.assertIn('#include "ai_home_res.h"', source)
        self.assertIn("container_cfg_t root_cfg = container_default_cfg();", source)
        self.assertIn("ui->root = container_create(parent, &root_cfg);", source)
        self.assertIn("container_set_layout_vbox_spaced(ui->root, 8);", source)
        self.assertIn("container_set_padding_box(ui->root, 12, 12, 8, 8);", source)
        self.assertIn("label_cfg_t title_cfg = label_default_cfg();", source)
        self.assertIn('#include "app_def.h"', source)
        self.assertIn('title_cfg.text = app_get_str("AI_HOME_TITLE");', source)
        self.assertIn("title_cfg.align = LABEL_ALIGN_CENTER;", source)
        self.assertIn("title_cfg.overflow = LABEL_OVERFLOW_WRAP;", source)
        self.assertIn("ui->title = label_create(root_obj, &title_cfg);", source)
        self.assertIn("robot_icon_cfg.src = AI_HOME_RES_IMAGE_ROBOT;", source)
        self.assertIn("ui->robot_icon = img_create(root_obj, &robot_icon_cfg);", source)
        self.assertIn('confirm_btn_cfg.label.text = "OK";', source)
        self.assertIn("ui->confirm_btn = button_create(root_obj, &confirm_btn_cfg);", source)

    def test_generates_container_padding_without_layout(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        del ui["root"]["layout"]
        ui["root"]["padding"] = [12, 12, 100, 10]
        ui_path = tmp_path / "padding.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("container_set_padding_box(ui->root, 12, 12, 100, 10);", source)

    def test_generates_container_max_size(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["max_size"] = {"h": "100%"}
        ui_path = tmp_path / "max_size.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("root_cfg.max_h = LV_PCT(100);", source)

    def test_generates_container_height_policy(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["height_policy"] = "content_max_parent"
        ui_path = tmp_path / "height_policy.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn(
            "container_set_height_policy(ui->root, CONTAINER_HEIGHT_POLICY_CONTENT_MAX_PARENT);",
            source,
        )

    def test_generates_common_object_options(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["children"][0]["visible"] = False
        ui["root"]["children"][0]["clip_corner"] = True
        ui["root"]["children"][0]["floating"] = True
        ui["root"]["children"][0]["opa"] = "60%"
        ui["root"]["children"][0]["object_align"] = {"type": "bottom_mid", "x": 0, "y": 0}
        ui["root"]["children"][0]["scroll"] = {
            "enabled": True,
            "dir": "ver",
            "scrollbar": "auto",
        }
        ui["root"]["children"][0]["max_lines"] = 3
        ui["root"]["children"][0]["layout_item"] = {
            "fill_x": True,
            "fill_y": False,
            "grow": 1,
        }
        ui_path = tmp_path / "options.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("title_cfg.max_lines = 3;", source)
        self.assertIn("title_cfg.opa = LV_OPA_60;", source)
        self.assertIn("ui_widget_set_visible(UI_WIDGET(ui->title), false);", source)
        self.assertIn("lv_obj_set_style_clip_corner(title_obj, true, LV_PART_MAIN);", source)
        self.assertIn("lv_obj_add_flag(title_obj, LV_OBJ_FLAG_FLOATING);", source)
        self.assertIn("lv_obj_align(title_obj, LV_ALIGN_BOTTOM_MID, 0, 0);", source)
        self.assertIn("lv_obj_set_scroll_dir(title_obj, LV_DIR_VER);", source)
        self.assertIn("lv_obj_set_scrollbar_mode(title_obj, LV_SCROLLBAR_MODE_AUTO);", source)
        self.assertIn("container_set_child_fill(title_obj, true, false);", source)
        self.assertIn("container_set_child_grow(title_obj, 1);", source)

    def test_generates_label_font_config(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["children"][0]["font"] = {
            "weight": 26,
            "wordSpace": 1,
            "rowSpace": 2,
        }
        ui_path = tmp_path / "label_font.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("title_cfg.font.weight = 26;", source)
        self.assertIn("title_cfg.font.wordSpace = 1;", source)
        self.assertIn("title_cfg.font.rowSpace = 2;", source)

    def test_generates_image_full_config(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        image = ui["root"]["children"][1]
        image["offset_x"] = 3
        image["offset_y"] = -4
        image["zoom"] = 300
        image["rotation"] = 900
        image["opa"] = 128
        ui_path = tmp_path / "image_full.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("robot_icon_cfg.offset_x = 3;", source)
        self.assertIn("robot_icon_cfg.offset_y = -4;", source)
        self.assertIn("robot_icon_cfg.zoom = 300;", source)
        self.assertIn("robot_icon_cfg.rotation = 900;", source)
        self.assertIn("robot_icon_cfg.opa = 128;", source)

    def test_generates_button_label_config(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        button = ui["root"]["children"][2]
        del button["text"]
        button["label"] = {
            "text_key": "CONFIRM",
            "align": "right",
            "overflow": "wrap",
            "max_lines": 2,
            "font": {
                "weight": 24,
                "wordSpace": 1,
                "rowSpace": 2,
            },
            "opa": 200,
            "pad_hor": 4,
            "pad_ver": 2,
        }
        ui_path = tmp_path / "button_label.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn('#include "app_def.h"', source)
        self.assertIn('confirm_btn_cfg.label.text = app_get_str("CONFIRM");', source)
        self.assertIn("confirm_btn_cfg.label.align = LABEL_ALIGN_RIGHT;", source)
        self.assertIn("confirm_btn_cfg.label.overflow = LABEL_OVERFLOW_WRAP;", source)
        self.assertIn("confirm_btn_cfg.label.max_lines = 2;", source)
        self.assertIn("confirm_btn_cfg.label.font.weight = 24;", source)
        self.assertIn("confirm_btn_cfg.label.font.wordSpace = 1;", source)
        self.assertIn("confirm_btn_cfg.label.font.rowSpace = 2;", source)
        self.assertIn("confirm_btn_cfg.label.opa = 200;", source)
        self.assertIn("confirm_btn_cfg.label.pad_hor = 4;", source)
        self.assertIn("confirm_btn_cfg.label.pad_ver = 2;", source)

    def test_generates_roller_widget_config(self):
        tmp_path = self.temp_dir()
        ui = {
            "name": "prompter_picker",
            "root": {
                "type": "container",
                "id": "root",
                "children": [
                    {
                        "type": "roller",
                        "id": "document_roller",
                        "geometry": {"x": 12, "y": 20, "w": 336, "h": "content"},
                        "items": ["Alpha", "Beta"],
                        "label": {
                            "size": {"w": "100%", "h": "content"},
                            "font": {"weight": 32, "wordSpace": 1, "rowSpace": 2},
                            "align": "center",
                            "overflow": "clip",
                        },
                        "selected_font": {"weight": 36},
                        "overflow_mode": "expand_height",
                        "row_height": 60,
                        "row_gap": 16,
                        "selected_pad_ver": 4,
                        "radius": 6,
                        "border_width": 2,
                        "opa_normal": "60%",
                        "opa_selected": "cover",
                        "visible": False,
                    }
                ],
            },
        }
        ui_path = tmp_path / "prompter_picker.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        header = generated.header.read_text(encoding="utf-8")
        source = generated.source.read_text(encoding="utf-8")
        self.assertIn('#include "common/widgets/roller.h"', header)
        self.assertIn("roller_t* document_roller;", header)
        self.assertIn('static const char* document_roller_items[] = {"Alpha", "Beta"};', source)
        self.assertIn("document_roller_cfg.items = document_roller_items;", source)
        self.assertIn("document_roller_cfg.count = 2;", source)
        self.assertIn("document_roller_cfg.label.w = LV_PCT(100);", source)
        self.assertIn("document_roller_cfg.label.font.weight = 32;", source)
        self.assertIn("document_roller_cfg.selected_font.weight = 36;", source)
        self.assertIn("document_roller_cfg.overflow_mode = ROLLER_OVERFLOW_EXPAND_HEIGHT;", source)
        self.assertIn("document_roller_cfg.row_height = 60;", source)
        self.assertIn("document_roller_cfg.opa_normal = LV_OPA_60;", source)
        self.assertIn("document_roller_cfg.opa_selected = LV_OPA_COVER;", source)
        self.assertIn("ui->document_roller = roller_create(root_obj, &document_roller_cfg);", source)
        self.assertIn("lv_obj_t* document_roller_obj = roller_get_obj(ui->document_roller);", source)
        self.assertIn("ui_widget_set_position(UI_WIDGET(ui->document_roller), 12, 20);", source)
        self.assertIn("ui_widget_set_size(UI_WIDGET(ui->document_roller), 336, LV_SIZE_CONTENT);", source)
        self.assertIn("ui_widget_set_visible(UI_WIDGET(ui->document_roller), false);", source)

    def test_generates_overlay_widget_config(self):
        tmp_path = self.temp_dir()
        ui = {
            "name": "map_overlay",
            "root": {
                "type": "overlay",
                "id": "layer",
                "geometry": {"x": 0, "y": 0, "w": "100%", "h": "100%"},
                "max_items": 8,
                "point": {"size": 10, "opa": "60%"},
                "text": {
                    "size": {"w": 120, "h": "content"},
                    "text_key": "OVERLAY_LABEL",
                    "align": "center",
                    "font": {"weight": 24},
                    "opa": 220,
                },
                "visible": False,
            },
        }
        ui_path = tmp_path / "map_overlay.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        header = generated.header.read_text(encoding="utf-8")
        source = generated.source.read_text(encoding="utf-8")
        self.assertIn('#include "common/widgets/overlay.h"', header)
        self.assertIn("overlay_t* layer;", header)
        self.assertIn('#include "app_def.h"', source)
        self.assertIn("overlay_cfg_t layer_cfg = overlay_default_cfg();", source)
        self.assertIn("layer_cfg.max_items = 8;", source)
        self.assertIn("layer_cfg.point.size = 10;", source)
        self.assertIn("layer_cfg.point.opa = LV_OPA_60;", source)
        self.assertIn("layer_cfg.text.w = 120;", source)
        self.assertIn('layer_cfg.text.text = app_get_str("OVERLAY_LABEL");', source)
        self.assertIn("layer_cfg.text.align = LABEL_ALIGN_CENTER;", source)
        self.assertIn("layer_cfg.text.font.weight = 24;", source)
        self.assertIn("ui->layer = overlay_create(parent, &layer_cfg);", source)
        self.assertIn("lv_obj_t* layer_obj = overlay_get_obj(ui->layer);", source)
        self.assertIn("ui_widget_set_position(UI_WIDGET(ui->layer), 0, 0);", source)
        self.assertIn("ui_widget_set_size(UI_WIDGET(ui->layer), LV_PCT(100), LV_PCT(100));", source)
        self.assertIn("ui_widget_set_visible(UI_WIDGET(ui->layer), false);", source)

    def test_generates_paged_text_widget_config(self):
        tmp_path = self.temp_dir()
        ui = {
            "name": "reader_body",
            "root": {
                "type": "container",
                "id": "root",
                "children": [
                    {
                        "type": "paged_text",
                        "id": "body",
                        "geometry": {"x": 0, "y": 24, "w": 312, "h": 216},
                        "label": {
                            "text": "Hello",
                            "align": "left",
                            "overflow": "wrap",
                            "font": {"weight": 26, "wordSpace": 1, "rowSpace": 8},
                        },
                        "highlight": {
                            "mask_opa": "60%",
                            "border_width": 2,
                            "radius": 6,
                            "outset": 10,
                        },
                        "step_mode": "view_percent",
                        "step_percent": 75,
                    }
                ],
            },
        }
        ui_path = tmp_path / "reader_body.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        header = generated.header.read_text(encoding="utf-8")
        source = generated.source.read_text(encoding="utf-8")
        self.assertIn('#include "common/widgets/paged_text.h"', header)
        self.assertIn("paged_text_t* body;", header)
        self.assertIn("paged_text_cfg_t body_cfg = paged_text_default_cfg();", source)
        self.assertIn("body_cfg.label.x = 0;", source)
        self.assertIn("body_cfg.label.y = 24;", source)
        self.assertIn("body_cfg.label.w = 312;", source)
        self.assertIn("body_cfg.label.h = 216;", source)
        self.assertIn('body_cfg.label.text = "Hello";', source)
        self.assertIn("body_cfg.label.overflow = LABEL_OVERFLOW_WRAP;", source)
        self.assertIn("body_cfg.highlight.enabled = true;", source)
        self.assertIn("body_cfg.highlight.mask_opa = LV_OPA_60;", source)
        self.assertIn("body_cfg.highlight.border_width = 2;", source)
        self.assertIn("body_cfg.highlight.radius = 6;", source)
        self.assertIn("body_cfg.highlight.outset = 10;", source)
        self.assertIn("body_cfg.step_mode = PAGED_TEXT_STEP_VIEW_PERCENT;", source)
        self.assertIn("body_cfg.step_percent = 75;", source)
        self.assertIn("ui->body = paged_text_create(root_obj, &body_cfg);", source)
        self.assertIn("lv_obj_t* body_obj = paged_text_get_obj(ui->body);", source)

    def test_emits_scroll_options_after_container_layout(self):
        tmp_path = self.temp_dir()
        ui = {
            "name": "scroll_panel",
            "root": {
                "type": "container",
                "id": "root",
                "layout": {
                    "type": "vbox",
                    "spacing": 4,
                    "main_align": "start",
                    "cross_align": "start",
                },
                "scroll": {
                    "enabled": True,
                    "dir": "ver",
                    "scrollbar": "auto",
                },
            },
        }
        ui_path = tmp_path / "scroll_panel.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertLess(
            source.index("container_set_layout_vbox_spaced(ui->root, 4);"),
            source.index("lv_obj_add_flag(root_obj, LV_OBJ_FLAG_SCROLLABLE);"),
        )

    def test_generates_full_mode_roots(self):
        tmp_path = self.temp_dir()
        message_root = {
            "type": "container",
            "id": "root",
            "geometry": {"x": 0, "y": 0, "w": "100%", "h": "100%"},
            "opa": 0,
            "layout": {
                "type": "vbox",
                "spacing": 0,
                "main_align": "space_between",
                "cross_align": "center",
            },
            "children": [
                {
                    "type": "label",
                    "id": "body_label",
                    "text_key": "NOTIFY_MESSAGE_HINT",
                    "size": {"w": "content", "h": "content"},
                }
            ],
        }
        call_root = {
            "type": "container",
            "id": "root",
            "geometry": {"x": 0, "y": 0, "w": "100%", "h": "100%"},
            "opa": 70,
            "layout": {
                "type": "vbox",
                "spacing": 8,
                "main_align": "start",
                "cross_align": "center",
            },
            "children": [
                {
                    "type": "label",
                    "id": "body_label",
                    "text_key": "NOTIFY_CALL_HINT",
                    "size": {"w": "content", "h": "content"},
                }
            ],
        }
        ui = {
            "name": "notify_popup",
            "modes": {
                "message": {"root": message_root},
                "call": {"root": call_root},
            },
        }
        ui_path = tmp_path / "notify_popup.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        header = generated.header.read_text(encoding="utf-8")
        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("typedef enum {", header)
        self.assertIn("NOTIFY_POPUP_MODE_MESSAGE", header)
        self.assertIn("NOTIFY_POPUP_MODE_CALL", header)
        self.assertIn(
            "bool notify_popup_init_ui(lv_obj_t* parent, notify_popup_ui_t* ui, notify_popup_mode_t mode);",
            header,
        )
        self.assertEqual(source.count("ui->root = container_create"), 2)
        self.assertEqual(source.count("ui->body_label = label_create"), 2)
        self.assertIn("static bool notify_popup_init_ui_message", source)
        self.assertIn("static bool notify_popup_init_ui_call", source)
        self.assertIn("case NOTIFY_POPUP_MODE_MESSAGE:", source)
        self.assertIn("case NOTIFY_POPUP_MODE_CALL:", source)
        self.assertIn('body_label_cfg.text = app_get_str("NOTIFY_MESSAGE_HINT");', source)
        self.assertIn('body_label_cfg.text = app_get_str("NOTIFY_CALL_HINT");', source)
        self.assertIn("container_set_layout_vbox_spaced(ui->root, 8);", source)
        self.assertIn(
            "container_set_align(ui->root, CONTAINER_ALIGN_START, CONTAINER_ALIGN_CENTER, CONTAINER_ALIGN_START);",
            source,
        )

    def test_rejects_modes_with_different_handle_sets(self):
        tmp_path = self.temp_dir()
        root = {
            "type": "container",
            "id": "root",
            "children": [{"type": "label", "id": "title", "text": "Hello"}],
        }
        bad_root = {
            "type": "container",
            "id": "root",
            "children": [{"type": "label", "id": "subtitle", "text": "Hello"}],
        }
        ui = {
            "name": "bad_modes",
            "modes": {
                "a": {"root": root},
                "b": {"root": bad_root},
            },
        }
        ui_path = tmp_path / "bad_modes.ui.json"
        self.write_json(ui_path, ui)

        with self.assertRaisesRegex(ValueError, "same ids"):
            uic.compile_ui_file(ui_path, tmp_path / "generated")

    def test_rejects_mode_without_full_root(self):
        tmp_path = self.temp_dir()
        ui = {
            "name": "bad_modes",
            "modes": {
                "message": {
                    "root": {
                        "type": "container",
                        "id": "root",
                    }
                },
                "call": {
                    "root": {
                        "opa": 70,
                    }
                },
            },
        }
        ui_path = tmp_path / "bad_modes.ui.json"
        self.write_json(ui_path, ui)

        with self.assertRaisesRegex(ValueError, "type"):
            uic.compile_ui_file(ui_path, tmp_path / "generated")

    def test_rejects_mode_object_without_root(self):
        tmp_path = self.temp_dir()
        ui = {
            "name": "bad_modes",
            "modes": {
                "message": {
                    "type": "container",
                    "id": "root",
                }
            },
        }
        ui_path = tmp_path / "bad_modes.ui.json"
        self.write_json(ui_path, ui)

        with self.assertRaisesRegex(ValueError, "root"):
            uic.compile_ui_file(ui_path, tmp_path / "generated")

    def test_legacy_root_mode_still_generates_original_signature(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui_path = tmp_path / "ai_home.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        header = generated.header.read_text(encoding="utf-8")
        self.assertIn("bool ai_home_init_ui(lv_obj_t* parent, ai_home_ui_t* ui);", header)
        self.assertNotIn("ai_home_mode_t", header)

    def test_rejects_duplicate_ids(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["children"][0]["id"] = "root"
        ui_path = tmp_path / "bad.ui.json"
        self.write_json(ui_path, ui)

        with self.assertRaisesRegex(ValueError, "duplicate id"):
            uic.compile_ui_file(ui_path, tmp_path / "generated")

    def test_rejects_invalid_layout_type(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["layout"]["type"] = "grid"
        ui_path = tmp_path / "bad.ui.json"
        self.write_json(ui_path, ui)

        with self.assertRaisesRegex(ValueError, "layout.type"):
            uic.compile_ui_file(ui_path, tmp_path / "generated")

    def test_generates_valid_local_handle_for_internal_container(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["children"].append(
            {
                "type": "container",
                "geometry": {"x": 4, "y": 6, "w": 100, "h": 40},
                "children": [
                    {
                        "type": "label",
                        "id": "nested_label",
                        "text": "Nested",
                        "geometry": {"x": 0, "y": 0, "w": 80, "h": 20},
                    }
                ],
            }
        )
        ui_path = tmp_path / "internal.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn("container_t* tmp_", source)
        self.assertNotIn("container_get_obj(container_t*", source)
        self.assertIn("ui->nested_label = label_create(container_", source)

    def test_uses_resource_file_name_for_include_and_macros(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["resources"] = "../shared/common_icons.res.json"
        ui_path = tmp_path / "custom.ui.json"
        self.write_json(ui_path, ui)

        generated = uic.compile_ui_file(ui_path, tmp_path / "generated")

        source = generated.source.read_text(encoding="utf-8")
        self.assertIn('#include "common_icons_res.h"', source)
        self.assertIn("robot_icon_cfg.src = COMMON_ICONS_RES_IMAGE_ROBOT;", source)

    def test_rejects_image_resource_reference_without_resources_file(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        del ui["resources"]
        ui_path = tmp_path / "missing_res.ui.json"
        self.write_json(ui_path, ui)

        with self.assertRaisesRegex(ValueError, "resources"):
            uic.compile_ui_file(ui_path, tmp_path / "generated")

    def test_cli_reports_validation_errors_without_traceback(self):
        tmp_path = self.temp_dir()
        ui = self.sample_ui()
        ui["root"]["layout"]["type"] = "grid"
        ui_path = tmp_path / "bad.ui.json"
        self.write_json(ui_path, ui)

        result = subprocess.run(
            [
                sys.executable,
                "tools/ui_compiler/uic.py",
                str(ui_path),
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
