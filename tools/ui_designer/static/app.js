const CANVAS_W = 540;
const CANVAS_H = 440;
const BUTTON_DEFAULT_RADIUS = 12;
const BUTTON_DEFAULT_BORDER_WIDTH = 1;
const BUTTON_DEFAULT_OPA = 255;
const ROLLER_DEFAULT_RADIUS = 16;
const ROLLER_DEFAULT_BORDER_WIDTH = 2;
const ROLLER_DEFAULT_NORMAL_OPA = 178;
const ROLLER_DEFAULT_SELECTED_OPA = 255;
const ROLLER_DEFAULT_SELECTED_PAD_VER = 2;
const OVERLAY_DEFAULT_MAX_ITEMS = 16;
const OVERLAY_DEFAULT_POINT_SIZE = 6;
const OVERLAY_DEFAULT_POINT_OPA = 255;
const PAGED_TEXT_DEFAULT_BORDER_WIDTH = 2;
const PAGED_TEXT_DEFAULT_RADIUS = 6;
const PAGED_TEXT_DEFAULT_OUTSET = 10;
const PAGED_TEXT_DEFAULT_STEP_PERCENT = 100;

const state = {
  files: [],
  currentPath: "",
  doc: null,
  mode: "",
  locale: "",
  i18n: { default_locale: "", locales: [], strings: {} },
  selectedPath: [],
  resources: {},
  defaultFont: { weight: 28, wordSpace: 0, rowSpace: 0 },
  dirty: false,
  drag: null,
};

const els = {
  fileSelect: document.getElementById("fileSelect"),
  modeSelect: document.getElementById("modeSelect"),
  localeSelect: document.getElementById("localeSelect"),
  reloadBtn: document.getElementById("reloadBtn"),
  saveBtn: document.getElementById("saveBtn"),
  deleteBtn: document.getElementById("deleteBtn"),
  duplicateBtn: document.getElementById("duplicateBtn"),
  moveUpBtn: document.getElementById("moveUpBtn"),
  moveDownBtn: document.getElementById("moveDownBtn"),
  autoPreviewInput: document.getElementById("autoPreviewInput"),
  statusText: document.getElementById("statusText"),
  canvas: document.getElementById("canvas"),
  tree: document.getElementById("tree"),
  props: document.getElementById("props"),
  jsonText: document.getElementById("jsonText"),
  applyJsonBtn: document.getElementById("applyJsonBtn"),
  lvglPreview: document.getElementById("lvglPreview"),
  refreshPreviewBtn: document.getElementById("refreshPreviewBtn"),
};

let previewTimer = 0;

function setStatus(text) {
  els.statusText.textContent = text;
}

async function apiJson(url, options) {
  const resp = await fetch(url, options);
  const data = await resp.json();
  if (!resp.ok) {
    throw new Error(data.error || resp.statusText);
  }
  return data;
}

function activeModeNames() {
  return state.doc && state.doc.modes ? Object.keys(state.doc.modes) : [];
}

function activeRoot() {
  if (!state.doc) return null;
  if (state.doc.modes) {
    const mode = state.mode || activeModeNames()[0];
    return state.doc.modes[mode].root;
  }
  return state.doc.root;
}

function selectedNode() {
  return nodeAtPath(state.selectedPath);
}

function nodeAtPath(path) {
  let node = activeRoot();
  for (const index of path) {
    if (!node || !Array.isArray(node.children)) return null;
    node = node.children[index];
  }
  return node;
}

function parentInfo(path) {
  if (!path.length) return { parent: null, index: -1 };
  const parent = nodeAtPath(path.slice(0, -1));
  return { parent, index: path[path.length - 1] };
}

function nodeLabel(node) {
  return `${node.id || "(无标识)"} : ${widgetName(node.type || "container")}`;
}

function widgetName(type) {
  return {
    container: "容器",
    label: "文本",
    img: "图片",
    button: "按钮",
    roller: "滚轮",
    paged_text: "分页文本",
    overlay: "覆盖层",
  }[type] || type;
}

function localeStrings() {
  return (state.i18n.strings && state.i18n.strings[state.locale]) || {};
}

function labelText(node) {
  if (node.text !== undefined) return node.text;
  if (node.text_key) return localeStrings()[node.text_key] || `{${node.text_key}}`;
  return "文本";
}

function buttonLabelSource(node) {
  const label = node.label && typeof node.label === "object" && !Array.isArray(node.label)
    ? node.label
    : {};
  if (Object.keys(label).length) return label;
  if (node.text !== undefined) return { text: node.text };
  return {};
}

function buttonLabelText(node) {
  const label = buttonLabelSource(node);
  if (label.text !== undefined) return label.text;
  if (label.text_key) return localeStrings()[label.text_key] || `{${label.text_key}}`;
  return "按钮";
}

function editableButtonLabel(node) {
  if (!node.label || typeof node.label !== "object" || Array.isArray(node.label)) {
    node.label = {};
  }
  if (node.text !== undefined && node.label.text === undefined && node.label.text_key === undefined) {
    node.label.text = node.text;
  }
  delete node.text;
  return node.label;
}

function imageName(src) {
  if (!src || !src.startsWith("@image/")) return "";
  return src.replace("@image/", "");
}

function fontValue(node, key) {
  return node.font && node.font[key] !== undefined ? node.font[key] : "";
}

function defaultFontValue(key) {
  return state.defaultFont && state.defaultFont[key] !== undefined ? state.defaultFont[key] : "";
}

function opaToAlpha(value, fallback = 255) {
  if (value === "transparent" || value === "transp") return 0;
  if (value === "cover") return 255;
  if (typeof value === "string" && value.endsWith("%")) {
    const percent = Number(value.slice(0, -1));
    return Number.isFinite(percent) ? Math.max(0, Math.min(255, Math.round(255 * percent / 100))) : fallback;
  }
  if (value === undefined || value === null || value === "") return fallback;
  const next = Number(value);
  return Number.isFinite(next) ? Math.max(0, Math.min(255, next)) : fallback;
}

function opaToCss(value, fallback = 255) {
  return opaToAlpha(value, fallback) / 255;
}

function resolvedFontNumber(node, key, fallback) {
  if (node && node.font && node.font[key] !== undefined) return Number(node.font[key]) || fallback;
  return Number(defaultFontValue(key)) || fallback;
}

function setOptionalFontNumber(node, key, value) {
  const next = Number(value);
  if (value === "") {
    if (node.font) delete node.font[key];
  } else if (next === Number(defaultFontValue(key))) {
    if (node.font) delete node.font[key];
  } else {
    if (!node.font) node.font = {};
    node.font[key] = next || 0;
  }
  if (node.font && Object.keys(node.font).length === 0) delete node.font;
  markDirty();
  renderCanvas();
}

function setOptionalObjectNumber(obj, key, value) {
  if (value === "") {
    delete obj[key];
  } else {
    obj[key] = Number(value) || 0;
  }
  markDirty();
  renderCanvas();
}

function setOptionalOpaValue(obj, key, value) {
  if (value === "") {
    delete obj[key];
  } else if (value === "transparent" || value === "transp" || value === "cover" || String(value).endsWith("%")) {
    obj[key] = value;
  } else {
    obj[key] = Number(value) || 0;
  }
  markDirty();
  renderCanvas();
}

function ensureObject(parent, key) {
  if (!parent[key] || typeof parent[key] !== "object" || Array.isArray(parent[key])) {
    parent[key] = {};
  }
  return parent[key];
}

function paddingValue(node, index) {
  const padding = paddingSource(node);
  if (!Array.isArray(padding)) return "";
  return padding[index] ?? "";
}

function setPaddingValue(node, index, value) {
  const owner = paddingOwner(node);
  const current = Array.isArray(owner.padding) ? owner.padding : [];
  const next = current.slice(0, 4);
  while (next.length < 4) next.push(undefined);
  if (value === "") {
    next[index] = undefined;
  } else {
    next[index] = Number(value) || 0;
  }
  if (next.every((item) => item === undefined)) {
    delete owner.padding;
  } else {
    owner.padding = next.map((item) => item === undefined ? 0 : item);
  }
  markDirty();
  renderCanvas();
}

function paddingSource(node) {
  if (node.padding !== undefined) return node.padding;
  if (node.layout && node.layout.padding !== undefined) return node.layout.padding;
  return undefined;
}

function paddingOwner(node) {
  if (node.padding !== undefined) return node;
  if (hasLayout(node)) return ensureObject(node, "layout");
  return node;
}

function markDirty() {
  state.dirty = true;
  setStatus("已修改");
  syncJsonText();
  schedulePreview();
}

function syncJsonText() {
  els.jsonText.value = state.doc ? JSON.stringify(state.doc, null, 2) : "";
}

function loadModes() {
  const names = activeModeNames();
  els.modeSelect.innerHTML = "";
  if (!names.length) {
    els.modeSelect.disabled = true;
    els.modeSelect.append(new Option("根节点", ""));
    state.mode = "";
    return;
  }
  els.modeSelect.disabled = false;
  for (const name of names) {
    els.modeSelect.append(new Option(name, name));
  }
  if (!state.mode || !names.includes(state.mode)) state.mode = names[0];
  els.modeSelect.value = state.mode;
}

function valueToCss(value, fallback) {
  if (value === undefined || value === null || value === "") return fallback;
  if (value === "content") return "auto";
  if (typeof value === "string" && value.endsWith("%")) return value;
  return `${Number(value) || 0}px`;
}

function hasLayout(node) {
  return node && node.layout && node.layout.type && node.layout.type !== "none";
}

function layoutTypeOf(node) {
  return hasLayout(node) ? node.layout.type : "";
}

function applyBoxStyle(el, node, parentHasLayout, parentLayout) {
  const box = parentHasLayout ? (node.size || node.geometry || {}) : (node.geometry || node.size || {});
  if (parentHasLayout) {
    const layoutItem = node.layout_item || {};
    el.classList.add("inLayout");
    if (layoutItem.fill_x) {
      el.style.alignSelf = "stretch";
      el.style.width = "100%";
    }
    if (layoutItem.grow !== undefined) {
      el.style.flexGrow = `${Number(layoutItem.grow) || 0}`;
      el.style.flexShrink = "1";
      el.style.flexBasis = "0";
      el.style.minWidth = "0";
    } else if (node.type === "label" && parentLayout === "hbox") {
      el.style.flexShrink = "1";
      el.style.minWidth = "0";
    } else {
      el.style.flexShrink = "0";
    }
    if (layoutItem.fill_y) {
      el.style.minHeight = "0";
    }
  } else {
    el.style.left = valueToCss(box.x, "0px");
    el.style.top = valueToCss(box.y, "0px");
    applyObjectAlignStyle(el, node);
  }
  if (!parentHasLayout || !node.layout_item || !node.layout_item.fill_x) {
    el.style.width = valueToCss(box.w, node.type === "label" ? "120px" : "80px");
  }
  if (!parentHasLayout || !node.layout_item || !node.layout_item.fill_y) {
    el.style.height = valueToCss(box.h, node.type === "label" ? "auto" : "40px");
  }
  if (node.max_size) {
    el.style.maxWidth = valueToCss(node.max_size.w, "");
    el.style.maxHeight = valueToCss(node.max_size.h, "");
  }
  if (parentHasLayout && node.height_policy === "content_max_parent") {
    el.style.maxHeight = "100%";
  }
}

function applyObjectAlignStyle(el, node) {
  const align = node.object_align;
  if (!align || typeof align !== "object" || Array.isArray(align)) return;
  const offsetX = valueToCss(align.x, "0px");
  const offsetY = valueToCss(align.y, "0px");
  const type = align.type || "top_left";
  const xMap = {
    top_left: ["0%", "0"],
    left_mid: ["0%", "0"],
    bottom_left: ["0%", "0"],
    top_mid: ["50%", "-50%"],
    center: ["50%", "-50%"],
    bottom_mid: ["50%", "-50%"],
    top_right: ["100%", "-100%"],
    right_mid: ["100%", "-100%"],
    bottom_right: ["100%", "-100%"],
  };
  const yMap = {
    top_left: ["0%", "0"],
    top_mid: ["0%", "0"],
    top_right: ["0%", "0"],
    left_mid: ["50%", "-50%"],
    center: ["50%", "-50%"],
    right_mid: ["50%", "-50%"],
    bottom_left: ["100%", "-100%"],
    bottom_mid: ["100%", "-100%"],
    bottom_right: ["100%", "-100%"],
  };
  const [left, tx] = xMap[type] || xMap.top_left;
  const [top, ty] = yMap[type] || yMap.top_left;
  el.style.left = `calc(${left} + ${offsetX})`;
  el.style.top = `calc(${top} + ${offsetY})`;
  el.style.transform = `translate(${tx}, ${ty})`;
}

function applyCommonStyle(el, node) {
  const nodeType = node.type || "container";
  if (node.radius !== undefined) el.style.borderRadius = `${node.radius}px`;
  if (node.border_width !== undefined) {
    el.style.borderWidth = `${node.border_width}px`;
    el.style.borderStyle = "solid";
  }
  if (node.opa !== undefined) {
    const alpha = opaToCss(node.opa);
    if (nodeType === "container") {
      el.style.backgroundColor = `rgba(20, 20, 20, ${alpha})`;
    } else if (nodeType === "button") {
      el.style.borderColor = `rgba(255, 255, 255, ${alpha})`;
    } else if (nodeType === "label") {
      el.style.color = `rgba(255, 255, 255, ${alpha})`;
      el.style.borderColor = `rgba(255, 255, 255, ${alpha})`;
    } else {
      el.style.opacity = alpha;
    }
  }
  if (node.pad_hor !== undefined) {
    el.style.paddingLeft = `${node.pad_hor}px`;
    el.style.paddingRight = `${node.pad_hor}px`;
  }
  if (node.pad_ver !== undefined) {
    el.style.paddingTop = `${node.pad_ver}px`;
    el.style.paddingBottom = `${node.pad_ver}px`;
  }
  const rawPadding = paddingSource(node);
  if (rawPadding) {
    const padding = Array.isArray(rawPadding)
      ? { left: rawPadding[0], right: rawPadding[1], top: rawPadding[2], bottom: rawPadding[3] }
      : rawPadding;
    if (padding.left !== undefined) el.style.paddingLeft = `${padding.left}px`;
    if (padding.right !== undefined) el.style.paddingRight = `${padding.right}px`;
    if (padding.top !== undefined) el.style.paddingTop = `${padding.top}px`;
    if (padding.bottom !== undefined) el.style.paddingBottom = `${padding.bottom}px`;
  }
  if (node.scroll && node.scroll.enabled) {
    el.style.overflow = "auto";
    el.classList.add("scrollable");
  }
  if (nodeType === "label") {
    el.style.backgroundColor = "#000";
    el.style.color = "#fff";
    el.style.borderColor = "#fff";
    const size = resolvedFontNumber(node, "weight", 28);
    const rowSpace = resolvedFontNumber(node, "rowSpace", 0);
    el.style.fontSize = `${size}px`;
    el.style.letterSpacing = `${resolvedFontNumber(node, "wordSpace", 0)}px`;
    el.style.lineHeight = `${size + rowSpace + 4}px`;
    applyTextOverflowStyle(el, node);
    if (Number.isInteger(node.max_lines) && node.max_lines > 0) {
      el.style.display = "-webkit-box";
      el.style.webkitLineClamp = `${node.max_lines}`;
      el.style.webkitBoxOrient = "vertical";
      el.style.overflow = "hidden";
    }
  }
}

function applyLabelStyle(el, cfg) {
  const size = resolvedFontNumber(cfg, "weight", 28);
  const rowSpace = resolvedFontNumber(cfg, "rowSpace", 0);
  el.style.color = "#fff";
  el.style.borderColor = "#fff";
  el.style.fontSize = `${size}px`;
  el.style.letterSpacing = `${resolvedFontNumber(cfg, "wordSpace", 0)}px`;
  el.style.lineHeight = `${size + rowSpace + 4}px`;
  el.style.textAlign = cfg.align || "center";
  applyTextOverflowStyle(el, cfg);
  if (cfg.pad_hor !== undefined) {
    el.style.paddingLeft = `${cfg.pad_hor}px`;
    el.style.paddingRight = `${cfg.pad_hor}px`;
  }
  if (cfg.pad_ver !== undefined) {
    el.style.paddingTop = `${cfg.pad_ver}px`;
    el.style.paddingBottom = `${cfg.pad_ver}px`;
  }
  if (cfg.opa !== undefined) {
    const alpha = opaToCss(cfg.opa);
    el.style.color = `rgba(255, 255, 255, ${alpha})`;
    el.style.borderColor = `rgba(255, 255, 255, ${alpha})`;
  }
  if (Number.isInteger(cfg.max_lines) && cfg.max_lines > 0) {
    el.style.display = "-webkit-box";
    el.style.webkitLineClamp = `${cfg.max_lines}`;
    el.style.webkitBoxOrient = "vertical";
    el.style.overflow = "hidden";
  }
}

function applyTextOverflowStyle(el, cfg) {
  const overflow = cfg.overflow || "clip";
  el.style.overflow = "hidden";
  el.classList.remove("overflow-wrap", "overflow-clip", "overflow-scroll", "overflow-circular", "canScroll");
  if (overflow === "wrap") {
    el.classList.add("overflow-wrap");
    el.style.whiteSpace = "pre-wrap";
    el.style.wordBreak = "break-word";
    el.style.overflowWrap = "anywhere";
  } else {
    el.classList.add(overflow === "scroll" ? "overflow-scroll" : overflow === "scroll_circular" ? "overflow-circular" : "overflow-clip");
    el.style.whiteSpace = "pre";
    el.style.wordBreak = "normal";
    el.style.overflowWrap = "normal";
  }
}

function createTextContent(text, cfg) {
  const content = document.createElement("span");
  content.className = "labelContent";
  const overflow = cfg.overflow || "clip";
  if (overflow === "scroll_circular") {
    const first = document.createElement("span");
    first.className = "scrollCopy";
    first.textContent = text || "\u00a0";
    const second = document.createElement("span");
    second.className = "scrollCopy";
    second.textContent = text || "\u00a0";
    content.append(first, second);
  } else {
    content.textContent = text || "\u00a0";
  }
  return content;
}

function applyLayoutStyle(el, node) {
  if (!hasLayout(node)) return;
  el.classList.add("layout");
  el.style.display = "flex";
  el.style.flexDirection = node.layout.type === "hbox" ? "row" : "column";
  el.style.gap = `${node.layout.spacing || 0}px`;
  const cross = node.layout.cross_align || "start";
  const main = node.layout.main_align || "start";
  el.style.alignItems = { start: "flex-start", center: "center", end: "flex-end" }[cross] || "flex-start";
  el.style.justifyContent = {
    start: "flex-start",
    center: "center",
    end: "flex-end",
    space_between: "space-between",
    space_around: "space-around",
    space_evenly: "space-evenly",
  }[main] || "flex-start";
}

function renderNode(node, path, parentHasLayout, parentLayout = "") {
  const el = document.createElement("div");
  el.className = `node ${node.type || "container"}`;
  el.dataset.path = path.join(".");
  if (samePath(path, state.selectedPath)) el.classList.add("selected");
  applyBoxStyle(el, node, parentHasLayout, parentLayout);
  applyCommonStyle(el, node);
  applyLayoutStyle(el, node);
  if (node.visible === false) {
    el.classList.add("hiddenNode");
    el.style.opacity = "0.35";
  }

  if ((node.type || "container") === "label") {
    el.style.textAlign = node.align || "center";
    el.append(createTextContent(labelText(node), node));
  } else if (node.type === "img") {
    const name = imageName(node.src);
    if (name) {
      el.textContent = "";
      const imageLayer = document.createElement("div");
      imageLayer.className = "imageContent";
      imageLayer.style.backgroundImage = `url(/api/resource/image?name=${encodeURIComponent(name)})`;
      const zoom = node.zoom !== undefined ? (Number(node.zoom) || 256) / 256 : 1;
      const rotation = node.rotation !== undefined ? (Number(node.rotation) || 0) / 10 : 0;
      const offsetX = Number(node.offset_x) || 0;
      const offsetY = Number(node.offset_y) || 0;
      imageLayer.style.transform = `translate(${offsetX}px, ${offsetY}px) scale(${zoom}) rotate(${rotation}deg)`;
      el.append(imageLayer);
    } else {
      el.textContent = "图片";
    }
  } else if (node.type === "button") {
    const labelCfg = { ...buttonLabelSource(node) };
    if (node.opa !== undefined && labelCfg.opa === undefined) {
      labelCfg.opa = node.opa;
    }
    el.style.borderRadius = `${node.radius ?? BUTTON_DEFAULT_RADIUS}px`;
    el.style.borderWidth = `${node.border_width ?? BUTTON_DEFAULT_BORDER_WIDTH}px`;
    el.style.borderColor = `rgba(255, 255, 255, ${opaToCss(node.opa, BUTTON_DEFAULT_OPA)})`;
    const label = document.createElement("span");
    label.className = "buttonLabel";
    applyLabelStyle(label, labelCfg);
    label.append(createTextContent(buttonLabelText(node), labelCfg));
    el.append(label);
  } else if (node.type === "roller") {
    renderRollerContent(el, node);
  } else if (node.type === "paged_text") {
    renderPagedTextContent(el, node);
  } else if (node.type === "overlay") {
    renderOverlayContent(el, node);
  }

  el.addEventListener("click", (event) => {
    event.stopPropagation();
    selectPath(path);
  });
  el.addEventListener("dragover", (event) => {
    if ((node.type || "container") === "container") event.preventDefault();
  });
  el.addEventListener("drop", (event) => {
    event.preventDefault();
    event.stopPropagation();
    const type = event.dataTransfer.getData("application/x-widget");
    if (type) addNode(type, path, event);
  });
  el.addEventListener("mousedown", (event) => beginMove(event, path));

  const children = node.children || [];
  const childLayout = layoutTypeOf(node);
  for (let i = 0; i < children.length; i++) {
    el.append(renderNode(children[i], path.concat(i), hasLayout(node), childLayout));
  }
  return el;
}

function rollerItems(node) {
  return Array.isArray(node.items) && node.items.length ? node.items.map(String) : ["Item 1", "Item 2", "Item 3"];
}

function rollerSelectedIndex(node, items) {
  const fallback = Math.floor(items.length / 2);
  const rawSelected = node.selected_index === undefined ? fallback : Number(node.selected_index);
  return Math.max(0, Math.min(items.length - 1, Number.isFinite(rawSelected) ? rawSelected : fallback));
}

function rollerVisibleRows(node) {
  const items = rollerItems(node);
  const selected = rollerSelectedIndex(node, items);
  if (items.length === 1) return [{ text: items[0], selected: true }];
  if (items.length === 2) {
    return [
      { text: items[0], selected: selected === 0 },
      { text: items[1], selected: selected === 1 },
    ];
  }
  return [
    { text: items[(selected - 1 + items.length) % items.length], selected: false },
    { text: items[selected], selected: true },
    { text: items[(selected + 1) % items.length], selected: false },
  ];
}

function renderRollerContent(el, node) {
  const rows = rollerVisibleRows(node);
  const labelCfg = node.label && typeof node.label === "object" && !Array.isArray(node.label) ? node.label : {};
  const { rowHeight, rowGap } = rollerRowMetrics(node, labelCfg);
  el.style.display = "flex";
  el.style.flexDirection = "column";
  el.style.justifyContent = "center";
  el.style.gap = `${rowGap}px`;
  rows.forEach(({ text, selected }) => {
    const row = document.createElement("div");
    row.className = "rollerRow";
    if (selected) row.classList.add("selected");
    row.style.height = `${rowHeight}px`;
    row.style.borderRadius = `${node.radius ?? ROLLER_DEFAULT_RADIUS}px`;
    row.style.borderWidth = `${node.border_width ?? ROLLER_DEFAULT_BORDER_WIDTH}px`;
    row.style.borderColor = `rgba(255, 255, 255, ${opaToCss(selected ? node.opa_selected : node.opa_normal, selected ? ROLLER_DEFAULT_SELECTED_OPA : ROLLER_DEFAULT_NORMAL_OPA)})`;
    const cfg = {
      ...labelCfg,
      text,
      align: labelCfg.align || "center",
      opa: selected
        ? (node.opa_selected ?? ROLLER_DEFAULT_SELECTED_OPA)
        : (node.opa_normal ?? ROLLER_DEFAULT_NORMAL_OPA),
    };
    applyLabelStyle(row, cfg);
    row.append(createTextContent(text, cfg));
    el.append(row);
  });
}

function rollerRowMetrics(node, labelCfg) {
  const size = resolvedFontNumber(labelCfg, "weight", 28);
  const rowSpace = resolvedFontNumber(labelCfg, "rowSpace", 0);
  const lineHeight = size + rowSpace + 4;
  const selectedPad = node.selected_pad_ver === undefined ? ROLLER_DEFAULT_SELECTED_PAD_VER : Number(node.selected_pad_ver) || 0;
  const explicitRowHeight = Number(node.row_height) || 0;
  const rowHeight = explicitRowHeight > 0 ? explicitRowHeight : lineHeight + Math.max(0, selectedPad) * 2;
  const explicitGap = node.row_gap === undefined ? -1 : Number(node.row_gap);
  const rowGap = explicitGap >= 0 ? explicitGap : Math.floor(lineHeight / 3);
  return { rowHeight, rowGap };
}

function renderPagedTextContent(el, node) {
  const labelCfg = node.label && typeof node.label === "object" && !Array.isArray(node.label)
    ? { ...node.label }
    : {};
  if (labelCfg.text === undefined && labelCfg.text_key === undefined) {
    labelCfg.text = node.preview_text || "";
  }
  labelCfg.align = labelCfg.align || "left";
  labelCfg.overflow = labelCfg.overflow || "wrap";
  const inset = node.highlight && node.highlight.outset !== undefined ? Number(node.highlight.outset) || 0 : (node.highlight ? PAGED_TEXT_DEFAULT_OUTSET : 0);
  const label = document.createElement("div");
  label.className = "pagedTextLabel";
  label.style.left = `${inset}px`;
  label.style.top = `${inset}px`;
  label.style.width = `calc(100% - ${inset * 2}px)`;
  label.style.height = `calc(100% - ${inset * 2}px)`;
  applyLabelStyle(label, labelCfg);
  label.append(createTextContent(labelText(labelCfg), labelCfg));
  el.append(label);
  if (node.highlight && typeof node.highlight === "object") {
    const frame = document.createElement("div");
    frame.className = "pagedTextHighlight";
    frame.style.inset = "0px";
    frame.style.borderWidth = `${node.highlight.border_width ?? PAGED_TEXT_DEFAULT_BORDER_WIDTH}px`;
    frame.style.borderRadius = `${node.highlight.radius ?? PAGED_TEXT_DEFAULT_RADIUS}px`;
    el.append(frame);
  }
}

function renderOverlayContent(el, node) {
  const maxItems = Math.max(1, node.max_items === undefined ? OVERLAY_DEFAULT_MAX_ITEMS : Number(node.max_items) || 1);
  const point = node.point && typeof node.point === "object" && !Array.isArray(node.point) ? node.point : {};
  const size = point.size === undefined ? OVERLAY_DEFAULT_POINT_SIZE : Number(point.size) || OVERLAY_DEFAULT_POINT_SIZE;
  const alpha = opaToCss(point.opa, OVERLAY_DEFAULT_POINT_OPA);
  const points = document.createElement("div");
  points.className = "overlayPoints";
  for (let i = 0; i < maxItems; i++) {
    const dot = document.createElement("span");
    dot.style.width = `${size}px`;
    dot.style.height = `${size}px`;
    dot.style.background = `rgba(255, 255, 255, ${alpha})`;
    points.append(dot);
  }
  el.append(points);
  if (node.text && typeof node.text === "object" && !Array.isArray(node.text)) {
    const label = document.createElement("div");
    label.className = "overlayText";
    applyLabelStyle(label, node.text);
    label.append(createTextContent(labelText(node.text), node.text));
    el.append(label);
  }
}

function renderCanvas() {
  els.canvas.innerHTML = "";
  const root = activeRoot();
  if (!root) return;
  els.canvas.append(renderNode(root, [], false));
  syncScrollableNodes();
  syncLabelOverflowNodes();
}

function syncScrollableNodes() {
  requestAnimationFrame(() => {
    const canvasRect = els.canvas.getBoundingClientRect();
    const nodes = Array.from(els.canvas.querySelectorAll(".node.scrollable"));
    nodes.forEach((node) => {
      const rect = node.getBoundingClientRect();
      const available = Math.max(1, canvasRect.bottom - rect.top - 1);
      node.style.maxHeight = `${available}px`;
      if (node.scrollHeight > available) {
        node.style.height = `${available}px`;
      }
    });
    requestAnimationFrame(() => {
      nodes.forEach((node) => {
        node.scrollTop = Math.max(0, node.scrollHeight - node.clientHeight);
      });
    });
  });
}

function syncLabelOverflowNodes() {
  requestAnimationFrame(() => {
    const nodes = Array.from(els.canvas.querySelectorAll(".overflow-scroll, .overflow-circular"));
    nodes.forEach((node) => {
      const content = node.querySelector(".labelContent");
      if (!content) return;
      const firstCopy = content.querySelector(".scrollCopy");
      const contentWidth = firstCopy ? firstCopy.scrollWidth : content.scrollWidth;
      const distance = Math.max(0, contentWidth - node.clientWidth);
      if (distance > 1) {
        node.classList.add("canScroll");
        node.style.setProperty("--scroll-distance", `${distance}px`);
      } else {
        node.classList.remove("canScroll");
        node.style.removeProperty("--scroll-distance");
      }
    });
  });
}

function renderTree() {
  els.tree.innerHTML = "";
  const root = activeRoot();
  if (!root) return;
  function walk(node, path, depth) {
    const item = document.createElement("div");
    item.className = "treeItem";
    if (samePath(path, state.selectedPath)) item.classList.add("selected");
    item.style.paddingLeft = `${4 + depth * 14}px`;
    item.textContent = nodeLabel(node);
    item.onclick = () => selectPath(path);
    els.tree.append(item);
    (node.children || []).forEach((child, index) => walk(child, path.concat(index), depth + 1));
  }
  walk(root, [], 0);
}

function samePath(a, b) {
  return a.length === b.length && a.every((value, index) => value === b[index]);
}

function selectPath(path) {
  state.selectedPath = path.slice();
  renderAll();
}

function renderAll() {
  renderCanvas();
  renderTree();
  renderProps();
  updateNodeActions();
  syncJsonText();
}

function uniqueId(type) {
  const root = activeRoot();
  const used = new Set();
  function walk(node) {
    if (node.id) used.add(node.id);
    (node.children || []).forEach(walk);
  }
  if (root) walk(root);
  for (let i = 1; ; i++) {
    const id = `${type}_${i}`;
    if (!used.has(id)) return id;
  }
}

function firstImageRef() {
  const names = Object.keys((state.resources && state.resources.images) || {});
  return names.length ? `@image/${names[0]}` : "";
}

function createNode(type, x, y, parent) {
  const parentIsLayout = hasLayout(parent);
  const boxKey = parentIsLayout ? "size" : "geometry";
  const node = { type, id: uniqueId(type) };
  node[boxKey] = type === "label"
    ? { x, y, w: 180, h: "content" }
    : { x, y, w: defaultNodeWidth(type), h: defaultNodeHeight(type) };
  if (parentIsLayout) {
    delete node[boxKey].x;
    delete node[boxKey].y;
  }
  if (type === "container") {
    node.opa = 0;
    node.children = [];
  } else if (type === "label") {
    node.text = "文本";
  } else if (type === "img") {
    node.src = firstImageRef();
  } else if (type === "button") {
    node.text = "按钮";
  } else if (type === "roller") {
    node.items = ["Alpha.txt", "Beta.txt", "Gamma.txt"];
    node.label = { size: { w: "100%", h: "content" }, align: "center", overflow: "clip" };
  } else if (type === "paged_text") {
    node.label = { text: "", align: "left", overflow: "wrap" };
  } else if (type === "overlay") {
    node.point = { size: OVERLAY_DEFAULT_POINT_SIZE, opa: "cover" };
  }
  return node;
}

function defaultNodeWidth(type) {
  if (type === "container") return 160;
  if (type === "roller" || type === "paged_text" || type === "overlay") return 240;
  if (type === "button") return 120;
  return 96;
}

function defaultNodeHeight(type) {
  if (type === "container") return 90;
  if (type === "roller") return "content";
  if (type === "paged_text") return 160;
  if (type === "overlay") return 80;
  if (type === "button") return 48;
  return 40;
}

function insertionPath() {
  const node = selectedNode();
  if (node && (node.type || "container") === "container") {
    return state.selectedPath.slice();
  }
  if (state.selectedPath.length) {
    return state.selectedPath.slice(0, -1);
  }
  return [];
}

function addNodeAt(type, parentPath, x, y) {
  const parent = nodeAtPath(parentPath) || activeRoot();
  if (!parent.children) parent.children = [];
  parent.children.push(createNode(type, x, y, parent));
  selectPath(parentPath.concat(parent.children.length - 1));
  markDirty();
}

function addNode(type, parentPath, event) {
  const rect = els.canvas.getBoundingClientRect();
  const x = Math.max(0, Math.round(event.clientX - rect.left));
  const y = Math.max(0, Math.round(event.clientY - rect.top));
  addNodeAt(type, parentPath, x, y);
}

function deleteSelectedNode() {
  if (!state.selectedPath.length) return;
  const info = parentInfo(state.selectedPath);
  if (!info.parent || !Array.isArray(info.parent.children)) return;
  info.parent.children.splice(info.index, 1);
  selectPath(state.selectedPath.slice(0, -1));
  markDirty();
}

function cloneNodeWithFreshIds(node) {
  const clone = JSON.parse(JSON.stringify(node));
  const assigned = new Set();
  function walk(current) {
    if (current.id) current.id = freshCloneId(current.type || "node", assigned);
    (current.children || []).forEach(walk);
  }
  walk(clone);
  return clone;
}

function freshCloneId(type, assigned) {
  for (let i = 1; ; i++) {
    const id = `${type}_${i}`;
    if (!assigned.has(id) && !idExists(id)) {
      assigned.add(id);
      return id;
    }
  }
}

function idExists(id) {
  let found = false;
  function walk(node) {
    if (!node || found) return;
    if (node.id === id) {
      found = true;
      return;
    }
    (node.children || []).forEach(walk);
  }
  walk(activeRoot());
  return found;
}

function duplicateSelectedNode() {
  if (!state.selectedPath.length) return;
  const info = parentInfo(state.selectedPath);
  const node = selectedNode();
  if (!info.parent || !Array.isArray(info.parent.children) || !node) return;
  info.parent.children.splice(info.index + 1, 0, cloneNodeWithFreshIds(node));
  selectPath(state.selectedPath.slice(0, -1).concat(info.index + 1));
  markDirty();
}

function moveSelectedNode(delta) {
  if (!state.selectedPath.length) return;
  const info = parentInfo(state.selectedPath);
  if (!info.parent || !Array.isArray(info.parent.children)) return;
  const next = info.index + delta;
  if (next < 0 || next >= info.parent.children.length) return;
  const items = info.parent.children;
  const tmp = items[info.index];
  items[info.index] = items[next];
  items[next] = tmp;
  selectPath(state.selectedPath.slice(0, -1).concat(next));
  markDirty();
}

function updateNodeActions() {
  const isRoot = state.selectedPath.length === 0;
  const info = parentInfo(state.selectedPath);
  const count = info.parent && Array.isArray(info.parent.children) ? info.parent.children.length : 0;
  els.deleteBtn.disabled = isRoot;
  els.duplicateBtn.disabled = isRoot;
  els.moveUpBtn.disabled = isRoot || info.index <= 0;
  els.moveDownBtn.disabled = isRoot || info.index < 0 || info.index >= count - 1;
}

function beginMove(event, path) {
  if (event.button !== 0 || samePath(path, [])) return;
  if (!samePath(path, state.selectedPath)) return;
  const info = parentInfo(path);
  if (!info.parent || hasLayout(info.parent)) return;
  const node = nodeAtPath(path);
  if (!node.geometry) return;
  event.stopPropagation();
  state.drag = {
    path: path.slice(),
    startX: event.clientX,
    startY: event.clientY,
    x: Number(node.geometry.x) || 0,
    y: Number(node.geometry.y) || 0,
  };
}

window.addEventListener("mousemove", (event) => {
  if (!state.drag) return;
  const node = nodeAtPath(state.drag.path);
  if (!node || !node.geometry) return;
  node.geometry.x = Math.max(0, Math.min(CANVAS_W, Math.round(state.drag.x + event.clientX - state.drag.startX)));
  node.geometry.y = Math.max(0, Math.min(CANVAS_H, Math.round(state.drag.y + event.clientY - state.drag.startY)));
  renderCanvas();
  syncJsonText();
});

window.addEventListener("mouseup", () => {
  if (state.drag) {
    state.drag = null;
    markDirty();
  }
});

function propInput(label, value, onChange, type = "text") {
  const l = document.createElement("label");
  l.textContent = label;
  const input = document.createElement("input");
  input.type = type;
  input.value = value ?? "";
  input.oninput = () => onChange(input.value);
  input.onblur = () => renderAll();
  return [l, input];
}

function propTextarea(label, value, onChange) {
  const l = document.createElement("label");
  l.textContent = label;
  const input = document.createElement("textarea");
  input.value = value ?? "";
  input.oninput = () => onChange(input.value);
  input.onblur = () => renderAll();
  return [l, input];
}

function propSelect(label, value, options, onChange) {
  const l = document.createElement("label");
  l.textContent = label;
  const select = document.createElement("select");
  for (const item of options) select.append(new Option(item.label, item.value));
  select.value = value ?? "";
  select.onchange = () => onChange(select.value);
  return [l, select];
}

function makeGroup(title, controls) {
  const group = document.createElement("div");
  group.className = "propGroup";
  const h = document.createElement("div");
  h.className = "propGroupTitle";
  h.textContent = title;
  const grid = document.createElement("div");
  grid.className = "propGrid";
  controls.forEach(([label, input]) => {
    grid.append(label, input);
  });
  group.append(h, grid);
  return group;
}

function setNumber(obj, key, value) {
  if (value === "") {
    delete obj[key];
  } else if (String(value).endsWith("%") || value === "content") {
    obj[key] = value;
  } else {
    obj[key] = Number(value) || 0;
  }
  markDirty();
  renderCanvas();
}

function boolOptions() {
  return [
    { label: "关闭", value: "false" },
    { label: "开启", value: "true" },
  ];
}

function labelAlignOptions() {
  return [
    { label: "左对齐", value: "left" },
    { label: "居中", value: "center" },
    { label: "右对齐", value: "right" },
  ];
}

function labelOverflowOptions() {
  return [
    { label: "裁剪", value: "clip" },
    { label: "换行", value: "wrap" },
    { label: "滚动", value: "scroll" },
    { label: "循环滚动", value: "scroll_circular" },
  ];
}

function styleControlsFor(node, keys) {
  const labels = {
    radius: "圆角",
    border_width: "边框",
    pad_hor: "左右内边距",
    pad_ver: "上下内边距",
    opa: "透明度",
  };
  const controls = [];
  keys.forEach((key) => {
    if (key === "opa") {
      controls.push(...propInput(labels[key], node[key], (v) => setOptionalOpaValue(node, key, v)));
    } else {
      controls.push(...propInput(labels[key], node[key], (v) => setOptionalObjectNumber(node, key, v), "number"));
    }
  });
  return controls;
}

function appendStyleGroup(node) {
  const type = node.type || "container";
  const keys = type === "img" ? ["opa"] : ["radius", "border_width", "pad_hor", "pad_ver", "opa"];
  els.props.append(makeGroup("外观", pairs(styleControlsFor(node, keys))));
}

function appendMaxSizeGroup(node) {
  if ((node.type || "container") !== "container") return;
  const maxSize = node.max_size || {};
  const controls = [
    ...propInput("最大宽", maxSize.w, (v) => { const cfg = ensureObject(node, "max_size"); setNumber(cfg, "w", v); }),
    ...propInput("最大高", maxSize.h, (v) => { const cfg = ensureObject(node, "max_size"); setNumber(cfg, "h", v); }),
  ];
  els.props.append(makeGroup("最大尺寸", pairs(controls)));
}

function appendPaddingGroup(node) {
  if ((node.type || "container") !== "container") return;
  const controls = [
    ...propInput("左", paddingValue(node, 0), (v) => setPaddingValue(node, 0, v), "number"),
    ...propInput("右", paddingValue(node, 1), (v) => setPaddingValue(node, 1, v), "number"),
    ...propInput("上", paddingValue(node, 2), (v) => setPaddingValue(node, 2, v), "number"),
    ...propInput("下", paddingValue(node, 3), (v) => setPaddingValue(node, 3, v), "number"),
  ];
  els.props.append(makeGroup("布局内边距", pairs(controls)));
}

function appendLayoutItemGroup(node) {
  const info = parentInfo(state.selectedPath);
  if (!info.parent || !hasLayout(info.parent)) return;
  const item = node.layout_item || {};
  const controls = [
    ...propSelect("横向填充", item.fill_x ? "true" : "false", boolOptions(), (v) => {
      const cfg = ensureObject(node, "layout_item");
      cfg.fill_x = v === "true";
      markDirty();
      renderAll();
    }),
    ...propSelect("纵向填充", item.fill_y ? "true" : "false", boolOptions(), (v) => {
      const cfg = ensureObject(node, "layout_item");
      cfg.fill_y = v === "true";
      markDirty();
      renderAll();
    }),
    ...propInput("伸展权重", item.grow, (v) => {
      const cfg = ensureObject(node, "layout_item");
      setNumber(cfg, "grow", v);
    }, "number"),
  ];
  els.props.append(makeGroup("父布局项", pairs(controls)));
}

function appendObjectAlignGroup(node) {
  if (!node.geometry) return;
  const align = node.object_align || {};
  const controls = [
    ...propSelect("对齐", align.type || "", objectAlignOptions(), (v) => {
      if (!v) {
        delete node.object_align;
      } else {
        const cfg = ensureObject(node, "object_align");
        cfg.type = v;
      }
      markDirty();
      renderAll();
    }),
    ...propInput("X 偏移", align.x, (v) => {
      const cfg = ensureObject(node, "object_align");
      setNumber(cfg, "x", v);
    }),
    ...propInput("Y 偏移", align.y, (v) => {
      const cfg = ensureObject(node, "object_align");
      setNumber(cfg, "y", v);
    }),
  ];
  els.props.append(makeGroup("对象对齐", pairs(controls)));
}

function appendFontGroup(title, node, getTarget = () => node) {
  const controls = [
    ...propInput(`字号（默认 ${defaultFontValue("weight")}）`, fontValue(node, "weight"), (v) => setOptionalFontNumber(getTarget(), "weight", v), "number"),
    ...propInput(`字距（默认 ${defaultFontValue("wordSpace")}）`, fontValue(node, "wordSpace"), (v) => setOptionalFontNumber(getTarget(), "wordSpace", v), "number"),
    ...propInput(`行距（默认 ${defaultFontValue("rowSpace")}）`, fontValue(node, "rowSpace"), (v) => setOptionalFontNumber(getTarget(), "rowSpace", v), "number"),
  ];
  els.props.append(makeGroup(title, pairs(controls)));
}

function appendLabelConfigGroups(title, node, getTarget) {
  const controls = [
    ...propInput("文本", node.text, (v) => {
      const target = getTarget();
      target.text = v;
      if (v) delete target.text_key;
      markDirty();
      renderCanvas();
    }),
    ...propSelect("文本键", node.text_key || "", i18nOptions(), (v) => {
      const target = getTarget();
      if (v) {
        target.text_key = v;
        delete target.text;
      } else {
        delete target.text_key;
      }
      markDirty();
      renderAll();
    }),
    ...propSelect("对齐", node.align || "center", labelAlignOptions(), (v) => { getTarget().align = v; markDirty(); renderAll(); }),
    ...propSelect("溢出", node.overflow || "clip", labelOverflowOptions(), (v) => { getTarget().overflow = v; markDirty(); renderAll(); }),
    ...propInput("最大行数", node.max_lines, (v) => setNumber(getTarget(), "max_lines", v), "number"),
  ];
  els.props.append(makeGroup(title, pairs(controls)));
  appendFontGroup(`${title}字体`, node, getTarget);
}

function ensureNestedObject(node, key) {
  return ensureObject(node, key);
}

function appendRollerGroups(node) {
  const controls = [
    ...propTextarea("选项", rollerItems(node).join("\n"), (v) => {
      node.items = v.split(/\r?\n/).map((item) => item.trim()).filter(Boolean);
      markDirty();
      renderCanvas();
    }),
    ...propInput("选中索引", node.selected_index, (v) => setOptionalObjectNumber(node, "selected_index", v), "number"),
    ...propInput("行高", node.row_height, (v) => setOptionalObjectNumber(node, "row_height", v), "number"),
    ...propInput("行距", node.row_gap, (v) => setOptionalObjectNumber(node, "row_gap", v), "number"),
    ...propInput("选中上下留白", node.selected_pad_ver, (v) => setOptionalObjectNumber(node, "selected_pad_ver", v), "number"),
    ...propSelect("溢出", node.overflow_mode || "", [
      { label: "", value: "" },
      { label: "循环滚动", value: "scroll" },
      { label: "扩展高度", value: "expand_height" },
    ], (v) => { if (v) node.overflow_mode = v; else delete node.overflow_mode; markDirty(); renderAll(); }),
    ...propInput("普通透明度", node.opa_normal, (v) => setOptionalOpaValue(node, "opa_normal", v)),
    ...propInput("选中透明度", node.opa_selected, (v) => setOptionalOpaValue(node, "opa_selected", v)),
  ];
  els.props.append(makeGroup("滚轮", pairs(controls)));
  appendLabelConfigGroups("滚轮文字", node.label || {}, () => ensureNestedObject(node, "label"));
  appendFontGroup("选中字体", node.selected_font || {}, () => ensureNestedObject(node, "selected_font"));
}

function appendPagedTextGroups(node) {
  const controls = [
    ...propSelect("翻页策略", node.step_mode || "", [
      { label: "", value: "" },
      { label: "按整页行数", value: "line_page" },
      { label: "按视口百分比", value: "view_percent" },
    ], (v) => { if (v) node.step_mode = v; else delete node.step_mode; markDirty(); renderAll(); }),
    ...propInput("步进百分比", node.step_percent, (v) => setOptionalObjectNumber(node, "step_percent", v), "number"),
  ];
  els.props.append(makeGroup("分页文本", pairs(controls)));
  appendLabelConfigGroups("正文文字", node.label || {}, () => ensureNestedObject(node, "label"));

  const highlight = node.highlight || {};
  const highlightControls = [
    ...propInput("遮罩透明度", highlight.mask_opa, (v) => setOptionalOpaValue(ensureNestedObject(node, "highlight"), "mask_opa", v)),
    ...propInput("边框", highlight.border_width, (v) => setOptionalObjectNumber(ensureNestedObject(node, "highlight"), "border_width", v), "number"),
    ...propInput("圆角", highlight.radius, (v) => setOptionalObjectNumber(ensureNestedObject(node, "highlight"), "radius", v), "number"),
    ...propInput("外扩", highlight.outset, (v) => setOptionalObjectNumber(ensureNestedObject(node, "highlight"), "outset", v), "number"),
  ];
  els.props.append(makeGroup("高亮窗口", pairs(highlightControls)));
}

function appendOverlayGroups(node) {
  const point = node.point || {};
  const controls = [
    ...propInput("最大项数", node.max_items, (v) => setOptionalObjectNumber(node, "max_items", v), "number"),
    ...propInput("点大小", point.size, (v) => setOptionalObjectNumber(ensureNestedObject(node, "point"), "size", v), "number"),
    ...propInput("点透明度", point.opa, (v) => setOptionalOpaValue(ensureNestedObject(node, "point"), "opa", v)),
  ];
  els.props.append(makeGroup("覆盖层", pairs(controls)));
  appendLabelConfigGroups("覆盖文字", node.text || {}, () => ensureNestedObject(node, "text"));
}

function renderProps() {
  els.props.innerHTML = "";
  const node = selectedNode();
  if (!node) return;
  const typeLabel = document.createElement("label");
  typeLabel.textContent = "类型";
  const typeValue = document.createElement("div");
  typeValue.className = "readonlyProp";
  typeValue.textContent = widgetName(node.type || "container");
  const common = [
    ...propInput("标识", node.id, (v) => { node.id = v; markDirty(); renderTree(); }),
    ...propSelect("初始显示", node.visible === false ? "false" : "true", boolOptions(), (v) => {
      if (v === "true") delete node.visible;
      else node.visible = false;
      markDirty();
      renderAll();
    }),
    typeLabel,
    typeValue,
  ];
  els.props.append(makeGroup("节点", pairs(common)));

  const box = node.geometry || node.size || {};
  const boxName = node.geometry ? "位置尺寸" : "尺寸";
  const boxControls = [];
  if (node.geometry) {
    boxControls.push(...propInput("x", box.x, (v) => setNumber(box, "x", v)));
    boxControls.push(...propInput("y", box.y, (v) => setNumber(box, "y", v)));
  }
  boxControls.push(...propInput("w", box.w, (v) => setNumber(box, "w", v)));
  boxControls.push(...propInput("h", box.h, (v) => setNumber(box, "h", v)));
  els.props.append(makeGroup(boxName, pairs(boxControls)));
  appendObjectAlignGroup(node);
  appendLayoutItemGroup(node);
  appendStyleGroup(node);

  if ((node.type || "container") === "container") {
    const layout = node.layout || {};
    const scroll = node.scroll || {};
    const layoutControls = [
      ...propSelect("布局", layout.type || "none", [
        { label: "无布局", value: "none" },
        { label: "纵向布局", value: "vbox" },
        { label: "横向布局", value: "hbox" },
      ], (v) => { if (v === "none") delete node.layout; else { node.layout = layout; node.layout.type = v; } markDirty(); renderAll(); }),
      ...propInput("间距", layout.spacing, (v) => { node.layout = layout; setNumber(node.layout, "spacing", v); }),
      ...propSelect("主轴", layout.main_align || "start", mainAlignOptions(), (v) => { node.layout = layout; node.layout.main_align = v; markDirty(); renderAll(); }),
      ...propSelect("交叉轴", layout.cross_align || "start", crossAlignOptions(), (v) => { node.layout = layout; node.layout.cross_align = v; markDirty(); renderAll(); }),
      ...propSelect("高度策略", node.height_policy || "", [
        { label: "", value: "" },
        { label: "内容高度，最大不超过父容器", value: "content_max_parent" },
      ], (v) => { if (v) node.height_policy = v; else delete node.height_policy; markDirty(); renderAll(); }),
      ...propSelect("滚动", scroll.enabled ? "true" : "false", boolOptions(), (v) => { node.scroll = scroll; node.scroll.enabled = v === "true"; markDirty(); renderAll(); }),
      ...propSelect("方向", scroll.dir || "ver", [
        { label: "纵向", value: "ver" },
        { label: "横向", value: "hor" },
        { label: "全部", value: "all" },
      ], (v) => { node.scroll = scroll; node.scroll.dir = v; markDirty(); renderAll(); }),
      ...propSelect("滚动条", scroll.scrollbar || "", [
        { label: "默认", value: "" },
        { label: "关闭", value: "off" },
        { label: "开启", value: "on" },
        { label: "活动时", value: "active" },
        { label: "自动", value: "auto" },
      ], (v) => { node.scroll = scroll; if (v) node.scroll.scrollbar = v; else delete node.scroll.scrollbar; markDirty(); renderAll(); }),
    ];
    els.props.append(makeGroup("容器", pairs(layoutControls)));
    appendMaxSizeGroup(node);
    appendPaddingGroup(node);
  }

  if (node.type === "label") {
    appendLabelConfigGroups("文本", node, () => node);
  }

  if (node.type === "img") {
    const imageOptions = [{ label: "", value: "" }];
    Object.keys((state.resources && state.resources.images) || {}).forEach((name) => {
      imageOptions.push({ label: `@image/${name}`, value: `@image/${name}` });
    });
    els.props.append(makeGroup("图片", pairs(propSelect("资源", node.src || "", imageOptions, (v) => {
      if (v) node.src = v; else delete node.src;
      markDirty();
      renderAll();
    }))));
    const transformControls = [
      ...propInput("X 偏移", node.offset_x, (v) => setOptionalObjectNumber(node, "offset_x", v), "number"),
      ...propInput("Y 偏移", node.offset_y, (v) => setOptionalObjectNumber(node, "offset_y", v), "number"),
      ...propInput("缩放", node.zoom, (v) => setOptionalObjectNumber(node, "zoom", v), "number"),
      ...propInput("旋转", node.rotation, (v) => setOptionalObjectNumber(node, "rotation", v), "number"),
    ];
    els.props.append(makeGroup("图片变换", pairs(transformControls)));
  }

  if (node.type === "button") {
    appendLabelConfigGroups("按钮文字", buttonLabelSource(node), () => editableButtonLabel(node));
  }

  if (node.type === "roller") {
    appendRollerGroups(node);
  }

  if (node.type === "paged_text") {
    appendPagedTextGroups(node);
  }

  if (node.type === "overlay") {
    appendOverlayGroups(node);
  }
}

function mainAlignOptions() {
  return [
    { label: "起始", value: "start" },
    { label: "居中", value: "center" },
    { label: "末尾", value: "end" },
    { label: "两端分散", value: "space_between" },
    { label: "环绕分散", value: "space_around" },
    { label: "均匀分散", value: "space_evenly" },
  ];
}

function crossAlignOptions() {
  return [
    { label: "起始", value: "start" },
    { label: "居中", value: "center" },
    { label: "末尾", value: "end" },
  ];
}

function objectAlignOptions() {
  return [
    { label: "不使用", value: "" },
    { label: "左上", value: "top_left" },
    { label: "上中", value: "top_mid" },
    { label: "右上", value: "top_right" },
    { label: "左中", value: "left_mid" },
    { label: "居中", value: "center" },
    { label: "右中", value: "right_mid" },
    { label: "左下", value: "bottom_left" },
    { label: "下中", value: "bottom_mid" },
    { label: "右下", value: "bottom_right" },
  ];
}

function i18nOptions() {
  const strings = state.i18n.strings[state.locale] || {};
  const keys = Object.keys(strings).sort();
  const options = [{ label: "不使用", value: "" }];
  keys.forEach((key) => {
    const value = strings[key] || "";
    options.push({ label: `${key} - ${value}`, value: key });
  });
  return options;
}

function pairs(flat) {
  const out = [];
  for (let i = 0; i < flat.length; i += 2) out.push([flat[i], flat[i + 1]]);
  return out;
}

async function loadFiles() {
  const data = await apiJson("/api/files");
  state.files = data.files;
  els.fileSelect.innerHTML = "";
  for (const file of state.files) els.fileSelect.append(new Option(file, file));
  if (!state.currentPath && state.files.length) state.currentPath = state.files[0];
  els.fileSelect.value = state.currentPath;
}

async function loadResources() {
  state.resources = await apiJson("/api/resources");
}

async function loadDefaultFont() {
  state.defaultFont = await apiJson("/api/default-font");
}

async function loadI18n() {
  state.i18n = await apiJson("/api/i18n");
  state.locale = state.i18n.default_locale || "";
  els.localeSelect.innerHTML = "";
  (state.i18n.locales || []).forEach((locale) => {
    els.localeSelect.append(new Option(locale, locale));
  });
  els.localeSelect.value = state.locale;
}

async function loadCurrentFile(options = {}) {
  if (!state.currentPath) return;
  state.doc = await apiJson(`/api/ui?path=${encodeURIComponent(state.currentPath)}`);
  state.selectedPath = [];
  state.dirty = false;
  loadModes();
  renderAll();
  setStatus("已载入");
  if (options.refreshPreview) {
    await refreshPreview();
  }
}

async function saveCurrentFile() {
  if (!state.currentPath || !state.doc) return;
  await apiJson(`/api/ui?path=${encodeURIComponent(state.currentPath)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(state.doc),
  });
  state.dirty = false;
  setStatus("已保存");
}

async function refreshPreview() {
  if (!state.doc) return;
  setStatus("正在生成预览");
  const resp = await fetch("/api/preview/render", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      doc: state.doc,
      mode: state.mode,
      locale: state.locale,
      width: CANVAS_W,
      height: CANVAS_H,
    }),
  });
  if (!resp.ok) {
    const data = await resp.json();
    throw new Error(data.error || resp.statusText);
  }
  const blob = await resp.blob();
  const oldUrl = els.lvglPreview.dataset.url;
  if (oldUrl) URL.revokeObjectURL(oldUrl);
  const url = URL.createObjectURL(blob);
  els.lvglPreview.dataset.url = url;
  els.lvglPreview.src = url;
  setStatus("预览已更新");
}

function schedulePreview() {
  if (!els.autoPreviewInput.checked) return;
  clearTimeout(previewTimer);
  previewTimer = setTimeout(() => {
    refreshPreview().catch((err) => setStatus(err.message));
  }, 350);
}

document.querySelectorAll(".palette button").forEach((button) => {
  button.addEventListener("dragstart", (event) => {
    event.dataTransfer.setData("application/x-widget", button.dataset.widget);
  });
  button.addEventListener("click", () => {
    addNodeAt(button.dataset.widget, insertionPath(), 24, 24);
  });
});

els.canvas.addEventListener("click", () => selectPath([]));
els.canvas.addEventListener("dragover", (event) => event.preventDefault());
els.canvas.addEventListener("drop", (event) => {
  event.preventDefault();
  const type = event.dataTransfer.getData("application/x-widget");
  if (type) addNode(type, [], event);
});

els.fileSelect.onchange = async () => {
  state.currentPath = els.fileSelect.value;
  await loadCurrentFile({ refreshPreview: true });
};
els.modeSelect.onchange = () => {
  state.mode = els.modeSelect.value;
  state.selectedPath = [];
  renderAll();
  schedulePreview();
};
els.localeSelect.onchange = () => {
  state.locale = els.localeSelect.value;
  renderAll();
  schedulePreview();
};
els.reloadBtn.onclick = () => {
  loadCurrentFile({ refreshPreview: true }).catch((err) => setStatus(err.message));
};
els.saveBtn.onclick = saveCurrentFile;
els.deleteBtn.onclick = deleteSelectedNode;
els.duplicateBtn.onclick = duplicateSelectedNode;
els.moveUpBtn.onclick = () => moveSelectedNode(-1);
els.moveDownBtn.onclick = () => moveSelectedNode(1);
els.applyJsonBtn.onclick = () => {
  state.doc = JSON.parse(els.jsonText.value);
  state.selectedPath = [];
  loadModes();
  markDirty();
  renderAll();
};
els.refreshPreviewBtn.onclick = () => {
  refreshPreview().catch((err) => setStatus(err.message));
};

(async function boot() {
  try {
    await loadResources();
    await loadDefaultFont();
    await loadI18n();
    await loadFiles();
    await loadCurrentFile({ refreshPreview: true });
  } catch (err) {
    setStatus(err.message);
  }
})();
