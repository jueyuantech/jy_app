#!/usr/bin/env python3
"""Local web server for editing jy_app *.ui.json files."""

from __future__ import annotations

import argparse
import csv
import importlib
import json
import mimetypes
import re
import sys
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.ui_designer import preview_runtime

STATIC_ROOT = Path(__file__).resolve().parent / "static"
UI_ROOTS = ("apps", "system")
PREVIEW_RUNTIME_MTIME = preview_runtime.__file__ and Path(preview_runtime.__file__).stat().st_mtime


def refresh_preview_runtime_if_changed() -> None:
    global PREVIEW_RUNTIME_MTIME
    if not preview_runtime.__file__:
        return
    current_mtime = Path(preview_runtime.__file__).stat().st_mtime
    if PREVIEW_RUNTIME_MTIME == current_mtime:
        return
    importlib.reload(preview_runtime)
    PREVIEW_RUNTIME_MTIME = current_mtime


def list_ui_files(root: Path) -> list[str]:
    files: list[str] = []
    for prefix in UI_ROOTS:
        base = root / prefix
        if not base.exists():
            continue
        for path in base.rglob("*.ui.json"):
            files.append(path.relative_to(root).as_posix())
    return sorted(files)


def read_resources(root: Path) -> dict:
    path = root / "ui.res.json"
    if not path.exists():
        return {"name": "ui", "images": {}}
    return json.loads(path.read_text(encoding="utf-8"))


def read_preview_resources(root: Path) -> dict:
    resources = read_resources(root)
    decoded_images = {}
    for image_id, image in (resources.get("images") or {}).items():
        if not isinstance(image, dict):
            continue
        symbol = image.get("symbol")
        if not isinstance(symbol, str) or not symbol:
            continue
        decoded = read_lvgl_image(root, symbol)
        if decoded is not None:
            decoded_images[image_id] = decoded
    if decoded_images:
        resources["_decoded_images"] = decoded_images
    return resources


def read_lvgl_image(root: Path, symbol: str):
    source = root / "images" / f"{symbol}.c"
    if not source.exists():
        matches = list((root / "images").glob(f"{symbol}*.c")) if (root / "images").exists() else []
        if len(matches) == 1:
            source = matches[0]
        else:
            return None

    text = source.read_text(encoding="utf-8", errors="ignore")
    descriptor = re.search(rf"lv_image_dsc_t\s+{re.escape(symbol)}\s*=\s*\{{(?P<body>.*?)\}};", text, re.S)
    if not descriptor:
        return None
    body = descriptor.group("body")
    data_match = re.search(r"\.data\s*=\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)", body)
    if not data_match:
        return None

    width = read_c_int_field(body, "w")
    height = read_c_int_field(body, "h")
    stride = read_c_int_field(body, "stride")
    cf = read_c_symbol_field(body, "cf")
    if width <= 0 or height <= 0 or stride <= 0 or not cf:
        return None

    array_name = data_match.group("name")
    array_match = re.search(rf"{re.escape(array_name)}\s*\[\].*?=\s*\{{(?P<body>.*?)\}};", text, re.S)
    if not array_match:
        return None
    raw = parse_c_byte_array(array_match.group("body"))
    if not raw:
        return None
    return {"w": width, "h": height, "stride": stride, "cf": cf, "data": raw}


def read_c_int_field(body: str, name: str) -> int:
    match = re.search(rf"\.header\.{name}\s*=\s*(\d+)", body)
    return int(match.group(1)) if match else 0


def read_c_symbol_field(body: str, name: str) -> str:
    match = re.search(rf"\.header\.{name}\s*=\s*([A-Za-z_][A-Za-z0-9_]*)", body)
    return match.group(1) if match else ""


def parse_c_byte_array(body: str) -> list[int]:
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//.*", "", body)
    values = []
    for token in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", body):
        values.append(int(token, 0) & 0xff)
    return values


def read_i18n(root: Path) -> dict:
    path = root / "StringPool.csv"
    if not path.exists():
        return {"default_locale": "zh-CN", "locales": [], "strings": {}}

    with path.open("r", encoding="utf-8-sig", newline="") as file:
        reader = csv.DictReader(file, skipinitialspace=True)
        fieldnames = [(name or "").strip() for name in (reader.fieldnames or [])]
        reader.fieldnames = fieldnames
        if not fieldnames or fieldnames[0] != "StringID":
            raise ValueError("StringPool.csv header must start with StringID")

        locales = [name for name in fieldnames[1:] if name]
        strings = {locale: {} for locale in locales}
        for row in reader:
            key = clean_csv_value(row.get("StringID"))
            if not key:
                continue
            for locale in locales:
                strings[locale][key] = clean_csv_value(row.get(locale))

    default_locale = "zh-CN" if "zh-CN" in locales else (locales[0] if locales else "")
    return {"default_locale": default_locale, "locales": locales, "strings": strings}


def read_default_font_info(root: Path) -> dict:
    path = root / "lfsd" / "system" / "config.json"
    fallback = {"weight": 28, "wordSpace": 0, "rowSpace": 0}
    if not path.exists():
        return fallback
    data = json.loads(path.read_text(encoding="utf-8"))
    fontinfo = data.get("fontinfo") if isinstance(data, dict) else None
    if not isinstance(fontinfo, dict):
        return fallback
    result = fallback.copy()
    for key in ("weight", "wordSpace", "rowSpace"):
        value = fontinfo.get(key)
        if isinstance(value, int) and value >= 0:
            result[key] = value
    return result


def resolve_system_font_file(root: Path) -> Path | None:
    path = root / "lfsd" / "system" / "font" / "font.ttf"
    return path if path.exists() else None


def clean_csv_value(value: str | None) -> str:
    text = (value or "").strip()
    if "\n" not in text and "\r" not in text:
        return text
    return "\n".join(line.strip() for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"))


def resolve_ui_path(root: Path, rel_path: str) -> Path:
    normalized = Path(unquote(rel_path).replace("\\", "/"))
    if normalized.is_absolute() or ".." in normalized.parts:
        raise ValueError("path escapes project")
    if normalized.parts[0:1] not in (("apps",), ("system",)):
        raise ValueError("ui path must be under apps/ or system/")
    if not normalized.name.endswith(".ui.json"):
        raise ValueError("ui path must end with .ui.json")
    target = (root / normalized).resolve()
    root_resolved = root.resolve()
    if root_resolved not in target.parents:
        raise ValueError("path escapes project")
    return target


def read_ui_file(root: Path, rel_path: str) -> dict:
    path = resolve_ui_path(root, rel_path)
    return json.loads(path.read_text(encoding="utf-8"))


def write_ui_file(root: Path, rel_path: str, payload: dict) -> Path:
    path = resolve_ui_path(root, rel_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    path.write_text(text + "\n", encoding="utf-8")
    return path


class DesignerServer(ThreadingHTTPServer):
    def __init__(self,
                 server_address,
                 handler_cls,
                 repo_root: Path):
        super().__init__(server_address, handler_cls)
        self.repo_root = repo_root


class DesignerHandler(BaseHTTPRequestHandler):
    server: DesignerServer

    def log_message(self, fmt, *args):
        print("[%s] %s" % (self.log_date_time_string(), fmt % args))

    def do_GET(self):
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/api/files":
                self.write_json({"files": list_ui_files(self.server.repo_root)})
            elif parsed.path == "/api/resources":
                self.write_json(read_resources(self.server.repo_root))
            elif parsed.path == "/api/i18n":
                self.write_json(read_i18n(self.server.repo_root))
            elif parsed.path == "/api/default-font":
                self.write_json(read_default_font_info(self.server.repo_root))
            elif parsed.path == "/api/font":
                self.write_font_file()
            elif parsed.path == "/api/resource/image":
                query = parse_qs(parsed.query)
                self.write_resource_image_png(query.get("name", [""])[0])
            elif parsed.path == "/api/ui":
                query = parse_qs(parsed.query)
                self.write_json(read_ui_file(self.server.repo_root, query.get("path", [""])[0]))
            else:
                self.write_static(parsed.path)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            self.write_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)

    def do_POST(self):
        parsed = urlparse(self.path)
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            if parsed.path == "/api/ui":
                query = parse_qs(parsed.query)
                target = write_ui_file(self.server.repo_root, query.get("path", [""])[0], payload)
                self.write_json({"ok": True, "path": target.relative_to(self.server.repo_root).as_posix()})
            elif parsed.path == "/api/preview/render":
                self.write_preview_png(payload)
            else:
                self.write_json({"error": "not found"}, HTTPStatus.NOT_FOUND)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            self.write_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)

    def write_json(self, payload: dict, status: HTTPStatus = HTTPStatus.OK):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def write_font_file(self):
        path = resolve_system_font_file(self.server.repo_root)
        if path is None:
            self.write_json({"error": "font not found"}, HTTPStatus.NOT_FOUND)
            return
        body = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "font/ttf")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def write_resource_image_png(self, image_name: str):
        decoded = (read_preview_resources(self.server.repo_root).get("_decoded_images") or {}).get(image_name)
        if decoded is None:
            self.write_json({"error": "image not found"}, HTTPStatus.NOT_FOUND)
            return
        body = preview_runtime.decoded_image_to_png(decoded)
        if body is None:
            self.write_json({"error": "unsupported image format"}, HTTPStatus.BAD_REQUEST)
            return
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "image/png")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def write_preview_png(self, payload: dict):
        doc = payload.get("doc")
        if not isinstance(doc, dict):
            raise ValueError("preview doc must be an object")
        refresh_preview_runtime_if_changed()
        i18n_data = read_i18n(self.server.repo_root)
        locale = str(payload.get("locale") or i18n_data.get("default_locale") or "")
        body = preview_runtime.render_png(
            doc,
            mode=str(payload.get("mode") or ""),
            resources=read_preview_resources(self.server.repo_root),
            i18n=(i18n_data.get("strings") or {}).get(locale, {}),
            default_font=read_default_font_info(self.server.repo_root),
            font_path=resolve_system_font_file(self.server.repo_root),
            width=int(payload.get("width") or preview_runtime.DEFAULT_WIDTH),
            height=int(payload.get("height") or preview_runtime.DEFAULT_HEIGHT),
            show_hidden=True,
        )
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "image/png")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def write_static(self, request_path: str):
        rel = "index.html" if request_path in ("", "/") else request_path.lstrip("/")
        path = (STATIC_ROOT / rel).resolve()
        if STATIC_ROOT.resolve() not in path.parents and path != STATIC_ROOT.resolve():
            self.write_json({"error": "not found"}, HTTPStatus.NOT_FOUND)
            return
        if not path.exists() or path.is_dir():
            self.write_json({"error": "not found"}, HTTPStatus.NOT_FOUND)
            return
        body = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the jy_app UI designer.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    server = DesignerServer((args.host, args.port), DesignerHandler, args.repo_root.resolve())
    print(f"UI designer: http://{args.host}:{server.server_port}/")
    print(f"Repo root: {server.repo_root}")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
