import io
import unittest

from PIL import Image, ImageDraw

from tools.ui_designer import preview_runtime


class PreviewRuntimeTests(unittest.TestCase):
    def test_renders_png_for_simple_label(self):
        doc = {
            "name": "demo",
            "root": {
                "type": "container",
                "id": "root",
                "geometry": {"x": 0, "y": 0, "w": "100%", "h": "100%"},
                "children": [
                    {
                        "type": "label",
                        "id": "title",
                        "geometry": {"x": 12, "y": 16, "w": 200, "h": "content"},
                        "text": "Hello",
                    }
                ],
            },
        }

        png = preview_runtime.render_png(doc, width=120, height=80)

        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        image = Image.open(io.BytesIO(png))
        self.assertEqual(image.size, (120, 80))

    def test_visible_false_skips_rendering_subtree(self):
        doc = {
            "name": "demo",
            "root": {
                "type": "container",
                "id": "root",
                "geometry": {"x": 0, "y": 0, "w": "100%", "h": "100%"},
                "children": [
                    {
                        "type": "label",
                        "id": "title",
                        "visible": False,
                        "geometry": {"x": 0, "y": 0, "w": 100, "h": 40},
                        "text": "Hello",
                    }
                ],
            },
        }

        png = preview_runtime.render_png(doc, width=120, height=80)
        image = Image.open(io.BytesIO(png))

        self.assertEqual(set(image.getdata()), {(0, 0, 0, 255)})

    def test_show_hidden_renders_visible_false_subtree_for_designer(self):
        doc = {
            "name": "demo",
            "root": {
                "type": "container",
                "id": "root",
                "visible": False,
                "geometry": {"x": 0, "y": 0, "w": "100%", "h": "100%"},
                "children": [
                    {
                        "type": "label",
                        "id": "title",
                        "geometry": {"x": 0, "y": 0, "w": 100, "h": 40},
                        "text": "Hello",
                    }
                ],
            },
        }

        png = preview_runtime.render_png(doc, width=120, height=80, show_hidden=True)
        image = Image.open(io.BytesIO(png))

        self.assertGreater(len(set(image.getdata())), 1)

    def test_selects_requested_mode(self):
        doc = {
            "name": "demo",
            "modes": {
                "a": {"root": {"type": "container", "id": "a_root"}},
                "b": {"root": {"type": "container", "id": "b_root"}},
            },
        }

        root = preview_runtime.active_root(doc, "b")

        self.assertEqual(root["id"], "b_root")

    def test_resolves_text_key_from_i18n(self):
        node = {"type": "label", "text_key": "HELLO"}

        text = preview_runtime.resolve_label_text(node, {"HELLO": "你好"})

        self.assertEqual(text, "你好")

    def test_resolves_button_nested_label_text_key(self):
        node = {"type": "button", "label": {"text_key": "OK"}}

        text = preview_runtime.resolve_button_label_text(node, {"OK": "确定"})

        self.assertEqual(text, "确定")

    def test_label_font_uses_node_font_before_default(self):
        node = {"type": "label", "font": {"weight": 30}}

        self.assertEqual(preview_runtime.font_size(node, {"weight": 26}), 30)

    def test_label_font_uses_default_font_config(self):
        node = {"type": "label"}

        self.assertEqual(preview_runtime.font_size(node, {"weight": 26}), 26)

    def test_label_align_center_offsets_line_within_box(self):
        draw = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
        font = preview_runtime.load_font(20)
        box = preview_runtime.Box(10, 0, 100, 30)

        x = preview_runtime.aligned_text_x(draw, "Hi", font, box, 0, "center")

        width = preview_runtime.text_width(draw, "Hi", font, 0)
        self.assertEqual(x, 10 + (100 - width) // 2)

    def test_label_default_align_matches_widget_default_center(self):
        self.assertEqual(preview_runtime.label_align({"type": "label"}), "center")

    def test_object_align_places_box_inside_parent(self):
        node = {
            "type": "label",
            "geometry": {"w": 20, "h": 10},
            "object_align": {"type": "bottom_right", "x": -5, "y": -6},
        }
        parent = preview_runtime.Box(0, 0, 100, 80)

        box = preview_runtime.resolve_box(node, parent, preview_runtime.Box(0, 0, 20, 10))

        self.assertEqual(box, preview_runtime.Box(75, 64, 20, 10))

    def test_label_style_draws_background_and_border(self):
        image = Image.new("RGBA", (40, 30), (30, 30, 30, 255))
        draw = ImageDraw.Draw(image)
        node = {
            "type": "label",
            "geometry": {"x": 5, "y": 5, "w": 20, "h": 12},
            "text": "",
            "border_width": 1,
            "opa": 255,
        }

        preview_runtime.render_node(
            image,
            draw,
            node,
            preview_runtime.Box(0, 0, 40, 30),
            preview_runtime.Box(0, 0, 20, 12),
            {},
            {},
            {},
            None,
        )

        self.assertEqual(image.getpixel((6, 6)), (0, 0, 0, 255))
        self.assertEqual(image.getpixel((5, 5)), preview_runtime.BORDER_COLOR)

    def test_content_height_uses_i18n_text_key_value(self):
        node = {"type": "label", "text_key": "LONG", "font": {"weight": 20}}

        fallback_height = preview_runtime.estimate_content_height(node, 80)
        resolved_height = preview_runtime.estimate_content_height(
            node,
            80,
            i18n={"LONG": "Open the SmartGlasses app and connect via Bluetooth"},
        )

        self.assertGreater(resolved_height, fallback_height)

    def test_hbox_grow_uses_remaining_width(self):
        children = [
            {"type": "img", "size": {"w": 32, "h": 32}},
            {"type": "label", "size": {"w": "content", "h": "content"}, "layout_item": {"grow": 1}},
        ]

        widths = preview_runtime.hbox_child_widths(children, 120, 12)

        self.assertEqual(widths, [32, 76])

    def test_resolve_box_keeps_layout_fallback_width_for_content_width(self):
        node = {"type": "label", "size": {"w": "content", "h": "content"}, "text": "hello"}
        parent = preview_runtime.Box(0, 0, 120, 100)
        fallback = preview_runtime.Box(44, 0, 76, 32)

        box = preview_runtime.resolve_box(node, parent, fallback)

        self.assertEqual(box.w, 76)

    def test_layout_child_uses_precomputed_content_box(self):
        image = Image.new("RGBA", (120, 80))
        draw = ImageDraw.Draw(image)
        node = {"type": "container", "size": {"w": "100%", "h": "100%"}}
        parent = preview_runtime.Box(0, 0, 120, 80)
        fallback = preview_runtime.Box(10, 0, 100, 40)

        box = preview_runtime.render_node(image, draw, node, parent, fallback, {}, {}, {}, None, True)

        self.assertEqual(box, fallback)

    def test_content_max_parent_clamps_height_to_parent(self):
        node = {
            "type": "container",
            "size": {"w": "100%", "h": "content"},
            "height_policy": "content_max_parent",
            "children": [
                {"type": "label", "text": "1\n2\n3\n4\n5", "font": {"weight": 20}},
            ],
        }
        parent = preview_runtime.Box(0, 0, 120, 40)

        box = preview_runtime.resolve_box(node, parent, parent)

        self.assertEqual(box.h, 40)

    def test_scroll_bottom_offset_aligns_to_line_height(self):
        node = {
            "type": "container",
            "scroll": {"enabled": True},
            "layout": {"type": "vbox", "spacing": 0},
            "children": [
                {"type": "label", "text": "1\n2\n3\n4\n5", "font": {"weight": 20}},
            ],
        }
        line_starts = preview_runtime.text_line_starts(node, 100)
        row_h = line_starts[1] - line_starts[0]

        offset = preview_runtime.scroll_bottom_offset(node, 100, row_h * 2 + 3)

        self.assertIn(offset, line_starts)

    def test_main_axis_center_offsets_children(self):
        start, spacing = preview_runtime.main_axis_start_and_spacing(0, 100, [20, 20], 10, "center")

        self.assertEqual(start, 25)
        self.assertEqual(spacing, 10)

    def test_main_axis_space_between_expands_spacing(self):
        start, spacing = preview_runtime.main_axis_start_and_spacing(0, 100, [20, 20], 10, "space_between")

        self.assertEqual(start, 0)
        self.assertEqual(spacing, 60)

    def test_decodes_lvgl_indexed_image(self):
        image = preview_runtime.decode_lvgl_image({
            "w": 2,
            "h": 1,
            "stride": 1,
            "cf": "LV_COLOR_FORMAT_I1",
            "data": [
                0x00, 0x00, 0x00, 0xff,
                0xff, 0xff, 0xff, 0xff,
                0x40,
            ],
        })

        self.assertIsNotNone(image)
        self.assertEqual(image.getpixel((0, 0)), (0, 0, 0, 255))
        self.assertEqual(image.getpixel((1, 0)), (255, 255, 255, 255))

    def test_transforms_image_zoom_and_opacity(self):
        image = Image.new("RGBA", (4, 4), (255, 255, 255, 255))
        node = {"zoom": 512, "opa": 128}

        transformed = preview_runtime.transform_image(image, node)

        self.assertEqual(transformed.size, (8, 8))
        self.assertEqual(transformed.getpixel((0, 0))[3], 128)

    def test_semantic_opacity_values_match_ui_json(self):
        self.assertEqual(preview_runtime.opa_value("transparent"), 0)
        self.assertEqual(preview_runtime.opa_value("cover"), 255)
        self.assertEqual(preview_runtime.opa_value("60%"), 153)

    def test_roller_preview_defaults_match_widget_defaults(self):
        self.assertEqual(preview_runtime.ROLLER_DEFAULT_RADIUS, 16)
        self.assertEqual(preview_runtime.ROLLER_DEFAULT_BORDER_WIDTH, 2)
        self.assertEqual(preview_runtime.ROLLER_DEFAULT_NORMAL_OPA, 178)
        self.assertEqual(preview_runtime.ROLLER_DEFAULT_SELECTED_OPA, 255)

    def test_layout_padding_matches_uic_location(self):
        node = {
            "type": "container",
            "layout": {
                "type": "vbox",
                "padding": [12, 12, 0, 0],
            },
        }

        self.assertEqual(preview_runtime.padding_box(node), (12, 12, 0, 0))

    def test_roller_visible_rows_matches_widget_three_label_model(self):
        node = {"type": "roller", "items": ["1", "2", "3", "4", "5"], "selected_index": 2}

        self.assertEqual(
            preview_runtime.roller_visible_rows(node),
            [("2", False), ("3", True), ("4", False)],
        )

    def test_roller_preview_draws_normal_item_borders(self):
        image = Image.new("RGBA", (100, 60), (0, 0, 0, 255))
        draw = ImageDraw.Draw(image)
        node = {
            "type": "roller",
            "items": ["", "", ""],
            "selected_index": 1,
            "row_height": 20,
            "row_gap": 0,
            "radius": 0,
            "border_width": 1,
            "opa_normal": "cover",
            "opa_selected": "cover",
        }

        preview_runtime.draw_roller(draw, node, preview_runtime.Box(0, 0, 100, 60), {}, {}, None)

        self.assertEqual(image.getpixel((0, 0)), preview_runtime.BORDER_COLOR)
        self.assertEqual(image.getpixel((0, 20)), preview_runtime.BORDER_COLOR)
        self.assertEqual(image.getpixel((0, 40)), preview_runtime.BORDER_COLOR)

    def test_roller_text_uses_center_vertical_alignment(self):
        box = preview_runtime.Box(0, 10, 100, 60)
        node = {"type": "label"}

        y = preview_runtime.text_block_y(box, node, line_count=1, row_h=20, vertical_align="center")

        self.assertEqual(y, 30)

    def test_preview_widget_defaults_match_common_widgets(self):
        self.assertEqual(preview_runtime.BUTTON_DEFAULT_RADIUS, 12)
        self.assertEqual(preview_runtime.BUTTON_DEFAULT_BORDER_WIDTH, 1)
        self.assertEqual(preview_runtime.BUTTON_DEFAULT_OPA, 255)
        self.assertEqual(preview_runtime.OVERLAY_DEFAULT_MAX_ITEMS, 16)
        self.assertEqual(preview_runtime.OVERLAY_DEFAULT_POINT_SIZE, 6)
        self.assertEqual(preview_runtime.OVERLAY_DEFAULT_POINT_OPA, 255)
        self.assertEqual(preview_runtime.PAGED_TEXT_DEFAULT_MASK_OPA, 153)
        self.assertEqual(preview_runtime.PAGED_TEXT_DEFAULT_BORDER_WIDTH, 2)
        self.assertEqual(preview_runtime.PAGED_TEXT_DEFAULT_RADIUS, 6)
        self.assertEqual(preview_runtime.PAGED_TEXT_DEFAULT_OUTSET, 10)
        self.assertEqual(preview_runtime.PAGED_TEXT_DEFAULT_STEP_PERCENT, 100)

    def test_renders_roller_widget(self):
        doc = {
            "name": "roller",
            "root": {
                "type": "roller",
                "id": "file_roller",
                "geometry": {"x": 0, "y": 0, "w": 120, "h": 120},
                "items": ["A.txt", "B.txt", "C.txt"],
                "row_height": 30,
                "row_gap": 4,
            },
        }

        png = preview_runtime.render_png(doc, width=120, height=120)
        image = Image.open(io.BytesIO(png))

        self.assertGreater(len(set(image.getdata())), 1)

    def test_paged_text_content_height_uses_label_config(self):
        node = {
            "type": "paged_text",
            "label": {
                "text": "这是一段用于换行的分页文本",
                "font": {"weight": 20},
            },
        }

        height = preview_runtime.estimate_content_height(node, 80)

        self.assertGreater(height, 40)

    def test_paged_text_outset_insets_text_without_shrinking_static_frame(self):
        node = {"type": "paged_text", "highlight": {"outset": 10}}
        box = preview_runtime.Box(20, 30, 120, 80)

        self.assertEqual(preview_runtime.inner_highlight_label_box(node, box), preview_runtime.Box(30, 40, 100, 60))
        self.assertEqual(preview_runtime.paged_text_static_highlight_frame_box(box), box)

    def test_rejects_unknown_mode(self):
        doc = {
            "name": "demo",
            "modes": {
                "a": {"root": {"type": "container", "id": "a_root"}},
            },
        }

        with self.assertRaises(ValueError):
            preview_runtime.active_root(doc, "missing")


if __name__ == "__main__":
    unittest.main()
