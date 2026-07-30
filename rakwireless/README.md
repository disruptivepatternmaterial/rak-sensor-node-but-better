# Vendored RAK4631 board support

**This directory is upstream code. Do not edit it.** Fixes go upstream; local changes get
silently destroyed the next time it is re-synced.

## Why it is here

The RAK4631 has no board definition in PlatformIO. Verified on 2026-07-30:

- absent from all 45 boards in the installed `nordicnrf52` platform (10.5.0 and 10.11.0);
- absent from `platform-nordicnrf52` upstream on both `master` and `develop` (404 for
  `boards/wiscore_rak4631.json` and `boards/wiscore_rak4630.json`);
- absent from `framework-arduinoadafruitnrf52` variants;
- `pio boards rak4631` and `pio boards wiscore` both return nothing.

So these four files are the only thing that makes `board = rak4630` resolve.

## Why vendored instead of installed

RAK's own instructions copy these files into `~/.platformio/platforms/nordicnrf52/boards`
and `~/.platformio/packages/framework-arduinoadafruitnrf52/variants`
([CIT-RAK-PIO-BSP-LEGACY]). That approach has three problems for this project:

| Problem | Consequence |
|---|---|
| Lives outside the repo | A toolchain reinstall silently removes it; the next build fails with a confusing "unknown board" |
| Not reproducible | The build depends on machine state, which breaks the whole point of ADR-0001 (author locally, build on the host) |
| Impossible in CI | GitHub Actions starts from a clean image every run |

The in-project method used here is RAK's own current guidance (`RAK_PATCH_V2`) and is
wired up via `boards_dir`, `build_src_filter`, and `build_flags` in `platformio.ini`
([CIT-RAK-PIO-BSP]).

## Provenance

| Field | Value |
|---|---|
| Upstream | `RAKWireless/WisBlock` |
| Path | `PlatformIO/RAK_PATCH_V2/rakwireless` |
| Pinned commit | `ae4137bbff8f003cfbf77c14eecd22a03cd37bb1` |
| Retrieved | 2026-07-30 |

SHA-256 of each file as retrieved:

| File | SHA-256 |
|---|---|
| `boards/rak4630.json` | `775ce062ea5b421cf9f747e333ea39c52275f7c562cd3f510d3ae5eeeee1cca8` |
| `variants/rak4630/WVariant.h` | `251233211637f6c15e3c41131f5463800d38af1f1edd739a58f9c4dbedb4868b` |
| `variants/rak4630/variant.cpp` | `a26dc9e40f3c4c7ed43b3c26532b01f332cc417f56ef418102ff765e50b0d5d7` |
| `variants/rak4630/variant.h` | `1636ad96efda8aed1a80d6f59f33abf5ef0dff0a932c623766bc2cad17a3b489` |

## What the board definition asserts

Values the firmware and flash tooling depend on, from `boards/rak4630.json`:

| Field | Value |
|---|---|
| MCU | `nrf52840`, Cortex-M4, 64 MHz |
| Flash available to the app | 815,104 bytes |
| RAM | 248,832 bytes |
| SoftDevice | s140 v6.1.1 |
| Upload protocol | `nrfutil`, with 1200 bps touch to enter the bootloader |
| USB VID/PID | `0x239A` / `0x8029`, `0x0029`, `0x002A`, `0x802A` |

The 1200 bps touch matters operationally: the board reboots into its bootloader and
re-enumerates as a **different** USB device mid-flash. `scripts/flash.sh` has to tolerate
the port disappearing and coming back.

## Re-syncing

Re-download at a new pin, update the commit and hashes above, and note it in
`CHANGELOG.md`. Confirm a clean build before committing — a variant change can move pin
numbers, and every pin in `docs/HARDWARE.md` is derived from this file.
