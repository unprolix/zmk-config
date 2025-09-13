# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a personal ZMK firmware configuration workspace for custom keyboard layouts, specifically optimized for the eyelash corne and sofle ergomech keyboards. The workspace uses Nix, direnv, and Just for streamlined local development, providing a completely isolated build environment with west, zephyr-sdk, and all dependencies.

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
- `config/eyelash_corne.keymap` - Main keymap configuration
- `config/west.yml` - Dependencies manifest pinned to ZMK v0.3
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
- The workspace is pinned to ZMK v0.3 for stability
- Custom functionality added through various ZMK modules
- Supports multiple keyboard variants (bureau, lavendre, salon variants)
- Settings reset firmware available for troubleshooting
- Keymap visualization powered by keymap-drawer

## Important Files to Check Before Making Changes
- `config/west.yml` - Check module versions and dependencies
- `build.yaml` - Understand build targets before adding new ones
- `config/eyelash_corne.keymap` - Main keymap logic
- `.envrc` and `flake.nix` - Development environment setup