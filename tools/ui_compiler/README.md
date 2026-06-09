# jy_app UI Compiler

`tools/ui_compiler` provides the first lightweight host-side toolchain for declarative UI layout generation.

## Roles

- `uic.py` compiles `*.ui.json` into `*_ui.h` and `*_ui.c`.
- `rcc.py` compiles `*.res.json` into `*_res.h`.
- Hand-written app code still owns messages, state, and content updates.

Generated UI code only exposes:

```c
bool <name>_init_ui(lv_obj_t* parent, <name>_ui_t* ui);
```

The `<name>_ui_t` struct contains handles for JSON nodes with an `id`.

If the JSON uses `modes`, each mode must provide a complete `root` tree. The
generator emits a mode enum and the init function receives the selected mode:

```c
bool <name>_init_ui(lv_obj_t* parent, <name>_ui_t* ui, <name>_mode_t mode);
```

All mode roots must expose the same `id` set and widget types so the handle
struct stays stable.

## Layout Rules

- If a container has no `layout`, its direct children use `geometry.x/y/w/h`.
- If a container has `layout`, its direct children are managed by the layout. Their `x/y` values are ignored; use `size`, alignment, and container layout fields instead.
- First version supports `vbox` and `hbox`.
- Containers can use `max_size.w/h` to cap growth while keeping `geometry.h` or `size.h` as `content`.
- Containers can use `height_policy: "content_max_parent"` when content should grow naturally but stop at the parent content height, then rely on scrolling.
- Children inside a layout can use `layout_item.fill_x/fill_y/grow` for flex item sizing.
- Labels can use `max_lines` with `overflow: "wrap"` to cap wrapped text height.
- Labels can use `text_key` to initialize text from `app_get_str("KEY")`.
- Any widget can use `floating: true` when it should be excluded from parent layout and drawn above sibling content.
- `opa` accepts integers or semantic values such as `"transparent"`, `"cover"`, and `"60%"`.

Example:

```json
{
  "type": "container",
  "geometry": { "w": "100%", "h": "content" },
  "max_size": { "h": "100%" },
  "height_policy": "content_max_parent",
  "scroll": { "enabled": true, "dir": "ver", "scrollbar": "auto" }
}
```

Mode example:

```json
{
  "name": "notify_popup",
  "modes": {
    "message": { "root": { "type": "container", "id": "root" } },
    "call": { "root": { "type": "container", "id": "root" } }
  }
}
```

## Supported Widgets

First version supports:

- `container`
- `label`
- `img`
- `button`
- `overlay`
- `paged_text`
- `roller`

The generator uses `common/widgets` wrappers instead of direct LVGL object creation.

`overlay` supports `max_items`, `point.size`, `point.opa`, and an embedded
`text` label config for default overlay text style.

`paged_text` supports top-level geometry/size fields, embedded `label` config,
optional `highlight` (`mask_opa`, `border_width`, `radius`, `outset`),
`step_mode` (`line_page` or `view_percent`), and `step_percent`.

`roller` supports static `items`, embedded `label` config, `selected_font`,
`overflow_mode` (`scroll` or `expand_height`), `row_height`, `row_gap`,
`selected_pad_ver`, `radius`, `border_width`, `opa_normal`, and `opa_selected`.

## Resource References

UI JSON can reference images from a resource JSON:

```json
"src": "@image/robot"
```

If the UI name is `ai_home`, this becomes:

```c
AI_HOME_RES_IMAGE_ROBOT
```

## Build Integration

CMake discovers `apps/**/*.ui.json` and `apps/**/*.res.json`.

Generated files are written under:

```text
build/generated/ui/
```

Generated `.c` files are added to the firmware or simulator target. Generated files are build outputs and should not be committed.

## Manual Usage

```bash
python tools/ui_compiler/rcc.py tools/ui_compiler/examples/ai_home.res.json --out-dir build/generated/ui
python tools/ui_compiler/uic.py tools/ui_compiler/examples/ai_home.ui.json --out-dir build/generated/ui
```

## Tests

```bash
python -m unittest discover -s tools/ui_compiler/tests -v
```
