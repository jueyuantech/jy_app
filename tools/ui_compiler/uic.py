#!/usr/bin/env python3
"""Generate jy_app widget creation code from UI JSON files."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


WIDGET_TYPES = {
    "container": "container_t",
    "label": "label_t",
    "img": "img_t",
    "button": "button_t",
    "overlay": "overlay_t",
    "paged_text": "paged_text_t",
    "roller": "roller_t",
}


@dataclass(frozen=True)
class GeneratedUi:
    header: Path
    source: Path


def _load_json(input_path: Path) -> dict[str, Any]:
    with input_path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError("ui root must be an object")
    return data


def _require_str(data: dict[str, Any], key: str, path: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{path}.{key} must be a non-empty string")
    return value


def _identifier(value: str, path: str) -> str:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value):
        raise ValueError(f"{path} must be a valid C identifier")
    return value


def _macro_part(value: str, path: str) -> str:
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value):
        raise ValueError(f"{path} must contain letters, numbers, or underscores")
    return value.upper()


def _c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def _coord(value: Any, path: str) -> str:
    if isinstance(value, int):
        return str(value)
    if value == "content":
        return "LV_SIZE_CONTENT"
    if isinstance(value, str) and value.endswith("%") and value[:-1].isdigit():
        return f"LV_PCT({value[:-1]})"
    raise ValueError(f"{path} must be an integer, content, or percentage string")


def _optional_int(data: dict[str, Any], key: str, path: str) -> str | None:
    if key not in data:
        return None
    value = data[key]
    if not isinstance(value, int):
        raise ValueError(f"{path}.{key} must be an integer")
    return str(value)


def _opa_value(data: dict[str, Any], key: str, path: str) -> str | None:
    if key not in data:
        return None
    value = data[key]
    if isinstance(value, int):
        return str(value)
    if not isinstance(value, str):
        raise ValueError(f"{path}.{key} must be an integer, transparent, cover, or 0%-100% opacity")
    mapping = {
        "transparent": "LV_OPA_TRANSP",
        "transp": "LV_OPA_TRANSP",
        "cover": "LV_OPA_COVER",
    }
    if value in mapping:
        return mapping[value]
    if value.endswith("%") and value[:-1].isdigit():
        percent = int(value[:-1])
        if percent in range(0, 101, 10):
            return f"LV_OPA_{percent}"
    raise ValueError(f"{path}.{key} must be an integer, transparent, cover, or 0%-100% in 10% steps")


def _font_value(data: dict[str, Any], key: str, path: str) -> str | None:
    if key not in data:
        return None
    value = data[key]
    if not isinstance(value, int) or value < 0:
        raise ValueError(f"{path}.{key} must be a non-negative integer")
    return str(value)


def _label_align(value: str, path: str) -> str:
    mapping = {
        "left": "LABEL_ALIGN_LEFT",
        "center": "LABEL_ALIGN_CENTER",
        "right": "LABEL_ALIGN_RIGHT",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be left, center, or right")
    return mapping[value]


def _label_overflow(value: str, path: str) -> str:
    mapping = {
        "clip": "LABEL_OVERFLOW_CLIP",
        "wrap": "LABEL_OVERFLOW_WRAP",
        "scroll": "LABEL_OVERFLOW_SCROLL",
        "scroll_circular": "LABEL_OVERFLOW_SCROLL_CIRCULAR",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be clip, wrap, scroll, or scroll_circular")
    return mapping[value]


def _container_align(value: str, path: str) -> str:
    mapping = {
        "start": "CONTAINER_ALIGN_START",
        "center": "CONTAINER_ALIGN_CENTER",
        "end": "CONTAINER_ALIGN_END",
        "space_between": "CONTAINER_ALIGN_SPACE_BETWEEN",
        "space_around": "CONTAINER_ALIGN_SPACE_AROUND",
        "space_evenly": "CONTAINER_ALIGN_SPACE_EVENLY",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be a supported container alignment")
    return mapping[value]


def _lv_align(value: str, path: str) -> str:
    mapping = {
        "top_left": "LV_ALIGN_TOP_LEFT",
        "top_mid": "LV_ALIGN_TOP_MID",
        "top_right": "LV_ALIGN_TOP_RIGHT",
        "left_mid": "LV_ALIGN_LEFT_MID",
        "center": "LV_ALIGN_CENTER",
        "right_mid": "LV_ALIGN_RIGHT_MID",
        "bottom_left": "LV_ALIGN_BOTTOM_LEFT",
        "bottom_mid": "LV_ALIGN_BOTTOM_MID",
        "bottom_right": "LV_ALIGN_BOTTOM_RIGHT",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be a supported LVGL alignment")
    return mapping[value]


def _lv_dir(value: str, path: str) -> str:
    mapping = {
        "none": "LV_DIR_NONE",
        "left": "LV_DIR_LEFT",
        "right": "LV_DIR_RIGHT",
        "top": "LV_DIR_TOP",
        "bottom": "LV_DIR_BOTTOM",
        "hor": "LV_DIR_HOR",
        "ver": "LV_DIR_VER",
        "all": "LV_DIR_ALL",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be a supported LVGL direction")
    return mapping[value]


def _scrollbar_mode(value: str, path: str) -> str:
    mapping = {
        "off": "LV_SCROLLBAR_MODE_OFF",
        "on": "LV_SCROLLBAR_MODE_ON",
        "active": "LV_SCROLLBAR_MODE_ACTIVE",
        "auto": "LV_SCROLLBAR_MODE_AUTO",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be off, on, active, or auto")
    return mapping[value]


def _container_height_policy(value: str, path: str) -> str:
    mapping = {
        "none": "CONTAINER_HEIGHT_POLICY_NONE",
        "content_max_parent": "CONTAINER_HEIGHT_POLICY_CONTENT_MAX_PARENT",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be none or content_max_parent")
    return mapping[value]


def _roller_overflow_mode(value: str, path: str) -> str:
    mapping = {
        "scroll": "ROLLER_OVERFLOW_SCROLL",
        "expand_height": "ROLLER_OVERFLOW_EXPAND_HEIGHT",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be scroll or expand_height")
    return mapping[value]


def _paged_text_step_mode(value: str, path: str) -> str:
    mapping = {
        "line_page": "PAGED_TEXT_STEP_LINE_PAGE",
        "view_percent": "PAGED_TEXT_STEP_VIEW_PERCENT",
    }
    if value not in mapping:
        raise ValueError(f"{path} must be line_page or view_percent")
    return mapping[value]


class UiCompiler:
    def __init__(self, data: dict[str, Any], output_dir: Path):
        self.data = data
        self.output_dir = output_dir
        self.name = _identifier(_require_str(data, "name", "$"), "$.name")
        self.resource_name = self._resource_name()
        self.modes = self._modes()
        self.ids: dict[str, str] = {}
        self.temp_index = 0
        self.source_lines: list[str] = []
        self.uses_text_key = False

    def _resource_name(self) -> str | None:
        resources = self.data.get("resources")
        if resources is None:
            return None
        if not isinstance(resources, str) or not resources:
            raise ValueError("$.resources must be a non-empty string")
        filename = Path(resources).name
        if filename.endswith(".res.json"):
            filename = filename[: -len(".res.json")]
        else:
            filename = Path(filename).stem
        return _identifier(filename, "$.resources")

    def _modes(self) -> dict[str, dict[str, Any]]:
        modes = self.data.get("modes")
        if modes is None:
            return {}
        if not isinstance(modes, dict) or not modes:
            raise ValueError("$.modes must be a non-empty object")

        parsed: dict[str, dict[str, Any]] = {}
        for mode_name, mode_data in modes.items():
            mode_name = _identifier(mode_name, "$.modes key")
            if not isinstance(mode_data, dict):
                raise ValueError(f"$.modes.{mode_name} must be an object")
            root = mode_data.get("root")
            if not isinstance(root, dict):
                raise ValueError(f"$.modes.{mode_name}.root must be an object")
            parsed[mode_name] = root
        return parsed

    def compile(self) -> GeneratedUi:
        roots = self._roots()

        first_ids: dict[str, str] | None = None
        for root_name, root in roots.items():
            ids = self._collect_ids(root, root_name)
            if first_ids is None:
                first_ids = ids
            elif ids != first_ids:
                raise ValueError(f"{root_name} must expose the same ids and widget types as the first UI root")
        self.ids = first_ids or {}
        self.uses_text_key = any(self._tree_uses_text_key(root) for root in roots.values())
        self.output_dir.mkdir(parents=True, exist_ok=True)
        header_path = self.output_dir / f"{self.name}_ui.h"
        source_path = self.output_dir / f"{self.name}_ui.c"
        header_path.write_text(self._render_header(), encoding="utf-8")
        source_path.write_text(self._render_source(roots), encoding="utf-8")
        return GeneratedUi(header=header_path, source=source_path)

    def _roots(self) -> dict[str, dict[str, Any]]:
        if self.modes:
            return {f"$.modes.{mode_name}.root": root for mode_name, root in self.modes.items()}

        root = self.data.get("root")
        if not isinstance(root, dict):
            raise ValueError("$.root must be an object")
        return {"$.root": root}

    def _collect_ids(self, node: dict[str, Any], path: str) -> dict[str, str]:
        ids: dict[str, str] = {}
        self._collect_ids_into(node, path, ids)
        return ids

    def _collect_ids_into(self, node: dict[str, Any], path: str, ids: dict[str, str]) -> None:
        node_type = _require_str(node, "type", path)
        if node_type not in WIDGET_TYPES:
            raise ValueError(f"{path}.type must be one of {', '.join(sorted(WIDGET_TYPES))}")
        node_id = node.get("id")
        if node_id is not None:
            node_id = _identifier(node_id, f"{path}.id")
            if node_id in ids:
                raise ValueError(f"duplicate id: {node_id}")
            ids[node_id] = WIDGET_TYPES[node_type]
        children = node.get("children", [])
        if children is None:
            children = []
        if not isinstance(children, list):
            raise ValueError(f"{path}.children must be an array")
        for index, child in enumerate(children):
            if not isinstance(child, dict):
                raise ValueError(f"{path}.children[{index}] must be an object")
            self._collect_ids_into(child, f"{path}.children[{index}]", ids)

    def _tree_uses_text_key(self, node: dict[str, Any]) -> bool:
        if "text_key" in node:
            return True
        label = node.get("label")
        if isinstance(label, dict) and "text_key" in label:
            return True
        text = node.get("text")
        if isinstance(text, dict) and "text_key" in text:
            return True
        return any(self._tree_uses_text_key(child) for child in node.get("children", []) or [])

    def _mode_type(self) -> str:
        return f"{self.name}_mode_t"

    def _mode_macro(self, mode_name: str) -> str:
        return f"{_macro_part(self.name, '$.name')}_MODE_{_macro_part(mode_name, '$.modes key')}"

    def _render_header(self) -> str:
        guard = f"{_macro_part(self.name, '$.name')}_UI_H"
        lines = [
            "/**",
            f" * @file {self.name}_ui.h",
            " * @brief Auto-generated UI layout declarations.",
            " */",
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            "#include <stdbool.h>",
            "",
            '#include "common/widgets/button.h"',
            '#include "common/widgets/container.h"',
            '#include "common/widgets/img.h"',
            '#include "common/widgets/label.h"',
            '#include "common/widgets/overlay.h"',
            '#include "common/widgets/paged_text.h"',
            '#include "common/widgets/roller.h"',
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            "",
            "/**",
            " * @brief Auto-generated UI handle collection.",
            " */",
            "typedef struct {",
        ]
        for node_id, c_type in self.ids.items():
            lines.append(f"    {c_type}* {node_id};")
        lines.extend([f"}} {self.name}_ui_t;", ""])
        if self.modes:
            lines.extend(["/**", " * @brief Auto-generated UI mode selector.", " */", "typedef enum {"])
            for mode_name in self.modes:
                lines.append(f"    {self._mode_macro(mode_name)},")
            lines.extend([f"}} {self._mode_type()};", ""])
        lines.extend(
            [
                "/**",
                " * @brief Create the UI layout under the given parent.",
                " * @param[in] parent Parent LVGL object.",
                " * @param[out] ui UI handle collection.",
            ]
        )
        if self.modes:
            lines.append(" * @param[in] mode UI mode selector.")
        lines.extend([" * @return true on success, false on invalid input or allocation failure.", " */"])
        if self.modes:
            lines.append(f"bool {self.name}_init_ui(lv_obj_t* parent, {self.name}_ui_t* ui, {self._mode_type()} mode);")
        else:
            lines.append(f"bool {self.name}_init_ui(lv_obj_t* parent, {self.name}_ui_t* ui);")
        lines.extend(["", "#ifdef __cplusplus", "}", "#endif", "", f"#endif /* {guard} */", ""])
        return "\n".join(lines)

    def _render_source(self, roots: dict[str, dict[str, Any]]) -> str:
        lines = [
            "/**",
            f" * @file {self.name}_ui.c",
            " * @brief Auto-generated UI layout creation.",
            " */",
            f'#include "{self.name}_ui.h"',
        ]
        if self.resource_name:
            lines.append(f'#include "{self.resource_name}_res.h"')
        if self.uses_text_key:
            lines.append('#include "app_def.h"')
        lines.extend(["", "#include <string.h>", ""])

        if self.modes:
            for mode_name, root in self.modes.items():
                lines.extend(self._render_mode_helper(mode_name, root))
            lines.extend(
                [
                    f"bool {self.name}_init_ui(lv_obj_t* parent, {self.name}_ui_t* ui, {self._mode_type()} mode) {{",
                    "    if (parent == NULL || ui == NULL) {",
                    "        return false;",
                    "    }",
                    "    memset(ui, 0, sizeof(*ui));",
                    "",
                    "    switch (mode) {",
                ]
            )
            for mode_name in self.modes:
                lines.extend(
                    [
                        f"    case {self._mode_macro(mode_name)}:",
                        f"        return {self.name}_init_ui_{mode_name}(parent, ui);",
                    ]
                )
            lines.extend(["    default:", "        return false;", "    }", "}", ""])
            return "\n".join(lines)

        signature = f"bool {self.name}_init_ui(lv_obj_t* parent, {self.name}_ui_t* ui) {{"
        lines.extend([signature])
        lines.extend(self._render_init_body(next(iter(roots.values())), "$.root", do_memset=True))
        lines.append("")
        return "\n".join(lines)

    def _render_mode_helper(self, mode_name: str, root: dict[str, Any]) -> list[str]:
        lines = [f"static bool {self.name}_init_ui_{mode_name}(lv_obj_t* parent, {self.name}_ui_t* ui) {{"]
        lines.extend(self._render_init_body(root, f"$.modes.{mode_name}.root", do_memset=False))
        lines.append("")
        return lines

    def _render_init_body(self, root: dict[str, Any], path: str, do_memset: bool) -> list[str]:
        self.source_lines = [
            "    if (parent == NULL || ui == NULL) {",
            "        return false;",
            "    }",
        ]
        if do_memset:
            self.source_lines.extend(["    memset(ui, 0, sizeof(*ui));", ""])
        else:
            self.source_lines.append("")
        self.temp_index = 0
        self._emit_node(root, path, "parent", False)
        self.source_lines.extend(["", "    return true;", "}"])
        return self.source_lines

    def _emit_node(self, node: dict[str, Any], path: str, parent_obj: str, parent_has_layout: bool) -> tuple[str, str]:
        node_type = _require_str(node, "type", path)
        node_id = node.get("id")
        handle = self._handle_expr(node_type, node_id)
        var_prefix = node_id if node_id else self._next_temp(node_type)

        if node_type == "container":
            self._emit_container(node, path, parent_obj, parent_has_layout, var_prefix, handle)
            obj_var = f"{var_prefix}_obj"
            self.source_lines.append(f"    lv_obj_t* {obj_var} = container_get_obj({handle});")
            self.source_lines.append(f"    if ({obj_var} == NULL) {{")
            self.source_lines.append("        return false;")
            self.source_lines.append("    }")
            self._emit_object_options(node, path, obj_var)
            self._emit_widget_options(node, path, handle)
            self._emit_container_padding(node, path, handle)
            self._emit_layout(node, path, handle)
            self._emit_container_height_policy(node, path, handle)
            self._emit_scroll_options(node, path, obj_var)
            child_parent_has_layout = "layout" in node
            for index, child in enumerate(node.get("children", []) or []):
                self._emit_node(child, f"{path}.children[{index}]", obj_var, child_parent_has_layout)
            return handle, obj_var

        if node_type == "label":
            self._emit_label(node, path, parent_obj, parent_has_layout, var_prefix, handle)
        elif node_type == "img":
            self._emit_img(node, path, parent_obj, parent_has_layout, var_prefix, handle)
        elif node_type == "button":
            self._emit_button(node, path, parent_obj, parent_has_layout, var_prefix, handle)
        elif node_type == "overlay":
            self._emit_overlay(node, path, parent_obj, parent_has_layout, var_prefix, handle)
        elif node_type == "paged_text":
            self._emit_paged_text(node, path, parent_obj, parent_has_layout, var_prefix, handle)
        elif node_type == "roller":
            self._emit_roller(node, path, parent_obj, parent_has_layout, var_prefix, handle)
        else:
            raise ValueError(f"{path}.type is unsupported")
        obj_var = f"{var_prefix}_obj"
        getter = f"{node_type}_get_obj" if node_type != "img" else None
        if getter:
            self.source_lines.append(f"    lv_obj_t* {obj_var} = {getter}({handle});")
            self.source_lines.append(f"    if ({obj_var} == NULL) {{")
            self.source_lines.append("        return false;")
            self.source_lines.append("    }")
            if node_type in ("overlay", "roller"):
                self._emit_widget_bounds(node, path, handle, parent_has_layout)
            self._emit_object_options(node, path, obj_var)
            self._emit_widget_options(node, path, handle)
            self._emit_scroll_options(node, path, obj_var)
        return handle, parent_obj

    def _handle_expr(self, node_type: str, node_id: Any) -> str:
        if node_id:
            return f"ui->{node_id}"
        self.temp_index += 1
        return f"tmp_{self.temp_index}"

    def _next_temp(self, node_type: str) -> str:
        self.temp_index += 1
        return f"{node_type}_{self.temp_index}"

    def _assign_create(self, handle: str, node_type: str, create_expr: str) -> None:
        if handle.startswith("ui->"):
            self.source_lines.append(f"    {handle} = {create_expr};")
            check = handle
        else:
            self.source_lines.append(f"    {WIDGET_TYPES[node_type]}* {handle} = {create_expr};")
            check = handle
        self.source_lines.append(f"    if ({check} == NULL) {{")
        self.source_lines.append("        return false;")
        self.source_lines.append("    }")

    def _emit_widget_options(self, node: dict[str, Any], path: str, handle: str) -> None:
        if "visible" not in node:
            return
        visible = node["visible"]
        if not isinstance(visible, bool):
            raise ValueError(f"{path}.visible must be a boolean")
        if not visible:
            self.source_lines.append(f"    ui_widget_set_visible(UI_WIDGET({handle}), false);")

    def _emit_widget_bounds(self, node: dict[str, Any], path: str, handle: str, parent_has_layout: bool) -> None:
        geometry = node.get("geometry", {})
        if geometry is None:
            geometry = {}
        if not isinstance(geometry, dict):
            raise ValueError(f"{path}.geometry must be an object")
        size = node.get("size", {})
        if size is None:
            size = {}
        if not isinstance(size, dict):
            raise ValueError(f"{path}.size must be an object")

        if not parent_has_layout and ("x" in geometry or "y" in geometry):
            x = _coord(geometry.get("x", 0), f"{path}.geometry.x")
            y = _coord(geometry.get("y", 0), f"{path}.geometry.y")
            self.source_lines.append(f"    ui_widget_set_position(UI_WIDGET({handle}), {x}, {y});")

        width_value = None
        height_value = None
        if not parent_has_layout:
            width_value = geometry.get("w")
            height_value = geometry.get("h")
        if "w" in size:
            width_value = size["w"]
        if "h" in size:
            height_value = size["h"]
        if width_value is not None or height_value is not None:
            w = _coord(width_value, f"{path}.geometry.w") if width_value is not None else "0"
            h = _coord(height_value, f"{path}.geometry.h") if height_value is not None else "0"
            self.source_lines.append(f"    ui_widget_set_size(UI_WIDGET({handle}), {w}, {h});")

    def _emit_common_cfg(self,
                         node: dict[str, Any],
                         path: str,
                         cfg: str,
                         parent_has_layout: bool,
                         style_keys: tuple[str, ...] = ("radius", "border_width", "pad_hor", "pad_ver", "opa")) -> None:
        geometry = node.get("geometry", {})
        if geometry is None:
            geometry = {}
        if not isinstance(geometry, dict):
            raise ValueError(f"{path}.geometry must be an object")
        size = node.get("size", {})
        if size is None:
            size = {}
        if not isinstance(size, dict):
            raise ValueError(f"{path}.size must be an object")

        if not parent_has_layout:
            for key in ("x", "y", "w", "h"):
                if key in geometry:
                    self.source_lines.append(f"    {cfg}.{key} = {_coord(geometry[key], f'{path}.geometry.{key}')};")
        for key in ("w", "h"):
            if key in size:
                self.source_lines.append(f"    {cfg}.{key} = {_coord(size[key], f'{path}.size.{key}')};")
        for key in style_keys:
            value = _opa_value(node, key, path) if key == "opa" else _optional_int(node, key, path)
            if value is not None:
                self.source_lines.append(f"    {cfg}.{key} = {value};")

    def _emit_container(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    container_cfg_t {cfg} = container_default_cfg();")
        self._emit_common_cfg(node, path, cfg, parent_has_layout)
        self._emit_container_max_size(node, path, cfg)
        self._assign_create(handle, "container", f"container_create({parent_obj}, &{cfg})")

    def _emit_label(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    label_cfg_t {cfg} = label_default_cfg();")
        self._emit_common_cfg(node, path, cfg, parent_has_layout)
        self._emit_font_cfg(node, path, cfg)
        if "text" in node:
            if not isinstance(node["text"], str):
                raise ValueError(f"{path}.text must be a string")
            if "text_key" in node:
                raise ValueError(f"{path} must not contain both text and text_key")
            self.source_lines.append(f"    {cfg}.text = {_c_string(node['text'])};")
        if "text_key" in node:
            if not isinstance(node["text_key"], str) or not node["text_key"]:
                raise ValueError(f"{path}.text_key must be a non-empty string")
            self.source_lines.append(f"    {cfg}.text = app_get_str({_c_string(node['text_key'])});")
        if "align" in node:
            self.source_lines.append(f"    {cfg}.align = {_label_align(node['align'], f'{path}.align')};")
        if "overflow" in node:
            self.source_lines.append(f"    {cfg}.overflow = {_label_overflow(node['overflow'], f'{path}.overflow')};")
        if "max_lines" in node:
            value = node["max_lines"]
            if not isinstance(value, int) or value < 0:
                raise ValueError(f"{path}.max_lines must be a non-negative integer")
            self.source_lines.append(f"    {cfg}.max_lines = {value};")
        self._assign_create(handle, "label", f"label_create({parent_obj}, &{cfg})")

    def _emit_font_cfg(self, node: dict[str, Any], path: str, cfg: str) -> None:
        font = node.get("font")
        if font is None:
            return
        if not isinstance(font, dict):
            raise ValueError(f"{path}.font must be an object")
        for key in ("weight", "wordSpace", "rowSpace"):
            value = _font_value(font, key, f"{path}.font")
            if value is not None:
                self.source_lines.append(f"    {cfg}.font.{key} = {value};")

    def _emit_img(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    img_cfg_t {cfg} = img_default_cfg();")
        self._emit_common_cfg(node, path, cfg, parent_has_layout, ("opa",))
        for key in ("offset_x", "offset_y", "zoom", "rotation"):
            value = _optional_int(node, key, path)
            if value is not None:
                self.source_lines.append(f"    {cfg}.{key} = {value};")
        if "src" in node:
            src = node["src"]
            if not isinstance(src, str):
                raise ValueError(f"{path}.src must be a string")
            self.source_lines.append(f"    {cfg}.src = {self._resource_expr(src, path)};")
        self._assign_create(handle, "img", f"img_create({parent_obj}, &{cfg})")

    def _emit_button(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    button_cfg_t {cfg} = button_default_cfg();")
        self._emit_common_cfg(node, path, cfg, parent_has_layout)
        if "text" in node:
            if not isinstance(node["text"], str):
                raise ValueError(f"{path}.text must be a string")
            if "label" in node:
                raise ValueError(f"{path} must not contain both text and label")
            self.source_lines.append(f"    {cfg}.label.text = {_c_string(node['text'])};")
        if "label" in node:
            self._emit_embedded_label_cfg(node["label"], f"{path}.label", f"{cfg}.label")
        self._assign_create(handle, "button", f"button_create({parent_obj}, &{cfg})")

    def _emit_paged_text(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    paged_text_cfg_t {cfg} = paged_text_default_cfg();")
        self._emit_common_cfg(node, path, f"{cfg}.label", parent_has_layout)
        if "label" in node:
            self._emit_embedded_label_cfg(node["label"], f"{path}.label", f"{cfg}.label")
        self._emit_paged_text_highlight_cfg(node, path, cfg)
        if "step_mode" in node:
            self.source_lines.append(
                f"    {cfg}.step_mode = {_paged_text_step_mode(node['step_mode'], f'{path}.step_mode')};"
            )
        value = _optional_int(node, "step_percent", path)
        if value is not None:
            self.source_lines.append(f"    {cfg}.step_percent = {value};")
        self._assign_create(handle, "paged_text", f"paged_text_create({parent_obj}, &{cfg})")

    def _emit_paged_text_highlight_cfg(self, node: dict[str, Any], path: str, cfg: str) -> None:
        highlight = node.get("highlight")
        if highlight is None:
            return
        if not isinstance(highlight, dict):
            raise ValueError(f"{path}.highlight must be an object")
        self.source_lines.append(f"    {cfg}.highlight.enabled = true;")
        mask_opa = _opa_value(highlight, "mask_opa", f"{path}.highlight")
        if mask_opa is not None:
            self.source_lines.append(f"    {cfg}.highlight.mask_opa = {mask_opa};")
        for key in ("border_width", "radius", "outset"):
            value = _optional_int(highlight, key, f"{path}.highlight")
            if value is not None:
                self.source_lines.append(f"    {cfg}.highlight.{key} = {value};")

    def _emit_overlay(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    overlay_cfg_t {cfg} = overlay_default_cfg();")
        value = _optional_int(node, "max_items", path)
        if value is not None:
            self.source_lines.append(f"    {cfg}.max_items = {value};")
        point = node.get("point")
        if point is not None:
            if not isinstance(point, dict):
                raise ValueError(f"{path}.point must be an object")
            for key in ("size", "opa"):
                value = (
                    _opa_value(point, key, f"{path}.point")
                    if key == "opa"
                    else _optional_int(point, key, f"{path}.point")
                )
                if value is not None:
                    self.source_lines.append(f"    {cfg}.point.{key} = {value};")
        if "text" in node:
            self._emit_embedded_label_cfg(node["text"], f"{path}.text", f"{cfg}.text")
        self._assign_create(handle, "overlay", f"overlay_create({parent_obj}, &{cfg})")

    def _emit_roller(self, node, path, parent_obj, parent_has_layout, var_prefix, handle) -> None:
        cfg = f"{var_prefix}_cfg"
        self.source_lines.append(f"    roller_cfg_t {cfg} = roller_default_cfg();")
        self._emit_roller_items(node, path, cfg, var_prefix)
        if "label" in node:
            self._emit_embedded_label_cfg(node["label"], f"{path}.label", f"{cfg}.label")
        selected_font = node.get("selected_font")
        if selected_font is not None:
            if not isinstance(selected_font, dict):
                raise ValueError(f"{path}.selected_font must be an object")
            for key in ("weight", "wordSpace", "rowSpace"):
                value = _font_value(selected_font, key, f"{path}.selected_font")
                if value is not None:
                    self.source_lines.append(f"    {cfg}.selected_font.{key} = {value};")
        if "overflow_mode" in node:
            self.source_lines.append(
                f"    {cfg}.overflow_mode = {_roller_overflow_mode(node['overflow_mode'], f'{path}.overflow_mode')};"
            )
        for key in (
            "row_height",
            "row_gap",
            "selected_pad_ver",
            "radius",
            "border_width",
            "opa_normal",
            "opa_selected",
        ):
            value = (
                _opa_value(node, key, path)
                if key in ("opa_normal", "opa_selected")
                else _optional_int(node, key, path)
            )
            if value is not None:
                self.source_lines.append(f"    {cfg}.{key} = {value};")
        self._assign_create(handle, "roller", f"roller_create({parent_obj}, &{cfg})")

    def _emit_roller_items(self, node: dict[str, Any], path: str, cfg: str, var_prefix: str) -> None:
        items = node.get("items")
        if items is None:
            return
        if not isinstance(items, list) or not all(isinstance(item, str) for item in items):
            raise ValueError(f"{path}.items must be an array of strings")
        if len(items) == 0:
            return
        item_var = f"{var_prefix}_items"
        values = ", ".join(_c_string(item) for item in items)
        self.source_lines.append(f"    static const char* {item_var}[] = {{{values}}};")
        self.source_lines.append(f"    {cfg}.items = {item_var};")
        self.source_lines.append(f"    {cfg}.count = {len(items)};")

    def _emit_embedded_label_cfg(self, label: Any, path: str, cfg: str) -> None:
        if not isinstance(label, dict):
            raise ValueError(f"{path} must be an object")
        self._emit_common_cfg(label, path, cfg, parent_has_layout=False)
        self._emit_font_cfg(label, path, cfg)
        if "text" in label:
            if not isinstance(label["text"], str):
                raise ValueError(f"{path}.text must be a string")
            if "text_key" in label:
                raise ValueError(f"{path} must not contain both text and text_key")
            self.source_lines.append(f"    {cfg}.text = {_c_string(label['text'])};")
        if "text_key" in label:
            if not isinstance(label["text_key"], str) or not label["text_key"]:
                raise ValueError(f"{path}.text_key must be a non-empty string")
            self.source_lines.append(f"    {cfg}.text = app_get_str({_c_string(label['text_key'])});")
        if "align" in label:
            self.source_lines.append(f"    {cfg}.align = {_label_align(label['align'], f'{path}.align')};")
        if "overflow" in label:
            self.source_lines.append(f"    {cfg}.overflow = {_label_overflow(label['overflow'], f'{path}.overflow')};")
        if "max_lines" in label:
            value = label["max_lines"]
            if not isinstance(value, int) or value < 0:
                raise ValueError(f"{path}.max_lines must be a non-negative integer")
            self.source_lines.append(f"    {cfg}.max_lines = {value};")

    def _emit_layout(self, node: dict[str, Any], path: str, handle: str) -> None:
        if "layout" in node:
            for line in self._layout_lines(node["layout"], f"{path}.layout", handle):
                self.source_lines.append(f"    {line}")

    def _layout_lines(self, layout: Any, path: str, handle: str) -> list[str]:
        if not isinstance(layout, dict):
            raise ValueError(f"{path} must be an object")
        layout_type = _require_str(layout, "type", path)
        spacing = layout.get("spacing", 0)
        if not isinstance(spacing, int):
            raise ValueError(f"{path}.spacing must be an integer")
        lines: list[str] = []
        if layout_type == "vbox":
            lines.append(f"container_set_layout_vbox_spaced({handle}, {spacing});")
        elif layout_type == "hbox":
            lines.append(f"container_set_layout_hbox_spaced({handle}, {spacing});")
        else:
            raise ValueError(f"{path}.type must be vbox or hbox")
        padding = layout.get("padding")
        if padding is not None:
            if not isinstance(padding, list) or len(padding) != 4 or not all(isinstance(v, int) for v in padding):
                raise ValueError(f"{path}.padding must be [left, right, top, bottom]")
            lines.append(f"container_set_padding_box({handle}, {padding[0]}, {padding[1]}, {padding[2]}, {padding[3]});")
        if "main_align" in layout or "cross_align" in layout:
            main = _container_align(layout.get("main_align", "start"), f"{path}.main_align")
            cross = _container_align(layout.get("cross_align", "start"), f"{path}.cross_align")
            lines.append(f"container_set_align({handle}, {main}, {cross}, CONTAINER_ALIGN_START);")
        return lines

    def _emit_container_padding(self, node: dict[str, Any], path: str, handle: str) -> None:
        padding = node.get("padding")
        if padding is None:
            return
        if not isinstance(padding, list) or len(padding) != 4 or not all(isinstance(v, int) for v in padding):
            raise ValueError(f"{path}.padding must be [left, right, top, bottom]")
        self.source_lines.append(
            f"    container_set_padding_box({handle}, {padding[0]}, {padding[1]}, {padding[2]}, {padding[3]});"
        )

    def _emit_container_max_size(self, node: dict[str, Any], path: str, cfg: str) -> None:
        max_size = node.get("max_size")
        if max_size is None:
            return
        if not isinstance(max_size, dict):
            raise ValueError(f"{path}.max_size must be an object")
        if "w" in max_size:
            self.source_lines.append(f"    {cfg}.max_w = {_coord(max_size['w'], f'{path}.max_size.w')};")
        if "h" in max_size:
            self.source_lines.append(f"    {cfg}.max_h = {_coord(max_size['h'], f'{path}.max_size.h')};")

    def _emit_container_height_policy(self, node: dict[str, Any], path: str, handle: str) -> None:
        policy = node.get("height_policy")
        if policy is None:
            return
        if not isinstance(policy, str):
            raise ValueError(f"{path}.height_policy must be a string")
        self.source_lines.append(
            f"    container_set_height_policy({handle}, {_container_height_policy(policy, f'{path}.height_policy')});"
        )

    def _emit_object_options(self, node: dict[str, Any], path: str, obj_var: str) -> None:
        if node.get("clip_corner") is True:
            self.source_lines.append(f"    lv_obj_set_style_clip_corner({obj_var}, true, LV_PART_MAIN);")
        elif "clip_corner" in node and node.get("clip_corner") is not False:
            raise ValueError(f"{path}.clip_corner must be true or false")

        floating = node.get("floating")
        if floating is True:
            self.source_lines.append(f"    lv_obj_add_flag({obj_var}, LV_OBJ_FLAG_FLOATING);")
        elif floating is False:
            self.source_lines.append(f"    lv_obj_remove_flag({obj_var}, LV_OBJ_FLAG_FLOATING);")
        elif floating is not None:
            raise ValueError(f"{path}.floating must be true or false")

        object_align = node.get("object_align")
        if object_align is not None:
            if not isinstance(object_align, dict):
                raise ValueError(f"{path}.object_align must be an object")
            align_type = _lv_align(_require_str(object_align, "type", f"{path}.object_align"), f"{path}.object_align.type")
            x = object_align.get("x", 0)
            y = object_align.get("y", 0)
            if not isinstance(x, int) or not isinstance(y, int):
                raise ValueError(f"{path}.object_align.x/y must be integers")
            self.source_lines.append(f"    lv_obj_align({obj_var}, {align_type}, {x}, {y});")

        layout_item = node.get("layout_item")
        if layout_item is not None:
            if not isinstance(layout_item, dict):
                raise ValueError(f"{path}.layout_item must be an object")
            fill_x = layout_item.get("fill_x", False)
            fill_y = layout_item.get("fill_y", False)
            grow = layout_item.get("grow")
            if not isinstance(fill_x, bool) or not isinstance(fill_y, bool):
                raise ValueError(f"{path}.layout_item.fill_x/fill_y must be booleans")
            if fill_x or fill_y:
                self.source_lines.append(
                    f"    container_set_child_fill({obj_var}, {str(fill_x).lower()}, {str(fill_y).lower()});"
                )
            if grow is not None:
                if not isinstance(grow, int) or grow < 0 or grow > 255:
                    raise ValueError(f"{path}.layout_item.grow must be an integer from 0 to 255")
                self.source_lines.append(f"    container_set_child_grow({obj_var}, {grow});")

    def _emit_scroll_options(self, node: dict[str, Any], path: str, obj_var: str) -> None:
        scroll = node.get("scroll")
        if scroll is None:
            return
        if not isinstance(scroll, dict):
            raise ValueError(f"{path}.scroll must be an object")
        enabled = scroll.get("enabled")
        if enabled is True:
            self.source_lines.append(f"    lv_obj_add_flag({obj_var}, LV_OBJ_FLAG_SCROLLABLE);")
        elif enabled is False:
            self.source_lines.append(f"    lv_obj_remove_flag({obj_var}, LV_OBJ_FLAG_SCROLLABLE);")
        elif enabled is not None:
            raise ValueError(f"{path}.scroll.enabled must be true or false")
        if "dir" in scroll:
            self.source_lines.append(f"    lv_obj_set_scroll_dir({obj_var}, {_lv_dir(scroll['dir'], f'{path}.scroll.dir')});")
        if "scrollbar" in scroll:
            self.source_lines.append(
                f"    lv_obj_set_scrollbar_mode({obj_var}, {_scrollbar_mode(scroll['scrollbar'], f'{path}.scroll.scrollbar')});"
            )

    def _resource_expr(self, src: str, path: str) -> str:
        if src.startswith("@image/"):
            image_id = src[len("@image/") :]
            if not self.resource_name:
                raise ValueError(f"{path}.src uses @image but $.resources is missing")
            return f"{_macro_part(self.resource_name, '$.resources')}_RES_IMAGE_{_macro_part(image_id, f'{path}.src')}"
        return _c_string(src)


def compile_ui_file(input_path: str | Path, output_dir: str | Path) -> GeneratedUi:
    """Compile one UI JSON file and return generated file paths."""

    input_path = Path(input_path)
    output_dir = Path(output_dir)
    data = _load_json(input_path)
    return UiCompiler(data, output_dir).compile()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate jy_app UI creation code.")
    parser.add_argument("input", help="Path to .ui.json")
    parser.add_argument("--out-dir", required=True, help="Directory for generated files")
    args = parser.parse_args()

    try:
        compile_ui_file(args.input, args.out_dir)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"uic: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
