# Evidence ledger

`AGENTS.md`: _"No aspirational 'deployed' claims without bench/TTN evidence."_ This file is
where that evidence lives. **If it is not written down here, it did not happen** — and the
project status stays `🚧 NOT YET DEPLOYED`.

## What counts as evidence

An entry records something **observed**, not something expected. Every entry carries:

| Field | Why |
|---|---|
| Date | when it was observed |
| Commit SHA | which firmware — a result without a SHA cannot be reproduced |
| Host | which machine ran it ([`ENVIRONMENTS.md`](ENVIRONMENTS.md)) |
| What was measured | the specific claim under test |
| Raw observation | the actual output, reading, or log excerpt |
| Verdict | pass / fail / inconclusive |

"Inconclusive" is a legitimate verdict and more useful than an optimistic pass.

Measurement traps that invalidate an entry:

- **Sleep current measured over USB is meaningless.** Measure on battery and say so
  ([`.cursor/rules/50-power-management.mdc`](../.cursor/rules/50-power-management.mdc)).
- A bench run with the debugger attached is not a field run.
- A decoded TTN payload proves the formatter matches *that* build, not the schema —
  the parity gate proves the schema ([`.cursor/rules/60-decoder-parity.mdc`](../.cursor/rules/60-decoder-parity.mdc)).

## Release gates awaiting evidence

From [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §7. None of these can be closed by inspection.

| ID | Requirement | Evidence needed | Status |
|---|---|---|---|
| H1 | Hardware WDT resets a hung Modbus/BMS read | Induced hang → observed reset | ⬜ none |
| H2 | Deep sleep between cycles; radio sleeps | Measured sleep current on battery | ⬜ none |
| H3 | Brownout: no flash thrash, no TX when low | Sag the supply → observed skip | ⬜ none |
| H4 | Bounded backoff; survives multi-day no-gateway | Gateway off ≥48 h → observed backoff | ⬜ none |
| H5 | Interval + keys survive power loss | Set interval, cut power, confirm retained | 🟡 partial — session (DevAddr) restore observed 2026-07-31; interval-survives-power-loss not yet isolated |
| H6 | RK900 absent → no livelock | Unplug sensor → cycle continues | 🟡 partial — silent-sensor bounded timeout observed 2026-07-31; needs re-confirmation with sensor connected then removed |
| H7 | BMS silent → no livelock | Unplug BMS data → cycle continues | ⬜ none |
| H8 | Bench soak ≥24 h, field shadow ≥7 d | Soak log + TTN ingest history | ⬜ none |

Also outstanding, from [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §9: one good RK900 frame, one
good BMS frame, one TTN uplink, one interval downlink applied.

## Power budget

Projections live in [`POWER_BUDGET.md`](POWER_BUDGET.md). A projection is a hypothesis;
only a measurement recorded here closes it.

## Log

Newest first, by the date the entry was committed (`git log --format='%ad' -- docs/EVIDENCE.md`),
not by the date embedded in its heading — two 2026-08-03 entries and two 2026-07-31 entries each
span more than one commit, so heading dates alone don't disambiguate order. If you add an entry,
add it at the top.

### 2026-08-03 — RK900 full-frame diagnostic flash did not survive DFU; no sensor result

- **Commit:** `f38480bca5460a409faada2f36ccc40672b6d19f`, `busscan` image. This was the
  first attempt to request FC `0x03`, slave `0x01`, registers `0x0000`–`0x0004` at the
  previously observed 9600 baud and capture the raw reply.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on
  `/dev/cu.usbmodem1101`.
- **Measured:** whether the diagnostic image could be installed and remain a valid
  application long enough to capture the full five-register response.
- **Raw observation:** the first upload reported `=== FLASH OK ===` and briefly enumerated
  as application PID `239A:8029`, but an attempted 115200-baud capture received EOF and the
  board subsequently enumerated as `239A:0029` (UF2 bootloader). A recovery upload then
  failed independently:
  ```
  Failed to upgrade target. Error is: No data received on serial port. Not able to proceed.
  Timed out waiting for acknowledgement from device.
  ...
  USB 239A:0029 -- UF2 bootloader -- NO valid application
  ```
- **Verdict:** **INCONCLUSIVE — no RK900 frame captured.** The board has no valid
  application, so an empty serial log is not sensor evidence. This reproduces the physical
  recovery condition documented by closed issue #27; the fixed `flash.sh` gate correctly
  reported `=== FLASH FAILED ===` rather than falsely accepting the upload.
- **Next physical action:** operator double-taps RESET on the RAK19007 to re-enter DFU
  cleanly, then re-run `scripts/flash.sh --yes --env busscan`. Do not capture serial or
  interpret sensor silence while the USB PID is `239A:0029`.

### 2026-08-03 — The RK900 answers, and it is at 9600 baud, not the 4800 the firmware asks at

- **Commit:** `6b70416` (the `busscan` image running on the board). Build host `HEAD` has
  since moved to `be06c98`; the running image predates those two commits, neither of which
  touches the Modbus path.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`,
  `USB VID:PID=239A:8029` (`WisCore RAK4631 Board` — an application, not the bootloader)
- **Image:** `busscan` — `FEATURE_BUS_SCAN=1`, everything else off. Sweeps 4800/9600/19200/
  38400/115200 against slaves `0x01`, `0x02`, `0x03`, `0x6E` with FC `0x03` at register
  `0x0000`, once with `WB_IO2` HIGH and once LOW, printing raw bytes rather than a verdict.
- **Measured:** whether anything on the RS-485 pair answers at all, and whether any bytes
  seen are a real reply or an undriven receiver.
- **Observation:** the same non-empty rows in every one of four consecutive sweeps:

  ```
       9600 baud  slave 0x01 : 7 byte(s)  <- 01 03 02 00 00 B8 44
     115200 baud  slave 0x01 : 6 byte(s)  <- 7F 7F FF FF FD BD
     115200 baud  slave 0x02 : 5 byte(s)  <- FF FF FF FD 7B
     115200 baud  slave 0x03 : 6 byte(s)  <- 7F 7F FF FF FD FD
     115200 baud  slave 0x6E : 5 byte(s)  <- FF FF FF FD 55
  [bus scan] verdict: 29 byte(s) powered vs 0 unpowered
  ```

  Every other combination, including **4800 at every slave address**, returned 0 bytes.

- **Verdict:** **PASS — the RK900 replied.** This is the first response ever observed from
  this sensor on this hardware. `01 03 02 00 00 B8 44` is a well-formed Modbus RTU reply:
  slave `0x01`, function `0x03`, byte count `0x02`, one register reading `0x0000`, checksum
  `0xB844`. The checksum was verified by hand against the reflected CRC-16 poly `0xA001`
  seeded `0xFFFF` ([CIT-MODBUS-SERIAL]): for `01 03 02 00 00` the result is `0x44B8`,
  appended low byte first as `B8 44`. It matches exactly, so this is not line noise.

  Register `0x0000` is wind speed at ×0.01 m/s, so `0x0000` is 0.00 m/s — plausible for a
  sensor sitting indoors on a bench, and recorded as the measurement it is. **No value here
  is inferred or filled in.**

- **What this rules out.** The rail comparison is what makes the rest of the read
  trustworthy: 29 bytes with `WB_IO2` HIGH and **0** with it LOW, in all three cycles. Every
  byte on the line depends on the transceiver being powered, which means the RAK5802 is
  alive, `Serial1` reaches it in both directions, and `WB_IO2` gates it exactly as
  `src/sensors/rk900.cpp` assumes. The A/B pair is the right way round and the sensor has
  12 V — a reversed pair or an unpowered sensor cannot produce a CRC-valid frame.
  **No physical check is required.**

  The 115200 rows are **not** replies. They are driver-turnaround transients: byte-identical
  on every sweep, tracking the request's own CRC, none of them valid Modbus, and they vanish
  with the rail down along with everything else. At 115200 a bit is 8.7 µs, short enough for
  the transceiver's enable/disable edge to frame as a character; at 4800 the same edge is far
  too short to register. Reading them as a sensor answering in the wrong framing would have
  sent the next person chasing baud rates on a bus that was already telling the truth.

- **The defect this exposes — the firmware asks at the wrong rate.**
  `src/sensors/rk900.cpp` pins `kBaud = 4800`, cited to [CIT-RK900] and corroborated by the
  deployed Sensor Hub (`forest-weather-machines` `efc0e3c`,
  `LoRaWAN/docs/RAK2560_weather_station_settings.md`, which configures RS-485 at 4800 8N1).
  **This unit does not answer at 4800 and does answer at 9600.** Four sweeps, no exceptions.

  That is a direct contradiction between the datasheet plus field-proven sibling config on
  one side and observed hardware on the other, which
  [`.cursor/rules/00-agent-liveness.mdc`](../.cursor/rules/00-agent-liveness.mdc) makes an
  operator decision rather than an agent guess. **The constant was deliberately not changed.**
  The likely explanation is that this RK900 was configured to 9600 at some point — the rate
  is settable, and the deployed unit's 4800 was itself set by hand through WisToolBox — but
  that is a hypothesis and nothing here confirms it.

- **Notes:** the earlier sweep in this same session, at 19:27, showed **0 bytes at 9600** and
  only the 115200 transients. Something changed between then and 19:59, during which the
  operator was at the bench and pushed `420558e` and `be06c98`. The reply has been stable
  across every sweep since. Worth knowing before treating the 19:27 capture as contradicting
  this one — it was taken of a different physical setup. **What changed physically between
  19:27 and 19:59 is not recorded anywhere and is load-bearing for the #30 decision below —
  append it here once known** (rewiring, a WisToolBox setting, a reseated connector, or
  something else).

  Still unproven: a full five-register read, any non-zero wind value, the register map beyond
  `0x0000`, the payload encoding, the join, and the uplink. **No TTN uplink carrying real
  wind data has been observed. Status stays `🚧 NOT YET DEPLOYED`.**

### 2026-08-03 — First RK900 read attempt never happened: DFU failed twice, board left in its bootloader

- **Commit:** `3c05058`
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`
- **Image:** `stage1` — RK900 only (`FEATURE_BATTERY=0 FEATURE_RADIO=0 FEATURE_SLEEP=0`),
  `FEATURE_BENCH_INTERVAL` giving a 60 s cadence and staying awake so USB persists.
- **Measured:** whether the RK900-09, physically connected for the first time just before
  this session, would answer a Modbus read. **It was never asked.** No firmware ran.
- **Observation:** the serial capture is **empty — 0 bytes**, not a timeout, not a partial
  frame, nothing at all:

      === CAPTURE DONE ===
             0 /tmp/stage1_serial.log

  The cause is upstream of the sensor. Both DFU attempts failed. Attempt 1 (port pinned to
  `/dev/cu.usbmodem1101`) got partway through the image and then stopped being acknowledged:

      Upgrading target on /dev/cu.usbmodem1101 with DFU package .../firmware.zip.
      Flow control is disabled, Single bank, Touch disabled
      ########################################
      Timed out waiting for acknowledgement from device.
      ######################
      Failed to upgrade target. Error is: No data received on serial port. Not able to proceed.
      ...
      nordicsemi.exceptions.NordicSemiException: No data received on serial port. Not able to proceed.

  Attempt 2 (auto-detected port, board already in DFU) failed **earlier** — at
  `send_start_dfu`, the very first packet, with zero bytes transferred:

      File ".../dfu/dfu.py", line 199, in _dfu_send_image
        self.dfu_transport.send_start_dfu(program_mode, softdevice_size, bootloader_size,
      File ".../dfu/dfu_transport_serial.py", line 179, in send_start_dfu
        self.send_packet(packet)
      nordicsemi.exceptions.NordicSemiException: No data received on serial port. Not able to proceed.

  The board is in its bootloader with no valid application. USB product ID confirms it —
  `0029` is the bootloader, `8029` is a running application:

      /dev/cu.usbmodem1101
      Hardware ID: USB VID:PID=239A:0029 SER=4BC1FCC87D1343AB LOCATION=1-1
      Description: WisBlock RAK4631

- **Verdict:** **FAIL** for the flash. **The RK900 remains entirely unproven** — this run
  produced no evidence about it whatsoever, in either direction. No read was attempted, so
  nothing here says the wiring, the A/B polarity, the `WB_IO2` switched rail, the 4800 8N1
  framing, the slave ID, or the register map are either right or wrong.
- **Defect found — [#27](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/27),
  fixed in `420558e` (not yet exercised on hardware):**
  `pio run -t upload` exits **0** and prints `[SUCCESS]` even when `adafruit-nrfutil` fails
  and prints a traceback. The first attempt therefore reported `=== FLASH OK ===` and went on
  to capture serial from a board that had just been bricked into its bootloader. This is the
  worst shape a failure can take: it reports success, and the empty capture that follows looks
  exactly like a silent sensor. `scripts/flash.sh` branched on the exit status of that same
  command and inherited the bug. It now scans the upload output for the DFU tool's own
  failure strings and re-reads the USB product ID afterwards, refusing to report success
  unless the board comes back as `8029`. That gate has been tested against this captured
  output but **not yet against a real flash** — the board was in use elsewhere.
- **Notes:** Two attempts, then stopped, per the bounded-retry rule. The second attempt is
  not a repeat of the first — it changed the port strategy from pinned to auto-detect and
  added real success detection — and it produced new evidence: the failure moved *earlier*,
  from mid-image to the first packet. That is the opposite of a flaky link warming up.

  Both attempts began with PlatformIO's `use_1200bps_touch` reset. On attempt 2 the board was
  **already** in the bootloader, where a 1200 bps touch has nothing to reset and may be
  leaving the CDC endpoint in a state the DFU protocol cannot use. That is a hypothesis, not
  a finding.

  This is a **physical-hardware blocker**. The documented recovery is a double-tap of RESET
  on the RAK19007, which re-enters DFU cleanly and holds the port — the same recovery that
  worked on 2026-07-31 when an interrupted flash left product ID `002A`. Nobody was at the
  bench to press it. Until someone does, no firmware can be loaded and the RK900 cannot be
  read. The pack, the radio, and sleep are untouched by this.

### 2026-08-03 — bench 60 s cadence builds and tests; RK900 read still unproven

- **Commit:** `2b3b500`
- **Host:** Heliotrope Ridge (`ComputerName` confirmed over SSH), RAK4631 on USB
  (`USB VID:PID=239A:8029 SER=4BC1FCC87D1343AB`, `WisCore RAK4631 Board`)
- **Measured:** that `FEATURE_BENCH_INTERVAL` compiles into `stage1`, that the off-target
  suite still passes at that commit, and whether `stage1` could be flashed to the board
- **Observation:**
  ```
  HEAD 2b3b5005fd7df3579ca6450a70fed7cc340c5a0c
  HOST Heliotrope Ridge
  native         test_crc16    PASSED    00:00:00.933
  native         test_payload  PASSED    00:00:00.552
  ================= 20 test cases: 20 succeeded in 00:00:01.485 =================
  RAM:   [=         ]   7.0% (used 17372 bytes from 248832 bytes)
  Flash: [=         ]  12.2% (used 99344 bytes from 815104 bytes)
  stage1         SUCCESS   00:00:06.247
  ```
  Flash attempt — a poll loop on the build host checked for `/dev/cu.usbmodem*` every 0.3 s
  in order to catch the awake window of the sleeping field image:
  ```
  catch: start 18:52:18 sha 2b3b500
  --- ports ---
  /dev/cu.Bluetooth-Incoming-Port
  /dev/cu.PT-P710BT3824
  /dev/cu.debug-console
  ```
  No `usbmodem` port appeared in the first ~6 minutes of polling.
- **Verdict:** PASS for the build and the off-target suite. **INCONCLUSIVE for the flash, and
  the RK900 remains entirely unproven — no sensor read has been observed on hardware, ever.**
- **Notes:** The board is running the `ffec8aa` full image, which has `FEATURE_SLEEP=1`;
  `src/power.cpp` calls `Serial.end()` and disables the USB peripheral before sleeping, so
  the port genuinely does not exist while it sleeps. That is designed behavior, not a fault.
  The awake window is therefore the only opportunity to flash, and the interval between
  windows is whatever the stored config says — up to 3600 s. A double-tap of RESET on the
  RAK19007 drops the board into its DFU bootloader, where the port appears immediately and
  persists, which is the reliable way to do this rather than racing a sleep cycle. **Racing a
  sleep window is never the flash strategy — put the board in DFU deliberately, every time.**

  Nothing here says anything about the RK900. The sensor was physically connected just before
  this session and no read has ever been attempted on hardware. Do not read the passing
  `stage1` build as evidence that the wiring, the RS-485 direction control, the 4800 8N1
  framing, or the register map are correct — none of that has been exercised.

### 2026-07-31 — first LoRaWAN join and first uplink accepted by The Things Network

- **Commit:** `stage3` build, after the DevEUI byte-order and empty-uplink fixes
- **Host:** Heliotrope Ridge, RAK4631 on USB, antenna attached, no sensors connected
- **Device:** `puma-concolor-001`, DevEUI `42BB96EF76E200F1`, US915 FSB2, MAC 1.0.3
- **Observation:** device side —
  ```
  session : restored 0x260CE734, counter 32
  [cycle 1]
     RK900   : no data (timeout)
     battery : no data (no reply, 0 bytes)
     uplink  : proof of life — no sensor data for 1 cycle(s)
     radio   : sent 0 bytes on port 2
     session : saved 0x260CE734, resume at 64
  ```
  network side — Network Server `nam1` reports `has session: True`, `has pending: False`,
  `adr_data_rate_index: 3`, `rx1_delay: 5`. Gateway `9181014c6051030034` heard the join at
  **RSSI −62, SNR 14** at 48.71066, −122.05389.
- **Verdict:** PASS — the radio path works end to end. Join, join-accept, session
  establishment, and an accepted uplink are all confirmed from both sides. Session
  persistence (H5) also demonstrated: the second boot restored DevAddr `0x260CE734` from
  flash and transmitted without rejoining.
- **Notes:** Two real defects were found getting here, both of which would have been far
  worse to diagnose in the field.

  First, `src/secrets.h` held the DevEUI **byte-reversed**. `SX126x-Arduino` requires
  most-significant-byte-first and reverses the bytes itself; the generator script had written
  the opposite convention. The node transmitted flawlessly and TTN logged **nothing at all** —
  an unrecognised DevEUI is neither answered nor reported, so it is indistinguishable from a
  dead radio or an absent gateway. The boot banner now prints the DevEUI so this comparison
  takes seconds.

  Second, the node did not join at all when both sensors were silent, and `Radio::send()`
  additionally discarded zero-length payloads. Together these meant a station installed with
  one bad wire would have sat in the woods transmitting nothing and been unreachable by
  downlink, since Class A only opens a receive window after an uplink.

  Still unproven: real sensor data (nothing is wired yet), sleep current, and the decoder
  against a live non-empty payload.

### 2026-07-31 — First flash. Firmware runs on real hardware; sensor not yet connected

- **Commit:** `8d4a41c` (first flash), then the attempt-log fix
- **Host:** Heliotrope Ridge · RAK4631 at `/dev/cu.usbmodem31101`, USB `239A:8029`,
  serial `4BC1FCC87D1343AB`
- **Image:** `stage1` — RK900 only. Radio, battery reader, and sleep all compiled out.
- **Measured:** serial console over USB CDC, several consecutive cycles.
- **Observation:** the RK900 was **not connected**, so every read timed out. That is the
  expected result for an absent sensor, and the behavior around it is what this run
  actually tested:

      [cycle 2]
            modbus attempt 1/3 failed (timeout)
            modbus attempt 2/3 failed (timeout)
            modbus attempt 3/3 failed (timeout)
         RK900   : no data (timeout)
         wait    : 30 s (sleep disabled)

- **Verdict:** PASS for four narrow claims, all of them first-time-on-hardware:
  1. The vendored board definition produces an image that boots and runs. Flash and USB
     CDC both work, which retires the risk that the board files were subtly wrong.
  2. The cycle loop runs and repeats on schedule.
  3. **H7 — a silent sensor does not livelock.** Three bounded attempts, then the cycle
     continues. This is the failure that would otherwise strand a node in the field, and it
     is now observed rather than reasoned.
  4. **A missing reading stays missing.** No zeros were fabricated for the absent sensor.
- **Defect found and fixed:** the log read `modbus retry 3/2`, which looks like the retry
  limit was breached. Three attempts is one initial plus the two retries the spec allows —
  the count was right and the label was wrong. Now `attempt N/3`.
- **Notes:** proves nothing about the RK900 register map, the pack, the radio, joining,
  sleep, or current draw. An interrupted flash left the board in its bootloader
  (product ID `002A` instead of `8029`); re-running `flash.sh` recovered it, which confirms
  the documented recovery path works.

### 2026-07-30 — Release 0.2.0: all four stages build, off-target tests pass

- **Commit:** `80de312` (tagged `v0.2.0`)
- **Host:** Heliotrope Ridge (PlatformIO 6.1.19)
- **Measured:** build output and host-run unit tests. **Nothing was run on hardware.**
- **Observation:**

  Field image (`rak4631`):

      RAM:   [=         ]   9.9% (used 24536 bytes from 248832 bytes)
      Flash: [==        ]  23.8% (used 193672 bytes from 815104 bytes)

  All four environments report `SUCCESS`: `stage1`, `stage2`, `stage3`, `rak4631`.
  `pio test -e native` — 20 test cases, 20 passed. `scripts/preflight.sh` PASS.

- **Verdict:** PASS for a narrow claim — the release builds and its host-testable parts
  behave.
- **Notes:** The four-reviewer pass that preceded this release found, among other things,
  that the radio was not restricted to the channels the network listens on, and that the
  node slept before the network's reply window opened. Both are corrected here and **both
  corrections are unverified** — they are reasoned from the regional parameters and the
  radio stack's own reported timing, not observed. The first join and the first downlink
  are what settle them. Same caveat applies to the low-voltage gate, the session
  persistence, the pack frame validation, and the sleep-current change: all compile, none
  have been exercised. H1–H8 remain open.

### 2026-07-30 — The build host and CI disagreed on the same commit

- **Commit:** `24c5d5e` (failing) → `fe3fc47` (passing)
- **Host:** Heliotrope Ridge and GitHub Actions, same source
- **Measured:** `pio test -e native` on both.
- **Observation:** the build host reported 20 of 20 tests passing on a commit where CI
  failed to compile them at all. Two causes, found in order: a stale object file in
  `.pio/build/native` that survived a header change and hid a missing include, and
  `src/features.h` shadowing the C library's own `<features.h>` once `src/` was on the
  include path.
- **Verdict:** the build host alone is **not** sufficient evidence for the off-target tests.
  Its result was wrong and confidently so.
- **Notes:** `scripts/build.sh` now wipes the native build directory and runs the tests
  before compiling, and the header is renamed `build_features.h`. On-target builds were
  never affected — the shadowing needs `-I src`, which only the test environment sets.

### 2026-07-30 — Full firmware compiles for all four stages; off-target tests pass

- **Commit:** `146d99e`
- **Host:** Heliotrope Ridge (PlatformIO 6.1.19)
- **Measured:** build output and host-run unit tests. **Nothing was run on hardware.**
- **Observation:**

  Field image (`rak4631`):

      RAM:   [=         ]   9.9% (used 24536 bytes from 248832 bytes)
      Flash: [==        ]  23.6% (used 192744 bytes from 815104 bytes)

  All four environments report `SUCCESS`: `stage1`, `stage2`, `stage3`, `rak4631`.

  `pio test -e native` — 20 test cases, 20 passed, covering the payload encoder
  (including the 11-byte worst-case data rate budget) and the Modbus checksum
  (pinned to published reference values).

- **Verdict:** PASS for a narrow claim. The full firmware links, every bring-up stage
  still builds, and the two pieces of pure computation behave correctly on a host.
- **Notes:** This closes none of H1–H8. Nothing here proves the node reads a sensor, joins
  a network, sleeps, or draws the current the budget assumes. The low-voltage gate, the
  session persistence, and the battery frame validation added on this date are all
  **unexercised** — they compile and are untested against hardware. First real evidence
  comes from [`FIRST_FLASH.md`](FIRST_FLASH.md).

### 2026-07-30 — Stage 0 compiles for the RAK4631 on two independent machines

- **Commit:** `7ae56ec`
- **Host:** Heliotrope Ridge (PlatformIO 6.1.19) and GitHub Actions `ubuntu-latest`
- **Measured:** build output only. **Nothing was run on hardware.**
- **Observation:**

  Identical on both machines:

      RAM:   [          ]   4.9% (used 12280 bytes from 248832 bytes)
      Flash: [=         ]   9.4% (used 76748 bytes from 815104 bytes)
      [SUCCESS]

- **Verdict:** PASS, for a narrow claim — the toolchain, the vendored board definition,
  and the Adafruit nRF52 framework combination produce a linkable image, and they do so
  reproducibly on a machine that has never seen the project before. Byte-identical sizes
  across two hosts indicate the build does not depend on local machine state, which was
  the specific risk in RAK's alternative "copy files into `~/.platformio`" approach.
- **Notes:** This closes none of H1–H8. It says nothing about whether the board runs,
  enumerates USB, joins, sleeps, or draws the current we hope. The first real evidence
  comes from flashing hardware and reading the serial banner.

<!-- Template:

### YYYY-MM-DD — one-line summary

- **Commit:** `abc1234`
- **Host:** Heliotrope Ridge
- **Measured:** sleep current between wake cycles, on battery, USB detached
- **Observation:**
  ```
  <raw reading / log excerpt>
  ```
- **Verdict:** PASS — H2 satisfied / FAIL — ... / INCONCLUSIVE — ...
- **Notes:** anything that would change how the next person reads this

-->
