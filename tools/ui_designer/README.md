# jy_app 界面设计器

本工具是本地网页界面设计器：Python 负责读写工程文件、读取资源和生成预览图，浏览器只负责拖拽、选中和属性编辑。

## 启动

先确认 Pillow 可用：

```bash
python -m pip install pillow
```

```bash
python tools/ui_designer/server.py --port 8765
```

打开：

```text
http://127.0.0.1:8765/
```

默认会列出 `apps/**/*.ui.json` 和 `system/**/*.ui.json`，保存时直接写回选中的 `.ui.json`。

## 资源

资源只读取项目根目录固定文件：

```text
ui.res.json
```

设计器不会扫描各页面自己的 `.res.json`。

图片预览会按 `ui.res.json` 里的 `symbol` 到 `images/<symbol>.c` 里读取 LVGL C 数组；当前支持 indexed、L8、A8 这几类常用格式，找不到或格式不支持时会回退成占位框。

## 文案

文案从项目根目录 `StringPool.csv` 读取，属性面板里的“文本键”会按当前语言显示下拉选项。预览时服务端会用同一份 `StringPool.csv` 把 `text_key` 渲染成实际文字。

## 字体

设计器预览使用项目字体：

```text
lfsd/system/font/font.ttf
```

默认字号、字距和行距读取：

```text
lfsd/system/config.json
```

文本节点没有 `font` 字段时使用默认字体配置；只有在属性面板里把某个字体值改成非默认值时，才会写入对应的 `font.weight`、`font.wordSpace` 或 `font.rowSpace`。

## 预览

点击“预览”后，浏览器会把当前 JSON 发给 Python 服务端，服务端用 `preview_runtime.py` 解释 JSON 并返回 PNG。

这条链路不触发 CMake、不编译 simulator，也不要求新页面先接入业务路由。

当前设计器支持 `container`、`label`、`img`、`button`、`roller`、`paged_text`、`overlay` 的基础编辑和预览；透明度字段兼容整数、`transparent`、`cover` 和 `0%` 到 `100%` 的语义写法。
