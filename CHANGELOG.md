# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning per [`docs/RELEASE.md`](docs/RELEASE.md).

`1.0.0` is earned, not scheduled — it requires H1–H8 closed in
[`docs/EVIDENCE.md`](docs/EVIDENCE.md), including a ≥24 h bench soak and ≥7 d field shadow.

## [Unreleased]

### Added

- **Stage 0 firmware skeleton — the project compiles for real hardware for the first time.**
  [`platformio.ini`](platformio.ini) plus [`src/main.cpp`](src/main.cpp): LED and USB serial
  only, no radio, no Modbus, no sleep. Builds clean on the build host at RAM 4.9% and flash
  9.4%. Bring-up is staged so each step adds exactly one new failure domain.
- [`rakwireless/`](rakwireless/) — vendored RAK4631 board definition and variant, pinned to
  `RAKWireless/WisBlock@ae4137b` with per-file SHA-256. **The RAK4631 is not in the
  PlatformIO board registry**: it is missing from all 45 boards in the installed
  `nordicnrf52` platform, from upstream `platform-nordicnrf52` on both `master` and
  `develop`, and from the Adafruit nRF52 variants. Without these four files there is no
  buildable target at all. Vendored in-tree rather than copied into `~/.platformio` per
  RAK's older instructions, because that version does not survive a toolchain reinstall
  and cannot work in CI.
- [ADR-0003](docs/decisions/ADR-0003-firmware-framework.md) — Arduino + WisBlock-API-V2
  over RUI3, closing open decision #4.
- Project discipline baseline in [`.cursor/rules/`](.cursor/rules/): agent liveness,
  two-environment topology, citation discipline, change workflow, LoRaWAN compliance,
  power management, and TTN formatter parity.
- [`payload/schema.yaml`](payload/schema.yaml) — firmware-side source of truth for the
  uplink, derived from the live TTN formatter and pinned to `forest-weather-machines@efc0e3c`.
- [`scripts/check_decoder_parity.py`](scripts/check_decoder_parity.py) — release-blocking
  gate that runs on every build, comparing the payload schema against the TTN formatter and
  calling out when the formatter must change. Dependency-free so it runs on both machines.
- [`scripts/check_citations.py`](scripts/check_citations.py) — validates `CITE(...)` markers,
  resolves `[CIT-KEY]` references against the registry, and enforces per-change minimums.
- [`scripts/remote.sh`](scripts/remote.sh), [`build.sh`](scripts/build.sh),
  [`flash.sh`](scripts/flash.sh), [`preflight.sh`](scripts/preflight.sh) — build-host
  workflow with SHA verification and flash confirmation.
- Docs: [`ENVIRONMENTS.md`](docs/ENVIRONMENTS.md), [`CITATIONS.md`](docs/CITATIONS.md),
  [`EVIDENCE.md`](docs/EVIDENCE.md), [`POWER_BUDGET.md`](docs/POWER_BUDGET.md),
  [`RELEASE.md`](docs/RELEASE.md), and [`docs/decisions/`](docs/decisions/).
- GitHub issue templates, a PR template with the citation gate, and CI.

### Fixed

- Sibling-repo links in [`docs/HARDWARE.md`](docs/HARDWARE.md) pointed at
  `../../forest-weather-machines`, which does not resolve — the siblings live in
  `~/Documents/GitHub/`, not beside this repo. Replaced with citation-registry references.
- [`AGENTS.md`](AGENTS.md) implied `rak-4-5-wire` was a RAK4631 reference. It is **M5Stack
  NanoC6 (ESP32-C6)** firmware. It remains authoritative for the RAK9154 Modbus protocol
  and is not a starting point for MCU-side code — there is no RAK4631 firmware to fork.
- [`scripts/flash.sh`](scripts/flash.sh) pinned `--upload-port` to the port detected before
  flashing. The board definition sets `use_1200bps_touch`, so the node reboots into its
  bootloader and re-enumerates under a different port mid-flash. The port is now passed
  only when the operator explicitly supplies one.
- [`scripts/check_citations.py`](scripts/check_citations.py) now skips vendored upstream
  code, whose provenance is a pinned commit and file hashes rather than inline citations.
  Without this, RAK's variant header alone would emit dozens of un-sourced-constant
  warnings and train everyone to ignore the gate.

### Known issues

- **Battery current sign is unresolved.** `docs/FIRMWARE_SPEC.md` §2.2 says negative =
  charging; the live decoder says positive = charging. Tracked in
  [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md); blocks the payload freeze.
- **Humidity type 104 is not interchangeable with 112**, contrary to `FIRMWARE_SPEC.md` §6 —
  104 decodes to an unmapped key and misses every consumer. Schema pins 112.
- **Battery temperature needs ×10 before encoding** — the BMS reports whole °C but payload
  type 103 carries divisor 10, so raw encoding reads 10× low.
- **A node that cannot join never sleeps.** Per the WisBlock-API author, a RAK4631 that
  initializes LoRaWAN but cannot reach a network retries the join indefinitely and never
  enters sleep [CIT-RAK-SLEEP]. This couples H4 (join backoff) directly to the power
  budget: a gateway outage would flatten the pack in days rather than months. Captured in
  rules 40 and 50.
- **`Serial.end()` before sleep is now properly sourced.** `docs/LIBRARIES.md` item 23
  credits the RAK forum thread, which does not actually state it. The requirement is real
  and now cited to Nordic's power documentation plus measured evidence of 1.2 mA / 890 µA
  falling to 10 µA once UART, SPI, and USBD are disabled [CIT-NRF-PERIPH-SLEEP].
- **The RS-485 bus needs two baud rates and has one transceiver.** RK900 is 4800, the
  RAK9154 is 9600, and both would share the single RAK5802. Either switch baud between
  polls or move the battery to the one-wire path. Blocks Stage 3; Stages 1 and 2 are
  unaffected.
- Stage 0 firmware compiles but **has never been run on hardware** — nothing has been
  flashed, and nothing has been measured.
