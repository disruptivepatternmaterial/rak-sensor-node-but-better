# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning per [`docs/RELEASE.md`](docs/RELEASE.md).

`1.0.0` is earned, not scheduled — it requires H1–H8 closed in
[`docs/EVIDENCE.md`](docs/EVIDENCE.md), including a ≥24 h bench soak and ≥7 d field shadow.

## [Unreleased]

**Now partly hardware-verified.** `d568574` was flashed to the board as `env:soak` — the field
image — on 2026-08-13 and its boot banner read back `commit   : d568574`, which is the first time
any image in this repository has identified its own commit on hardware.

### Verified on hardware

- **The downlink command matrix passes 8 of 8 on the physical node.** `scripts/downlink_matrix.sh`
  drove valid set-interval and request-status, both malformed-length rejections, an unknown opcode,
  a valid command on the wrong FPort, and two commands queued at once, then checked the node had
  not reset. Every case matched a console line emitted from inside `Radio::take_downlink()`, so
  **`take_downlink()` is observed on hardware for the first time** — the acceptance criterion that
  kept [#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)
  open. The applied interval is visible changing from 1800 s to 900 s and persisting across a
  reflash and power cycle. Ran on a `stage3` bench image with `FEATURE_SLEEP=0`, so this is
  evidence about the shared downlink path and **not** about the sleep path. Raw log, per-case
  console quotes and the caveats are in
  [`docs/EVIDENCE.md`](docs/EVIDENCE.md). The length-checking fixes shipped in `0.4.1` on compile
  evidence alone ([#63](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/63),
  [#64](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/64)) are
  now confirmed on hardware.
- **The field image reaches sleep, and wakes from it.** Two cycles of `env:soak` at `d568574`:
  both sensors read, an uplink went out, and each cycle ended `sleep   : 900 s` rather than
  `wait    : N s (sleep disabled)`. The second cycle arrived ~900 s later with no boot banner in
  between, so the node woke from sleep instead of resetting through it. Still two cycles, not a
  soak — [`docs/EVIDENCE.md`](docs/EVIDENCE.md) records zero soak hours and that is unchanged.

### Fixed

- **The downlink matrix harness can no longer record PASS with no answering uplink.** In
  `case_a`, `wait_for` was called for the FPort 2 uplink and its exit status discarded, so a
  timeout still recorded PASS — with the literal string `<no uplink observed>` pasted into the
  evidence field of the passing row. The status now decides: PASS on the observed uplink, FAIL
  otherwise. Tonight's `case_a` result was independently re-verified against the raw console,
  so the recorded evidence stands; the harness simply must not be able to report green over a
  missing observation. `case_a` was the only unchecked call — the other six `wait_for` sites
  already test it.

- **The RK900 console summary no longer prints a pressure the uplink deliberately omitted.**
  The encoder correctly leaves the field null when register `0x0004` reads `0`, and the very
  next line printed `0.0 hPa` as though it had been measured — so a bench capture contradicted
  the payload and pointed the next debugger at the decoder or the register map rather than at
  the barometer. The line now reads `pressure null` in that case. Introduced by `6c9bdc0`
  earlier the same night. Diagnostic output only; the encoded payload is unchanged.

- **A set-interval downlink taken during a brownout hold is no longer dropped on the second
  retry, after the console said it would persist.** `Config::set_interval_seconds()` assigned
  `m_interval` before attempting the write and left it there when the write failed, so the next
  retry matched the unwritten value at the `seconds == m_interval` short-circuit, wrote nothing,
  and returned true — and `main.cpp` cleared its pending value believing the command had landed.
  The commanded cadence was then live only until the next reset, having been reported as saved.
  Reachable on any failing write, including every save on a node whose filesystem did not mount,
  where `save()` returns false without touching flash. The value is now staged and rolled back on
  failure, so a true return means the value is on flash and nothing else; the RAM-apply stays in
  `main.cpp`, which also now honors the return value on the gate-allowed path where it was
  previously discarded. The retry is bounded to three flash attempts so the fix cannot turn a
  broken filesystem into a settings-page rewrite on every wake — H3's thrash rule arriving through
  the door opened to fix a different defect. After that the cadence stays applied in RAM and the
  console says exactly that. Refs
  [#65](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/65).
  **Compile-verified only — unflashed and unobserved.**

- **A second kind of battery failure no longer postpones an already-scheduled recovery
  retry.** Both threshold branches assigned `m_next_full_cycle` unconditionally, so crossing
  the second threshold moved a retry that was already pending: three silent cycles schedule
  the full ladder for cycle 27, six empty records crossing at cycle 26 push it to cycle 50 —
  nearly six more hours at the 900 s field interval in which the only path that can recover
  the pack does not run. The deadline is now armed once and never moved, re-armed only after
  it fires, and cleared by a real reading. The two streak counters also reset when the other
  kind of failure occurs, so the console's "consecutive" is now true and the gate no longer
  sums two streaks that were never concurrent; a new total-stalled counter carries the hard
  power bound that the streaks alone cannot. The bound is unchanged in size: at most six
  expensive cycles after the last good reading, then one every 24 cycles — six hours at
  900 s. An absent pack costs exactly what it did before.
  **Compile-verified only — unflashed and unobserved.**

- **A routine empty battery reply no longer reboots a healthy pack, or burns the one BOOT this
  power cycle is allowed.** Phase 0 required `Ok || m_pack_latched` to call the direct probe
  answered, and `m_pack_latched` is false after every MCU reset — so the `Unsampled` reply the
  pack sends for roughly its first two cycles while it samples was treated as no answer at all.
  That path calls `boot_once()`, which prints `pack silent at its id — one BOOT this power
  cycle` and sends the reference's reboot verb to a pack that had just answered a matched SENDAT
  from `0x01`, and spends the single allowed BOOT before any real failure can ask for it. A
  matched response settles the address whether or not the record carries a measurement: an
  unprovisioned pack answers `0xFF` and returns zero bytes from `0x01`. The push listen, which
  is where the placeholder record actually gets resolved, is gated on `m_last != Ok` and is
  unaffected. Interaction between `342d994` and `955fc01`; bears on
  [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) and
  [#71](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/71).
  **Compile-verified only — unflashed and unobserved.**

### Added

- **The boot banner names the commit it was built from** — `commit   : a7381e7`, with `-dirty`
  appended when the tree carried anything uncommitted, and `unknown` when there is no git
  history to read. Injected at build time by `scripts/pio_git_rev.py`, a PlatformIO `pre:`
  extra script that appends one `-D FIRMWARE_COMMIT="…"`; every environment carries it,
  `native` included. `docs/EVIDENCE.md` cannot accept a result without a SHA, and until now the
  firmware could not state one: the banner printed a version and a `__DATE__`/`__TIME__` stamp,
  so two builds of `0.4.1` read identically on the console and a board already in the field
  could not be matched to a commit at all. Today's `stage3` entry had to record its SHA as
  *inferred* from a build timestamp; the next one will not.
  Costs nothing when `FEATURE_CONSOLE=0` — the `LOGF` macro discards its arguments without
  expanding them, so the string never reaches the image — and touches neither the sleep nor the
  USB path, so the field image's power behaviour is unchanged (rule 50,
  [ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md)).
  Deliberately **not** in the uplink: that is a payload contract change needing a paired TTN
  formatter change (rule 60), and `batt_current` already blocks the payload freeze.

### Documentation

- [`docs/EVIDENCE.md`](docs/EVIDENCE.md) states how to obtain the SHA from a running board now
  that the banner carries it, and what to do with an older capture whose banner predates the
  change: record the build timestamp and mark the SHA **inferred**, never assert it.

## [0.4.1] — 2026-08-12

**A hardening pass. Nothing in it has run on hardware.** Ten defects fixed, every one of them
reasoned from code and from the reference master and **compile-verified only** — `env:rak4631`,
`env:soak` and `env:battdiag` each build `SUCCESS` on Heliotrope Ridge, and that is the entire
extent of the verification ([`docs/EVIDENCE.md`](docs/EVIDENCE.md), 2026-08-12 night). The board
was asleep with USB detached for the whole session; no image was flashed and no serial port was
opened.

PATCH per [`docs/RELEASE.md`](docs/RELEASE.md): all ten are bug fixes, hardening and timing
corrections. No payload channel or type changed, so the TTN formatter needs no paired change, and
no new capability was added.

**This release is further from deployable than the last one, not closer.** Several of the fixes
sit on the sleep, brownout and rejoin paths — precisely the paths a compiler cannot exercise — so
the code is now *believed* correct in places where it was previously *known* wrong, and belief is
not evidence. Each fix needs its own bench observation before it may be described as working.

Status remains **`🚧 NOT YET DEPLOYED`**, and nothing here moves it:

- **No H1–H8 gate closed.** H8 has not started — **zero soak hours exist.** The harness is built
  (`scripts/soak.sh`, [`docs/SOAK.md`](docs/SOAK.md), `env:soak`); the single attempt on
  2026-08-12 never attached to the board.
- **Sleep current is still unmeasured** and cannot be measured from the pack (10 mA LSB against a
  ~1 mA question).
- **The battery-current sign is still unresolved** —
  [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md). `batt_current` remains
  `BLOCKED` in [`payload/schema.yaml`](payload/schema.yaml), and as of this release
  `scripts/preflight.sh` says so out loud instead of printing `PREFLIGHT OK` over it.

Do not read this changelog as a readiness signal. It is a list of things that were wrong.

### Changed

- **The one-wire receive timeouts carry their reasoning, and the SENDAT turnaround uses the
  named constant.** `kFirstByteTimeoutUs` and `kInterByteTimeoutUs` were bare numbers with
  half-line comments; both are now sourced against the watchdog window, the awake-time budget,
  and the 3.5-character framing idea they are modelled on. `query()` also had a bare `delay(2)`
  where it means `kTurnaroundMs` — the cited guard gap that made the pack latch in the first
  place — which was a second place for that value to drift.
- **`owscan`'s verdict no longer reads as proof that the handshake works.** The scan never
  answers a VER3 announcement, so it cannot latch a pid or observe one; "bytes arrived, the
  pack talks" has been mistaken for "the pid latched" more than once. The verdict now says so
  and points at `battdiag`.

### Fixed

- **The Modbus pre-transaction drain is bounded and feeds the watchdog.** `drain_and_settle()`
  was `while (m_serial.available())` with no ceiling. A silent slave exits immediately, but a
  babbling or stuck RS-485 driver keeps `available()` true forever, and the watchdog then resets
  the node from inside a sensor read with `WB_IO2` still HIGH — so the transceiver keeps drawing
  across the reset, on a node that has to survive months unattended. The drain now stops after
  twice the longest frame this node asks for or a quarter of the 1000 ms per-transaction budget,
  whichever comes first, feeds the watchdog on every byte, and logs when it hits the bound.

### Fixed

- **A CRC-valid all-zero RK900 reply is no longer encoded as real weather.** Any `Ok` from the
  Modbus read called `.set()` on all five registers, including `0`, so a station returning an
  empty span published `0.0` hPa, `0.0` %RH and calm wind as measurements. The battery path has
  refused the pack's equivalent all-zero record for exactly this reason; the weather path had no
  such guard. An all-zero span is now `ModbusResult::Unsampled` and contributes no fields, and a
  zero pressure register is omitted on its own even when the rest of the span is plausible.
  Genuine zeros — calm wind, due north, 0.0 °C — are unaffected. `docs/FIRMWARE_SPEC.md` §2.1
  records the rule.

### Fixed

- **A pack answering with an empty record no longer switches off the listen that would fill
  it.** `Unsampled` — a checksum-valid SENDAT reply carrying the all-zero record template —
  was counted as both "the address works" (so the provisioning ladder was skipped) and "the
  pack is silent" (so after three cycles the 20 s push listen was gated off). The reference
  reads every real value from an unsolicited push, so gating that listen off is what left the
  battery null for the rest of a deployment. `Unsampled` now counts in its own bounded
  allowance: it no longer skips provisioning unless the pack has itself confirmed a pid other
  than `0xFF`, and it keeps the push listen alive for up to six consecutive cycles before the
  driver drops to the cheap probe and retries the full ladder once every 24 cycles — the same
  ceiling silence has always had. An absent pack still cannot cost more than one expensive
  cycle in 24. (#62, #39)

### Fixed

- **The battery driver rebooted the pack on every attempt to re-latch it.** `acquire_pid()`
  opened each provisioning window with a BOOT broadcast. In the reference master
  (`forest-weather-machines` @ `efc0e3c`,
  `rak-4-5-wire/firmware/nanoc6-onewire-poll/lib/RAK-OneWire/src/onewire_master_protocol.c`)
  BOOT is sent from exactly two places — `api_init()` at :906 and `api_set_provision()` at
  :1063, which the API table exports as `.reboot` at :1076 — so it is a reboot request, not a
  provisioning retry. A pack sitting at `PID_UNKNOW` fails the fast probe every cycle, so the
  node restarted it on every full ladder and the id could never stick. BOOT is now sent at
  most once per power cycle, and only when the pack has stopped answering its assigned id; the
  re-latch itself is done by answering the pack's unprompted announcement, which is the only
  thing that assigns a pid (same file :398-474, pid = slot index + 1 at :458). Best available
  code-level explanation for the pack never returning from `0xFF`. (#62)
- **A failed re-latch no longer prints as a success, and no longer claims an address the pack
  is not listening on.** `m_pack_latched` was write-once, so after any single good latch the
  "pack still reports pid 0xFF" summary was skipped forever and every later failure logged as
  if it had worked. It is now cleared whenever an answered announcement still carries
  `provId == 0xFF`. `acquire_pid()` also used to return true — and set `m_pid = 0x01` — on the
  strength of having answered an announcement at all, so the next SENDAT went to `0x01` while
  the pack was still listening on `0xFF`, clobbering the fallback that would have reached it.
  It now returns false and leaves `m_pid` at `0xFF` until the pack itself reports another id.
  (#62)

### Fixed

- **Two small honesty fixes in the uplink path.** `Radio::send()` refused on "not joined"
  without a console line, unlike every other refusal in the function, so the most ordinary
  reason for a quiet cycle was the one that looked like nothing at all. And the
  fields-dropped log re-queried the MAC for the payload budget instead of reporting the
  figure the encoder had actually built against, so a rate change between the two calls
  would have printed a number that was never used.

### Fixed

- **A set-interval downlink delivered during a brownout hold is no longer thrown away.**
  `main.cpp` gated the command on `brownout.flash_write_allowed()`, so while the gate held
  the command was dropped with no console line, no effect in RAM, and nothing to retry —
  `take_downlink()` had already consumed the frame and the network had already drained its
  queue. Being Class A, there was no way to ask for it again either. A hold is precisely
  when an operator reaches for this command, since a longer interval is how a low node is
  nursed back. The new interval now takes effect on the next sleep and is written to flash
  on the first cycle the gate allows a write, so H3's no-flash-during-brownout rule is
  unchanged. Out-of-range values are still ignored rather than clamped. (#65)

### Fixed

- **A failed join or uplink no longer retries fifteen times faster than the fair-use floor
  allows.** `config.h` carries a `static_assert` pinning every radio build to the 900 s
  floor, but `main.cpp` replaces the sleep with `Radio::backoff_seconds()` on any join or
  send failure, and its first step was 60 s. An alternating fail-then-succeed pattern
  therefore reached roughly 67 s of airtime a day at DR0 — over twice TTN's 30 s allowance —
  through the one path the compile-time guard could not see. The first backoff step is now
  the fair-use floor itself, and a matching `static_assert` in `radio.cpp` keeps it there.
  Raising the constant rather than clamping the sleep at the call site also keeps the
  join-failure log line honest about how long the wait really is (#24).

### Fixed

- **A corrupted downlink can no longer pass itself off as a status request.**
  `0x01` set-interval was length-checked exactly (5 bytes) but `0x03` request-status accepted
  any frame of one byte or more, contrary to `docs/FIRMWARE_SPEC.md` §4 and rule 40, which
  both specify one byte. Any garbled frame whose first byte happened to be `0x03` shortened
  the sleep to the interval floor and spent an unscheduled uplink out of a 30 s/day airtime
  budget. `0x03` is now checked exactly, and a known opcode arriving with the wrong length
  says so instead of being reported as `unknown opcode 0x01` — which named the wrong cause
  and pointed whoever was reading the console at a firmware mismatch that did not exist.
  (#63, #64)

### Fixed

- **The rejoin escape no longer leaves the radio on the wrong eight channels.**
  `Radio::begin()` selects US915 sub-band 2 once and then returns early on every later call,
  so the only path that reconfigures the MAC afterwards — `lmh_reset_mac()` in the
  three-failures rejoin escape — had nothing re-applying the channel mask. If the reset
  restores the region default of 72 channels, roughly seven join attempts in eight go out on
  frequencies no TTN gateway is tuned to, which is the failure `begin()`'s own comment warns
  about arriving at the exact moment the node has already given up three times. The sub-band
  is now re-selected immediately after the reset, and a refusal is logged.

### Fixed

- **The boot counter no longer writes flash before the brownout gate exists.** `Config::begin()`
  incremented the count and, every eighth boot, called `save()` — an erase and rewrite of the
  configuration page. It runs at `main.cpp` setup before `brownout.begin()`, so nothing could
  refuse it, and the count comes due exactly when the node is resetting in a loop, which is when
  the pack is least able to carry a write. A reset storm on a low pack could therefore spend
  charge and churn the page holding the interval and the persisted brownout bit. The increment
  still happens at boot; the write is deferred to the main cycle, after the first battery reading,
  and gated on `power::Brownout::flash_write_allowed()`. A boot spent entirely under a hold writes
  on the first later cycle where the gate permits it, and the count may under-report by up to
  eight, which is what it already tolerated by design.

- **A brownout keepalive no longer runs out of frame counter and silences the node for good.**
  The stored counter is written ahead of the live one by `session::kCounterMargin` (32) uplinks,
  and H3 forbids advancing it while the brownout gate holds (#51). A hold that rests on a pack
  which has stopped answering is lifted only by a valid reading at or above the resume threshold,
  so the keepalive uplinks that keep the node reachable spend that reserve without ever being able
  to replenish it. On the 33rd, `session::counter_headroom_ok()` returned false and
  `Radio::send()` refused every uplink from then on — permanently mute, and being Class A
  therefore permanently uncommandable, with no route left for a downlink to fix it. That is the
  state `AGENTS.md` says the node must never reach, and it is the second instance of the shape
  behind #61: a safety hold disabling its own only exit.

  A keepalive the brownout gate itself armed now authorizes exactly one counter checkpoint write
  (`session::permit_counter_checkpoint()`), consumed by the next headroom check whether or not it
  was needed. Ordinary uplinks and joins still obey the gate unchanged, and a hold backed by a
  measured low voltage never arms a keepalive at all (#38), so nothing writes flash on a pack that
  is genuinely too low. The write lands about once a month at the default cadence — one per 32
  keepalives — on a cycle that has already committed to a transmit burst far larger than a page
  write, and LittleFS commits it atomically, so a supply that collapses mid-write leaves the
  previous record rather than a plausible-looking wrong one.

### Documentation

- **Every document now agrees with the evidence ledger, and the ledger is what changed today.**
  Five docs were asserting things the bench had already disproved, and each one was pointed at
  work that did not need doing:

  - **`README.md` listed "RAK9154 provisioning" as a known blocker** — "22 correct-looking
    answers, still `0xFF`". The pack was never broken. On 2026-08-12 at `b436aa9`, `battdiag`
    gave 19 of 20 cycles with live values, latched at pid `0x01` throughout, with `provId FF`
    absent from the whole capture. The blocker is deleted. The reason it survived so long is
    now recorded where it will be read: `stage3` sleeps for its whole interval, so one capture
    window holds exactly one cycle, and the pack's normal post-boot settling null was the cycle
    everyone kept catching. **Use `battdiag` for any question about the pack, never `stage3`.**
  - **`docs/decisions/ADR-0004`'s verification section said "Not yet verified on hardware"**,
    a week after `docs/EVIDENCE.md` recorded the one-wire read it asked for. It now separates
    what is verified — one good BMS frame, and both sensors on their separate buses in one
    field-image cycle at `4510763` — from what is still owed, which is the H6/H7 unplug test.
    The risk it carried was a redundant bench run.
  - **`docs/EVIDENCE.md`'s 2026-08-04 audit row claimed `Serial.end()` and
    `NRF_USBD->ENABLE = 0` were "present on the sleep path."** Those two calls were the two
    independent causes of the dead USB console, removed in `7dfc26f`, and
    `docs/FIRMWARE_SPEC.md:200` now forbids both. The log is append-only, so the row stands with
    a correction attached rather than being edited away — but it was the exact string a future
    reader would grep for when re-checking H2.
  - **`docs/HARDWARE.md` and `docs/FIRST_FLASH.md` still wired the RK900 at 4800.** This unit
    answers only at 9600 and returns zero bytes at 4800 across four sweeps
    ([ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md)). Anyone wiring from
    those docs would have debugged a bus that was silent by configuration — a failure this
    project has already paid a bench session for once.
  - **`plans/P0_HARDENED_NODE.md` said "planning + specs only, parts on order, no firmware
    in-tree"** and listed five open decisions, three of which had been closed by ADRs or by
    observation. Re-scoped as a work-package and open-decision page, with the status pointing
    at `README.md` and `docs/EVIDENCE.md`.

- **The H1–H8 gate table now distinguishes "implemented in source" from "gate closed."**
  Conflating the two is how a project talks itself into a deployment. Every gate gains a source
  column drawn from the read-only audit in `docs/reviews/2026-08-12_spec_drift.md`, and **no
  status changed**: H4 is fully implemented and still `⬜ none`, because only a measurement
  closes a gate. H2's entry now says outright that the "deep" half of deep sleep is a `delay()`
  loop rather than the chip's deepest state, and that pack telemetry cannot size it — a 10 mA
  LSB against a ~1 mA question. H8 records that **zero soak hours exist** — see below.

- **Corrected: no soak ever ran.** An earlier revision of this changelog, `README.md`,
  `AGENTS.md`, and `docs/EVIDENCE.md` all said a 24 h bench soak had **started** on 2026-08-12.
  It had not. The harness waited 180 s for `/dev/cu.usbmodem*`, never attached, and gave up;
  the log is 140 bytes and two lines, and no process was left running. **Zero soak hours
  exist and H8 has not started.**

  The mechanism is worth keeping, because it is the failure mode this whole pass was meant to
  catch: **the evidence entry was committed at 09:41:23, ninety-two seconds before the harness
  gave up at 09:42:53.** It described a launch as though it were a run, because the outcome did
  not exist yet when it was written — and three other documents then inherited the claim. An
  entry for something still in flight is not evidence, it is a prediction.

  The obvious explanation was checked and **does not apply.** `FEATURE_CONSOLE=0` looked like it
  would make "device never appeared" correct behaviour rather than a fault. But that change and
  `env:soak` both landed in `094d5f5` at 11:26:50, **1 h 44 m after the attempt ended**; at
  `f626698` the field image still compiled the console in. It was then reverted entirely in
  `636e421`, and the mechanism refutes it independently of the timeline — the core creates the
  USB task before `setup()`, so the flag could never have suppressed enumeration. The cause is
  **unestablished**, and the repo cannot establish it — nothing records what was actually
  flashed and running at 09:39. Said plainly in `docs/EVIDENCE.md` so the next reader neither
  hunts a hardware fault that may not exist nor writes it off as the console change.

  What is real and is genuine progress: the soak **harness** — `scripts/soak.sh`,
  [`docs/SOAK.md`](docs/SOAK.md), and `env:soak`, now byte-identical to `env:rak4631`. It has
  simply never produced a soak hour, and `scripts/soak.sh selftest 90` should prove it with no
  board attached before it is trusted with 24 h.

- **`docs/FIRMWARE_SPEC.md` §5 no longer states the withdrawn console rationale as fact.** It
  asserted RAK's mechanism — the serial port "**MUST NOT** be initialized" because FreeRTOS
  starts a background task that "never sleeps" — as settled contract. That is how the mistake
  reproduced: the spec is the authority an agent reads before changing sleep behaviour, and
  reading it today would re-derive the same `FEATURE_CONSOLE=0` change that was made in
  `094d5f5` and reverted in `636e421`. §5 now states what is established — the USB device task
  is created before `setup()` regardless of application code, RAK's own `MAX_SAVE` builds carry
  it too, the lever is VBUS and `detach()`, and omitting `Serial.begin()` saves nothing — and
  cites [ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md) plus the
  `CIT-RAK-NRF52-CORE` / `CIT-TINYUSB-CORE` counter-citations rather than restating the
  analysis. The `Serial.end()` / `NRF_USBD->ENABLE` prohibition in the preceding paragraph is
  untouched and still stands.

- **Corrected in this repo's own docs: `env:soak` no longer described as differing from the
  field image.** `README.md`, `AGENTS.md`, `CHANGELOG.md` and the soak-failure entry in
  `docs/EVIDENCE.md` all called it "the console-bearing twin." The two environments now build
  byte-identical, which strengthens H8 — a soak is evidence about the shipped image without
  needing an argument that one differing flag was harmless.

- **The `FIRMWARE_SPEC.md` §9 first-light list is recorded as closed** — RK900 frame, BMS
  frame, TTN uplink, and one downlink applied — with the explicit note that first light is not
  hardening and the deployment gate is unmoved. The downlink item is annotated as half-covered:
  a `0x03` status request was delivered and drained, but `take_downlink()` has never been seen
  on the console and malformed-downlink bounds checking is untested
  ([#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)).

- **Status wording across `README.md` and `AGENTS.md` updated to today's results**, which are
  materially further along than yesterday's: both sensors reading in the same cycle of the
  `rak4631` field image, network-side confirmation that uplinks have been landing at TTN the
  whole time (`dev_addr 260CE734`, gateway `3356-gateway-002`, 13–14 dB SNR, `f_cnt 1792`
  timestamped to the same second as the console's send line), and the first downlink ever
  delivered to this node. **Status stays `🚧 NOT YET DEPLOYED`** — `AGENTS.md` is explicit that
  every subsystem answering once is not a deployment, and the ≥24 h bench soak and ≥7 d field
  shadow are the gates that govern.

- **The five 2026-08-12 review reports are linked from the docs that matter** rather than left
  orphaned in `docs/reviews/`: the spec-drift audit from the gate table and from `AGENTS.md`,
  the ADR-0002 dependency analysis from both blocker lists, and the folder itself from the
  `README.md` doc index.

- No version bump. Per [`docs/RELEASE.md`](docs/RELEASE.md) a release needs a clean tree, a
  build, and evidence for anything claimed; this is a documentation correction pass and belongs
  in `[Unreleased]` until those close.

### Fixed

- **The brownout hold no longer disables the provisioning handshake that is its only exit.**
  Four unreadable pack cycles engage the hold with no voltage evidence at all, and
  `Battery::ladder_allowed()` then skipped the fallback ladder for as long as the hold stood.
  A pack that loses its latched pid answers as unprovisioned with empty records — which is an
  unreadable cycle — so the hold engaged, the ladder that re-latches the pid was switched off,
  and the valid reading that is the hold's only exit could never arrive. Observed on the bench
  for fifteen consecutive cycles at 12.07 V: a healthy pack, a node locked out by its own gate.
  In the woods there is no reflash to break that loop, and battery telemetry would have been
  gone for good. The gate now consults `Brownout::engaged_without_evidence()`, which already
  existed: a hold resting on a measured low voltage still suppresses the ladder, so the #39
  power saving is intact, while a hold resting on nothing runs the handshake that can clear it.
  The ladder stays bounded either way — after `kSilentCyclesBeforeProbeOnly` (3) unproductive
  cycles the expensive phases run only once every `kFullLadderRetryCycles` (24 in the field
  image, ~once a day at the hourly interval), so an absent pack cannot cost meaningful energy.
  ([#61](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/61),
  refs [#39](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/39))

- **The stored frame counter can no longer fall behind what was transmitted**
  ([#51](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/51)).
  Two ways in, one outcome. `session::save()` advanced `s_saved_counter_ceiling` *before*
  `write_file()`, so a failed write left the ceiling claiming headroom the file did not have;
  and the H3 brownout gate returns before the assignment, so a withheld save left the ceiling
  stale while the keepalive kept transmitting. Either way the live counter runs past the stored
  value, and a reset then restores a counter below what the network has already seen.

  The network discards a replayed frame in silence. Because uplinks are unconfirmed,
  `lmh_send()` still reports success, so `m_failures` never increments and the
  `kFailuresBeforeRejoin` escape never fires — the node reports healthy, transmits into a void,
  and recovers at roughly one frame per `kCounterMargin` resets. Days to weeks of loss with no
  fault indication anywhere.

  The ceiling is now assigned only after the write has landed, so it and the file cannot
  disagree, and `session::counter_headroom_ok()` is checked in `Radio::send()` *before* the
  frame reaches the MAC — `lmh_send()` consumes the counter and there is no putting it back.
  When the stored value cannot be advanced the uplink is refused rather than replayed: the same
  silence, without the power cost, and it clears itself the moment the pack recovers and the
  write becomes affordable. It is inert when no session is stored, because a reset then rejoins
  to a fresh address and counter and there is nothing to collide with — without that
  distinction the check would refuse the first uplink after a join whose save was withheld,
  which is the healthy case. Deliberately not counted as a send failure: the session is fine,
  the flash write is not, and a rejoin is both the most expensive thing this node can do and
  useless against it.

- **A brownout hold the node cannot lift by itself is now bounded, not permanent**
  (hike-class). `Brownout::update()` reset the keepalive clock on *any* valid reading, so a pack
  answering every cycle from inside the 960–1020 cV hysteresis band never accumulated silent
  cycles and never earned a keepalive. The hold is persisted and restored on every boot, and its
  only exit is a reading at or above `kTxResumeCentivolts` — which nothing the node does can
  cause. A solar pack hovering in that band through short winter days therefore parked the node
  permanently: mute, and being Class A, uncommandable, because a downlink can only follow an
  uplink. Recoverable only by walking out there, which is exactly what `AGENTS.md` forbids —
  "never let the pack reach a state it cannot recover from by itself."

  The keepalive is now armed for either hold the node cannot escape unaided: the no-evidence
  hold, and the in-band hold. A reading at or below `kTxInhibitCentivolts` still earns no
  keepalive at all, because that pack really is too low to spend energy on a transmission —
  [#38](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/38)
  exists because a keepalive there used to be sent anyway, and that behavior is preserved.

### Changed

- ~~**The field image no longer initializes the serial console; `env:soak` is the observable
  twin.**~~ **WITHDRAWN AND REVERTED the same day in `636e421`** — the RAK guidance quoted below
  is refuted by the Adafruit core's own source, which creates the USB task before `setup()` runs
  regardless of application code, and by RAK's own BSP, which builds `-DUSE_TINYUSB`
  unconditionally so their `MAX_SAVE` builds carry the same task. `env:rak4631` builds
  `FEATURE_CONSOLE=1` and `env:soak` is byte-identical to it. See
  [ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md). The original entry is kept
  below rather than deleted, because the reasoning that looked convincing is the useful part.
  RAK's own low-power document is unambiguous — "As we want to achieve maximum power
  savings, the Serial port **MUST NOT** be initialized… FreeRTOS is as well starting a task
  running in the background (and never sleeps), that prevents the MCU from sleeping"
  ([`Low_Power_Example.md:45`](https://github.com/RAKWireless/WisBlock/blob/master/examples/RAK4630/communications/LoRa/LoRaWAN/Low_Power_Example.md))
  — and their sketch enforces it by wrapping `Serial.begin()` itself in `#ifndef MAX_SAVE`.
  `docs/LIBRARIES.md:55` already carried the same rule from the RAK forum while `env:rak4631`
  ignored it.

  `env:rak4631` now builds `-D FEATURE_CONSOLE=0`. The obvious cost is that a deployed node
  prints nothing, so `env:soak` was added: byte-identical apart from `FEATURE_CONSOLE`, with the
  field values for sleep, radio, both sensors, the watchdog and the 1800 s cadence. Anything to
  be observed is observed there, and the two images differ in exactly one dimension so an
  observation on one is evidence about the other. Do not add a second difference between them.

  **Unmeasured.** RAK's document predicts milliamps against a budget in microamps; this node has
  never metered either configuration
  ([#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47),
  and `docs/reviews/2026-08-12_rak_reference_benchmark.md` §8). The change is made on the
  strength of the vendor's documentation, not on a reading taken here.

- **The sleep wait is a bounded `xSemaphoreTake()` rather than a `delay(1000)` loop.** RAK
  rejects `delay()` for this — "while in the `delay()` function, the task cannot receive any
  information about external events… for most scenarios the `delay` is not a good solution"
  (`Low_Power_Example.md:13`) — and both the shipped sketch and WisBlock-API-V2's
  [`api_wait_wake()`](https://github.com/beegee-tokyo/WisBlock-API-V2/blob/main/src/api_functions.cpp)
  use the semaphore instead.

  Their `portMAX_DELAY` is deliberately **not** copied. With `WDT_CONFIG_SLEEP_Pause` the
  watchdog does not count while the CPU sleeps, so an indefinite wait whose wake source fails to
  arrive means sleeping forever *with no watchdog left to recover it* — trading current for a
  hike. The timeout is the bound, and it is what makes this safe where the reference is not.
  Slices are 60 s rather than 1 s, cutting scheduler wakeups from 1800 per 1800 s cycle to 30.
  Nothing gives the semaphore yet, so today it behaves as a coarser bounded wait; the shape is
  what lets a future sensor interrupt wake the node without reopening the watchdog question.
  Also unmeasured, and the smaller of the two sleep-path changes — whether it matters at all
  depends on the core's tickless-idle behavior, which is unverified.

- **`lmh_reset_mac()` on the rejoin path.** WisBlock-API-V2 ships that call as
  [`re_init_lorawan()`](https://github.com/beegee-tokyo/WisBlock-API-V2/blob/main/src/lorawan.cpp),
  titled "Workaround for bug after NAK". We were dropping the session and rejoining on top of
  whatever MAC state produced three consecutive failures. Watchdog-recoverable, so this was
  battery and data cost rather than a hike, but cheap to close.

- **The decoder-parity gate catches three classes of drift it used to pass.** All three were
  demonstrated failing and then passing again on a restored tree:
  - **Cross-wired emits.** `_CALL_RE` matched the channel constant and then a *backreference*
    to it, so `put_u16(kChHumidity, kTyPressure, …)` did not match the pattern at all and was
    dropped before any comparison ran — the single most damaging encoder mistake was the one the
    gate was structurally blind to. Channel and type names are now captured independently, a
    mismatch is a failure, and an emit naming an undeclared constant is a failure rather than a
    silent skip.
  - **Scale errors.** The encoder's contract is that it never scales, because the decoder owns
    every divisor (`src/payload.cpp:51-53`). Nothing enforced it, so `w.pressure.value * 10`
    shipped green and arrived as a plausible wrong number. The value argument must now be a
    plain pass-through.
  - **Total payload length.** Nothing compared the bytes the encoder emits against
    `kMaxPayloadBytes`. A buffer one byte short does not fail — `put_*()` sheds the
    lowest-priority field on every uplink, which reads as a sensor fault and gets chased in the
    wrong place. The gate now reports `35 byte(s) across 9 field(s), buffer 35, DR0 floor 11`
    and fails on any disagreement.

  Also added: two schema fields sharing a decoder type must agree on `size`, `signed` and
  `divisor`. Gate 2 compares each field to the decoder independently, so a disagreement between
  *them* was unreachable — and channels 3 and 24 both use type 103 with an open question about
  the pack's temperature scaling (`src/payload.cpp:101-106`).

- **`FEATURE_CONSOLE=0` builds again.** `TinyUSBDevice.detach()` sat outside the
  `#if FEATURE_CONSOLE` guard while the matching `attach()` and the `Adafruit_TinyUSB.h` include
  sat inside it. Nothing set the flag to `0`, so the break was latent — until the field
  environment below started setting it, which would have turned it live.

- **`busscan` no longer sweeps a slave that cannot answer it**
  ([#34](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/34)).
  The scan included slave `0x6E`, the RAK9154, reading register `0x0000` when the pack
  implements `0x6000+`. On 2026-08-04 the resulting silence was read as "the pack is not driving
  the bus," which was a false negative twice over: the register was wrong, and the pack is not
  on the RS-485 line at all. [ADR-0004](docs/decisions/ADR-0004-bms-one-wire-path.md) gave it a
  dedicated one-wire line, raw Modbus at `0x6E` over that line returned zero bytes every cycle
  because the adapter does not bridge it, and the path was deleted in `b6bbf31`.

  Correcting the register would have been the wrong repair — it would have kept a probe that
  cannot succeed, on a bus the pack has never been on. The slave is removed instead, with the
  reasoning recorded at the sweep so it is not re-added. What remains is the RS-485 diagnostic
  that is actually load-bearing: it is the tool that established the RK900's real baud and
  register map for [ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md), and its
  datasheet citation no longer asserts 4800 as though the bench had not contradicted it.
  Battery bring-up stays with `env:battdiag` and `env:owscan`.

- **`m_ever_sampled` no longer advertises a configuration path that was deleted**
  ([#43](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/43)).
  Its comment claimed the flag stopped a PARAMGET/PARAMSET enable pass from repeating on every
  wake. That pass went away in `98486f0` / `e0df6af` when reply turnaround turned out to be the
  actual blocker, and `kParamPassEnabled` no longer exists in `src/`. The flag itself survives
  because it does one useful thing — it announces the first live measurement of a boot once
  instead of on every wake — but nothing configures the pack any more, and the comment now says
  so plainly rather than pointing the next reader at machinery that is gone.

- **`docs/FIRMWARE_SPEC.md` no longer contradicts the decided ADRs or the running firmware**
  ([#41](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/41)).
  Three sections had drifted, and each one sent a maintainer down a wrong path during a failure.
  §2.1 listed the RK900 at 4800 while the code runs 9600 ([ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md)),
  so debugging a silent sensor by the spec produced silence and a false "the sensor is dead."
  §2.2 named Modbus the preferred battery path and one-wire the alternate — backwards relative to
  [ADR-0004](docs/decisions/ADR-0004-bms-one-wire-path.md) and to the harness now reporting live
  values, pointing anyone tracing a battery fault at the wrong socket; the shared-RS-485
  baud-switching note is marked historical rather than live. §6 listed humidity as "104/112" as
  though the two were interchangeable, when type 104 decodes to `humidity_4`, a key absent from
  the formatter's `CHANNEL_NAMES` and therefore dropped without an error anywhere.

  `kTurnaroundMs` also gains a `bench` citation and an explicit note that the pack's datasheet
  specifies no receiver re-arm time. The 2 ms guard gap is held by measurement alone, and it is
  precisely the constant an optimiser deletes as redundant — after which battery telemetry stops
  and the symptom looks like a wiring fault. ADR-0002 (battery current sign) stays open.

- **The USB console no longer dies after the first sleep**
  ([#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)).
  This is the defect that made hardware verification impossible for most of a day. After a
  reflash the board would enumerate as `239A:8029` and the application would demonstrably keep
  running — TTN showed `f_cnt` advancing 898 → 960 during one window — while the serial port
  delivered zero bytes across repeated 60 s reads. Alive and mute is the hardest failure to
  read from the bench, because it is indistinguishable from a hang.

  Two independent causes, both in the pre-sleep path, and either one alone is sufficient:

  1. `NRF_USBD->ENABLE = 0` was written directly and nothing restored it. The core only ever
     runs the USBD enable sequence — errata 171/187/166, the `EVENTCAUSE.READY` handshake, the
     HFCLK start — from its VBUS power-event handler, which fires on a cable transition and
     never again. `Serial.begin()` re-registers the CDC interface but cannot bring the
     peripheral back, so the endpoint stayed dead for the rest of the boot.
  2. `Serial.end()` calls `TinyUSBDevice.clearConfiguration()`, which discards the whole
     configuration descriptor. The following `Serial.begin()` rebuilt it, but with no detach in
     between there was no re-enumeration, so the host kept addressing endpoints from a
     descriptor the device had thrown away.

  Both are replaced by the one reversible pair the core exposes as public API:
  `TinyUSBDevice.detach()` before sleep, `attach()` after. The descriptor is left alone.

  This also explains the intermittency. The guard is `(bool)Serial`, which reports whether a
  host has the port *open*, so the destructive path was taken only when nobody was watching —
  precisely the reflash-then-attach ordering used all day. A cable plugged in mid-deployment
  now gets a console at the next awake window, where before it got nothing until a reset.

  The peripheral now stays enabled while asleep, where the old code intended to shut it down.
  The residual draw is unmeasured in both directions — the old write left the pull-up, the USBD
  interrupt, and HFCLK all running, so it was never the documented teardown either. Measurement
  is [#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47).

- **The join backoff message now reports the real next attempt**
  ([#24](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/24)).
  It printed the radio's backoff — `next try in 60 s` — but while both sensors are silent
  `main.cpp` only reaches `ensure_joined()` on the 1st and every 8th quiet cycle, so the node
  then sat through cycles 2–7 without retrying. The backoff is the sleep *between* cycles, not
  the wait until the next attempt; the two differ by up to the heartbeat cadence. `main.cpp` now
  passes that cadence in, and the message reports the product as an upper bound, since a sensor
  recovering brings the attempt forward to the next cycle.

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

- **The brownout gate now actually reaches the battery driver**
  ([#39](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/39),
  [#46](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/46)).
  `Battery::set_brownout()` was declared, and `ladder_allowed()` read the pointer, but nothing
  ever called it — so `m_brownout` stayed null and the brownout half of #39 was compiled in and
  inert. A node that had correctly stopped transmitting to save the pack still spent roughly
  28 s of every wake cycle hunting for a pack that was not answering, which is the exact
  condition the gate exists to stop spending energy on.

  The gate is now handed over in `setup()`, after `brownout.begin()` and inside
  `FEATURE_BATTERY` — only a build that reads the pack, and can therefore lift the hold through
  `update()`, gets one, so a restored hold can never become permanent.

  An engaged gate cannot suppress a battery read. `Battery::read()` issues its direct SENDAT
  query at `kProbeId` before consulting `ladder_allowed()`, and the announcement window is
  guarded by `if (!answered_direct && full_ladder)`. So the skip drops only the 5 s
  announcement window and the 20 s push listen; the direct query — the thing that detects the
  pack coming back — is still paid for every cycle, at under half a second. A null gate still
  reads as *not* engaged, which keeps the off-target tests and any build without the power
  subsystem behaving exactly as before.

### Changed

- **The off-target test suite now compiles at `gnu++11`, the standard the device is held to**
  ([#42](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/42)).
  `env:native` built at `gnu++17` while the Arduino nRF52 core builds at `gnu++11`, so a green
  `pio test -e native` was never evidence that the firmware compiled. That is not a theoretical
  gap: a full 10/10 host pass went green on code that could not build for the board at all,
  because a struct with default member initialisers is an aggregate under C++14 and later but
  **not** under C++11, so every brace-initialised `BatteryQueryMatch` call site failed only on
  the device. The occurrence was fixed separately; this removes the trap that produced it. All
  30 host tests pass unchanged at the lower standard, so nothing was traded away for it.

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
TTN decoder, so a minor bump per SemVer and [`docs/RELEASE.md`](docs/RELEASE.md). The firmware
version is now `0.4.0`.

**Corrected 2026-08-12:** this section originally read "firmware version emitted in the uplink is
now `0.4.0`." That was never true. `src/payload.cpp` encodes nine fields and none of them is a
version; [`payload/schema.yaml`](payload/schema.yaml) lists "Firmware version reported in the
uplink" under `requires_formatter_change` with `status: open`, and `scripts/check_decoder_parity.py`
prints it as a call-out on every run. The version reaches a human through the boot serial banner
(`src/main.cpp` `LOGF("firmware : %s\n", FIRMWARE_VERSION)`) and nowhere else, so ingest cannot
tell a stale node from a current one. [`docs/RELEASE.md`](docs/RELEASE.md) §"The version must be
observable on the device" already said this correctly; the changelog did not.

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
