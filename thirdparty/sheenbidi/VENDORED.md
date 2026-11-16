# SheenBidi

Vendored from `Tehreer/SheenBidi` release `v3.0.0`.

Source:
`https://github.com/Tehreer/SheenBidi/archive/refs/tags/v3.0.0.zip`

Only runtime source, public headers, license, and README files are kept.

Local patch:
- `SB_CONFIG_LVGL_MINIMAL` excludes unused Text/Attribute/ScriptLocator/MirrorLocator APIs and
  GeneralCategory lookup data from the unity build. LVGL only uses the
  Algorithm/Paragraph/Line/Run path for visual run ordering. Codepoint script
  lookup and mirror lookup remain enabled for shaping segmentation and RTL fallback.
