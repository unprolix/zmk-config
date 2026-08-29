# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a personal ZMK firmware configuration workspace for custom keyboard layouts. The workspace uses Nix, direnv, and Just for streamlined local development, providing a completely isolated build environment with west, zephyr-sdk, and all dependencies.

Every keyboard here runs one shared layout, `config/jjb.keymap`, with the same sixteen layers in the same order. The per-board keymaps are thin wrappers that map that layout onto the hardware:

| Keyboard | Board | Notes |
|---|---|---|
| eyelash corne | nice!nano v2 | 42 keys + joystick cluster; nice!view; per-column RGB |
| sofle ergomech | nice!nano v2 | |
| Rolio ("Rolio Three") | rolio3 (vendored) | 48 positions, roller, trackpad, Sharp panel |
| Toucan2 | xiao_ble | 36 keys — the 5-column experiment; uses the layout's `_IN` row macros |
| Corne v4 | rpi_pico | RP2040, **wired only** — no BLE at all; single-wire split link |

## Key Commands

### Building Firmware
- `just build all` - Build firmware for all targets defined in build.yaml
- `just build <target>` - Build firmware for specific target (e.g., `just build zen`)
- `just build all -p` - Pristine build (clean before building)
- `just list` - Show all available build targets
- `just clean` - Clear build cache and artifacts

### Development Environment
- `just init` - Initialize west workspace (west init -l config && west update && west zephyr-export)
- `just update` - Update ZMK dependencies
- `just upgrade-sdk` - Update Zephyr SDK and Python dependencies
- `direnv allow` - Setup isolated development environment (first time only)

### Testing
- `just test <testpath>` - Run tests for specific test case
- Add `--verbose` flag to see test output
- Add `--auto-accept` flag to update test snapshots

### Utilities
- `just draw` - Generate keymap visualization using keymap-drawer
- `just upstream-check` - Check for new upstream commits since last sync
- `just upstream-status` - Show detailed upstream status and available commits

### Automation Scripts
- `scripts/auto-build` - Automated build script
- `scripts/auto-flash` - Automated flashing script
- `scripts/generate_build_info.sh` - Generate build information (automatically called)

## Architecture

### Workspace Structure
```
zmk-workspace/
├── config/          # User configuration files (keymaps, board definitions)
├── modules/         # ZMK modules (external dependencies)
├── zephyr/          # Zephyr RTOS
├── zmk/             # ZMK firmware source
├── build.yaml       # Build target definitions
├── Justfile         # Build automation recipes
└── west.yml         # West manifest for dependencies
```

### Configuration Files
- `config/jjb.keymap` - The shared layout every keyboard builds from
- `config/<keyboard>.keymap` / `.conf` - Per-board wrappers, picked up automatically by ZMK's candidate-name search (shield `rolio_left` -> `config/rolio.keymap`)
- `config/boards/` - Vendored boards and shields (`boards/rolio/rolio3`, `boards/shields/{rolio,toucan,corne_v4,vista508,nice_view_jjb,rgbzone,...}`)
- `config/dts/bindings/` - Out-of-tree devicetree bindings for the C in those shields
- `config/west.yml` - Dependencies manifest; ZMK tracks `main`, Zephyr is pinned to `v4.1.0+zmk-fixes`
- `build.yaml` - Defines all build targets and variants
- Various `.dtsi` files in config/ for modular keymap features

### ZMK Modules Used
- zmk-adaptive-key, zmk-auto-layer, zmk-helpers
- zmk-leader-key, zmk-tri-state, zmk-unicode
- zmk-antecedent-morph, zmk-raw-hid
- Custom eyelash_corne board definitions
- Ergomech sofle hybrid support

### Build System
- Uses west for dependency management
- Just for task automation and build recipes
- Nix + direnv for reproducible development environment
- GitHub Actions for CI/CD with artifact generation

## Development Notes

- All builds are local by default with dynamically-generated build info
- ZMK tracks upstream `main` (it was pinned to v0.3.0 once; `just upstream-check` reports drift)
- Custom functionality added through various ZMK modules
- Supports multiple eyelash corne units (bureau, lavendre, salon, fuligin, xan), each with its own `.conf`
- Behaviour nodes the shared keymap references must be declared in a shield's *shared* `.dtsi`: both halves compile the same keymap, so a node visible to only one of them fails the other's build
- Shield changes need a pristine build (`just build <target> -p`); a stale cmake cache links the old config and still goes green
- Settings reset firmware available for troubleshooting
- Keymap visualization powered by keymap-drawer

## Important Files to Check Before Making Changes
- `config/west.yml` - Check module versions and dependencies
- `build.yaml` - Understand build targets before adding new ones
- `config/jjb.keymap` - The shared layout; a change here hits every keyboard, so rebuild them all
- `.envrc` and `flake.nix` - Development environment setup