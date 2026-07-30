# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning per [`docs/RELEASE.md`](docs/RELEASE.md).

`1.0.0` is earned, not scheduled — it requires H1–H8 closed in
[`docs/EVIDENCE.md`](docs/EVIDENCE.md), including a ≥24 h bench soak and ≥7 d field shadow.

## [Unreleased]

### Added

- **The LoRaWAN session survives a reset** (`src/session.cpp`). Previously any reset — a
  watchdog recovery, a brownout, a battery swap — threw away the session and forced a
  rejoin, which needs a gateway reachable at that moment. A node that reset during an
  outage therefore stayed silent even after the outage ended. It now wakes up already
  joined and sends its next reading. The frame counter is stored with a forward margin
  rather than on every uplink: writing each time would wear a flash page out in about a
  year at hourly reporting, and resuming below an already-used counter has the network
  discard the uplinks while the node reports success.
- **Tests that run without hardware** (`pio test -e native`, 15 cases, passing). The
  payload encoder and the Modbus checksum are pure computation, and both fail silently in
  the field: a field encoded one byte too wide shifts everything after it and the uplink
  decodes into plausible wrong numbers, while a wrong checksum makes a working sensor look
  unplugged. The checksum tests are pinned to published Modbus reference values, so they
  confirm the convention rather than merely being self-consistent.
- **[`docs/FIRST_FLASH.md`](docs/FIRST_FLASH.md)** — bring-up in the order that isolates
  faults, one subsystem per stage, with the specific symptom-to-cause mapping for each.
  `scripts/flash.sh` gained `--env` to make that ordering actually reachable; it previously
  always flashed the full image.
- **The firmware, in modules.** `src/` now holds the actual node rather than a blink test:
  `sensors/rk900` (Modbus over the RAK5802), `sensors/battery` (RAK9154 over the Sensor Hub
  one-wire link), `payload` (encoder pinned to the TTN decoder), `radio` (join, send,
  downlink, capped backoff), `power` (watchdog and sleep), `config` (settings that survive
  a reset), and a `main.cpp` that is just the cycle. Each module owns one failure domain.
- **Staged build environments** — `stage1` (wind sensor only), `stage2` (+ battery),
  `stage3` (+ radio, still awake and printing), `rak4631` (everything, including sleep).
  Same source throughout; each build adds exactly one new way to fail, so a problem on new
  hardware has a short suspect list. All four compile.
- **Battery temperature is decoded.** The sibling bench project only handled IPSO types
  184, 185, and 186 and documented one-wire as having no temperature. It does: type 103 is
  in the stream and their parser was discarding it as an unrecognized record. Ours reads
  all four, so pack temperature is available on the one-wire path after all — which matters
  on a heated pack in winter.
- Notable behaviors, each written against a documented failure: joins are bounded and never
  tight-loop (the way this board's battery disappears during a gateway outage), backoff is
  capped and never gives up, the watchdog pauses while asleep so a short hang-detection
  timeout can coexist with an hour-long interval, the USB peripheral is disabled before
  sleep, and settings are written to flash only when they change.

- [ADR-0004](docs/decisions/ADR-0004-bms-one-wire-path.md) — **the RAK9154 goes on the
  one-wire path, and the RAK5802 is dedicated to the RK900 at a fixed 4800.** Closes open
  decision #1 and removes the shared-bus baud-switching problem entirely rather than
  handling it. Two separate buses mean a hung sensor cannot block the other, which matters
  because the battery reading is the one that says whether the node is about to die.
  Unblocks Stage 3; `docs/HARDWARE.md` wiring updated to match.
- [`docs/reviews/2026-07-30-DOWNLINK-AND-RESILIENCE.md`](docs/reviews/2026-07-30-DOWNLINK-AND-RESILIENCE.md)
  — spec review against multi-day gateway outages. Nine findings; the load-bearing ones are
  that a reboot currently discards the LoRaWAN session (join storm on every watchdog
  reset), the watchdog cannot be stopped once started on the nRF52840 yet the sleep interval
  is downlink-settable, and ADR makes an outage progressively more expensive. Sequenced
  against the bring-up stages; none of it blocks Stages 0–1.
- `CIT-NRF-WDT` and `CIT-RAK9154-SOLAR` in [`docs/CITATIONS.md`](docs/CITATIONS.md).

### Changed

- **`docs/POWER_BUDGET.md` said the system had no solar. It was wrong.** The claim
  conflated the *enclosure* having no panel (Unify 910406) with the *system* having none —
  but the RAK9154 is solar-recharged, with a 10 W panel in the regular variant, an
  integrated 18 V charge controller, and a heater [CIT-RAK9154-SOLAR]. The deployment uses
  the large panel. The page is rewritten around the right question: not "how long until it
  dies" but "can it ride out the longest stretch with a snow-covered panel and still
  recover on its own". Adds the heater as an unbudgeted winter load outside firmware
  control, and defines the low-voltage cutoff as a **recoverability guarantee** — RAK warns
  of malfunction on cold-start from an empty battery, and a node that needs a physical
  visit has failed regardless of why.
- **Cut the proposed downlink command set from seven commands to two** (set interval, and
  a read-only status request). The original proposal optimized for configurability; the
  deployment goal is that nobody ever touches it, which argues for fewer remote levers, not
  more. Command framing is still specified so the option stays open, but reboot, force
  rejoin, sensor enable/disable, and a settable battery cutoff are dropped — the last one
  because a remotely settable cutoff is a remote way to flatten a pack that may not
  self-recover.
- `AGENTS.md` — records the never-touch deployment goal and the two rules that follow from
  it; drops the resolved RS-485 bus blocker.

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
- **The TTN formatter parity gate could never pass in CI.** `forest-weather-machines` is
  private and the default workflow token is scoped to this repository, so the sibling
  checkout always failed — but `actions/checkout` creates its target directory before
  failing, so the workflow's `[ -d ]` test passed on an empty folder and the gate then
  hard-failed on a missing decoder. A gate that always fails gets switched off, which is
  precisely how the payload contract would have broken quietly later. The workflow now
  tests for the decoder file itself, and [`payload/reference/`](payload/reference/) holds
  a byte-identical pinned copy so the field-by-field contract is verifiable anywhere. The
  checker prefers a live checkout, prints `source: live` or `source: pinned`, and warns
  that a pinned run cannot detect upstream drift.

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
