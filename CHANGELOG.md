# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning per [`docs/RELEASE.md`](docs/RELEASE.md).

`1.0.0` is earned, not scheduled — it requires H1–H8 closed in
[`docs/EVIDENCE.md`](docs/EVIDENCE.md), including a ≥24 h bench soak and ≥7 d field shadow.

## [Unreleased]

Firmware version is now `0.3.0` — a new capability, backward compatible with the decoder.

### Added

- **A bench-only 60 s reading cadence, `FEATURE_BENCH_INTERVAL`.** Bring-up needed a reading
  about once a minute; the field floor is 1800 s, so confirming a wiring change took half an
  hour. The flag lowers the interval floor and default to 60 s and is defined only by the
  `stage1` and `stage2` environments — never by `[env:rak4631]`.

  It is deliberately awkward to misuse. `src/config.h` **fails the build** if it is combined
  with `FEATURE_RADIO`, because a 60 s cadence is roughly 1440 uplinks a day against a
  fair-use allowance of about 30 seconds of airtime; rather than guarding that at run time,
  no such image can be produced. It is compile-time only, so no downlink and no stored
  setting can talk a node in the woods into it. And a bench build ignores the stored interval
  outright — every field value sits inside the bench build's widened range, so loading flash
  would have quietly restored a half-hour cadence on a board the operator was standing in
  front of, reading as if the setting had been ignored. The bench value is never written
  back. The boot banner now prints the interval bounds and the bench flag, so a build's
  cadence is identifiable from the console alone.

  The sleep-disabled wait cap, previously a flat 30 s, now rises to the bench interval on a
  bench build; left at 30 s it would have silently overridden the cadence it exists to serve.
  Issue #25.

### Fixed

- **The node now transmits proof of life when both sensors are silent.** It previously skipped
  joining entirely in that case, so a station installed with one bad wire would have sat in the
  woods transmitting nothing — indistinguishable from a dead node, a flat pack, or a dead
  gateway, all of which want different responses from whoever decides to drive out. It was also
  unreachable: Class A only opens a receive window after an uplink, so a node that never
  uplinks can never be commanded back to health. Sends a zero-length uplink on the first silent
  cycle and every 8th after, so an installation mistake is visible before anyone leaves the
  site without a permanently dead sensor spending the airtime budget on saying nothing.
  `Radio::send()` was separately discarding zero-length payloads, which had been dropping that
  uplink with no log line at all.
- **The DevEUI was byte-reversed, so no join could ever succeed.** `SX126x-Arduino` requires the
  keys most-significant-byte-first and reverses them itself when building the join request; the
  registration script had written the opposite convention, which a different and widely-copied
  Arduino LoRaWAN library uses. The consequence is the worst kind: an unrecognised DevEUI is
  neither answered nor logged, so the node transmitted perfectly while the console showed no
  join attempt, no error, and nothing to bisect. The boot banner now prints the DevEUI, AppEUI,
  and sub-band so the comparison against the console is immediate, and
  `src/secrets.example.h` records the failure mode.

### Added

- **Real encoder bytes are checked through the real decoder** (`scripts/check_golden_vectors.py`,
  `tools/`). The existing parity gate compares the encoder to the schema and the schema to
  the decoder, which cannot catch anything the schema does not describe — the decoder
  applies a 230-degree installation offset to wind direction, and no schema field says so.
  This gate compiles the encoder, takes the bytes it would transmit, and runs them through
  the JavaScript that ingest actually runs. It catches a right type on a wrong channel,
  which is the failure ADR-0002 called out as silently missing its consumer. Runs on the
  build host and in CI; skips on the workstation, which has no node.

### Fixed

- **Modbus notices a gap inside a reply** (`src/sensors/modbus.cpp`). Bytes arriving after
  a pause were concatenated onto the previous frame, and the pair was rejected on its
  checksum a full second later, reported as a timeout. The gap is now measured against the
  protocol's own end-of-frame silence, so a truncated reply is rejected immediately and
  retried while the line is still quiet.

### Changed

- **Open items moved to GitHub Issues** and `TODO.md` retired. A checklist in the repo and
  a tracker drift apart, and then neither is worth reading. Code comments cite issue
  numbers now.

## [0.2.1] — 2026-07-30

Supersedes 0.2.0, which was tagged at a commit that does not pass CI. Use this one.

### Fixed

- **The off-target tests build again** (`src/build_features.h`, renamed from
  `src/features.h`). The C library has its own `features.h` and includes it from inside
  nearly every system header. The test build puts `src/` on the include path, so the C
  library found ours and compiled without any of its own configuration — producing hundreds
  of errors inside `stdio.h`, `stdint.h`, and `cmath`, not one of which named the file
  responsible. The header no longer pulls in `Arduino.h` on a machine that has no Arduino.
- **`scripts/build.sh` runs the off-target tests before compiling**, wiping their build
  directory first. A stale object had been hiding a missing include, which is why the build
  host reported success on a commit CI rejected. A green build host that disagrees with CI
  is worse than a red one, because it is the machine nobody re-checks.
- **`scripts/push.sh` relays tags.** A release tag left behind on the workstation leaves the
  GitHub release pointing at a commit nobody can fetch.

## [0.2.0] — 2026-07-30

First release with firmware in the tree. It compiles for all four bring-up stages and its
off-target tests pass, but **no part of it has run on hardware** — the board arrived today
and nothing has been flashed. Treat every behavior below as reasoned from the datasheets
and the reviews, not demonstrated. [`docs/FIRST_FLASH.md`](docs/FIRST_FLASH.md) is the
order to bring it up in, and [`docs/EVIDENCE.md`](docs/EVIDENCE.md) is where the results
belong.

### Fixed

- **The node now transmits on the channels the network is listening to** (`src/radio.cpp`).
  US915 defines 72 channels; The Things Network uses eight of them. Nothing restricted the
  radio to those eight, so it spread its transmissions across all 72 and roughly seven in
  eight reached no gateway at all. Joins would have failed repeatedly for no visible
  reason, and the node would have reported every uplink as sent. Found independently by two
  reviewers, which is the only reason it was caught before the hardware arrived.
- **Downlinks can actually arrive** (`src/radio.cpp`, `src/session.cpp`). The node waited
  three seconds after each uplink before sleeping, against a network that answers after
  five — so the radio was powered down while the reply was being sent, and no downlink
  could ever have been received. The wait is now read from the radio stack instead of
  assumed, and the network's assigned timing is stored alongside the session, because a
  node resuming from flash never sees the join handshake that carries it and would
  otherwise revert to the wrong timing after every reset.
- **A milliamp reclaimed during sleep** (`src/power.cpp`). Putting the transceiver to sleep
  did not stop the bus that talks to it, which stayed clocked at close to a milliamp — more
  than everything else the node draws while asleep combined. The console is now left
  running when a host is actually plugged in, so diagnosing the node in the field no longer
  disconnects the person doing it at the first sleep.
- **The pack parser stops rather than guessing** (`src/sensors/battery.cpp`). An
  unrecognized record has an unknown length, and stepping through it a byte at a time read
  the tail of one reading as the head of the next — producing a voltage or temperature that
  looks entirely plausible and is wrong. It now keeps what it decoded and stops.
- **Battery temperature scale corrected in the documentation**
  ([ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md), `payload/schema.yaml`).
  The ×10 decision was made when the pack was to be read over Modbus; ADR-0004 moved it to
  the one-wire path, where the value already arrives scaled. Applying the old rule would
  have put every reading ten times high. The code was already right; the records were not.

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
