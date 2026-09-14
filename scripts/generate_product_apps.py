"""根据产品清单生成 App 构建清单、身份定义和注册表。"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


CAPABILITIES = {
    "upload_progress": "PRODUCT_APP_CAP_UPLOAD_PROGRESS",
    "assistant_open_blocked": "PRODUCT_APP_CAP_ASSISTANT_OPEN_BLOCKED",
    "display_position": "PRODUCT_APP_CAP_DISPLAY_POSITION",
    "display_position_float": "PRODUCT_APP_CAP_DISPLAY_POSITION_FLOAT",
    "guide": "PRODUCT_APP_CAP_GUIDE",
    "guide_simple": "PRODUCT_APP_CAP_GUIDE_SIMPLE",
    "stt_question_no_skip": "PRODUCT_APP_CAP_STT_QUESTION_NO_SKIP",
}

HOME_ACTIONS = {
    "set_view": "PRODUCT_APP_HOME_ACTION_SET_VIEW",
    "open_assistant": "PRODUCT_APP_HOME_ACTION_OPEN_ASSISTANT",
}

ROLES = {
    "home": "PRODUCT_APP_ROLE_HOME",
    "langselection": "PRODUCT_APP_ROLE_LANGSELECTION",
    "watch_home": "PRODUCT_APP_ROLE_WATCH_HOME",
    "guide": "PRODUCT_APP_ROLE_GUIDE",
    "assistant": "PRODUCT_APP_ROLE_ASSISTANT",
}

IDENTIFIER_PATTERN = re.compile(r"^[A-Z][A-Z0-9_]*$")
MODULE_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")
STATUS_BAR_POSITIONS = {"top", "bottom"}
HOME_GUIDE_STEP2_MODES = {
    "hidden": "PRODUCT_HOME_GUIDE_STEP2_MODE_HIDDEN",
    "transcribe": "PRODUCT_HOME_GUIDE_STEP2_MODE_TRANSCRIBE",
    "translate": "PRODUCT_HOME_GUIDE_STEP2_MODE_TRANSLATE",
}

def fail(message: str) -> None:
    raise ValueError(message)


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        fail(f"product manifest not found: {path}")
    except json.JSONDecodeError as exc:
        fail(f"invalid product manifest {path}: {exc}")
    if not isinstance(data, dict):
        fail(f"product manifest root must be an object: {path}")
    if data.get("schema_version") != 1:
        fail(f"unsupported product manifest schema_version: {data.get('schema_version')!r}")
    product_name = data.get("product")
    if not isinstance(product_name, str) or not MODULE_PATTERN.fullmatch(product_name):
        fail(f"product is invalid: {product_name!r}")
    if product_name != path.parent.name:
        fail(f"product name {product_name!r} does not match directory {path.parent.name!r}")
    return data


def parse_modules(
    data: dict[str, Any],
    apps_dir: Path,
    common_modules: set[str],
) -> list[dict[str, str]]:
    raw_modules = data.get("modules")
    if not isinstance(raw_modules, list) or not raw_modules:
        fail("modules must be a non-empty array")

    modules: list[dict[str, str]] = []
    seen_directories: set[str] = set()
    seen_apis: set[str] = set()
    for index, raw_module in enumerate(raw_modules):
        if isinstance(raw_module, str):
            directory = raw_module
            api = raw_module
            registration = "function"
            symbol = f"{api}_app_register"
            runtime = None
        elif isinstance(raw_module, dict):
            directory = raw_module.get("directory")
            api = raw_module.get("api", directory)
            registration = raw_module.get("registration", "function")
            runtime = raw_module.get("runtime")
            symbol = raw_module.get(
                "symbol",
                f"{api}_app_module" if registration == "descriptor" else f"{api}_app_register",
            )
        else:
            fail(f"modules[{index}] must be a string or object")

        if not isinstance(directory, str) or not MODULE_PATTERN.fullmatch(directory):
            fail(f"modules[{index}].directory is invalid: {directory!r}")
        if not isinstance(api, str) or not MODULE_PATTERN.fullmatch(api):
            fail(f"modules[{index}].api is invalid: {api!r}")
        if registration not in {"function", "descriptor"}:
            fail(f"modules[{index}].registration is invalid: {registration!r}")
        if registration == "descriptor":
            if not isinstance(runtime, str) or runtime not in common_modules:
                fail(
                    f"descriptor module {directory!r} must reference a selected "
                    f"common runtime: {runtime!r}"
                )
        elif runtime is not None:
            fail(f"function module {directory!r} cannot declare runtime")
        if not isinstance(symbol, str) or not re.fullmatch(r"^[A-Za-z_][A-Za-z0-9_]*$", symbol):
            fail(f"modules[{index}].symbol is invalid: {symbol!r}")
        if directory in seen_directories:
            fail(f"duplicate module directory: {directory}")
        if api in seen_apis:
            fail(f"duplicate module api: {api}")
        if not (apps_dir / directory).is_dir():
            fail(f"selected app module directory not found: {apps_dir / directory}")

        header = f"{directory}/{api}.h"
        if not (apps_dir / header).is_file():
            fail(f"selected app module header not found: {apps_dir / header}")

        seen_directories.add(directory)
        seen_apis.add(api)
        modules.append(
            {
                "directory": directory,
                "api": api,
                "header": header,
                "registration": registration,
                "symbol": symbol,
            }
        )
    return modules


def parse_common_modules(data: dict[str, Any], apps_dir: Path) -> list[str]:
    raw_modules = data.get("common_modules", [])
    if not isinstance(raw_modules, list):
        fail("common_modules must be an array")

    modules: list[str] = []
    for index, module in enumerate(raw_modules):
        if not isinstance(module, str) or not MODULE_PATTERN.fullmatch(module):
            fail(f"common_modules[{index}] is invalid: {module!r}")
        if module in modules:
            fail(f"duplicate common module: {module}")
        if not (apps_dir / "common" / module).is_dir():
            fail(f"selected common module directory not found: {apps_dir / 'common' / module}")
        modules.append(module)
    return modules


def parse_apps(
    data: dict[str, Any],
    module_directories: set[str],
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    raw_apps = data.get("apps")
    if not isinstance(raw_apps, list) or not raw_apps:
        fail("apps must be a non-empty array")

    apps: list[dict[str, Any]] = []
    by_key: dict[str, dict[str, Any]] = {}
    names: set[str] = set()
    msg_ids: set[int] = set()
    macros: set[str] = set()

    for index, raw_app in enumerate(raw_apps):
        if not isinstance(raw_app, dict):
            fail(f"apps[{index}] must be an object")

        key = raw_app.get("key")
        macro = raw_app.get("macro")
        name = raw_app.get("name")
        module = raw_app.get("module")
        msg_id = raw_app.get("msg_id")
        home_action = raw_app.get("home_action", "set_view")
        raw_capabilities = raw_app.get("capabilities", [])

        if not isinstance(key, str) or not key:
            fail(f"apps[{index}].key must be a non-empty string")
        if key in by_key:
            fail(f"duplicate app key: {key}")
        if not isinstance(macro, str) or not IDENTIFIER_PATTERN.fullmatch(macro):
            fail(f"apps[{index}].macro is invalid: {macro!r}")
        if macro in macros:
            fail(f"duplicate app macro: {macro}")
        if not isinstance(name, str) or not name:
            fail(f"apps[{index}].name must be a non-empty string")
        if len(name.encode("utf-8")) >= 32:
            fail(f"app name is too long for protocol field: {name}")
        if name in names:
            fail(f"duplicate app name: {name}")
        if module is not None and module not in module_directories:
            fail(f"app {key} references an unselected module: {module}")
        if msg_id is not None:
            if not isinstance(msg_id, int) or isinstance(msg_id, bool) or msg_id <= 0 or msg_id > 0xFFFFFFFF:
                fail(f"apps[{index}].msg_id is invalid: {msg_id!r}")
            if msg_id in msg_ids:
                fail(f"duplicate app msg_id: {msg_id}")
            msg_ids.add(msg_id)
        if not isinstance(home_action, str) or home_action not in HOME_ACTIONS:
            fail(
                f"apps[{index}].home_action must be one of "
                f"{sorted(HOME_ACTIONS)!r}: {home_action!r}"
            )
        if not isinstance(raw_capabilities, list):
            fail(f"apps[{index}].capabilities must be an array")

        capability_names: list[str] = []
        for capability in raw_capabilities:
            if capability not in CAPABILITIES:
                fail(f"app {key} has unknown capability: {capability!r}")
            if capability not in capability_names:
                capability_names.append(capability)

        app = {
            "key": key,
            "macro": macro,
            "name": name,
            "module": module,
            "msg_id": msg_id,
            "home_action": home_action,
            "capabilities": capability_names,
        }
        apps.append(app)
        by_key[key] = app
        names.add(name)
        macros.add(macro)

    return apps, by_key


def parse_roles(data: dict[str, Any], apps_by_key: dict[str, dict[str, Any]]) -> dict[str, str]:
    raw_roles = data.get("roles")
    if not isinstance(raw_roles, dict):
        fail("roles must be an object")

    roles: dict[str, str] = {}
    for role, enum_name in ROLES.items():
        app_key = raw_roles.get(role)
        if not isinstance(app_key, str) or app_key not in apps_by_key:
            fail(f"role {role} must reference an app key")
        roles[enum_name] = apps_by_key[app_key]["name"]
    return roles


def parse_status_bar_headset(data: dict[str, Any]) -> bool:
    enabled = data.get("status_bar_headset", False)
    if not isinstance(enabled, bool):
        fail("status_bar_headset must be a boolean")
    return enabled


def parse_home_status_bar_position(data: dict[str, Any]) -> str:
    position = data.get("home_status_bar_position", "bottom")
    if position not in STATUS_BAR_POSITIONS:
        fail(
            "home_status_bar_position must be one of "
            f"{sorted(STATUS_BAR_POSITIONS)!r}: {position!r}"
        )
    return position


def parse_home_guide_step2_mode(
    data: dict[str, Any],
    apps_by_key: dict[str, dict[str, Any]],
) -> tuple[str, str]:
    mode = data.get("home_guide_step2_mode", "translate")
    if mode not in HOME_GUIDE_STEP2_MODES:
        fail(
            "home_guide_step2_mode must be one of "
            f"{sorted(HOME_GUIDE_STEP2_MODES)!r}: {mode!r}"
        )

    demo_app_key = "transcribe" if mode == "transcribe" else "translate"
    if demo_app_key not in apps_by_key:
        fail(
            f"home_guide_step2_mode {mode!r} requires app key {demo_app_key!r}"
        )
    return mode, apps_by_key[demo_app_key]["macro"]


def parse_home_guide_apps(
    data: dict[str, Any],
    apps_by_key: dict[str, dict[str, Any]],
    demo_app_macro: str,
) -> list[str]:
    """校验独立教学菜单，返回按展示顺序排列的应用名称宏。"""
    keys = data.get("home_guide_apps", [])
    if not isinstance(keys, list) or any(not isinstance(key, str) for key in keys):
        fail("home_guide_apps must be an array of app keys")
    if len(keys) > 255 or len(set(keys)) != len(keys):
        fail("home_guide_apps must contain at most 255 unique app keys")
    for key in keys:
        if key not in apps_by_key:
            fail(f"home_guide_apps references unknown app key: {key!r}")
    macros = [apps_by_key[key]["macro"] for key in keys]
    if macros and demo_app_macro not in macros:
        fail("home_guide_apps must include the step2 demo app")
    return macros


def cmake_quote(value: str) -> str:
    return value.replace("\\", "/").replace('"', '\\"')


def write_sources_cmake(
    output: Path,
    manifest: Path,
    common_modules: list[str],
    modules: list[dict[str, str]],
) -> None:
    lines = [
        "# 由 scripts/generate_product_apps.py 自动生成，请勿手动修改。",
        f'set(PRODUCT_APP_MANIFEST "{cmake_quote(str(manifest))}")',
        "set(PRODUCT_APP_COMMON_MODULES",
    ]
    lines.extend(f'    "{module}"' for module in common_modules)
    lines.extend([
        ")",
        "set(PRODUCT_APP_MODULES",
    ])
    lines.extend(f'    "{module["directory"]}"' for module in modules)
    lines.extend([")", ""])
    output.write_text("\n".join(lines), encoding="utf-8")


def write_definitions_header(
    output: Path,
    apps: list[dict[str, Any]],
    home_status_bar_position: str,
    home_guide_step2_mode: str,
    home_guide_step2_app_macro: str,
    status_bar_headset: bool,
    home_guide_apps: list[str],
) -> None:
    lines = [
        "/**",
        " * @file product_app_generated.h",
        " * @brief 当前产品 App 名称与消息 ID 生成定义。",
        " */",
        "#pragma once",
        "",
        "#define PRODUCT_HOME_STATUS_BAR_AT_TOP "
        f"({1 if home_status_bar_position == 'top' else 0})",
        "",
        "#define PRODUCT_STATUS_BAR_HEADSET "
        f"({1 if status_bar_headset else 0}) "
        "///< 是否启用状态栏耳机附件显示功能。",
        "",
        "#define PRODUCT_HOME_GUIDE_STEP2_MODE_HIDDEN (0) ///< 隐藏 Home 引导第二步语言栏。",
        "#define PRODUCT_HOME_GUIDE_STEP2_MODE_TRANSCRIBE (1) ///< 使用转写单语言栏。",
        "#define PRODUCT_HOME_GUIDE_STEP2_MODE_TRANSLATE (2) ///< 使用翻译双语言栏。",
        "#define PRODUCT_HOME_GUIDE_STEP2_MODE \\",
        f"    ({HOME_GUIDE_STEP2_MODES[home_guide_step2_mode]})",
        "",
    ]
    for app in apps:
        lines.append(f'#define APP_NAME_{app["macro"]} "{app["name"]}"')
        if app["msg_id"] is not None:
            lines.append(f'#define APP_MSG_ID_{app["macro"]} ({app["msg_id"]}u)')
    lines.extend(
        [
            "",
            "#define PRODUCT_HOME_GUIDE_STEP2_APP_NAME \\",
            f"    APP_NAME_{home_guide_step2_app_macro}",
        ]
    )
    lines.extend([
        "",
        f"#define PRODUCT_HOME_GUIDE_APPS_COUNT ({len(home_guide_apps)}) ///< 独立教学菜单项数，0 沿用真实首页。",
        "#define PRODUCT_HOME_GUIDE_APPS {" + ", ".join(
            f"APP_NAME_{macro}" for macro in home_guide_apps
        ) + "} ///< 独立教学菜单应用名，按左到右排列。",
        "",
    ])
    output.write_text("\n".join(lines), encoding="utf-8")


def capability_expression(app: dict[str, Any]) -> str:
    capability_names = app["capabilities"]
    if not capability_names:
        return "0u"
    return " | ".join(CAPABILITIES[name] for name in capability_names)


def write_registry_source(
    output: Path,
    modules: list[dict[str, str]],
    apps: list[dict[str, Any]],
    roles: dict[str, str],
) -> None:
    lines = [
        "/**",
        " * @file product_app_generated.c",
        " * @brief 当前产品 App 注册表与公共属性查询实现。",
        " */",
        '#include "product_app.h"',
        '#include "product_app_generated.h"',
        "",
    ]
    for module in modules:
        lines.append(f'#include "{module["header"]}"')
    lines.extend(
        [
            "",
            "#include <stddef.h>",
            "#include <string.h>",
            "",
            "typedef struct {",
            "    const char* name;       ///< App 名称。",
            "    uint32_t msg_id;        ///< App 消息 ID。",
            "    uint32_t capabilities;  ///< App 通用能力集合。",
            "    product_app_home_action_t home_action; ///< Home 菜单激活动作。",
            "    bool has_msg_id;        ///< 是否声明消息 ID。",
            "} product_app_entry_t;",
            "",
            "static const product_app_entry_t s_product_apps[] = {",
        ]
    )
    for app in apps:
        msg_id = f'APP_MSG_ID_{app["macro"]}' if app["msg_id"] is not None else "0u"
        has_msg_id = "true" if app["msg_id"] is not None else "false"
        lines.append(
            f'    {{APP_NAME_{app["macro"]}, {msg_id}, {capability_expression(app)}, '
            f'{HOME_ACTIONS[app["home_action"]]}, {has_msg_id}}},'
        )
    lines.extend(
        [
            "};",
            "",
            "bool product_apps_register_all(void) {",
        ]
    )
    for module in modules:
        if module["registration"] == "descriptor":
            symbol = module["symbol"]
            lines.extend(
                [
                    f"    if ({symbol}.register_profile == NULL ||",
                    f"        {symbol}.profile == NULL ||",
                    f"        !{symbol}.register_profile({symbol}.profile)) {{",
                    "        return false;",
                    "    }",
                ]
            )
        else:
            lines.extend(
                [
                    f'    if (!{module["symbol"]}()) {{',
                    "        return false;",
                    "    }",
                ]
            )
    lines.extend(
        [
            "",
            "    return true;",
            "}",
            "",
            "const char* product_app_role_name(product_app_role_t role) {",
            "    switch (role) {",
        ]
    )
    for role_enum, name in roles.items():
        macro = next(app["macro"] for app in apps if app["name"] == name)
        lines.append(f"        case {role_enum}: return APP_NAME_{macro};")
    lines.extend(
        [
            "        default: return NULL;",
            "    }",
            "}",
            "",
            "bool product_app_name_has_capability(const char* name, product_app_capability_t capability) {",
            "    if (name == NULL) {",
            "        return false;",
            "    }",
            "    for (size_t i = 0; i < sizeof(s_product_apps) / sizeof(s_product_apps[0]); i++) {",
            "        if (strcmp(name, s_product_apps[i].name) == 0) {",
            "            return (s_product_apps[i].capabilities & (uint32_t)capability) != 0u;",
            "        }",
            "    }",
            "    return false;",
            "}",
            "",
            "bool product_app_msg_id_has_capability(uint32_t msg_id, product_app_capability_t capability) {",
            "    for (size_t i = 0; i < sizeof(s_product_apps) / sizeof(s_product_apps[0]); i++) {",
            "        if (s_product_apps[i].has_msg_id && s_product_apps[i].msg_id == msg_id) {",
            "            return (s_product_apps[i].capabilities & (uint32_t)capability) != 0u;",
            "        }",
            "    }",
            "    return false;",
            "}",
            "",
            "product_app_home_action_t product_app_get_home_action(const char* name) {",
            "    if (name == NULL) {",
            "        return PRODUCT_APP_HOME_ACTION_SET_VIEW;",
            "    }",
            "    for (size_t i = 0; i < sizeof(s_product_apps) / sizeof(s_product_apps[0]); i++) {",
            "        if (strcmp(name, s_product_apps[i].name) == 0) {",
            "            return s_product_apps[i].home_action;",
            "        }",
            "    }",
            "    return PRODUCT_APP_HOME_ACTION_SET_VIEW;",
            "}",
            "",
        ]
    )
    output.write_text("\n".join(lines), encoding="utf-8")


def generate(manifest: Path, apps_dir: Path, output_dir: Path) -> None:
    data = load_manifest(manifest)
    common_modules = parse_common_modules(data, apps_dir)
    modules = parse_modules(data, apps_dir, set(common_modules))
    apps, apps_by_key = parse_apps(data, {module["directory"] for module in modules})
    roles = parse_roles(data, apps_by_key)
    status_bar_headset = parse_status_bar_headset(data)
    home_status_bar_position = parse_home_status_bar_position(data)
    home_guide_step2_mode, home_guide_step2_app_macro = parse_home_guide_step2_mode(
        data,
        apps_by_key,
    )
    home_guide_apps = parse_home_guide_apps(data, apps_by_key, home_guide_step2_app_macro)
    output_dir.mkdir(parents=True, exist_ok=True)
    write_sources_cmake(
        output_dir / "product_app_sources.cmake",
        manifest,
        common_modules,
        modules,
    )
    write_definitions_header(
        output_dir / "product_app_generated.h",
        apps,
        home_status_bar_position,
        home_guide_step2_mode,
        home_guide_step2_app_macro,
        status_bar_headset,
        home_guide_apps,
    )
    write_registry_source(output_dir / "product_app_generated.c", modules, apps, roles)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--apps-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    try:
        generate(args.manifest.resolve(), args.apps_dir.resolve(), args.output_dir.resolve())
    except (OSError, ValueError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
