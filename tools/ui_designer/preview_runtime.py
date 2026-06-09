"""Render jy_app UI JSON to a bitmap preview without compiling firmware code."""

from __future__ import annotations

import io
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont


DEFAULT_WIDTH = 540
DEFAULT_HEIGHT = 440
BG_COLOR = (0, 0, 0, 255)
FG_COLOR = (150, 150, 150, 255)
BORDER_COLOR = (100, 100, 100, 255)
ACCENT_COLOR = (80, 80, 80, 255)
BUTTON_DEFAULT_RADIUS = 12
BUTTON_DEFAULT_BORDER_WIDTH = 1
BUTTON_DEFAULT_OPA = 255
ROLLER_DEFAULT_RADIUS = 16
ROLLER_DEFAULT_BORDER_WIDTH = 2
ROLLER_DEFAULT_NORMAL_OPA = 178
ROLLER_DEFAULT_SELECTED_OPA = 255
ROLLER_DEFAULT_SELECTED_PAD_VER = 2
OVERLAY_DEFAULT_MAX_ITEMS = 16
OVERLAY_DEFAULT_POINT_SIZE = 6
OVERLAY_DEFAULT_POINT_OPA = 255
PAGED_TEXT_DEFAULT_MASK_OPA = 153
PAGED_TEXT_DEFAULT_BORDER_WIDTH = 2
PAGED_TEXT_DEFAULT_RADIUS = 6
PAGED_TEXT_DEFAULT_OUTSET = 10
PAGED_TEXT_DEFAULT_STEP_PERCENT = 100


@dataclass
class Box:
    x: int
    y: int
    w: int
    h: int


def active_root(doc: dict[str, Any], mode: str = "") -> dict[str, Any]:
    if "modes" not in doc:
        root = doc.get("root")
        if not isinstance(root, dict):
            raise ValueError("ui root must be an object")
        return root

    modes = doc.get("modes")
    if not isinstance(modes, dict) or not modes:
        raise ValueError("ui modes must be a non-empty object")
    selected = mode or next(iter(modes))
    if selected not in modes:
        raise ValueError(f"unknown mode: {selected}")
    root = modes[selected].get("root") if isinstance(modes[selected], dict) else None
    if not isinstance(root, dict):
        raise ValueError(f"mode {selected} root must be an object")
    return root


def render_png(doc: dict[str, Any],
               mode: str = "",
               resources: dict[str, Any] | None = None,
               i18n: dict[str, str] | None = None,
               default_font: dict[str, Any] | None = None,
               font_path: str | Path | None = None,
               width: int = DEFAULT_WIDTH,
               height: int = DEFAULT_HEIGHT,
               show_hidden: bool = False) -> bytes:
    image = Image.new("RGBA", (width, height), BG_COLOR)
    draw = ImageDraw.Draw(image)
    root = active_root(doc, mode)
    root_box = Box(0, 0, width, height)
    render_node(image, draw, root, root_box, root_box, resources or {}, i18n or {}, default_font or {}, font_path, show_hidden=show_hidden)
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def render_node(image: Image.Image,
                draw: ImageDraw.ImageDraw,
                node: dict[str, Any],
                parent_box: Box,
                fallback_box: Box,
                resources: dict[str, Any],
                i18n: dict[str, str],
                default_font: dict[str, Any],
                font_path: str | Path | None,
                use_fallback_box: bool = False,
                show_hidden: bool = False) -> Box:
    node_type = node.get("type", "container")
    box = fallback_box if use_fallback_box else resolve_box(node, parent_box, fallback_box, default_font, font_path, i18n)
    if node.get("visible") is False and not show_hidden:
        return box

    if node_type == "label":
        draw_label(draw, node, box, i18n, default_font, font_path)
    elif node_type == "img":
        draw_image(image, draw, node, box, resources)
    elif node_type == "button":
        draw_button(draw, node, box, i18n, default_font, font_path)
    elif node_type == "roller":
        draw_roller(draw, node, box, i18n, default_font, font_path)
    elif node_type == "paged_text":
        draw_paged_text(draw, node, box, i18n, default_font, font_path)
    elif node_type == "overlay":
        draw_overlay(draw, node, box, i18n, default_font, font_path)
    else:
        draw_container(draw, node, box)
        if scroll_enabled(node):
            render_scroll_children(image, node, box, resources, i18n, default_font, font_path, show_hidden)
        else:
            render_children(image, draw, node, box, resources, i18n, default_font, font_path, show_hidden)

    return box


def render_scroll_children(image: Image.Image,
                           node: dict[str, Any],
                           box: Box,
                           resources: dict[str, Any],
                           i18n: dict[str, str],
                           default_font: dict[str, Any],
                           font_path: str | Path | None,
                           show_hidden: bool = False) -> None:
    content_h = estimate_content_height(node, max(1, box.w), default_font, font_path, i18n)
    offset_y = scroll_bottom_offset(node, max(1, box.w), box.h, default_font, font_path, i18n)
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    layer_draw = ImageDraw.Draw(layer)
    shifted_box = Box(box.x, box.y - offset_y, box.w, max(box.h, content_h))
    render_children(layer, layer_draw, node, shifted_box, resources, i18n, default_font, font_path, show_hidden)
    cropped = layer.crop(crop_rect(box))
    image.alpha_composite(cropped, dest=(box.x, box.y))


def scroll_bottom_offset(node: dict[str, Any],
                         width: int,
                         viewport_h: int,
                         default_font: dict[str, Any] | None = None,
                         font_path: str | Path | None = None,
                         i18n: dict[str, str] | None = None) -> int:
    content_h = estimate_content_height(node, width, default_font, font_path, i18n)
    raw_offset = max(0, content_h - viewport_h)
    line_starts = text_line_starts(node, width, default_font, font_path, i18n or {})
    candidates = [start for start in line_starts if start <= raw_offset]
    if not candidates:
        return raw_offset
    return max(candidates)


def text_line_starts(node: dict[str, Any],
                     width: int,
                     default_font: dict[str, Any] | None = None,
                     font_path: str | Path | None = None,
                     i18n: dict[str, str] | None = None,
                     origin_y: int = 0) -> list[int]:
    if node.get("type") == "label":
        font = load_font(font_size(node, default_font), font_path)
        letter_space = font_word_space(node, default_font)
        lines = wrap_text(resolve_label_text(node, i18n or {}), font, max(1, width - padding_h(node)), letter_space)
        row_h = line_height(font, node, default_font)
        start_y = origin_y + padding_top(node)
        return [start_y + index * row_h for index, _ in enumerate(lines)]

    if node.get("type", "container") != "container":
        return []

    children = [child for child in (node.get("children") or []) if isinstance(child, dict)]
    if not children:
        return []
    if layout_type(node) != "vbox":
        return []

    spacing = int((node.get("layout") or {}).get("spacing") or 0)
    content_w = max(1, width - padding_h(node))
    y = origin_y + padding_top(node)
    starts: list[int] = []
    for child in children:
        raw = child.get("size") or child.get("geometry") or {}
        child_w = coord(raw.get("w", "100%"), content_w)
        starts.extend(text_line_starts(child, child_w, default_font, font_path, i18n or {}, y))
        child_h = coord(raw.get("h", 0), 1)
        if raw.get("h") == "content" or child_h <= 0:
            child_h = estimate_content_height(child, child_w, default_font, font_path, i18n or {})
        y += child_h + spacing
    return starts


def resolve_box(node: dict[str, Any],
                parent_box: Box,
                fallback_box: Box,
                default_font: dict[str, Any] | None = None,
                font_path: str | Path | None = None,
                i18n: dict[str, str] | None = None) -> Box:
    raw = node.get("geometry") or node.get("size") or {}
    x = parent_box.x + coord(raw.get("x", fallback_box.x - parent_box.x), parent_box.w)
    y = parent_box.y + coord(raw.get("y", fallback_box.y - parent_box.y), parent_box.h)
    w_value = raw.get("w", fallback_box.w)
    if w_value == "content" and fallback_box.w > 0:
        w = fallback_box.w
    else:
        w = coord(w_value, parent_box.w)
    h_value = raw.get("h", fallback_box.h)
    if h_value == "content":
        h = estimate_content_height(node, max(1, w), default_font or {}, font_path, i18n or {})
    else:
        h = coord(h_value, parent_box.h)
    if node.get("height_policy") == "content_max_parent":
        h = min(h, max(1, parent_box.h))
    box = Box(x, y, max(1, w), max(1, h))
    return apply_object_align(node, parent_box, box)


def apply_object_align(node: dict[str, Any], parent_box: Box, box: Box) -> Box:
    align = node.get("object_align")
    if not isinstance(align, dict):
        return box
    align_type = str(align.get("type") or "")
    offset_x = coord(align.get("x", 0), parent_box.w)
    offset_y = coord(align.get("y", 0), parent_box.h)
    x = box.x
    y = box.y
    if align_type in ("top_left", ""):
        x = parent_box.x
        y = parent_box.y
    elif align_type == "top_mid":
        x = parent_box.x + (parent_box.w - box.w) // 2
        y = parent_box.y
    elif align_type == "top_right":
        x = parent_box.x + parent_box.w - box.w
        y = parent_box.y
    elif align_type == "left_mid":
        x = parent_box.x
        y = parent_box.y + (parent_box.h - box.h) // 2
    elif align_type == "center":
        x = parent_box.x + (parent_box.w - box.w) // 2
        y = parent_box.y + (parent_box.h - box.h) // 2
    elif align_type == "right_mid":
        x = parent_box.x + parent_box.w - box.w
        y = parent_box.y + (parent_box.h - box.h) // 2
    elif align_type == "bottom_left":
        x = parent_box.x
        y = parent_box.y + parent_box.h - box.h
    elif align_type == "bottom_mid":
        x = parent_box.x + (parent_box.w - box.w) // 2
        y = parent_box.y + parent_box.h - box.h
    elif align_type == "bottom_right":
        x = parent_box.x + parent_box.w - box.w
        y = parent_box.y + parent_box.h - box.h
    else:
        return box
    return Box(x + offset_x, y + offset_y, box.w, box.h)


def coord(value: Any, parent: int) -> int:
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if value == "content":
        return 0
    if isinstance(value, str) and value.endswith("%"):
        try:
            return round(parent * float(value[:-1]) / 100.0)
        except ValueError:
            return 0
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def estimate_content_height(node: dict[str, Any],
                            width: int,
                            default_font: dict[str, Any] | None = None,
                            font_path: str | Path | None = None,
                            i18n: dict[str, str] | None = None) -> int:
    node_type = node.get("type", "container")
    if node_type == "label":
        font = load_font(font_size(node, default_font), font_path)
        letter_space = font_word_space(node, default_font)
        lines = wrap_text(resolve_label_text(node, i18n or {}), font, max(1, width), letter_space)
        max_lines = node.get("max_lines")
        if isinstance(max_lines, int) and max_lines > 0:
            lines = lines[:max_lines]
        return max(1, len(lines)) * line_height(font, node, default_font) + padding_v(node)
    if node_type == "container":
        children = node.get("children") or []
        if not children:
            return 1
        if layout_type(node) == "hbox":
            content_w = max(1, width - padding_h(node))
            widths = hbox_child_widths(children, content_w, int((node.get("layout") or {}).get("spacing") or 0))
            heights = [
                estimate_content_height(child, max(1, widths[index]), default_font, font_path, i18n or {})
                for index, child in enumerate(children)
                if isinstance(child, dict)
            ]
            return (max(heights) if heights else 1) + padding_v(node)
        spacing = int((node.get("layout") or {}).get("spacing") or 0)
        return sum(estimate_content_height(child, width, default_font, font_path, i18n or {}) for child in children) + spacing * max(0, len(children) - 1) + padding_v(node)
    if node_type == "roller":
        label_cfg = node.get("label") if isinstance(node.get("label"), dict) else {}
        row_height, row_gap = roller_row_metrics(node, label_cfg, default_font, font_path)
        count = max(1, len(roller_visible_rows(node)))
        return count * row_height + max(0, count - 1) * row_gap + padding_v(node)
    if node_type == "paged_text":
        label = paged_text_label_config(node)
        return estimate_content_height(label, max(1, width), default_font, font_path, i18n or {})
    if node_type == "overlay":
        return 40
    return 40


def render_children(image: Image.Image,
                    draw: ImageDraw.ImageDraw,
                    node: dict[str, Any],
                    box: Box,
                    resources: dict[str, Any],
                    i18n: dict[str, str],
                    default_font: dict[str, Any],
                    font_path: str | Path | None,
                    show_hidden: bool = False) -> None:
    children = node.get("children") or []
    if not isinstance(children, list):
        return

    layout = layout_type(node)
    if layout in ("vbox", "hbox"):
        render_layout_children(image, draw, node, children, box, resources, i18n, default_font, font_path, layout, show_hidden)
        return

    for child in children:
        if isinstance(child, dict):
            render_node(image, draw, child, box, Box(0, 0, 80, 40), resources, i18n, default_font, font_path, show_hidden=show_hidden)


def render_layout_children(image: Image.Image,
                           draw: ImageDraw.ImageDraw,
                           node: dict[str, Any],
                           children: list[Any],
                           box: Box,
                           resources: dict[str, Any],
                           i18n: dict[str, str],
                           default_font: dict[str, Any],
                           font_path: str | Path | None,
                           layout: str,
                           show_hidden: bool = False) -> None:
    pad_l, pad_r, pad_t, pad_b = padding_box(node)
    spacing = int((node.get("layout") or {}).get("spacing") or 0)
    x = box.x + pad_l
    y = box.y + pad_t
    content_w = max(1, box.w - pad_l - pad_r)
    content_h = max(1, box.h - pad_t - pad_b)
    visible = [(index, child) for index, child in enumerate(children) if isinstance(child, dict) and (show_hidden or child.get("visible") is not False)]
    main_align = str((node.get("layout") or {}).get("main_align") or "start")

    items: list[tuple[dict[str, Any], int, int]] = []
    hbox_widths = hbox_child_widths(children, content_w, spacing, default_font, font_path, i18n) if layout == "hbox" else []
    for index, child in visible:
        if not isinstance(child, dict):
            continue
        raw = child.get("size") or child.get("geometry") or {}
        if layout == "vbox":
            child_w = coord(raw.get("w", "100%"), content_w)
            child_h = coord(raw.get("h", 0), content_h)
            if raw.get("h") == "content" or child_h <= 0:
                child_h = estimate_content_height(child, child_w, default_font, font_path, i18n)
            if child.get("height_policy") == "content_max_parent":
                child_h = min(child_h, content_h)
            items.append((child, child_w, child_h))
        else:
            child_w = hbox_widths[index] if index < len(hbox_widths) else coord(raw.get("w", 80), content_w)
            child_h = coord(raw.get("h", "100%"), content_h)
            if raw.get("h") == "content" or child_h <= 0:
                child_h = estimate_content_height(child, child_w, default_font, font_path, i18n)
            items.append((child, child_w, child_h))

    if layout == "vbox":
        item_heights = [item[2] for item in items]
        y, effective_spacing = main_axis_start_and_spacing(y, content_h, item_heights, spacing, main_align)
        for child, child_w, child_h in items:
            child_x = aligned_child_offset(x, content_w, child_w, str((node.get("layout") or {}).get("cross_align") or "start"))
            child_box = Box(child_x, round(y), child_w, child_h)
            render_node(image, draw, child, box, child_box, resources, i18n, default_font, font_path, True, show_hidden)
            y += child_h + effective_spacing
    else:
        item_widths = [item[1] for item in items]
        x, effective_spacing = main_axis_start_and_spacing(x, content_w, item_widths, spacing, main_align)
        for child, child_w, child_h in items:
            child_y = aligned_child_offset(y, content_h, child_h, str((node.get("layout") or {}).get("cross_align") or "start"))
            child_box = Box(round(x), child_y, child_w, child_h)
            render_node(image, draw, child, box, child_box, resources, i18n, default_font, font_path, True, show_hidden)
            x += child_w + effective_spacing


def main_axis_start_and_spacing(start: int,
                                parent_size: int,
                                child_sizes: list[int],
                                spacing: int,
                                align: str) -> tuple[float, float]:
    if not child_sizes:
        return float(start), float(spacing)
    total_children = sum(child_sizes)
    total_spacing = spacing * max(0, len(child_sizes) - 1)
    used = total_children + total_spacing
    free = max(0, parent_size - used)
    if align == "center":
        return start + free / 2, float(spacing)
    if align == "end":
        return start + free, float(spacing)
    if align == "space_between" and len(child_sizes) > 1:
        return float(start), (parent_size - total_children) / (len(child_sizes) - 1)
    if align == "space_around" and child_sizes:
        gap = free / len(child_sizes)
        return start + gap / 2, gap
    if align == "space_evenly" and child_sizes:
        gap = free / (len(child_sizes) + 1)
        return start + gap, gap
    return float(start), float(spacing)


def aligned_child_offset(start: int, parent_size: int, child_size: int, align: str) -> int:
    if align == "center":
        return start + max(0, (parent_size - child_size) // 2)
    if align == "end":
        return start + max(0, parent_size - child_size)
    return start


def hbox_child_widths(children: list[Any],
                      content_w: int,
                      spacing: int,
                      default_font: dict[str, Any] | None = None,
                      font_path: str | Path | None = None,
                      i18n: dict[str, str] | None = None) -> list[int]:
    widths: list[int] = []
    fixed_w = 0
    grow_total = 0
    grow_indices: list[int] = []
    auto_indices: list[int] = []
    visible_count = sum(1 for child in children if isinstance(child, dict))
    total_spacing = spacing * max(0, visible_count - 1)
    for child in children:
        if not isinstance(child, dict):
            widths.append(0)
            continue
        layout_item = child.get("layout_item") or {}
        grow = int(layout_item.get("grow") or 0)
        if grow > 0:
            widths.append(0)
            grow_total += grow
            grow_indices.append(len(widths) - 1)
            continue
        raw = child.get("size") or child.get("geometry") or {}
        raw_w = raw.get("w", 80)
        child_w = coord(raw_w, content_w)
        if raw_w == "content" or child_w <= 0:
            widths.append(0)
            auto_indices.append(len(widths) - 1)
            continue
        widths.append(max(1, child_w))
        fixed_w += widths[-1]

    remaining = max(1, content_w - total_spacing - fixed_w)
    for index in auto_indices:
        child = children[index]
        child_w = estimate_content_width(child, remaining, default_font, font_path, i18n or {})
        widths[index] = max(1, min(remaining, child_w))
        remaining = max(1, remaining - widths[index])

    for index in grow_indices:
        child = children[index]
        grow = int((child.get("layout_item") or {}).get("grow") or 1)
        widths[index] = max(1, round(remaining * grow / max(1, grow_total)))
    return widths


def estimate_content_width(node: dict[str, Any],
                           max_width: int,
                           default_font: dict[str, Any] | None = None,
                           font_path: str | Path | None = None,
                           i18n: dict[str, str] | None = None) -> int:
    node_type = node.get("type", "container")
    if node_type == "img":
        raw = node.get("size") or node.get("geometry") or {}
        return max(1, coord(raw.get("w", 32), max_width))
    if node_type == "label":
        font = load_font(font_size(node, default_font), font_path)
        letter_space = font_word_space(node, default_font)
        draw = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
        width = text_width(draw, resolve_label_text(node, i18n or {}), font, letter_space) + padding_h(node)
        return max(1, min(max_width, width))
    if node_type == "container":
        children = node.get("children") or []
        spacing = int((node.get("layout") or {}).get("spacing") or 0)
        content_max = max(1, max_width - padding_h(node))
        if layout_type(node) == "hbox":
            child_widths = hbox_child_widths(children, content_max, spacing, default_font, font_path, i18n or {})
            width = sum(child_widths) + spacing * max(0, len([child for child in children if isinstance(child, dict)]) - 1) + padding_h(node)
        else:
            child_widths = [
                estimate_content_width(child, content_max, default_font, font_path, i18n or {})
                for child in children
                if isinstance(child, dict)
            ]
            width = (max(child_widths) if child_widths else 1) + padding_h(node)
        return max(1, min(max_width, width))
    if node_type == "roller":
        label_cfg = node.get("label") if isinstance(node.get("label"), dict) else {}
        widths = [
            estimate_content_width({"type": "label", **label_cfg, "text": item}, max_width, default_font, font_path, i18n or {})
            for item in roller_items(node)
        ]
        return max(1, min(max_width, max(widths) if widths else 80))
    if node_type == "paged_text":
        label = paged_text_label_config(node)
        return estimate_content_width(label, max_width, default_font, font_path, i18n or {})
    if node_type == "overlay":
        return min(max_width, 80)
    if node_type == "button":
        label = button_label_config(node)
        return min(max_width, estimate_content_width(label, max_width, default_font, font_path, i18n or {}) + padding_h(node))
    return min(max_width, 80)


def draw_container(draw: ImageDraw.ImageDraw, node: dict[str, Any], box: Box) -> None:
    border = int(node.get("border_width") or 0)
    radius = int(node.get("radius") or 0)
    alpha = node_opa(node, 0)
    fill = (20, 20, 20, max(0, min(255, alpha)))
    if alpha > 0 or border > 0:
        draw.rounded_rectangle(rect(box), radius=radius, fill=fill, outline=BORDER_COLOR if border else None, width=max(1, border))


def draw_label(draw: ImageDraw.ImageDraw,
               node: dict[str, Any],
               box: Box,
               i18n: dict[str, str],
               default_font: dict[str, Any],
               font_path: str | Path | None) -> None:
    draw_label_frame(draw, node, box)
    font = load_font(font_size(node, default_font), font_path)
    text = resolve_label_text(node, i18n)
    letter_space = font_word_space(node, default_font)
    lines = wrap_text(str(text), font, max(1, box.w - padding_h(node)), letter_space)
    max_lines = node.get("max_lines")
    if isinstance(max_lines, int) and max_lines > 0:
        lines = lines[:max_lines]
    y = box.y + padding_top(node)
    text_box = Box(box.x + padding_left(node), box.y, max(1, box.w - padding_h(node)), box.h)
    for line in lines:
        x = aligned_text_x(draw, line, font, text_box, letter_space, label_align(node))
        draw_spaced_text(draw, (x, y), line, font, with_alpha(FG_COLOR, node_opa(node)), letter_space)
        y += line_height(font, node, default_font)


def draw_label_frame(draw: ImageDraw.ImageDraw, node: dict[str, Any], box: Box) -> None:
    border = int(node.get("border_width") or 0)
    radius = int(node.get("radius") or 0)
    opa = node_opa(node)
    outline = with_alpha(BORDER_COLOR, opa) if border else None
    draw.rounded_rectangle(rect(box), radius=radius, fill=BG_COLOR, outline=outline, width=max(1, border))


def draw_image(image: Image.Image,
               draw: ImageDraw.ImageDraw,
               node: dict[str, Any],
               box: Box,
               resources: dict[str, Any]) -> None:
    decoded = resolve_resource_image(node, resources)
    if decoded is not None:
        transformed = transform_image(decoded, node)
        dest_x = box.x + (box.w - transformed.width) // 2 + int(node.get("offset_x") or 0)
        dest_y = box.y + (box.h - transformed.height) // 2 + int(node.get("offset_y") or 0)
        composite_clipped(image, transformed, dest_x, dest_y)
        return

    draw_image_placeholder(draw, node, box, resources)


def transform_image(image: Image.Image, node: dict[str, Any]) -> Image.Image:
    zoom = int(node.get("zoom") or 256)
    if zoom <= 0:
        zoom = 256
    scale = zoom / 256.0
    width = max(1, round(image.width * scale))
    height = max(1, round(image.height * scale))
    transformed = image.resize((width, height), Image.Resampling.NEAREST)
    rotation = int(node.get("rotation") or 0)
    if rotation:
        transformed = transformed.rotate(-rotation / 10.0, expand=True, resample=Image.Resampling.BICUBIC)
    opa = node_opa(node)
    if opa < 255:
        alpha = transformed.getchannel("A").point(lambda value: value * opa // 255)
        transformed.putalpha(alpha)
    return transformed


def composite_clipped(dest: Image.Image, src: Image.Image, x: int, y: int) -> None:
    left = max(0, x)
    top = max(0, y)
    right = min(dest.width, x + src.width)
    bottom = min(dest.height, y + src.height)
    if left >= right or top >= bottom:
        return
    crop = src.crop((left - x, top - y, right - x, bottom - y))
    dest.alpha_composite(crop, dest=(left, top))


def draw_image_placeholder(draw: ImageDraw.ImageDraw, node: dict[str, Any], box: Box, resources: dict[str, Any]) -> None:
    draw.rectangle(rect(box), outline=BORDER_COLOR, width=1)
    src = str(node.get("src") or "image")
    name = src.replace("@image/", "")
    if name in (resources.get("images") or {}):
        text = name
    else:
        text = "image"
    font = load_font(14)
    draw.text((box.x + 4, box.y + max(2, box.h // 2 - 8)), text, fill=FG_COLOR, font=font)


def resolve_resource_image(node: dict[str, Any], resources: dict[str, Any]) -> Image.Image | None:
    src = str(node.get("src") or "")
    if not src.startswith("@image/"):
        return None
    name = src.replace("@image/", "", 1)
    image_data = (resources.get("_decoded_images") or {}).get(name)
    if not isinstance(image_data, dict):
        return None
    return decode_lvgl_image(image_data)


def decode_lvgl_image(image_data: dict[str, Any]) -> Image.Image | None:
    cf = str(image_data.get("cf") or "")
    width = int(image_data.get("w") or 0)
    height = int(image_data.get("h") or 0)
    stride = int(image_data.get("stride") or 0)
    data = image_data.get("data")
    if width <= 0 or height <= 0 or stride <= 0 or not isinstance(data, list):
        return None
    raw = bytes(int(value) & 0xff for value in data)

    if cf == "LV_COLOR_FORMAT_I4":
        return decode_indexed_image(raw, width, height, stride, 4)
    if cf == "LV_COLOR_FORMAT_I2":
        return decode_indexed_image(raw, width, height, stride, 2)
    if cf == "LV_COLOR_FORMAT_I1":
        return decode_indexed_image(raw, width, height, stride, 1)
    if cf == "LV_COLOR_FORMAT_I8":
        return decode_indexed_image(raw, width, height, stride, 8)
    if cf == "LV_COLOR_FORMAT_L8":
        return decode_l8_image(raw, width, height, stride)
    if cf == "LV_COLOR_FORMAT_A8":
        return decode_a8_image(raw, width, height, stride)
    return None


def decoded_image_to_png(image_data: dict[str, Any]) -> bytes | None:
    image = decode_lvgl_image(image_data)
    if image is None:
        return None
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def decode_indexed_image(raw: bytes, width: int, height: int, stride: int, bits: int) -> Image.Image | None:
    palette_len = (1 << bits) * 4
    if len(raw) < palette_len + stride * height:
        return None
    palette = [tuple(raw[i:i + 4]) for i in range(0, palette_len, 4)]
    pixels = bytearray()
    body = raw[palette_len:]
    mask = (1 << bits) - 1
    per_byte = 8 // bits
    for y in range(height):
        row = body[y * stride:(y + 1) * stride]
        for x in range(width):
            byte = row[x // per_byte]
            shift = 8 - bits - (x % per_byte) * bits
            index = (byte >> shift) & mask
            pixels.extend(palette[index])
    return Image.frombytes("RGBA", (width, height), bytes(pixels))


def decode_l8_image(raw: bytes, width: int, height: int, stride: int) -> Image.Image | None:
    if len(raw) < stride * height:
        return None
    pixels = bytearray()
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        for x in range(width):
            value = row[x]
            pixels.extend((value, value, value, 255))
    return Image.frombytes("RGBA", (width, height), bytes(pixels))


def decode_a8_image(raw: bytes, width: int, height: int, stride: int) -> Image.Image | None:
    if len(raw) < stride * height:
        return None
    pixels = bytearray()
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        for x in range(width):
            pixels.extend((255, 255, 255, row[x]))
    return Image.frombytes("RGBA", (width, height), bytes(pixels))


def draw_button(draw: ImageDraw.ImageDraw,
                node: dict[str, Any],
                box: Box,
                i18n: dict[str, str],
                default_font: dict[str, Any],
                font_path: str | Path | None) -> None:
    radius = int(node.get("radius") if node.get("radius") is not None else BUTTON_DEFAULT_RADIUS)
    border = int(node.get("border_width") if node.get("border_width") is not None else BUTTON_DEFAULT_BORDER_WIDTH)
    alpha = node_opa(node, BUTTON_DEFAULT_OPA)
    fill = (0, 0, 0, 255)
    outline = with_alpha(BORDER_COLOR, alpha) if border else None
    draw.rounded_rectangle(rect(box), radius=radius, fill=fill, outline=outline, width=max(1, border))
    label = button_label_config(node)
    if "opa" not in label:
        label["opa"] = alpha
    raw = label.get("geometry") or label.get("size") or {}
    pad_l, pad_r, pad_t, pad_b = padding_box(node)
    if raw:
        label_box = resolve_box(label, box, Box(box.x + pad_l, box.y + pad_t, max(1, box.w - pad_l - pad_r), max(1, box.h - pad_t - pad_b)), default_font, font_path, i18n)
    else:
        label_box = Box(box.x + pad_l, box.y + pad_t, max(1, box.w - pad_l - pad_r), max(1, box.h - pad_t - pad_b))
    draw_label(draw, label, label_box, i18n, default_font, font_path)


def draw_roller(draw: ImageDraw.ImageDraw,
                node: dict[str, Any],
                box: Box,
                i18n: dict[str, str],
                default_font: dict[str, Any],
                font_path: str | Path | None) -> None:
    label_cfg = node.get("label") if isinstance(node.get("label"), dict) else {}
    row_h, row_gap = roller_row_metrics(node, label_cfg, default_font, font_path)
    radius = int(node.get("radius") if node.get("radius") is not None else ROLLER_DEFAULT_RADIUS)
    border = int(node.get("border_width") if node.get("border_width") is not None else ROLLER_DEFAULT_BORDER_WIDTH)
    normal_opa = opa_value(node.get("opa_normal"), ROLLER_DEFAULT_NORMAL_OPA)
    selected_opa = opa_value(node.get("opa_selected"), ROLLER_DEFAULT_SELECTED_OPA)
    rows = roller_visible_rows(node)
    total_h = len(rows) * row_h + max(0, len(rows) - 1) * row_gap
    y = box.y + max(0, (box.h - total_h) // 2)

    for text, is_selected in rows:
        item_box = Box(box.x, y, box.w, row_h)
        alpha = selected_opa if is_selected else normal_opa
        draw.rounded_rectangle(rect(item_box),
                               radius=radius,
                               fill=(0, 0, 0, 255),
                               outline=with_alpha(BORDER_COLOR, alpha) if border else None,
                               width=max(1, border))
        label = {"type": "label", **label_cfg, "text": text, "align": label_cfg.get("align", "center"), "opa": alpha}
        draw_label_text_only(draw, label, item_box, i18n, default_font, font_path, vertical_align="center")
        y += row_h + row_gap


def draw_paged_text(draw: ImageDraw.ImageDraw,
                    node: dict[str, Any],
                    box: Box,
                    i18n: dict[str, str],
                    default_font: dict[str, Any],
                    font_path: str | Path | None) -> None:
    label = paged_text_label_config(node)
    text_box = inner_highlight_label_box(node, box)
    draw_label_text_only(draw, label, text_box, i18n, default_font, font_path)
    highlight = node.get("highlight")
    if not isinstance(highlight, dict):
        return
    border = int(highlight.get("border_width") if highlight.get("border_width") is not None else PAGED_TEXT_DEFAULT_BORDER_WIDTH)
    radius = int(highlight.get("radius") if highlight.get("radius") is not None else PAGED_TEXT_DEFAULT_RADIUS)
    if border <= 0:
        return
    frame = paged_text_static_highlight_frame_box(box)
    draw.rounded_rectangle(rect(frame), radius=radius, outline=with_alpha(FG_COLOR, 220), width=max(1, border))


def draw_overlay(draw: ImageDraw.ImageDraw,
                 node: dict[str, Any],
                 box: Box,
                 i18n: dict[str, str],
                 default_font: dict[str, Any],
                 font_path: str | Path | None) -> None:
    point = node.get("point") if isinstance(node.get("point"), dict) else {}
    size = int(point.get("size") if point.get("size") is not None else OVERLAY_DEFAULT_POINT_SIZE)
    alpha = opa_value(point.get("opa"), OVERLAY_DEFAULT_POINT_OPA)
    max_items = max(1, int(node.get("max_items") if node.get("max_items") is not None else OVERLAY_DEFAULT_MAX_ITEMS))
    draw.rounded_rectangle(rect(box), radius=4, outline=with_alpha(BORDER_COLOR, 120), width=1)
    for index in range(max_items):
        x = box.x + box.w // 2 + (index - (max_items - 1) / 2) * (size * 3)
        y = box.y + box.h // 2
        draw.ellipse((round(x - size / 2), round(y - size / 2), round(x + size / 2), round(y + size / 2)),
                     fill=with_alpha(FG_COLOR, alpha))
    text_cfg = node.get("text") if isinstance(node.get("text"), dict) else {}
    if text_cfg:
        draw_label_text_only(draw, {"type": "label", **text_cfg}, box, i18n, default_font, font_path)


def draw_label_text_only(draw: ImageDraw.ImageDraw,
                         node: dict[str, Any],
                         box: Box,
                         i18n: dict[str, str],
                         default_font: dict[str, Any],
                         font_path: str | Path | None,
                         vertical_align: str = "top") -> None:
    font = load_font(font_size(node, default_font), font_path)
    text = resolve_label_text(node, i18n)
    letter_space = font_word_space(node, default_font)
    lines = wrap_text(str(text), font, max(1, box.w - padding_h(node)), letter_space)
    max_lines = node.get("max_lines")
    if isinstance(max_lines, int) and max_lines > 0:
        lines = lines[:max_lines]
    row_h = line_height(font, node, default_font)
    y = text_block_y(box, node, len(lines), row_h, vertical_align)
    text_box = Box(box.x + padding_left(node), box.y, max(1, box.w - padding_h(node)), box.h)
    for line in lines:
        x = aligned_text_x(draw, line, font, text_box, letter_space, label_align(node))
        draw_spaced_text(draw, (x, y), line, font, with_alpha(FG_COLOR, node_opa(node)), letter_space)
        y += row_h


def text_block_y(box: Box,
                 node: dict[str, Any],
                 line_count: int,
                 row_h: int,
                 vertical_align: str = "top") -> int:
    top = box.y + padding_top(node)
    if vertical_align != "center":
        return top
    content_h = max(1, box.h - padding_v(node))
    text_h = max(1, line_count) * max(1, row_h)
    return top + max(0, (content_h - text_h) // 2)


def button_label_config(node: dict[str, Any]) -> dict[str, Any]:
    label = node.get("label")
    if isinstance(label, dict):
        cfg = dict(label)
    else:
        cfg = {}
    if "text" not in cfg and "text_key" not in cfg and node.get("text") is not None:
        cfg["text"] = node.get("text")
    if "align" not in cfg:
        cfg["align"] = "center"
    return {"type": "label", **cfg}


def paged_text_label_config(node: dict[str, Any]) -> dict[str, Any]:
    label = node.get("label")
    cfg = dict(label) if isinstance(label, dict) else {}
    if "text" not in cfg and "text_key" not in cfg:
        cfg["text"] = str(node.get("preview_text") or "")
    if "align" not in cfg:
        cfg["align"] = "left"
    if "overflow" not in cfg:
        cfg["overflow"] = "wrap"
    return {"type": "label", **cfg}


def inner_highlight_label_box(node: dict[str, Any], box: Box) -> Box:
    highlight = node.get("highlight")
    inset = int(highlight.get("outset") if highlight.get("outset") is not None else PAGED_TEXT_DEFAULT_OUTSET) if isinstance(highlight, dict) else 0
    return Box(box.x + inset, box.y + inset, max(1, box.w - inset * 2), max(1, box.h - inset * 2))


def paged_text_static_highlight_frame_box(box: Box) -> Box:
    return box


def roller_items(node: dict[str, Any]) -> list[str]:
    items = node.get("items")
    if isinstance(items, list):
        text_items = [str(item) for item in items]
        return text_items if text_items else ["Item"]
    return ["Item 1", "Item 2", "Item 3"]


def roller_selected_index(node: dict[str, Any], items: list[str]) -> int:
    if not items:
        return 0
    raw = node.get("selected_index")
    try:
        selected = int(raw) if raw is not None else len(items) // 2
    except (TypeError, ValueError):
        selected = len(items) // 2
    return max(0, min(len(items) - 1, selected))


def roller_visible_rows(node: dict[str, Any]) -> list[tuple[str, bool]]:
    items = roller_items(node)
    selected = roller_selected_index(node, items)
    if len(items) == 1:
        return [(items[0], True)]
    if len(items) == 2:
        return [(items[0], selected == 0), (items[1], selected == 1)]
    return [
        (items[(selected - 1) % len(items)], False),
        (items[selected], True),
        (items[(selected + 1) % len(items)], False),
    ]


def roller_row_metrics(node: dict[str, Any],
                       label_cfg: dict[str, Any],
                       default_font: dict[str, Any] | None = None,
                       font_path: str | Path | None = None) -> tuple[int, int]:
    row_height = int(node.get("row_height") or 0)
    selected_pad = int(node.get("selected_pad_ver") if node.get("selected_pad_ver") is not None else ROLLER_DEFAULT_SELECTED_PAD_VER)
    font = load_font(font_size(label_cfg, default_font), font_path)
    label_line_height = line_height(font, label_cfg, default_font)
    if row_height <= 0:
        row_height = label_line_height + max(0, selected_pad) * 2
    row_gap_value = node.get("row_gap")
    row_gap = int(row_gap_value) if row_gap_value is not None else -1
    if row_gap < 0:
        row_gap = label_line_height // 3
    return max(1, row_height), max(0, row_gap)


def rect(box: Box) -> tuple[int, int, int, int]:
    return (box.x, box.y, box.x + box.w - 1, box.y + box.h - 1)


def crop_rect(box: Box) -> tuple[int, int, int, int]:
    return (box.x, box.y, box.x + box.w, box.y + box.h)


def layout_type(node: dict[str, Any]) -> str:
    layout = node.get("layout") or {}
    if not isinstance(layout, dict):
        return ""
    return layout.get("type") or ""


def scroll_enabled(node: dict[str, Any]) -> bool:
    scroll = node.get("scroll") or {}
    return isinstance(scroll, dict) and bool(scroll.get("enabled"))


def node_opa(node: dict[str, Any], default: int = 255) -> int:
    return opa_value(node.get("opa"), default)


def opa_value(value: Any, default: int = 255) -> int:
    if value == "transparent" or value == "transp":
        return 0
    if value == "cover":
        return 255
    if isinstance(value, int):
        return max(0, min(255, value))
    if isinstance(value, str) and value.endswith("%"):
        try:
            return max(0, min(255, round(255 * float(value[:-1]) / 100.0)))
        except ValueError:
            return default
    return default


def with_alpha(color: tuple[int, int, int, int], alpha: int) -> tuple[int, int, int, int]:
    return (color[0], color[1], color[2], max(0, min(255, alpha)))


def font_size(node: dict[str, Any], default_font: dict[str, Any] | None = None) -> int:
    font = node.get("font") or {}
    if isinstance(font, dict) and isinstance(font.get("weight"), int):
        return max(8, min(72, font["weight"]))
    if isinstance(default_font, dict) and isinstance(default_font.get("weight"), int):
        return max(8, min(72, default_font["weight"]))
    return 28


def font_word_space(node: dict[str, Any], default_font: dict[str, Any] | None = None) -> int:
    font = node.get("font") or {}
    if isinstance(font, dict) and isinstance(font.get("wordSpace"), int):
        return max(0, font["wordSpace"])
    if isinstance(default_font, dict) and isinstance(default_font.get("wordSpace"), int):
        return max(0, default_font["wordSpace"])
    return 0


def font_row_space(node: dict[str, Any], default_font: dict[str, Any] | None = None) -> int:
    font = node.get("font") or {}
    if isinstance(font, dict) and isinstance(font.get("rowSpace"), int):
        return max(0, font["rowSpace"])
    if isinstance(default_font, dict) and isinstance(default_font.get("rowSpace"), int):
        return max(0, default_font["rowSpace"])
    return 0


def load_font(size: int, font_path: str | Path | None = None) -> ImageFont.ImageFont:
    if font_path is not None:
        path = Path(font_path)
        if path.exists():
            return ImageFont.truetype(str(path), size=size)
    candidates = [
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ]
    for path in candidates:
        if path.exists():
            return ImageFont.truetype(str(path), size=size)
    return ImageFont.load_default()


def wrap_text(text: str, font: ImageFont.ImageFont, width: int, letter_space: int = 0) -> list[str]:
    if not text:
        return [""]
    draw = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    lines: list[str] = []
    for paragraph in text.splitlines() or [""]:
        current = ""
        for char in paragraph:
            probe = current + char
            if current and text_width(draw, probe, font, letter_space) > width:
                lines.append(current)
                current = char
            else:
                current = probe
        lines.append(current)
    if not lines:
        return textwrap.wrap(text, width=20) or [text]
    return lines


def text_width(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont, letter_space: int = 0) -> int:
    if not text:
        return 0
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox[2] - bbox[0] + max(0, len(text) - 1) * letter_space


def draw_spaced_text(draw: ImageDraw.ImageDraw,
                     pos: tuple[int, int],
                     text: str,
                     font: ImageFont.ImageFont,
                     fill: tuple[int, int, int, int],
                     letter_space: int = 0) -> None:
    if letter_space <= 0:
        draw.text(pos, text, fill=fill, font=font)
        return
    x, y = pos
    for char in text:
        draw.text((x, y), char, fill=fill, font=font)
        x += text_width(draw, char, font, 0) + letter_space


def aligned_text_x(draw: ImageDraw.ImageDraw,
                   text: str,
                   font: ImageFont.ImageFont,
                   box: Box,
                   letter_space: int,
                   align: str) -> int:
    left = box.x
    available = max(1, box.w)
    width = text_width(draw, text, font, letter_space)
    if align == "center":
        return left + max(0, (available - width) // 2)
    if align == "right":
        return left + max(0, available - width)
    return left


def line_height(font: ImageFont.ImageFont,
                node: dict[str, Any] | None = None,
                default_font: dict[str, Any] | None = None) -> int:
    try:
        ascent, descent = font.getmetrics()
        base_height = ascent + descent
    except AttributeError:
        bbox = font.getbbox("Hg")
        base_height = bbox[3] - bbox[1]
    return max(1, base_height + 4 + font_row_space(node or {}, default_font))


def text_from_key(node: dict[str, Any]) -> str:
    key = node.get("text_key")
    return f"{{{key}}}" if key else ""


def resolve_label_text(node: dict[str, Any], i18n: dict[str, str]) -> str:
    if node.get("text_key"):
        key = str(node["text_key"])
        return i18n.get(key) or text_from_key(node)
    if node.get("text") is not None:
        return str(node.get("text"))
    return ""


def resolve_button_label_text(node: dict[str, Any], i18n: dict[str, str]) -> str:
    return resolve_label_text(button_label_config(node), i18n)


def label_align(node: dict[str, Any]) -> str:
    return str(node.get("align") or "center")


def padding_box(node: dict[str, Any]) -> tuple[int, int, int, int]:
    hor = int(node.get("pad_hor") or 0)
    ver = int(node.get("pad_ver") or 0)
    raw_padding = raw_padding_for_node(node)
    if isinstance(raw_padding, list):
        values = [int(value or 0) for value in raw_padding[:4]]
        while len(values) < 4:
            values.append(0)
        return values[0], values[1], values[2], values[3]
    padding = raw_padding if isinstance(raw_padding, dict) else {}
    left = int(padding.get("left", hor) or 0)
    right = int(padding.get("right", hor) or 0)
    top = int(padding.get("top", ver) or 0)
    bottom = int(padding.get("bottom", ver) or 0)
    return left, right, top, bottom


def raw_padding_for_node(node: dict[str, Any]) -> Any:
    if node.get("padding") is not None:
        return node.get("padding")
    layout = node.get("layout")
    if isinstance(layout, dict):
        return layout.get("padding")
    return None


def padding_left(node: dict[str, Any]) -> int:
    return padding_box(node)[0]


def padding_top(node: dict[str, Any]) -> int:
    return padding_box(node)[2]


def padding_h(node: dict[str, Any]) -> int:
    left, right, _, _ = padding_box(node)
    return left + right


def padding_v(node: dict[str, Any]) -> int:
    _, _, top, bottom = padding_box(node)
    return top + bottom
