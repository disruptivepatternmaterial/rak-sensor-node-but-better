# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning per [`docs/RELEASE.md`](docs/RELEASE.md).

`1.0.0` is earned, not scheduled — it requires H1–H8 closed in
[`docs/EVIDENCE.md`](docs/EVIDENCE.md), including a ≥24 h bench soak and ≥7 d field shadow.

## [Unreleased]

### Fixed

- **A dead one-wire link no longer silences a healthy node forever**
  ([#45](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/45)).
  The #38 fix made the brownout gate fail closed, which was right, but it left the gate
  single-sourced on the RAK9154 one-wire link — the least reliable element in this build and
  the one that has actually failed repeatedly on the bench. A broken wire or connector then
  silenced a node with a full pack, permanently, and because a Class A downlink can only follow
  an uplink there was no remote route left to override it. Silent-forever and drained both end
  in a hike; the silent one gives no warning first.

  The intended fix was a second, independent voltage source. **There isn't one.** The base
  board's battery divider observes the `BAT` connector, and this build deliberately never
  connects the pack there — `P+` goes through a 12 V→5 V buck, and `BAT` is a 4.2 V output, not
  a supply input. Everything the chip can measure downstream of the buck is regulated and
  therefore flat across the pack's whole usable range. Scaling a rail reading back to pack
  voltage would be a fabricated measurement that the gate would then act on, which is worse
  than no measurement. Recorded in
  [ADR-0007](docs/decisions/ADR-0007-no-second-voltage-source.md).

  So the silence is bounded instead: after `kNoEvidenceKeepaliveCycles` (24, about a day at the
  default interval) held on the **no-evidence** path, the node transmits once regardless, then
  resumes holding. Its silence is no longer indistinguishable from its death, and the downlink
  route reopens daily. Critically this does not reopen #38 — a hold backed by a *measured* low
  voltage gets no keepalive at all, because there the evidence says staying quiet is right, and
  flash writes stay blocked on a keepalive cycle either way. This is a mitigation, not a
  removal of the single point of failure; that needs a hardware divider from `P+`. Untested on
  hardware — needs a bench run with the one-wire lead pulled from a full pack. No H1–H8 gate
  closes.

- **Brownout protection no longer fails open**
  ([#38](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/38)).
  The gate lived only in RAM and started at transmit-allowed, so any reset — watchdog,
  panel flicker, the pack's own brownout reset — cleared the protection, and it only
  self-corrected if the pack was still answering. It also held the previous decision
  forever on an unreadable voltage, which from a cold boot means transmitting
  unconditionally with no voltage evidence at all. The field case is the bad one: overcast
  week, pack sags, the BMS stops answering at low voltage, and the node then transmits
  every cycle at the largest current it ever draws until the pack hits protection cutoff.
  Two changes: the engaged bit is now persisted in `config` and restored at boot, written
  only on a state change — the transition into brownout happens while the pack is still
  answering and still above the level where a write is unsafe, so it costs one write per
  event; and `kInvalidReadsBeforeInhibit` consecutive unreadable voltages now engage the
  gate instead of leaving it open. The stored-settings record is version 2; version 1
  records are still read so an operator-set interval survives the update.

  The consequence is deliberate and worth stating: a node whose one-wire link dies goes
  quiet after four cycles even if the pack is healthy. That is the intended direction —
  lost data costs one hike, a pack driven to cutoff may not restart on panel current at
  all. No H1–H8 gate closes on this; the evidence needs a bench measurement that has not
  been taken.

- **The fair-use guard now checks the effective uplink cadence rather than one bad flag
  pair** ([#44](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/44)).
  `src/config.h` refused `FEATURE_BENCH_INTERVAL` together with `FEATURE_RADIO`, but
  `FEATURE_SLEEP=0` with the radio on reached the same place by another route: with sleep
  compiled out the awake-wait cap *is* the cadence, and at 30 s that is roughly 2880
  uplinks/day. The cap lived in `main.cpp`, where no fair-use check could see it. It now
  lives in `config.h`, the effective minimum interval is derived from the flags rather than
  enumerated, and a `static_assert` requires any transmitting build to be incapable of a
  cadence below the 900 s floor — so a flag combination nobody has thought of yet still has
  to pass through it. The floor is single-sourced as `kFupFloorSeconds`, so the assertion
  cannot contradict the interval floor it defends. The awake-wait cap no longer applies when
  the radio is compiled in, which restores what `stage3` already claimed in its own
  `platformio.ini` comment: it runs at the field floor.

- **`scripts/flash.sh` no longer reports FLASH FAILED when the image landed**
  ([#33](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/33)).
  The board re-enumerates the moment the DFU write completes, which can drop the serial link
  before `adafruit-nrfutil` reads its final acknowledgement — so the tool reports a transport
  error with every page already written. The script trusted the tool over the board. There is
  now a third verdict: when the tool errors but the post-flash USB PID is `8029`, it prints
  `FLASH INDETERMINATE` and exits 2 rather than declaring failure. It does not claim success
  either, because the PID proves an application is running but not *which* one — a previously
  resident image enumerates identically — so the output names the positive check needed to
  settle it. A false FAILED costs a wasted flash cycle and an operator double-tap on hardware
  that was fine, and puts a wrong verdict next to a commit SHA in the narrative
  `docs/EVIDENCE.md` depends on.

### Changed

- **Minimum reporting interval lowered from 1800 s to 900 s**, so the operator can run
  15-minute reporting. `docs/FIRMWARE_SPEC.md` §4 carries the corrected fair-use reasoning
  rather than just the new number: 900 s is 96 uplinks a day, which is about 36 s of
  airtime at DR0 — **over** the ~30 s TTN allowance — and about 6 s at DR3, comfortably
  under. So the floor is compliant at DR3 or better and marginal at DR0. That is a
  coverage-dependent condition, not the unconditional guarantee the 1800 s floor gave at
  every data rate, and it is written down that way on purpose. ADR starts at `DR_3`, so a
  node with usable gateway coverage settles high enough to be legal while a node at the
  edge does not; a deployed node observed sitting at DR0 should have its interval raised.
  Airtime is the only constraint here — the energy cost is irrelevant at any interval in
  the legal range.

## [0.4.0] — 2026-08-05

**Battery telemetry works on hardware.** A new working subsystem, backward compatible with the
TTN decoder, so a minor bump per SemVer and [`docs/RELEASE.md`](docs/RELEASE.md). Firmware
version emitted in the uplink is now `0.4.0`.

Gates for this release: `scripts/preflight.sh` PASS; **decoder parity PASS across all nine
emitted fields** against the live TTN formatter in `forest-weather-machines`; LoRaWAN airtime
well inside the TTN Fair Use Policy at the 3600 s default interval. Parity is the gate that
matters most here — a drifted encoder does not lose one field, it makes the decoder throw and
discard the whole uplink.

Two High findings are **open and known** against the battery path, deliberately deferred to a
consolidated fix pass:
[#36](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/36) — the
SENDAT response is not matched to the query, so flag, dest, source and sequence go unverified;
[#37](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/37) — a
partial record set can return `Ok` carrying stale values from a previous read.

Status remains **`🚧 NOT YET DEPLOYED`**. No H1–H8 gate closed, and the ≥24 h soak and ≥7 d
field shadow have not run.

### Added

- **The RAK9154 reports real values.** `12.23 V`, `98 %`, `23.0 °C`, `+0.00 A`, stable across
  seven consecutive cycles on commit `1a203d3`, build host Heliotrope Ridge, `battdiag`. The
  frame is a genuine `hub_type` SENDAT (`0x03`) reply and the pack now announces itself as
  `source = 0x01` — it accepted the assigned probe id.

  What unblocked it was reply timing, not framing. Two changes landed together: a guard gap
  before the first response byte (`FEATURE_BATTERY_TURNAROUND_MS`, default 2 ms, the figure the
  reference master pays as a side effect of its `delay(2)`-per-byte drain) and `kWakeCount`
  restored to 4. Before them the driver answered under one bit time after the pack's stop bit,
  on a line the pack had just been driving.

  Sampling lags the latch by about two cycles: cycles 1–2 still returned the all-zero record
  template, cycles 3 onward carried values. The `Unsampled` guard correctly reported the first
  two as no data rather than as a 0.00 V pack.

  The current sign is **not** settled by this — `+0.00 A` at rest distinguishes nothing.
  [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md) stays open.

  This closes no H1–H8 gate and does not change the `🚧 NOT YET DEPLOYED` status.

- **The success path hex-dumps its frame.** Every failure path in the battery driver has always
  printed its bytes; the one path that produces a number printed only the number, so the first
  real reading arrived with no frame behind it. It now dumps under the same `sendat` label and
  format the other paths use, which is what makes a reading evidence rather than an assertion.

- **The raw sensor integers are logged unscaled.** The temperature scale was inferred, not
  measured: `src/payload.cpp` hands the value to Cayenne LPP type 103 unscaled and the decoder
  divides by 10, which is correct only if the pack reports tenths. A bench "23.0 °C" cannot
  distinguish raw `230` (tenths, correct) from raw `23` (whole degrees, in which case every
  temperature ever shipped is 10× low). The capture reads `v=1223 i=0 soc=98 t=230`, so
  **tenths is confirmed and the decoder's `/10` is right.**

- **New `battdiag` build environment** — battery only, RK900 out, radio and sleep out, ~10 s
  cycle. `stage2` ran 110.5 s, of which the battery driver alone took 50.5 s and the RK900 added
  3.1 s of guaranteed timeouts on a bus whose sensor is physically unplugged. Every experiment
  cost two minutes and arrived buried in timeout noise. Diagnostic only: `FEATURE_BATTERY_FAST`
  shortens the provisioning window to 3 s and the push listen to 2 s, neither of which is long
  enough to test what the full-length versions exist to test.

### Changed

- **Awake time drops from ~50 s to ~5 s per cycle.** `acquire_pid()` was 45.4 s of a 50.5 s
  wake. It is now not entered at all in the steady state, because the pack latches the id and the
  phase-0 direct probe answers instead. [`docs/DEPLOY.md`](docs/DEPLOY.md) is updated; any
  power-budget arithmetic written against the 50 s figure is stale.

- **The provisioning window is capped at 5 s, down from 45 s.** It was 45 s to test one
  hypothesis: that the pack latches an id only if the master is still answering when it next
  announces, the way the reference master — which answers forever on a 50 ms tick — is. That is
  now a verified negative: 22 byte-correct answers across 45,382 ms left `provId` at `0xFF`. The
  response bytes have also been independently confirmed to match the reference's
  mutate-and-echo exactly (`onewire_master_protocol.c:443-466` versus `Battery::provision()`),
  so the framing is not what is failing either.

  This was 45.4 s of a 50.5 s wake spent re-running a settled experiment — the single largest
  avoidable cost in the cycle. The path is kept rather than deleted: a replacement pack that
  does latch would still be provisioned by it, and the negative result is about this pack, not
  about the protocol.

### Fixed

- **The battery console log inverted every current between −0.99 A and −0.01 A.** The
  formatter took its sign from the integer part: for −0.50 A, `value / 100` truncates toward
  zero to `0`, `%+d` renders that as `+0`, and the magnitude was supplied separately — so a
  discharging pack printed as `+0.50 A`. Temperatures from −0.9 to −0.1 °C had the same
  defect.

  That band is where a pack sits on an overcast day under a light load, which is exactly the
  condition an operator watches while settling the current sign convention — and this log is
  what they read to settle it. A wrong sign here would have made the wrong convention look
  confirmed, and that convention goes into `payload/schema.yaml` and the TTN decoder, where
  unwinding it costs a coordinated two-repo change. Sign and magnitude are now separated
  before the division. Blocks [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md).

- **A provisioned pack no longer spends 45 s per wake listening for an announcement that
  will never come.** `Battery::read()` called `acquire_pid()` unconditionally. That phase
  exists to hear an *unprovisioned* pack announce itself, so on a pack that has already latched
  an id it hears nothing and burns the entire window doing it — and with the 20 s
  push-listen behind it, a wake that should take under a second took over a minute, every
  hour, forever.

  `read()` now asks probe id `0x01` for data first and skips the announcement phase when it
  answers. Deliberately a probe rather than a persisted "pack is provisioned" flag: a stored
  flag has to be invalidated by hand the first time the hardware changes, and a stale one
  fails as silence that reads exactly like a dead sensor. Asking is self-healing — a
  replacement pack that was never provisioned still falls through to the old path.

  Bench-confirmed on hardware at `8720dea`: the probe costs ~0.5 s, draws nothing on an
  unprovisioned pack, and leaves phase 2's `0xFF` reply intact — against the `acquire_pid()`
  window it exists to skip, now measured at **45.4 s of a 50.5 s wake**
  ([`docs/EVIDENCE.md`](docs/EVIDENCE.md) 2026-08-05).

- **A truncated record no longer reports as an unknown one.** A recognized IPSO type whose
  payload ran past the end of the frame logged as `unknown record type 185`, which would send
  the next reader to write a decoder for a type that is already decoded. The real fault is in
  the transport, and the two now say different things.

- **The watchdog is fed inside `acquire_pid()` and `receive()`.** Both can hold the CPU for
  tens of seconds — 45 s and 20 s respectively — against a 120 s window (`FIRMWARE_SPEC.md`
  §7 H1), and they stack with a join backoff and a slow RK900 read on the same wake. Neither
  fed. A node working exactly as designed could reset itself, and the reset would look like a
  hang rather than a budget overrun. Both loops are bounded by their own deadlines, so a
  genuinely stuck line still returns.

### Removed

Roughly 700 lines, none of which a field image ever executed. Each was kept at the time
because "it might be needed for the next pack"; together they had become the thing every
reader has to rule out before touching the battery driver.

- **The raw-Modbus path and `FEATURE_BATTERY_MODBUS`. Negative result — do not re-attempt it.**
  A plain Modbus RTU read at slave `0x6E`, FC `0x03`, 21 registers from `0x6000` — request
  `6E 03 60 00 00 15 93 5A`, the same register map the deployed sibling node reads this pack with
  over its own RS-485 harness — **returned 0 bytes on every cycle it ran.** The one-wire peer is a
  Generic Probe IO adapter that speaks SensorHub northbound and Modbus southbound to the BMS; it
  does not forward a Modbus frame arriving from the north.

  It was a reasonable experiment while the SensorHub handshake was stuck, because a register read
  has no provisioning step to get stuck in. The handshake now works and returns 12.23 V on the
  same wire, so the path costs an 8-byte request plus a 1 s wait per wake to re-derive an answer
  already in [`docs/EVIDENCE.md`](docs/EVIDENCE.md). Recorded here so the next reader who notices
  that the sibling reads this pack over Modbus finds the result instead of repeating it.

- **The PARAMGET/PARAMSET pass (~210 lines) and `FEATURE_BATTERY_PARAM_PASS`.** Built on the
  hypothesis that the pack's sensors sit at `RULE_DISABLE` until armed. Falsified three ways:
  the pack's own announcement descriptors already report rule `0x0008` (periodic), the
  working reference reader never sends a parameter write at all, and on this pack PARAMGET
  drew no reply while the "PARAMSET ack" turned out to be an announcement arriving on its own
  schedule. It had been switched off rather than deleted on the argument that the conclusion
  was about one pack — but the real blocker turned out to be elsewhere entirely, so the pass
  was aimed at a question that is no longer being asked.

- **`FEATURE_ONEWIRE_SPLIT` (~90 lines).** Tested whether bridging the pack's TXD and RXD
  onto one wire caused TX contention. The bench settled it the other way: the pack answers
  SENDAT with checksum-valid frames on the bridged harness. A second wiring mode no evidence
  supports is a thing every future reader has to eliminate.

- **`FEATURE_ONEWIRE_RAIL_CYCLE`.** Its own comment recorded it as a no-op on this harness —
  the pack's pin 4 is on the always-on VDD pad, not the switched 3V3_S rail `WB_IO2` gates.

- **Four write-only members** (`m_assigned_pid`, `m_sids`, `m_sid_count`,
  `m_enable_attempts`) and the four constants left with no reader. The parameter pass held
  their only consumers. The field image's RAM fell by exactly the 12 bytes they occupied.

- **`src/owprobe.h`** — untracked, referenced a feature flag and a build environment that
  never existed, and tested a topology question that is now closed.

### Changed

- **The two bench scanners moved out of `main.cpp` into `src/diagnostics/`.** `bus_scan()`
  (150 lines) and `onewire_scan()` (371 lines) were 521 of the file's 808 lines — roughly two
  thirds of it — and reading the actual wake cycle meant scrolling past all of it. `main.cpp`
  is now 282 lines. Both scanner environments still build, and `nm` confirms the field images
  contain no `diagnostics::` symbol at all.

  The one-wire scanner sent **four** wake bytes where the production driver sends **one**, so
  every "the pack answers" result it ever produced was obtained with framing the driver never
  transmits. The pack tolerates both, so nothing was wrong — but nothing was proven about
  production either. The count now matches the driver, and the pin is passed in rather than
  redeclared, so neither can drift again.

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

- **`scripts/flash.sh` can no longer report a failed flash as `=== FLASH OK ===`.** Issue #27.
  `pio run -t upload` exits 0 and prints `[SUCCESS]` even when `adafruit-nrfutil` dies with a
  traceback, and the script branched on that exit status alone. On 2026-08-03 that produced a
  green flash, a green harness, and then a serial capture from a board sitting in its
  bootloader with nothing to run. The capture was 0 bytes — and a 0-byte log from a board
  with no firmware is indistinguishable from a 0-byte log from a silent or miswired sensor,
  so the obvious reading of it would have sent the next session after RS-485 polarity on
  hardware that was never executing a single instruction.

  Two independent checks now decide the result. The upload output is scanned for the DFU
  tool's own failure strings (`Failed to upgrade target`, `Timed out waiting for
  acknowledgement`, `No data received on serial port`, `Serial port could not be opened`, a
  Python traceback), any of which fails the flash whatever the exit status says. Then the
  board's USB product ID is re-read over a bounded 30 s settle window: `8029` is a running
  application, `0029` and `002A` are the bootloader. Anything but `8029` fails, and the
  failure message says outright that the board has no valid application, that a capture
  taken now is not evidence, and that the fix is a double-tap of RESET on the RAK19007.
  The PID table is now written down in `docs/FIRST_FLASH.md`, having been rediscovered
  twice; `scripts/remote.sh usbpid` and `devices` report it.
- **`scripts/build.sh` refuses an `upload` target** rather than passing it to the same
  untrustworthy exit status. Uploading goes through `flash.sh`, which verifies the result.
- **`scripts/remote.sh sync` no longer reads an unreachable build host as a clean tree.** The
  dirty-tree check discarded its own exit status with `|| true`, so an SSH failure and an
  empty `git status` were the same answer — the same class of defect as #27, a check that
  could not run reporting as a check that passed.
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
