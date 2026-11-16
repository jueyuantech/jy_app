# HarfBuzz

Vendored from `harfbuzz/harfbuzz` release `14.2.1`.

Source archive:
`https://github.com/harfbuzz/harfbuzz/archive/refs/tags/14.2.1.zip`

Only the upstream `src` directory plus license/readme files are kept.

Local patch:
- `src/hb-ot-face.cc` guards OpenType color table includes with `HB_NO_COLOR`.
  The ARM SDK's NuttX headers conflict with newlib `locale_t` when HarfBuzz's
  SVG color table path includes `<ctype.h>`. Color font APIs are disabled for
  this build, so the guarded includes match the existing `HB_NO_COLOR` table list.
- `src/hb-algs.hh` supports `HB_NO_BUILTIN_BIT_OPS` to force HarfBuzz's pure C
  popcount/clz/ctz fallbacks. ARM GCC otherwise emits libgcc helper symbols such
  as `__popcountdi2` and `__ctzdi2`, which are not exported by the firmware
  symbol table.

AP firmware and the Windows simulator use upstream `src/hb-ucd.cc` Unicode data.
ARM uimain does not link HarfBuzz.

Build warning maintenance:
- Match the Ligature diagnostic argument to its unsigned format.
- Avoid default-only Device switches when hinting/variation support is disabled.
- Convert UPEM to signed int before negating vertical phantom metrics.
- The simulator target uses upstream HarfBuzz 14.2.1 MSVC conversion/CRT warning
  settings, scoped to jy_harfbuzz only; application warnings remain enabled.
