# 东盟文字字库补全（2026-09-14）

正式字库：`romfs/system/font/font.ttf`。原有 35,574 个 Unicode 字符全部保留。
逐字符验证了轮廓、advance、left side bearing 完全相同；UPEM=1000、hhea ascent=928、descent=-244、lineGap=0，以及 OS/2 度量保持原值。
复杂文字正确组合后，最终整词的字形数量和占宽可能与旧的错误排版不同。

## 来源

- 高棉文、老挝文、泰米尔文：Google Fonts 官方 Noto Sans 字体，实例化默认 Regular 轴值、UPEM 1000，保留全部布局特性和字形闭包。已验证这些子集的已有字符轮廓和字宽与旧字库完全一致。对应 OFL 许可证随本目录保存。
- 拉丁/越南文、泰文、缅甸文、阿拉伯/爪夷文：从 `Glasses_Android/app/src/main/assets/font_h6.ttf` 提取完整布局子集。直接换用当前 Google Fonts 版本会改变已有字符尺寸，因此选择与原字库度量一致的现有来源。
- `sources.json` 记录来源与 SHA-256；本目录保留实际合并输入，避免上游字体版本变化影响复现。

## 覆盖和验证

柬埔寨高棉文、泰国泰文、老挝老挝文、缅甸缅甸文、越南越南文，以及印度尼西亚、马来西亚、文莱、菲律宾、新加坡使用的拉丁文字；同时检查泰米尔文和爪夷文。原有中文覆盖保持不变。

使用与 AP 相同的 HarfBuzz 14.2.1 源码及 HB_TINY 配置编译主机库，25 段样例的字形轮廓、cluster、advance 和 offset 均与各自完整来源子集一致，无缺字。样例包括高棉文 `ខ្ញុំ`、老挝文组合音标、缅甸文叠字、越南文预组合/分解形式和泰米尔连字。拉丁文本样例的排版占宽与旧版一致。这不是全部语言语料覆盖测试，仍需实机确认首行附标裁剪情况。

## 复现

必须从下列 SHA-256 的原字库开始，不能在已经补全的字库上重复合并，否则会重复追加字形。使用 `scripts/merge_ttf_subset.py` 的 `merge_ttf_subset`，fallback 按以下顺序传入本目录的七份 TTF；override_ranges 传入这些 TTF 的 getBestCmap() 码点并集（每个码点表示为 `(cp, cp)`）。

1. notosans-preserved-subset.ttf
2. notosanskhmer-regular-subset.ttf
3. notosansthai-preserved-subset.ttf
4. notosanslao-regular-subset.ttf
5. notosansmyanmar-preserved-subset.ttf
6. notosanstamil-regular-subset.ttf
7. notosansarabic-preserved-subset.ttf

合并器保留主字库已有字形和排版表，只转移被补全字符的 cmap 映射，并保留原字库全局度量。补充字库开启布局闭包，不能按 Unicode 字符数直接裁掉无独立编码的组合字形。原字库非目标脚本中已有的异常上下文记录未在本次无关扩改。

## 烧录

重新打包并更新字体所在的 ROMFS；只更新 AP 程序不会更新字体。M55 与 AP 必须读取同一版字体，更新后重启以清除缓存。ARM 和模拟器目标均增加字体文件的链接依赖，使仅更新字体时也执行资源打包/复制。

原文件 SHA-256：`e7a5e84210d71dcba8d8c5d28b3b5e43f3417adee229ff1b65cfdaed3d5b72ce`

新文件 SHA-256：`a2d9db4d98a73ed85f06fae707e6f84c4d908da57962fa376f862770c77e37ce`

大小：12,465,996 → 12934604 字节（增加 468608 字节）。

## 手机字体对照结论

手机配置的 Khmer UI 字体与当前普通 Noto Sans Khmer 对同一个词选择不同的下方组合字形。2026-09-14 用户确认到此停止对齐；保留本目录的普通 Noto Sans Khmer 补全版，不纳入手机 UI 字体试验。
