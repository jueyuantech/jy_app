# Contributing to Floatair

Thank you for helping improve Floatair. This repository contains the firmware application layer for JY smart glasses, including protocol handling, system services, shared UI infrastructure, simulator workflows, and firmware delivery tooling.

## Ways to Contribute

- Report bugs with clear reproduction steps, expected behavior, and actual behavior.
- Improve documentation for setup, simulator usage, protocol integration, or firmware packaging.
- Fix issues in application modules, system services, shared widgets, resources, or build scripts.
- Propose focused improvements that fit the existing architecture and product direction.

## Before You Start

For changes that affect behavior, protocol fields, firmware packaging, or cross-platform flows, open an issue or discussion first. This helps align the scope before implementation.

For documentation-only changes, typo fixes, or small clarifications, a pull request is usually enough.

## Development Areas

| Area | Entry |
| --- | --- |
| Protocol reference | `docs/datapath_v3_protocol.md` / `docs/datapath_v3_protocol_cn.md` |
| Board setup | `docs/FloatairBoard.md` / `docs/FloatairBoard_cn.md` |
| Desktop simulator | `docs/FloatairSimulator.md` / `docs/FloatairSimulator_cn.md` |
| Application pages | `apps/<app_name>/` |
| Shared widgets | `common/widgets/` |
| System services | `system/` |
| Build and packaging | `scripts/`, `cmake/`, `bes28/` |

## Pull Request Guidelines

- Keep changes focused on one purpose.
- Follow the existing structure, naming, and UI interaction patterns.
- Reuse shared widgets and common helpers where they already exist.
- Update related documentation when changing protocols, workflows, build behavior, or user-facing tools.
- Include screenshots when changing simulator UI, page UI, docs images, or burn-tool documentation.
- Describe what changed, why it changed, and how it was verified.

## Verification

Choose verification based on the area you changed:

| Change type | Recommended verification |
| --- | --- |
| Documentation only | Review rendered Markdown and run `git diff --check` |
| Simulator or UI changes | Run the simulator flow and capture screenshots when useful |
| Protocol changes | Check protocol docs and related message handling paths |
| Build or packaging changes | Run the relevant platform build or package script |
| Firmware target changes | Verify with the appropriate ARM firmware workflow |

If you cannot run a relevant verification step, note that clearly in the pull request.

## Community

All participation in this repository is covered by the [Code of Conduct](CODE_OF_CONDUCT.md).
