set(HARFBUZZ_DIR "${PROJECT_ROOT}/thirdparty/harfbuzz")
set(HARFBUZZ_SRC_DIR "${HARFBUZZ_DIR}/src")

set(HARFBUZZ_SOURCES
    "${HARFBUZZ_SRC_DIR}/hb-blob.cc"
    "${HARFBUZZ_SRC_DIR}/hb-buffer.cc"
    "${HARFBUZZ_SRC_DIR}/hb-common.cc"
    "${HARFBUZZ_SRC_DIR}/hb-face.cc"
    "${HARFBUZZ_SRC_DIR}/hb-face-builder.cc"
    "${HARFBUZZ_SRC_DIR}/hb-fallback-shape.cc"
    "${HARFBUZZ_SRC_DIR}/hb-font.cc"
    "${HARFBUZZ_SRC_DIR}/hb-map.cc"
    "${HARFBUZZ_SRC_DIR}/hb-number.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-face.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-font.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-layout.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-map.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-metrics.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shape.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shape-fallback.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shape-normalize.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-arabic.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-default.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-hangul.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-hebrew.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-indic-table.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-indic.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-khmer.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-myanmar.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-syllabic.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-thai.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-use.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-shaper-vowel-constraints.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ot-tag.cc"
    "${HARFBUZZ_SRC_DIR}/hb-set.cc"
    "${HARFBUZZ_SRC_DIR}/hb-shape.cc"
    "${HARFBUZZ_SRC_DIR}/hb-shape-plan.cc"
    "${HARFBUZZ_SRC_DIR}/hb-shaper.cc"
    "${HARFBUZZ_SRC_DIR}/hb-static.cc"
    "${HARFBUZZ_SRC_DIR}/hb-unicode.cc"
    "${HARFBUZZ_SRC_DIR}/hb-ucd.cc"
)

set(HARFBUZZ_INCLUDE_DIRS
    "${HARFBUZZ_SRC_DIR}"
)

set(HARFBUZZ_COMPILE_DEFS
    HB_TINY
    HB_NO_SUBSET
    HB_NO_BUILTIN_BIT_OPS
)
