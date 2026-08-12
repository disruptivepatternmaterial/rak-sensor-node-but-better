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

From [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §7. **None of these can be closed by inspection.**

Two different questions get confused here, so the table separates them. **"In source"** means
the mechanism exists in `src/` and was read by a human — that is a precondition, not a pass,
and it is the weakest form of evidence this repo accepts. **"Status"** is the gate itself,
which only a measurement closes. A gate can be fully implemented and still `⬜ none`; H4 is
exactly that. The source column is from the read-only audit in
[`reviews/2026-08-12_spec_drift.md`](reviews/2026-08-12_spec_drift.md) §1, which names the
implementing lines for each gate.

| ID | Requirement | In source (2026-08-12 audit) | Evidence needed | Status |
|---|---|---|---|---|
| H1 | Hardware WDT resets a hung Modbus/BMS read | ✅ `NRF_WDT` armed at 120 s, fed on the long sensor paths. Paused across sleep by design — guards the awake path only | Induced hang → observed reset | ⬜ none |
| H2 | Deep sleep between cycles; radio sleeps | 🟡 radio + SPI + USB-detach sleep are real; the "deep" half is a `delay()` loop, not the chip's deepest state, and the code says so | Measured sleep current on battery — **and it must be a meter.** Pack telemetry cannot answer it: 10 mA LSB against a ~1 mA question (2026-08-12, `4510763`) | ⬜ none |
| H3 | Brownout: no flash thrash, no TX when low | ✅ thresholds, TX gate and flash gate all wired; the ungated session writer found by the audit was closed in `378384e` | Sag the supply → observed skip | ⬜ none |
| H4 | Bounded backoff; survives multi-day no-gateway | ✅ doubling, clamped | Gateway off ≥48 h → observed backoff | ⬜ none |
| H5 | Interval + keys survive power loss | ✅ interval and session over `InternalFS`; keys are compiled into the image, so "keys survive" is true trivially rather than by storage | Set interval, cut power, confirm retained | 🟡 partial — session (DevAddr) restore observed 2026-07-31; interval-survives-power-loss not yet isolated |
| H6 | RK900 absent → no livelock | ✅ bounded 1000 ms reply timeout; caller tolerates failure without retrying forever | Unplug sensor → cycle continues | 🟡 partial — silent-sensor bounded timeout observed 2026-07-31; needs re-confirmation with sensor connected then removed |
| H7 | BMS silent → no livelock | ✅ bounded first-byte and inter-byte timeouts, bounded provisioning window. Bounded but **long** — `acquire_pid()` measured at 45.4 s of a 50.5 s wake, inside the 120 s WDT window with less margin than it sounds | Unplug BMS data → cycle continues | ⬜ none |
| H8 | Bench soak ≥24 h, field shadow ≥7 d | n/a — a process gate; no code implements it and none can | Soak log + TTN ingest history | ⬜ none — **zero soak hours exist.** The harness and procedure are built (`scripts/soak.sh`, [`SOAK.md`](SOAK.md), `env:soak`); the one attempt, 2026-08-12 at `f626698`, never attached to the board and logged 140 bytes in 180 s. **H8 has not started** |

The [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §9 first-light list is now **closed**, and closing
it changes nothing about the status above:

| §9 item | Closed by |
|---|---|
| One good RK900 frame | 2026-08-03, `998dc26` — full five-register read at 9600 |
| One good BMS frame | 2026-08-05, `1a203d3` / `b6bbf31` — and re-confirmed 2026-08-12 at `b436aa9`, 19 of 20 `battdiag` cycles live |
| One TTN uplink | 2026-07-31 join + accepted uplink; network-side session confirmed still advancing 2026-08-12 at `f4075c0` |
| One downlink applied | 2026-08-12 — a `0x03` status request delivered and drained across one uplink. **Half the surface only**: `take_downlink()` has never been observed on the console and malformed-downlink bounds checking is untested ([#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)) |

First light is not hardening. H1–H8 above is the gate that governs deployment.

## Power budget

Projections live in [`POWER_BUDGET.md`](POWER_BUDGET.md). A projection is a hypothesis;
only a measurement recorded here closes it.

## Log

Newest first, by the date the entry was committed (`git log --format='%ad' -- docs/EVIDENCE.md`),
not by the date embedded in its heading — two 2026-08-03 entries and two 2026-07-31 entries each
span more than one commit, so heading dates alone don't disambiguate order. If you add an entry,
add it at the top.

### 2026-08-12 — H8 soak attempt FAILED to attach. Zero soak hours exist. Cause unestablished

> **This entry replaces one titled "24 h bench soak started (H8)."** That title was wrong and
> the reason is worth keeping: **it was committed at 09:41:23, ninety-two seconds before the
> harness gave up at 09:42:53.** It described a launch as though it were a run, because the
> outcome did not exist yet when it was written. Nothing was soaked. Two later documents
> repeated the claim before it was caught. **Never write an entry for a thing still in
> flight** — the ledger records observations, and an observation is not available until the
> thing has finished happening.

- **Commit:** `f626698` · **Host:** Heliotrope Ridge
- **Attempted:** 2026-08-12 09:39:53 local · **Gave up:** 09:42:53 · **Duration: 180 s, all
  of it spent waiting**
- **Measured:** whether a detached capture could hold a console across the field image's
  sleep cycles for 24 h (H8).
- **Observation** — the log is 140 bytes and two lines, and no capture process is running:

  ```
  2026-08-12 09:39:53 === CAPTURE WAITING /dev/cu.usbmodem* ===
  2026-08-12 09:42:53 === CAPTURE GAVE UP after 180s, device never appeared ===
  ```

- **Verdict: fail. Zero soak hours.** **H8 has not started, let alone closed.** No pass, no
  partial, no "in progress." The serial device never appeared, so nothing was ever captured.

**The tempting explanation is wrong, and checking it matters.** At the time this was written
`env:rak4631` built `FEATURE_CONSOLE=0` and did not call `Serial.begin()`, which looked like it
would explain "device never appeared" perfectly — the console change working as designed rather
than a fault. **It does not apply here, and it never could have.** Both
`FEATURE_CONSOLE=0` and `env:soak` landed in `094d5f5`, committed at **11:26:50** — one hour
forty-four minutes *after* this attempt ended. At `f626698` the `rak4631` image still compiled
the console in, so the board should have enumerated.

> **Doubly refuted, appended later the same day.** `FEATURE_CONSOLE=0` was itself reverted in
> `636e421` ([ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)): the Adafruit core
> calls `SerialTinyUSB.begin()` and creates the `usbd` task before `setup()` runs, so USB
> enumeration never depended on the sketch calling `Serial.begin()` at all. The flag could not
> have suppressed enumeration even had it been on the board. The timeline above already ruled
> it out; the mechanism rules it out independently. `env:soak` is now byte-identical to
> `env:rak4631`.

**So the cause is unestablished**, and this repo cannot establish it. Nothing here records
what was actually flashed and running at 09:39 — and this project has been caught by a stale
binary on the board once already (see the 2026-08-05 entry on `pio run` reusing a hex from an
older commit). The candidates, none of them checked: the board was in DFU rather than running;
a stale or absent flash; the board physically off the bus; a harness defect. **Whoever picks
this up should establish what is on the board before theorising**, and should not go hunting a
hardware fault on the strength of this entry alone.

**What actually exists, and it is real progress:** the soak *harness* — `scripts/soak.sh`,
[`SOAK.md`](SOAK.md), and `env:soak` — now byte-identical to `env:rak4631`, so a soak is
evidence about the shipped image. That is the machinery a soak needs, and it did not exist
yesterday. It has never produced a soak hour.

**Prove the harness before trusting it with 24 h.** `scripts/soak.sh selftest 90`
([`SOAK.md`](SOAK.md)) runs it with no board attached. A harness that has only ever been
started, never validated, is how 180 s of waiting got recorded as a day of soaking.

**One harness lesson did survive from the attempt:** `setsid` does not exist on macOS, so an
earlier launch died on that immediately and captured nothing. `nohup … & disown` is what works
on this host.

**Independent of the soak — the interval is 900 s, not 1800 s.** From the network's uplink
timestamps: `16:21:07Z → 16:36:14Z` is 15 min 7 s, and `16:05:59Z → 16:21:07Z` is 15 min 8 s.
This comes from TTN, not from the console, so the failed capture does not affect it. At 900 s
the node sits at the fair-use floor (`kFupFloorSeconds`, `src/config.h:62`), which is
compliant. Note that the `env:soak` comment in `platformio.ini` still describes the cadence as
1800 s.

- **Status is unchanged: `🚧 NOT YET DEPLOYED`.** H8 needs ≥24 h bench and ≥7 d field shadow.
  Neither has closed and neither has begun.

### 2026-08-12 — Sleep current is not measurable from pack telemetry. Resolution floor 10 mA against a ~1 mA question

- **Commit:** `4510763` · **Host:** Heliotrope Ridge, RAK4631 on `/dev/cu.usbmodem31101`
- **Measured:** whether the RAK9154's own current telemetry can size the `delay()`-based
  sleep at `src/power.cpp:134-136`, as a stopgap before a meter.
- **Observation:** the pack reports current with a **0.01 A (10 mA) LSB** — raw `i=-1` for
  the reported `-0.01 A`. The reading did not move across 20 consecutive `battdiag` cycles
  or in the field image, awake and transmitting:

  ```
  08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
  08:04:47 battery : raw v=1212 i=-1 soc=91 t=230
  ```

  A pack supplying a ~30 mA awake node should read about `-0.03 A`. It does not, because
  with USB attached the board is USB-powered and the pack sees almost no load — and USB is
  the only way the reading leaves the board.

- **Verdict: cannot answer — and this is a complete answer, not a partial one.**
  `docs/POWER_BUDGET.md` turns on ~1 mA; the pack resolves 10 mA. Even the documented
  defect cases (0.89–1.2 mA peripherals-enabled, ~6 mA radio-awake) fall at or under one
  LSB. **A meter is the only instrument that can settle this.** No number is recorded and
  none should be quoted from pack telemetry.
- **Not a `bench` citation for sleep current.** It is a bench citation for the *resolution
  floor*, and the citable form of it names this file and the commit so it points somewhere:
  `CITE(bench): docs/EVIDENCE.md 2026-08-12 @ 4510763 — pack current LSB = 0.01 A`.

### 2026-08-12 — Field image: both sensors in one cycle. Uplink transmitted; TTN acceptance NOT verified

- **Commit:** `4510763`
- **Host:** Heliotrope Ridge, RAK4631 on `/dev/cu.usbmodem31101`
- **Image:** `rak4631` — the actual field image. Radio in, **sleep in**. Not a diagnostic.
- **Measured:** whether both sensors read in the same image and the same cycle (never
  previously observed), and how far toward TTN acceptance the console can actually take us.
- **Observation:** one full cycle, verbatim:

  ```
  08:04:47 config  : interval 1800 s, boot #2
  08:04:47 session : restored 0x260CE734, counter 1792
  08:04:47 [cycle 1]
  08:04:47 RK900   : raw 0x0000-0x0004 = 0000 0000 00F7 024B 273C
  08:04:47 RK900   : wind 0.00 m/s @ 0 deg, 24.7 C, 58.7 %RH, 1004.4 hPa
  08:04:47 battery : pack answered at 0x01 — skipping provisioning
  08:04:47 battery : sampling confirmed — pack is reporting live values
  08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
  08:04:47 radio   : sent 35 bytes on port 2
  08:04:47 session : saved 0x260CE734, resume at 1824
  08:04:55 sleep   : 1800 s
  ```

**What this does and does not establish:**

| Claim | Status |
|---|---|
| Both sensors, one image, one cycle | **pass** — first time observed |
| Field image (`FEATURE_SLEEP=1`) runs a complete cycle | **pass** |
| Sleep reached | **pass** — `sleep : 1800 s` |
| OTAA join | **not observed.** The node restored a saved session (`restored 0x260CE734`) rather than joining. That is correct behavior and evidence the earlier join persisted, but it is not a join event |
| Uplink **accepted at TTN** | **pass** — confirmed from the network side at 09:20. See the correction below; an earlier row in this entry claimed the opposite and was wrong |
| Downlink handled | **delivered to the device** at `16:21:07Z`. Console confirmation of the handler running was not captured — see below |

**On TTN acceptance — read this before re-testing the radio path.**

**The radio path is already proven and this entry does not reopen it.** It closed twice:
2026-07-31 ([`EVIDENCE.md` §first LoRaWAN join](#2026-07-31--first-lorawan-join-and-first-uplink-accepted-by-the-things-network),
join and uplink accepted, confirmed from both sides, gateway `9181014c6051030034`) and
2026-08-04 (first end-to-end real-sensor uplink, operator-confirmed). Do not re-derive
those.

What is new and narrower: **nobody has seen *today's* image arrive.** `radio : sent 35
bytes on port 2` is the SX126x library reporting a frame handed to the transceiver — it is
transmit, not reception, and the two must not be conflated.

Measured 2026-08-12 08:38-08:40. A `ttn-lw-cli events subscribe` stream was held open for
110 s spanning a board reset, so the device booted, restored its session and uplinked
inside the window. 22 events parsed, 8 of them `as.up.data.forward`. **None were from
`puma-concolor-001`.** The uplinks that did arrive in that same window came from five other
devices on the same application:

```
"device_id": "6773a47722230004"
"device_id": "9181010k6063240022"
"device_id": "earthquake-rak-10703"
"device_id": "la666050494"
"device_id": "rak10701-plus-001"
```

Those five are the load-bearing part of this observation: they rule out the subscription,
the credentials, the CLI invocation and the network path. The stream was working. Our
device was not heard.

**CORRECTION, 2026-08-12 09:20 — the paragraphs above are wrong, and the error is
instructive enough to leave in place rather than delete.** The device was being heard the
whole time. Querying the network's own record of the device settles it:

```
$ ttn-lw-cli end-devices get my-app-tobi puma-concolor-001 --all
last_seen_at : 2026-08-12T16:05:59.401582Z
freq_plan    : US_902_928_FSB_2
--- NS session ---
 "dev_addr": "260CE734",
 "last_f_cnt_up": 1857,
 "started_at": "2026-07-31T14:33:20.636657834Z"
```

The network-side `dev_addr` is `260CE734`, byte-identical to the device console's
`session : restored 0x260CE734`. The session established on 2026-07-31 is still live, and
its uplink counter is advancing. There is no divergence and no stale session.

The uplink history makes the mistake obvious (times UTC; local is UTC-7):

```
 t= 2026-08-12T15:04:47Z f_cnt= 1792 port= 2 gw= 3356-gateway-002 snr= 13.5
 t= 2026-08-12T15:33:12Z f_cnt= 1824 port= 2 gw= 3356-gateway-002 snr= 13.5
 t= 2026-08-12T15:35:51Z f_cnt= 1856 port= 2 gw= 3356-gateway-002 snr= 13.75
 t= 2026-08-12T16:05:59Z f_cnt= 1857 port= 2 gw= 3356-gateway-002 snr= 13.75
```

`f_cnt 1792` at `15:04:47Z` is `08:04:47` local — the exact second of the console line
`08:04:47  radio : sent 35 bytes on port 2` recorded earlier in this entry. That frame was
received. So were the rest, all on gateway `3356-gateway-002` at SNR 13–14 dB, which is a
comfortable link, not a marginal one.

**Why the 110 s window saw nothing: it sat entirely inside the sleep.** The two uplinks
bracketing it are `15:35:51Z` and `16:05:59Z`; the subscription ran `15:38–15:39:50Z`.
Nothing was transmitted during it. The gap between those two uplinks is 30 min 8 s, which
is `sleep : 1800 s` plus a cycle — the node was doing exactly what it was told to.

The counter is the tell that should have caught this sooner. `1824 → 1856` is a jump of 32,
the reserved block a session restore burns on boot; `1856 → 1857` is a plain increment. So
the board did **not** reset when that capture's upload reported `SUCCESS`, and the window
was observing a sleeping node.

**The lesson worth keeping: on a node with a 1800 s duty cycle, a two-minute observation
window that sees nothing has established nothing.** The five other devices proved the
subscription worked — they did not, and could not, prove ours was silent. Absence of
evidence was recorded as evidence of absence. Query `last_seen_at` and the counter first;
they are cheap, and they are the network's own memory rather than a sample of it.

**Device identity, corrected.** `puma-concolor-001` / DevEUI `42BB96EF76E200F1` lives in
application **`my-app-tobi`**. The plausible-sounding `middle-fork-area` has **no end
devices at all** (`ttn-lw-cli end-devices list middle-fork-area` returns `[]`). Anyone
reasoning from the application name will look in the wrong place.

**Downlink: queued, not delivered.** A status-request downlink was queued successfully —

```
"f_port": 10,
"frm_payload": "Aw==",
```

`Aw==` is `0x03`, matching the firmware contract at `src/radio.cpp:398`.

**Delivered at 09:21, once the uplink question was settled.** With the frame queued, the
board was reset and uplinked at `16:21:07Z` (`f_cnt 1858`, port 2). The queue drained
across that single uplink:

```
before:  [{"f_port": 10, "frm_payload": "Aw==", ...}]
after:   []
```

That is the network confirming it handed the frame to the device in the RX window that
uplink opened — the first time anything has come *down* to this node on hardware. It
exercises the Class A ordering the earlier note described correctly: the uplink is what
creates the opportunity.

What is still missing is narrower and worth stating plainly: **the console did not capture
the handler running.** `Radio::take_downlink()` and the `0x03` branch are unverified by
observation, even though the frame reached the radio. The capture helper gives up looking
for the USB CDC device after roughly 40 s regardless of the duration it is asked for, and
after a DFU flash this board takes about two minutes to re-enumerate and run — so every
attempt stopped watching before the node woke. That is a tooling gap, not a firmware
finding. Bounds-checking of a malformed downlink is likewise untested on hardware.
[#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)
stays open for those two, narrowed from "never run on hardware" to "delivered but not
observed being handled."

**Working `ttn-lw-cli` harness**, established the hard way and worth keeping:

| Purpose | Command |
|---|---|
| List applications | `ttn-lw-cli applications list` |
| List devices | `ttn-lw-cli end-devices list my-app-tobi` |
| Live events | `ttn-lw-cli events subscribe --application-id my-app-tobi` |
| Queue downlink | `ttn-lw-cli end-devices downlink push my-app-tobi puma-concolor-001 --f-port 10 --frm-payload 03` |
| Inspect queue | `ttn-lw-cli end-devices downlink list my-app-tobi puma-concolor-001` |

Traps that cost real time here:

- **`end-devices subscribe-events` is not a subcommand.** It silently prints the help text
  and exits 0, so a redirect captures help output and looks like "no events". The event
  stream lives under `ttn-lw-cli events subscribe`.
- The downlink flags are **`--f-port` and `--frm-payload`**, not `--port`/`--payload`.
- A successful `downlink push` prints **nothing** but `INFO`/`WARN` lines. Confirm with
  `downlink list`, not by the absence of an error.
- Event JSON is pretty-printed: the key is `"name": "..."` **with a space**. A grep for
  `"name":"` matches nothing and reads as an empty stream.
- The CLI config is not at `~/.config/ttn-lw-cli`; it resolves
  `~/.ttn-lw-cli.yml` and `~/Library/Application Support/.ttn-lw-cli.yml`. The build host
  **is** authenticated — absence of the file where you first looked is not absence of auth.

- **Verdict: partial pass.** Sensors and cycle: pass. Today's uplink reaching TTN: **fail,
  one window, cause not yet isolated** — recorded as a negative observation, not as an
  unverified gate.
- **Status unchanged: `🚧 NOT YET DEPLOYED`.** H1-H8, the ≥24 h soak and the ≥7 d field
  shadow are all untouched by this run.

### 2026-08-12 — The RAK9154 pack latches and reports. It was never broken; the 1800 s cycle hid cycle 3

- **Commit:** `b436aa9`
- **Host:** Heliotrope Ridge (`130.111.32.200`), RAK4631 on `/dev/cu.usbmodem31101`
- **Image:** `battdiag` — battery only, ~10 s cycle (`FEATURE_RK900=0 FEATURE_RADIO=0
  FEATURE_SLEEP=0 FEATURE_BENCH_INTERVAL=1 FEATURE_BATTERY_FAST=1`)
- **Measured:** whether the all-zero record set that every prior session reported as a
  provisioning failure was instead the documented boot behavior — `AGENTS.md` records
  "expect ~2 null cycles after boot while the pack samples" from the 2026-08-05 success.
  `stage3` runs an 1800 s cycle, so every capture window ever taken held exactly one cycle.
- **Observation:** 20 consecutive cycles in 210 s. **19 of 20 carried live values, one
  did not, and the one was cycle 2.** The pack was latched at pid `0x01` throughout; the
  string `provId FF` does not appear anywhere in the capture.

  ```
  07:56:08 [cycle 2]
  07:56:10 battery : pack answered at 0x01 — skipping provisioning
  07:56:10 battery : raw FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 27
  07:56:10 battery : no data (all-zero records (pack not sampled), 28 bytes)
  07:56:20 [cycle 3]
  07:56:20 battery : pack answered at 0x01 — skipping provisioning
  07:56:20 battery : sampling confirmed — pack is reporting live values
  07:56:20 battery : 12.12 V  -0.01 A  91%  23.0 C
  07:56:20 battery : raw v=1212 i=-1 soc=91 t=230 (t scale UNCONFIRMED)
  ...
  07:59:26 [cycle 21]
  07:59:26 battery : pack answered at 0x01 — skipping provisioning
  07:59:26 battery : 12.12 V  -0.01 A  91%  23.0 C
  ```

  Values are consistent with the 2026-08-05 reference (12.23 V, +0.00 A, 98%, 23.0 °C) for
  a pack that has since been sitting: 12.12 V, -0.01 A, 91%, 23.0 °C, stable to the
  reported digit across all 19 cycles.

  One cycle (8) logged `no announcement — proceeding unprovisioned` and still returned live
  values, which is correct — the latched pid survives a missed announcement window.

- **Verdict: pass.** The provisioning handshake works. This closes the investigation that
  the same-day byte-for-byte comparison had already pointed at: our frame matched
  `onewire_master_protocol.c` on all five checked properties because it *was* correct, and
  the reference algorithm reproducing the pack's own `0x82` checksum was the tell. The
  remaining symptom was an observation artifact of the 1800 s cadence, not a defect.
- **Correction to the record:** prior 2026-08-12 entries describing the pack as "refusing
  to latch" are wrong. The `provId FF` announcements those entries captured are the pack's
  pre-latch announcements; no capture window was ever long enough to show what followed.
- **Does not change project status.** `🚧 NOT YET DEPLOYED` stands — H1-H8 and the ≥24 h
  soak / ≥7 d shadow are untouched by this. Temperature scale remains UNCONFIRMED
  (issue #4) and the current-sign conflict in ADR-0002 is still open.

### 2026-08-12 — Transmit timing is within async tolerance. The 11.4% overshoot is legal inter-character idle, not a stretched bit period

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`, commit **`eb7fff8`**, `owscan`.

The provisioning capture's `tx 95 bytes in 110352 us = 1161 us/byte` against 1041.7 us of ten bit
periods at 9600 raised the hypothesis that our transmit clock is 11.4% slow and the pack is
discarding our provisioning response as a framing error. **That hypothesis is disproven.**

`owscan` phase 0 measures `write()` cost at three bauds, which over-determines the model
`T = k * bit_period + F`. Cycles 2 and 3 were identical to the microsecond:

```
4800  baud  tx 64 x 0x55 : 149414 us total, 2334.59 us/byte (10 bits = 2083.33 us, excess 251.26 us)
9600  baud  tx 64 x 0x55 :  75195 us total, 1174.92 us/byte (10 bits = 1041.66 us, excess 133.26 us)
19200 baud  tx 64 x 0x55 :  38086 us total,  595.09 us/byte (10 bits =  520.83 us, excess  74.26 us)
```

Solving for `k` across all three pairs, using the library's integer-truncated bit delays
(4800 → 208 us, 9600 → 104 us, 19200 → 52 us):

| Pair | ΔT (us) | Δbit (us) | k |
|---|---|---|---|
| 4800 − 19200 | 1739.50 | 156 | **11.151** |
| 4800 − 9600 | 1159.67 | 104 | **11.151** |
| 9600 − 19200 | 579.83 | 52 | **11.150** |

`k = 11.15` on all three pairs, and back-substituting gives `F = 15.3 us` at every baud
(1174.92 − 11.15×104 = 15.3; 595.09 − 11.15×52 = 15.3; 2334.59 − 11.15×208 = 15.4). The model
fits exactly, so neither term is assumed.

**What that means.** A byte costs 11.15 bit periods, not 10. The extra whole bit period is
`beginTx()`'s own `delayMicroseconds(_tx_delay)`, spent with the line idle-HIGH *before* the start
bit is driven. The residual 0.15 spread over 11 `delayMicroseconds()` calls is ~1.4 us of call
overhead each, so the real on-wire bit period is ≈ **105.4 us against an ideal 104.17 us, an error
of +1.18%.**

+1.18% is inside the few-percent budget an asynchronous receiver has, and the accumulated error at
the stop-bit sample point is ~0.11 of a bit — the sample lands nowhere near a boundary. The
remaining excess is inter-character idle, which async framing permits without limit because the
receiver resynchronises on every start bit.

**Conclusion: the bytes we transmit are individually well-formed and the pack can receive them.
Transmit timing is not the reason the pack will not latch.** Do not "fix" `_tx_delay`; the
truncation to 104 us makes bits marginally *short*, not long.

#### Cross-read: the sibling is not authoritative for provisioning

`forest-weather-machines/rak-4-5-wire` @ `8378435` (repo HEAD `efc0e3c`):

- `firmware/nanoc6-rak9154-poll/src/main.cpp` is **0 bytes — an empty file.** The Modbus path
  (slave `0x6E`, 21 registers from `0x6000`) that `AGENTS.md` cites it as authoritative for is
  not implemented there.
- `firmware/nanoc6-onewire-poll/src/onewire_protocol.cpp` (236 lines) implements
  `send_query_probe01()`, `rx_frame()` and `parse_response()` — **and no provisioning handler at
  all.** It polls a probe that is already `0x01` and parses the reply.

So the sibling is authoritative for the *poll and parse* of an already-provisioned pack, and has
nothing to say about the announcement handshake, which is the step actually failing here.
`AGENTS.md`'s claim of protocol authority overstates what that repo contains. The only
provisioning references remain the vendored upstream `onewire_master_protocol.c` and Meshtastic,
both already cited throughout `src/sensors/battery.cpp`.

### 2026-08-12 — `WB_IO2` rail exonerated; the "no announcement" failure was a stale binary. Current HEAD answers the pack but the pack still will not latch

Two results, both on **Heliotrope Ridge**, board on `/dev/cu.usbmodem31101`, commit
**`9c35e2f`** (build host on a clean tree at that SHA; the previously-uncommitted `owscan.cpp`
accumulator fix was stashed as a duplicate of `b967008` and the branch fast-forwarded).

#### Result 1 — the switched `3V3_S` rail does NOT gate the pack. Hypothesis disproven

`owscan` phase 2b listens twice at 9600, once with `WB_IO2` driven HIGH and once driven LOW —
the state `RK900::power_off()` leaves behind. Three consecutive cycles:

```
07:33:54 rail A/B verdict: HIGH 184 byte(s), LOW 184 byte(s) — the rail does not gate the pack; look at the driver
07:34:29 rail A/B verdict: HIGH  92 byte(s), LOW 184 byte(s) — the rail does not gate the pack; look at the driver
07:35:03 rail A/B verdict: HIGH  92 byte(s), LOW 184 byte(s) — the rail does not gate the pack; look at the driver
```

With the rail off the pack still emitted the full 92-byte announcement, twice per window. The
`3V3_S` hypothesis raised in the entry below is **disproven**: no wiring change is required, and
the pack's `3V3_In` is not gated by `WB_IO2` on this harness.

#### Result 2 — `stage3` at `9c35e2f`: the driver sees and answers the announcement

The 0-byte / `no announcement` behaviour recorded below came from an **unidentified image** at
`[cycle 203]`, not from current code. Flashing `stage3` built from `9c35e2f` produced entirely
different behaviour on the same harness:

```
07:36:41 battery : turnaround 2 ms (gap 1953 us), tx 95 bytes in 110352 us = 1161 us/byte (10 bits @ 9600 = 1042 us)
07:36:41 battery : answered probe 0xFF (announced pid 0xFF) with pid 0x01
07:36:41 battery : reply FF 7E 00 55 02 01 FF 00 00 01 50 03 44 01 02 09 00 30 00 ... 01 00 47 45 00 00 00 00 52 41 4B 32 35 36 30 2D 69 6F ... 06 15 BA 08 00 16 B9 08 00 17 B8 08 00 18 67 08 00 19 F3 08 00 1A F3 08 00 7C
07:36:41 battery : probe 0xFF announces 6 sensor(s)
07:36:41 battery :   sid 0x15 ipso 186 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x16 ipso 185 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x17 ipso 184 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x18 ipso 103 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x19 ipso 243 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x1A ipso 243 rule 0x0008 (08 00) periodic
07:36:42 battery : answered 2 announcement(s) in 5079 ms — pack still reports pid 0xFF
07:36:43 battery : PROVISION announcement where a SENDAT reply was expected — the pack is announcing, not answering
07:36:45 battery : no data (all-zero records (pack not sampled), 28 bytes)
07:36:45 radio   : sent 20 bytes on port 2
07:36:45 session : saved 0x260CE734, resume at 1760
07:36:53 wait    : 1800 s (sleep disabled)
```

What this establishes:

- **Announcement detection, parsing and the response mutation all work.** The reply frame carries
  `01` in the `provId` slot (frame index 34) where the announcement carried `FF`, the addresses
  are swapped (`dest FF`, `source 00`), the flag is `01` RSP, and the checksum is recomputed
  (`7C`). The six descriptors decode correctly, every one at rule `0x0008` RULE_PERIODIC.
- **The `no announcement — proceeding unprovisioned` symptom is not present in current code.** It
  belonged to the unidentified binary. Any theory built on it — including the `3V3_S` theory
  above — was explaining a stale artifact.
- **The pack still refuses to latch.** Two answered announcements in 5079 ms, and the pack's next
  announcement still carries `provId 0xFF`. SENDAT then returns the 28-byte all-zero template.
- **The radio path works:** `radio : sent 20 bytes on port 2`, session restored from NVM.
- Measured transmit cost of the 92-byte echo: **110352 us for 95 bytes = 1161 us/byte** against a
  1042 us/byte theoretical floor at 9600 8N1. The whole provisioning response occupies the wire
  for ~110 ms.

**Not established:** why the pack will not latch. The 2026-08-05 entry records it latching pid
`0x01` at `1a203d3`. Today, on `9c35e2f`, it does not, and `SENDAT` to `0x01`/`0x02`/`0x03` draws
0 bytes while `0xFF` draws 120. Reconciliation: the 2026-08-05 latch is **historically accurate
but describes a state the pack no longer holds** — the announcement's own `provId` field is the
pack's belief about its id, and it reads `FF` on every capture today. `AGENTS.md`'s claim that the
pack "latches pid `0x01`" is therefore stale as a description of current hardware state. The
driver already handles this correctly by construction (`m_pid` starts at `0xFF` and falls back),
so this is not an addressing regression.

### 2026-08-12 — The pack talks. `owscan` draws the announcement every cycle; the production image on the same bench gets 0 bytes

Reverses the 2026-08-11 entry below on the one-wire bus. The RAK9154 is **not** silent: a
standalone `owscan` image reads its 92-byte announcement on every cycle at 9600, and draws a
SENDAT reply from dest `0xFF`. The production ladder, on the same board and the same wiring
minutes earlier, saw nothing at all.

- **Host:** Heliotrope Ridge, board on `/dev/cu.usbmodem31101`
- **Build-host tree:** `8994d02` plus the uncommitted `src/diagnostics/owscan.cpp`
  accumulator-reset fix. That working-tree diff is byte-identical to the diff local commit
  `b967008` carries (`git diff … | md5` = `8b02d1ef78ea5c232c01385e34b9933f` on both sides), so
  the image built here is the content of `b967008`.
- **Measured:** whether the pack transmits at all on `WB_IO1`, and at which baud and address.

#### Observation A — the production image already on the board, before anything was flashed

Image SHA **not established** — it was not the `owscan` image it was believed to be. It reported
`[cycle 203]`, read the RK900, and ran the battery ladder:

```
07:21:19 battery : no announcement — proceeding unprovisioned
07:21:40 battery : no data (no reply, 0 bytes)
07:21:40 wait    : 60 s (sleep disabled)
07:22:40 [cycle 203]
07:22:40 RK900   : raw 0x0000-0x0004 = 0000 0000 00F4 024C 2738
07:22:40 RK900   : wind 0.00 m/s @ 0 deg, 24.4 C, 58.8 %RH, 1004.0 hPa
```

The 21 s between the two battery lines is the non-`FAST` 20 s push listen. **Zero bytes across
the whole cycle.** RK900 reads correctly on the same cycle.

#### Observation B — `owscan`, flashed immediately afterwards, three consecutive cycles

Passive listen, transmitting nothing, 9600 baud, identical on all three cycles:

```
9600 baud  passive 3000 ms : 92 byte(s)  <- FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF 00 47 45 00 00 00 00 52 41 4B 32 35 36 30 2D
69 6F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 06 15 BA 08 00 16 B9 08 00 17 B8 08 00 18 67 08
00 19 F3 08 00 1A F3 08 00 82
```

Decoded: `hub_type 0x01` PROVISION, `payload_type 0x03` VER3, `dest 0x00` master, `source 0xFF`.
`provId` at frame index 34 reads **`FF`** — still unprovisioned. `snsr_num` = **6**, and every
descriptor carries rule **`0x0008` (RULE_PERIODIC)**: sid `0x15` ipso `0xBA` (186 DC voltage),
`0x16`/`0xB9` (185 DC current), `0x17`/`0xB8` (184 capacity), `0x18`/`0x67` (103 temperature),
`0x19` and `0x1A` both `0xF3` (243 status word). The sensors are armed, not disabled.

SENDAT probe-id sweep at 9600:

```
9600 baud  SENDAT dest 0x01 : 0 byte(s)
9600 baud  SENDAT dest 0x02 : 0 byte(s)
9600 baud  SENDAT dest 0x03 : 0 byte(s)
9600 baud  SENDAT dest 0xFF : 120 byte(s)  <- FF 7E 00 15 02 01 00 FF 09 03 10 02 15 BA 00 00
16 B9 00 00 17 B8 00 18 67 00 00 2F FF 7E 00 55 02 00 ...
```

The first frame is `flag 0x01` (RSP), `dest 0x00`, `source 0xFF`, `hub_type 0x03` SENDAT — a
genuine answer to our request — and **every record value is zero**, i.e. the
`BatteryResult::Unsampled` template. It is immediately followed in the same read by the 92-byte
announcement. BOOT drew 0 bytes at 4800/9600/19200/38400, unchanged from prior sweeps.

Phase 1 line census, cycles 2 and 3:

```
INPUT_PULLUP   idle HIGH : 334 falling edge(s), 121385 of 1732860 samples LOW
INPUT (float)  idle HIGH : 0 falling edge(s), 0 of 1734024 samples LOW
```

Open-drain behaving as expected: activity only with the pull-up engaged.

#### What this establishes, and what it does not

Established: the wire, the pin (`WB_IO1`), the baud (9600) and the pack are all good. The pack
is **unprovisioned** (`provId 0xFF`), listens only on `0xFF`, and answers with an all-zero
record template. Nothing here is a framing or checksum fault.

**Not established:** why the production image gets 0 bytes where `owscan` gets 92. The leading
hypothesis is that `owscan` never reads the RK900 and therefore never drops `WB_IO2`, whereas the
production cycle reads the RK900 first and `src/sensors/rk900.cpp` drops `WB_IO2` LOW afterwards —
which is precisely the failure `docs/HARDWARE.md` predicts if the pack's `3V3_In` sits on the
switched `3V3_S` rail rather than the always-on `VDD` pad ("the symptom would be a battery that
never replies"). **This was not tested.** The controlled test is one `owscan` cycle with `WB_IO2`
driven LOW; if the announcement disappears, the hypothesis is proven.

The operator states the pack is wired `IO1` + `VDD` + `GND` with P+/P−, which matches
`docs/HARDWARE.md` §"P0 wiring — decided" exactly. No contradiction with the documented pinout.

### 2026-08-11 — Both known-good images reflashed; both stayed silent. Firmware is exonerated on both buses

The headline result of the day, and the one that redirects the search. Two images with recorded
passes were put back on the board unchanged. Neither reproduced its own recorded result.

- **Host:** Heliotrope Ridge
- **Commits reflashed:** `998dc26` (`busscan`) and `8720dea` (`stage2`) — the exact images
  behind the 2026-08-03 RK900 pass and the 2026-08-05 battery pass recorded below.
- **Measured:** whether either recorded pass still reproduces on today's bench, with the
  firmware held constant and only the rig between then and now.
- **Observation — `998dc26`, `busscan`, three full cycles:**

  ```
  total with rail HIGH: 0 byte(s)
  total with rail LOW: 0 byte(s)
  9600/0x01 production frame: 0 byte(s)
  ```

  The 2026-08-03 pass on this same image read
  `15 byte(s) <- 01 03 0A 00 00 00 00 00 FB 01 F8 27 56 DA A1`. **Not reproduced.**

- **Observation — `8720dea`, `stage2`, full 50.5 s awake window:**

  ```
  battery : no announcement — proceeding unprovisioned
  battery : no data (no reply, 0 bytes)
  wait    : 60 s (sleep disabled)
  ```

  The 2026-08-05 pass on this same image read 22 announcements in 45.4 s plus a 28-byte
  checksum-valid SENDAT reply. **Not reproduced.**

- **Verdict:** FAIL on both buses, and **firmware is exonerated for both.** The binaries that
  produced the passes are byte-for-byte the binaries that are silent now, so no code change
  since can be the cause and no code change can be the fix.

**Why this is conclusive for the one-wire side in particular.** The pack's 2026-08-05
announcements were **unsolicited** — the pack talks first, and that depends on nothing the node
transmits. Framing, addressing, turnaround timing, `kWakeCount`, provisioning state: none of it
can suppress a message we never asked for. Silence from an unsolicited talker means nothing is
driving the wire.

**Operator-confirmed context.** The RK900 has 12 V. The pack is healthy and powers the board
when the board is off USB. The rig that passed on 2026-08-05 is recorded below
(_"Phase-0 direct probe exonerated on hardware"_) as having pack pin 1 `P+` **deliberately
unconnected with the buck out of circuit** — 12 V entered the circuit only after that capture.
That is the largest known delta between the passing rig and today's, and it is physical.

Remaining suspects on both buses are wiring, connectors, and the 12 V introduction — not
firmware. Tracked as [#49](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/49)
(RS-485) and [#50](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/50)
(one-wire).

### 2026-08-11 — One-wire dead: no edge, no byte, in any mode — and the scan reporting otherwise was lying

- **Host:** Heliotrope Ridge
- **Commit:** `8994d02`, `owscan` image. The after-fix capture additionally carries the
  accumulator reset committed alongside this entry.
- **Measured:** whether the RAK9154 drives the one-wire line at all — idle level and
  falling-edge census with no UART and no framing, passive listen at five bauds, BOOT to dest
  `0xFF` at five bauds, and SENDAT to dest `0x01`/`0x02`/`0x03`/`0xFF` at 9600.
- **Observation — the edge census, which needs no protocol to be right:**

  ```
  INPUT_PULLUP idle HIGH : 0 falling edge(s), 0 of 1735294 samples LOW
  INPUT (float) idle HIGH : 0 falling edge(s), 0 of 1734024 samples LOW
  ```

  0 bytes on passive listen at all five bauds. 0 bytes from BOOT dest `0xFF` at all five
  bauds. 0 bytes from SENDAT dest `0x01`/`0x02`/`0x03`/`0xFF` at 9600. Verdict line:

  ```
  0 pulled-up edge(s), 0 floating edge(s), 0 byte(s) total
  ```

- **Verdict:** FAIL. Nothing pulled the line low across ~1.73 M samples in either pin mode,
  so the pack is not driving the wire — this is below the level where framing or addressing
  could matter. Tracked as [#50](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/50).

#### The diagnostic lied first, so every earlier `owscan` verdict inherits the doubt

`owscan`'s four verdict accumulators — `ow_edges_pulled`, `ow_edges_float`, `ow_bytes`,
`ow_best_baud` — are file scope and were never cleared between sweeps, while `main.cpp` calls
the scan once per cycle. Two bytes captured in some early cycle therefore latched their verdict
permanently.

**Before the fix**, at 20:47:32, 20:48:00 and 20:48:28, the scan printed a screen of zeros in
every phase and then concluded:

```
2 byte(s) total
bytes arrived. The pack talks, so the fault is framing or addressing, not the wire
```

and pinned phase 4 to `19200 baud (first baud that answered)` when 19200 had returned nothing.

**After the fix**, at 20:52:40, the same wire and the same board:

```
0 byte(s) total
nothing ever pulled this line low… the pack is not driving the wire at all
```

This is recorded as evidence, not as a changelog line, because it invalidates readings: **any
`owscan` verdict captured before 20:52:40 today reports the union of every cycle since boot,
not the cycle printed above it.** The failure mode is the precise one the scan exists to
prevent — it sent the reader after a framing constant while the wire was dead. `bus_scan()` was
never exposed to this; it totals in locals.

### 2026-08-11 — RS-485 dead on bench: busscan 0 bytes powered and unpowered

- **Commit on the board:** `e2c7088` (`busscan` image, flashed same day)
- **Host:** Heliotrope Ridge
- **Measured:** Modbus FC `0x03` sweep at 4800/9600/19200/38400/115200, slaves `0x01`–
  `0x03`, with WB_IO2 HIGH then LOW; plus the 9600/0x01 five-register production frame.
- **Observation** (two independent captures, same verdict):

  ```
  [bus scan] total with rail HIGH: 0 byte(s)
  [bus scan] total with rail LOW: 0 byte(s)
  [bus scan] verdict: 0 byte(s) powered vs 0 unpowered; 9600/0x01 production frame: 0 byte(s)
  [bus scan] the line is dead in both states. Nothing the firmware controls
             can change that — check 12 V at the RK900 and the A/B pair.
  ```

- **Verdict:** FAIL for a live RK900 path. Baud, slave ID, and IO2 control are ruled out
  (all rates/slaves silent; HIGH≡LOW). Remaining: pack pin 1 `P+` → RK900 12 V, and A/B on
  the RAK5802. Tracked as [#49](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/49).
- **Notes:** Board left on `busscan` for the re-check after wiring. Field image was
  `0.4.0` / Aug 5 before this flash; restore `rak4631` only after `busscan` shows a
  non-zero production frame.

### 2026-08-05 — Board recovered via UF2; "off the USB bus" turns out to be the sleep detach, not a fault

**Host:** Heliotrope Ridge. **Commit on the board:** `7dfc26f` (see the stale-binary note below —
**not** `bf5ceb2`, which is what was intended). **Verification still incomplete.**

Two operational findings, both of which cost time today and neither of which is a code defect.

**1. Serial DFU can fail while the bootloader is healthy; UF2 mass storage works.** With the
board in the UF2 bootloader (`239A:0029`), the port present at `/dev/cu.usbmodem1101`, and
`lsof` showing nothing holding it, `pio run -t upload` failed twice with *"No data received on
serial port. Not able to proceed."* The UF2 route succeeded on the first try. The non-obvious
step is that **macOS left the bootloader's drive unmounted** — `diskutil list` showed
`/dev/disk6` as `RAK4631`, 33.7 MB, with no `/Volumes` entry until `diskutil mount disk6`. That
absence is what made the UF2 path look unavailable. Procedure now in
[`FIRST_FLASH.md`](FIRST_FLASH.md).

Converted with the framework's own tool, `uf2conv.py -c -f 0xADA52840`, which reported
`start address: 0x26000` — the application offset above the SoftDevice. Copy, then the
bootloader flashes and resets itself; the application was up about 12 s later.

**2. A sleeping node has no USB device at all, and that is the fix working.** This reading was
gotten wrong twice in one session, so it is written down. After the flash the board enumerated,
uplinked, and then vanished completely from the bus: no `239A:*`, no `/dev/cu.usbmodem*`, no
`RAK4631` disk. That looks identical to the dead-board state, and `flash.sh` says so in as many
words — *"no 239A device on the bus at all"* → *"THE BOARD HAS NO VALID APPLICATION."*

It was asleep. `TinyUSBDevice.detach()` releases the D+ pull-up, so the host sees the device
removed, exactly as intended. TTN settled it: `last_f_cnt_up` **1024**, session `updated_at`
`2026-08-05T15:15:56Z`, against a build-host clock reading `08:17:13 PDT` — the node had uplinked
**77 seconds** before the bus was declared empty. `dev_addr 260CE734`,
`last_n_f_cnt_down`/`last_a_f_cnt_down` both 26.

The lesson is procedural: **USB presence is not a liveness test on a build that sleeps.** TTN
session state is, and it costs one command.

**Stale binary — what is actually on the board.** `pio run -e rak4631` reported `SUCCESS` in
1.0 s and reused a `firmware.hex` built from `7dfc26f`, even though `git diff 7dfc26f bf5ceb2`
shows `src/sensors/battery.cpp` and `src/sensors/battery.h` changed and both compile into that
environment. The UF2 was converted from that stale hex, so **the board is running `7dfc26f`,
not `bf5ceb2`.** A later `rm -rf .pio/build/rak4631 && pio run` produced a fresh hex which has
not been flashed. `7dfc26f` does contain the CDC fix, so the pending verification is still
meaningful, but no result from this board may be attributed to `bf5ceb2`.

**What is still not proven.** Everything the session set out to prove:

- **CDC across sleep — not yet.** The detach half is demonstrated (the device disappears on
  schedule). The half that matters, `attach()` restoring a working console *after* a sleep, needs
  the wake. Stored interval is 1800 s, so the wake was due at `08:45:56` host time, past the
  session budget. A capture armed on the build host at `/tmp/wakecap.log` waits for the port to
  reappear and then records 240 s of console. Neither branch of the `(bool)Serial` guard has been
  exercised on hardware yet.
- **900 s interval** — not sent. **Persistence across reset** — not tested. **`v0.4.0`** — not
  tagged, correctly, because none of the legs closed.

[#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40) stays
open on both halves.

### 2026-08-05 — USB CDC death root-caused in source; verification blocked, board off the bus

**Host:** Heliotrope Ridge. **Commit built:** `7dfc26f`. **Commit previously on the board:** `406df01`.

Not a measurement entry — a state entry, so the next session does not misread the board.

**What was established, and how.** The dead-console fault behind [#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)
was root-caused by reading the core source rather than by instrumenting hardware, and it turned
out to be **two independent defects** in the pre-sleep path, either sufficient alone:

1. `NRF_USBD->ENABLE = 0` with nothing to restore it. In
   `Adafruit_TinyUSB_Arduino/src/portable/nordic/nrf5x/dcd_nrf5x.c`, `tusb_hal_nrf_power_event()`
   is the only writer of `ENABLE = 1`, and only on `USB_EVT_DETECTED` — a VBUS transition. It
   also runs the errata 171/187/166 workarounds, waits on `EVENTCAUSE.READY`, and starts HFCLK.
   `Serial.begin()` reaches none of that.
2. `Serial.end()` → `Adafruit_USBD_CDC::end()` → `TinyUSBDevice.clearConfiguration()`, which
   discards the configuration descriptor. `Serial.begin()` rebuilds it, but with no detach in
   between the host never re-enumerates and keeps addressing endpoints from a descriptor the
   device threw away.

Both guarded on `(bool)Serial`, which reports whether the host has the port *open* — so the
destructive path ran only when no monitor was attached. That is the intermittency, and it fits
the primary observation exactly: the application ran (TTN `f_cnt` 898 → 960) while serial
delivered zero bytes across repeated 60 s reads. Nothing was hung; the console had been
dismantled during the first sleep.

Fixed in `7dfc26f` with `TinyUSBDevice.detach()` / `attach()`.

**This is source-reading, not bench evidence.** Under the rules at the top of this file it does
not close anything. #40 stays open.

**Board state — read this before capturing anything.** `scripts/flash.sh --yes -e rak4631`
compiled `7dfc26f` cleanly in 24 s, then the DFU upload failed with *"No data received on serial
port. Not able to proceed."* The board is now absent from the build host's USB bus in **every**
mode: no `239A:*` device in `system_profiler SPUSBDataType`, and `/dev/cu.usbmodem*` does not
match. It has no valid application and is not in DFU either.

Recovery needs physical access: **double-tap RESET on the RAK19007**, then re-run
`scripts/flash.sh`. See [`docs/FIRST_FLASH.md`](FIRST_FLASH.md).

Nothing is running on it, so there is no Fair Use exposure while it sits, and no
sleep-disabled radio-on build was left on it.

**Still unproven, all three blocked on the same hardware step:** two sleep cycles with the
console still alive afterwards; the 900 s interval floor accepting `01 00 00 03 84` on port 10;
and interval persistence across a reset. No `v0.4.0` tag — the release is not evidenced.

### 2026-08-05 — Stage 3: the RAK9154 reads. 12.23 V over one-wire, seven consecutive cycles

The pack reports live telemetry. This is the first non-null battery reading this project has
ever taken, and it closes the Step 1 hold point that the entry below left open: `SENDAT Ok`
from dest `0x01` carrying a non-zero voltage.

- **Host:** Heliotrope Ridge (`ntableman@192.168.10.223`) · RAK4631 `239A:8029`, port
  `/dev/cu.usbmodem1101`.
- **Commit:** `1a203d3`. **Image:** `battdiag` — battery only, no RK900, no radio, no sleep;
  a 10 s cycle in place of `stage2`'s 110.5 s.
- **Wiring:** unchanged from the entry below. Pin 1 `P+` (12 V) still deliberately unconnected.

**The reading, stable across seven consecutive cycles:**

```
12.23 V, +0.00 A, 98%, 23.0 °C
```

**Cycle-2 frame**, a genuine reply rather than a truncated announcement:

```
FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 27
```

RUI3 length `0x15` = 21, type `02` SENSORHUB, flag `01` RSP, dest `00`, **source `0x01` — not
`0xFF`**, hub_type `0x03` SENDAT, four records at sids `0x15`–`0x18`.

**The pack latched its provisioning id.** Cycle 1 answered one announcement at 3031 ms; from
cycle 2 onward the log reads `pack answered at 0x01 — skipping provisioning`. The id survives
the cycle boundary, so the phase-0 direct probe is now the path that runs in the steady state
and `acquire_pid()` is not entered at all.

#### Root cause: reply turnaround timing, not frame construction

Our reply bytes always matched the RAK reference field for field — that was independently
confirmed before this run and is not what changed. What changed is *when* they went out. Our
early-exit drain transmitted under one bit time after the pack's stop bit; the reference cannot
reply sooner than about 2 ms, because its drain loop is
`while (available()) { read(); delay(2); }` and the last iteration always pays that delay. On an
open-drain line the pack has just finished driving, answering that early appears to beat its
receiver re-arming.

Two changes landed together and their individual contributions are **not** separated:

- a **2 ms guard gap** before the first response byte (`kTurnaroundMs`), and
- **`kWakeCount` restored from 1 to 4.**

5 ms and 10 ms turnarounds were swept but never needed.

#### Sampling lags the latch by about two cycles

Cycles 1–2 returned the all-zero record template and the `Unsampled` guard reported **no data**
rather than a fabricated 0.00 V. That is expected startup behaviour, not a fault: the id latches
before the pack has sampled, and the guard is doing exactly what the null policy requires in the
window between. Anyone reading a fresh boot log should expect two null cycles before the first
number.

#### Negative result: raw Modbus does not bridge

A raw Modbus RTU read at slave `0x6E` on the same one-wire line — request
`6E 03 60 00 00 15 93 5A`, the register map the deployed sibling node uses over its own RS-485
harness — returned **0 bytes on every cycle**. The one-wire peer is a Generic Probe IO adapter
that speaks SensorHub northbound and Modbus southbound to the BMS; it does not forward a Modbus
frame arriving from the north. Settled, and the path is deleted rather than carried. Do not
re-attempt it.

#### What this does not prove

**No H1–H8 gate closes here, and the project status stays `🚧 NOT YET DEPLOYED`.** One good
frame is not a soak: H8 still requires ≥24 h on the bench and ≥7 d of field shadow, and H7
(BMS silent → no livelock) has not been exercised. **ADR-0002 stays open** — `+0.00 A` at rest
settles no sign convention, and a resting pack is precisely the reading that cannot.

Two High findings are open against this path and are deliberately not fixed in this entry's
commits:
[#36](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/36) (the
SENDAT response is not matched to the query — flag, dest, source and sequence go unverified) and
[#37](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/37) (a
partial record set can return `Ok` carrying stale values from a previous read).

#### Re-verified after the cleanup — `b6bbf31`, same host, same day

The commit that deleted the raw-Modbus path, corrected the provisioning comments and added the
success-path hex dump was reflashed and recaptured. **No regression:** five consecutive cycles,
`pack answered at 0x01 — skipping provisioning` on every one, and the reading unchanged at
`12.23 V, +0.00 A, 98%, 23.0 °C`. The frame is now on record from the driver itself rather than
reconstructed:

```
battery : sendat FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA C7 04 16 B9 00 00 17 B8 62 18 67 E6 00 35
```

**The temperature scale is confirmed, and it was the risky one.** The raw-integer log added in
this commit reads:

```
battery : raw v=1223 i=0 soc=98 t=230
```

`t=230` at a decoded 23.0 °C means the pack reports **tenths of a degree**, so passing the value
unscaled to Cayenne type 103 and letting the decoder divide by 10 is correct. Had it read `23`,
every temperature this node has ever shipped would have been 10× low. That was inferred before
this capture and is measured now. Voltage is likewise hundredths (`1223` → 12.23 V) and charge is
whole percent.

**Verdict: PASS on battery telemetry over one-wire. PASS on the temperature scale (tenths).
Inconclusive on the current sign. Fail-safe behaviour (null, not zero) confirmed on the
unsampled cycles.**

### 2026-08-05 — Phase-0 direct probe exonerated on hardware; `acquire_pid()` measured at 45.4 s of a 50.5 s wake

First capture of the production battery path on a board that is actually on USB since the
phase-0 direct-`0x01` probe landed. It answers the question the previous entry could not:
the probe is not what was costing us the pack's reply.

- **Host:** Heliotrope Ridge (`ntableman@192.168.10.223`) · RAK4631 `239A:8029` (application
  running, not DFU), port `/dev/cu.usbmodem1101`.
- **Commit:** `8720dea`. **Image:** `stage2` — both sensors, no radio, no sleep, 60 s bench
  cadence.
- **Wiring** (operator-confirmed): RAK9154 socket B pins 3+5 joined → `IO1`; pin 4 `3V3_In` →
  always-on `VDD`; pin 2 `P−` → base-board GND; **pin 1 `P+` (12 V) deliberately unconnected**
  — the board is USB-powered with the buck out of circuit.

**Cycle timing, measured.** Cycle period **110.5 s** = **50.5 s awake** + 60 s wait. Cycle 6
began at t=45.3 s and finished at t=95.8 s; cycle 7 began at t=155.8 s.

| Phase | Duration |
|---|---|
| RK900 read (three timeouts) | 3.1 s |
| Phase 0 — direct `0x01` probe | ~0.5 s |
| `acquire_pid()` | **45.4 s** |
| Phase 2 query + push listen | ~1.7 s |

**The pack answers.** A 28-byte checksum-valid SENDAT reply, addressed from dest `0xFF`:

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30 00 00 00 00 00 00 00 00 00 00
battery : no data (all-zero records (pack not sampled), 28 bytes)
```

That is `BatteryResult::Unsampled` — **correctly discarded rather than encoded as a
fabricated 0.00 V**, which is what `AGENTS.md` requires of a null.

**Provisioning still refused.** `battery : answered 22 announcement(s) in 45382 ms — pack
still reports pid 0xFF`. The pack identifies as `RAK2560-io` and announces **six sensors,
sids `0x15`–`0x1A`, every one at rule `0x0008` (periodic)** — unchanged from 2026-08-04.
`pack answered at 0x01 — skipping provisioning` never appeared, which is correct: the pack
does not hold `0x01`.

**RK900 timed out on every cycle** (`modbus attempt 1/3..3/3 failed (timeout)`). Expected, not
a fault — with pin 1 unconnected the RK900 has no supply, and it is physically disconnected
besides.

#### What this proves

1. **The phase-0 direct-`0x01` probe added in `05847bd` is exonerated.** It probes `0x01`,
   draws nothing, falls through to `acquire_pid()`, and phase 2 still gets its reply from
   `0xFF`. It leaves no stale bytes on the line and does not consume the pack's reply window.
   Cost is ~0.5 s per cycle, as designed. This matters because the prior field-firmware
   capture showed `no reply, 0 bytes` and phase 0 was the prime suspect.
2. **Nothing physical has regressed.** This capture reproduces the 2026-08-04 `owscan` result
   inline — the pack drives the wire at 9600, identifies itself, and returns a valid frame —
   so the harness and the byte layer are confirmed good on a USB-powered board with the buck
   removed. `owscan` was therefore not reflashed.
3. **`acquire_pid()` is 90 % of the awake time** — 45.4 s of 50.5 s. That is now a *measured*
   number rather than an inferred one, and it is the quantified case for the direct probe:
   awake time collapses to roughly 5 s the moment provisioning latches and phase 0 starts
   hitting. Until then the node cannot meet the sub-5 s awake target in [`DEPLOY.md`](DEPLOY.md).

#### What this does not prove

**No H1–H8 release gate closes here.** In particular the Step 1 hold point is still open: it
requires `SENDAT Ok` from dest **`0x01`** with a **non-zero voltage**, and this capture
produced `Unsampled` from `0xFF`. Removal of `acquire_pid()` therefore stays held.

**The remaining blocker is a host-side protocol defect in `acquire_pid()`, and it is ours to
fix.** On this link the RAK4631 is the **host/master** and the pack is the **slave**: the pack
announces at `provId = 0xFF` and waits for the host to assign it an id. We answer with `0x01`
twenty-two times and it never latches, so every record stays the unsampled template. The prime
suspects are our reply frame and our handshake sequence. **The root cause is not diagnosed** —
this entry deliberately does not name one. Tracked in
[#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

> **Diagnosed 2026-08-05 on `1a203d3` — see the entry at the top of this log.** The defect was
> not in the reply frame or the handshake sequence, both of which were correct. It was **reply
> turnaround timing**: we answered under one bit time after the pack's stop bit, where the
> reference cannot answer sooner than ~2 ms. The paragraph above is left standing because "not
> diagnosed" was the honest verdict at the time and the suspect list it named is what a later
> reader needs in order to see why the timing hypothesis took so long to surface — the bytes
> were right, so nobody was looking at the clock.

#### Retraction — a fabricated external blocker

Until this entry, this ledger and [`DEPLOY.md`](DEPLOY.md) recorded that the pack had to be
provisioned out-of-band through RAK's **WisToolBox mobile app over NFC/BLE**, and that firmware
could not do it. **That claim was fabricated and is withdrawn.** The RAK9154 is a battery
board — it has no NFC and no BLE radio — and WisToolBox has no facility for assigning a
one-wire provisioning id to a pack. Assigning that id is the host firmware's job and always
was.

The correction is recorded here rather than quietly deleted because of what the false claim
cost: it reclassified a fixable firmware defect as an external, operator-actionable step, which
is the one kind of error that stops work outright. A false blocker in the evidence log is worse
than no entry at all. Every measured observation above and in the entries below stands; only
the attributed cause changes.

**Verdict: PASS on "phase 0 is harmless and the pack still talks." Inconclusive on battery
telemetry, by design — the pack is unprovisioned.**

### 2026-08-04 — Debt removal and H1–H8 audit: what the build proves, and what it cannot

**Host:** Heliotrope Ridge (`ntableman@192.168.10.223`). **Commits:** `05847bd` … `98486f0`
and the diagnostics extraction that follows it.

**This entry records build-verified and source-verified facts only.** No board was on USB
for any of it — `ioreg -p IOUSB` on the build host listed two Apple hubs and a SuperDrive and
nothing else. Nothing below is a hardware measurement, and none of it closes a release gate.

#### Build-verified

| Environment | Result | Flash |
|---|---|---|
| `rak4631` (field) | BUILD OK | 200 288 B (24.6 %) |
| `stage1` | BUILD OK | 99 464 B (12.2 %) |
| `stage2` | BUILD OK | 107 516 B (13.2 %) |
| `stage3` | BUILD OK | 199 840 B (24.5 %) |
| `busscan` | BUILD OK | 109 604 B (13.4 %) |
| `owscan` | BUILD OK | 113 604 B (13.9 %) |

Two measurements worth keeping:

- **Field-image RAM fell by exactly 12 bytes** (24 980 → 24 968) after four write-only
  members were deleted — `m_assigned_pid` (1), `m_sids[8]` (8), `m_sid_count` (1),
  `m_enable_attempts` (1), plus one byte of padding. The removal is confirmed to have taken
  effect and to have touched nothing else's layout.
- **`nm -C` finds zero `diagnostics::` symbols in `rak4631` and `stage3`**, and finds them in
  `owscan`. The extracted scanners are genuinely absent from the field images rather than
  merely unreferenced.

This is deliberately *not* claimed as a byte-identical refactor. The three removed feature
flags defaulted OFF and were compiled out, so those carry no behavior change by construction
— but the four members were live stores, so the binary legitimately differs. What holds is
narrower and checkable: nothing reads them.

#### H1–H8 audit — source-verified only

| Gate | Source state | Still needs hardware |
|---|---|---|
| H1 watchdog 120 s | `watchdog_begin(120)`; feeds now inside `acquire_pid()` and `receive()`, which previously ran unfed for up to 45 s and 20 s | Measured worst-case awake time across a real cycle |
| H2 sleep | `SPI_LORA.end()`, `Serial.end()`, `NRF_USBD->ENABLE = 0` all present on the sleep path — **superseded, see the correction below** | **Sleep current with a meter** (issue #8) — the number the power budget rests on |
| H3 brownout | `power::Brownout` instantiated and wired: `update()` from the pack voltage, `transmit_allowed()` gates TX, `flash_write_allowed()` gates the flash write. Thresholds 9.60 V stop / 10.20 V resume | Behavior through a real low-voltage excursion |
| H4 backoff | `radio.backoff_seconds()` replaces the normal interval after any join or send failure | — implemented |
| H5 session persist | `session.cpp` writes through `Adafruit_LittleFS` to `InternalFS` | **Real join → reset → rejoin** (issue #12) |
| H6/H7 no livelock | Sensors read sequentially and independently; neither read gates the other; watchdog fed between them | **Physically unplug each sensor mid-cycle** — [ADR-0004](decisions/ADR-0004-bms-one-wire-path.md) requires the bench test, and a code audit is explicitly not sufficient |
| H8 soak | — | 24 h bench, then 7 d field shadow |

**Verdict: no gate closes here.** H4 is implemented and H1's known feeding gap is fixed in
source; everything else is a code reading, which is the weakest form of evidence this repo
accepts and is not what `FIRMWARE_SPEC.md` §7 asks for.

> **Correction, appended 2026-08-12.** The H2 row above is no longer true of the firmware and
> must not be used to re-check H2. `Serial.end()` and `NRF_USBD->ENABLE = 0` were the **two
> independent causes of the dead USB console** root-caused the next day (see the 2026-08-05
> entry "USB CDC death root-caused in source"). They were removed in `7dfc26f` and replaced
> with `TinyUSBDevice.detach()` / `attach()`, and `FIRMWARE_SPEC.md:200` now **forbids both**.
> Neither appears on the sleep path in `src/power.cpp` today — confirmed by the read-only
> audit in [`reviews/2026-08-12_spec_drift.md`](reviews/2026-08-12_spec_drift.md) §3.4.
> Only `SPI_LORA.end()` survives from that row. The log is append-only, so the row stands as
> written with this note attached rather than being edited away.

#### Blocked, and on what

1. **No RAK4631 on USB at the build host.** Blocks every measurement above.
2. **The RAK9154 pack is not provisioned.** ~~Provisioning is a WisToolBox NFC/BLE session on a
   phone — see [`DEPLOY.md`](DEPLOY.md) — and firmware cannot perform it.~~ **Retracted
   2026-08-05: that attribution was fabricated.** The host firmware assigns the id over the
   one-wire link in `acquire_pid()`; it is not latching, and that is an undiagnosed host-side
   protocol defect. The observation stands: an unprovisioned pack answers only `0xFF`, which is
   the fallback, not the direct-`0x01` path added in `05847bd`.

Until both close, the `acquire_pid()` removal stays held. Deleting the only working
provisioning path before its replacement has answered a real pack once is how a node reaches
the woods with no battery telemetry and no way back.

### 2026-08-04 — RAK9154 one-wire PROVEN ALIVE: pack drives the line, identifies as "RAK2560-io", answers SENDAT at dest 0xFF

The one-wire scanner (`owscan`) settled the question the driver could not: **the pack talks.**
It supersedes the "narrowed to physical" entry below — the wire was fine all along; the
production driver was addressing the wrong destination.

- **Commit:** `3d3425df5b5acee8b4999d3972e148a14092890e`, `owscan` image
  (`FEATURE_ONEWIRE_SCAN=1`, RK900/battery/radio/sleep all off).
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029`.
- **Wiring** (metered good the same day): RAK9154 5-pin socket B — pins 3+5 joined → `IO1`
  pad; pin 4 `3V3_In` → `VDD` (3.3 V confirmed); pin 2 `P−` common ground; pack output 11.6 V.

**Phase 1 — falling-edge census on P0.17, no UART, no framing:**

```
INPUT_PULLUP   idle HIGH : 334 falling edge(s), 126321 of 1805615 samples LOW
INPUT (float)  idle HIGH :   0 falling edge(s),      0 of 1811089 samples LOW
```

With the pull-up engaged an undriven open-drain line cannot produce a falling edge, and the
floating control produced exactly zero. **Something is actively pulling the line low.**

**Phase 2 — passive listen at 9600, transmitting nothing, repeatable every cycle:**

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30 00 ... FF 00 47 45 00 00 00 00
52 41 4B 32 35 36 30 2D 69 6F 00 ...
```

`52 41 4B 32 35 36 30 2D 69 6F` is ASCII **"RAK2560-io"**. Header decodes as length `0x55`,
RUI3 type `02`, flag `00`, then SNHub `dest=0x00` (master), `source=0xFF` (unprovisioned),
`hub_type=0x01` (PROVISION), `payload_length=0x50`, `payload_type=0x03`. **The pack
announces itself to the master unprompted.** 4800/19200/38400 returned garbage
(`EF F8 08…`, `FE F8 06 66…`, `F8 80 78…`) — the same signal sampled at the wrong rate,
which independently confirms **9600** is correct.

**Phase 4 — SENDAT probe-id sweep at 9600 (the finding that explains the production bug):**

```
SENDAT dest 0x01 : 0 byte(s)
SENDAT dest 0x02 : 0 byte(s)
SENDAT dest 0x03 : 0 byte(s)
SENDAT dest 0xFF : 64 byte(s)  <- FF 7E 00 15 02 01 00 FF 12 03 10 02
                                  15 BA 00 00  16 B9 00 00  17 B8 00  18 67 00 00  2F ...
```

Reply header: length `0x15`, type `02`, **flag `01`** (response), `dest=0x00`, `source=0xFF`,
`seq=0x12`, `hub_type=0x03` (SENDAT), `payload_length=0x10`, `payload_type=0x02`. The 16
payload bytes are IPSO records carrying a leading sensor-id byte:

| Bytes | Sensor id | IPSO type | Meaning | Value |
|---|---|---|---|---|
| `15 BA 00 00` | `0x15` | `0xBA` = 186 | DC voltage | `00 00` |
| `16 B9 00 00` | `0x16` | `0xB9` = 185 | DC current | `00 00` |
| `17 B8 00` | `0x17` | `0xB8` = 184 | capacity / SoC | `00` |
| `18 67 00 00` | `0x18` | `0x67` = 103 | temperature | `00 00` |

The sequence byte increments across cycles (`0x12`, `0x1B`, `0x24`), so this is live traffic,
not a replay.

**Checksum algorithm confirmed against the capture (desk verification, 2026-08-04).** Running
`cal_chksum()` by hand over the `seq=0x1B` reply — `popcount(type=0x02) + popcount(flag=0x01)`
plus the popcount of all 21 bytes the length field covers — sums to **49 = `0x31`**, which is
exactly the trailing byte captured. The driver's `frame_chksum()` therefore implements the
reference algorithm correctly, verifies **with the response flag `0x01`** as well as `0x00`,
and the frame is genuine rather than mis-framed. The all-zero values are the pack's own
content, not a decode error.

**Phase 3 — our BOOT/provision broadcast drew 0 bytes at every baud.** Reading the reference
settles this as **expected, not a fault**: `protocol_list[SNHUB_TYPE_PROVISION]` defines only
`.req` and leaves `.rsp` NULL, and `snhub_provision_command()` has no code path awaiting a
reply. BOOT is a "re-announce yourselves" nudge that nothing acknowledges. The 0-byte result
is therefore not evidence of a malformed BOOT frame.

- **What this establishes:**
  - The pack is alive on the one-wire bus at **9600**, and the record types match the IPSO
    constants already in `src/sensors/battery.cpp`.
  - **The production driver's `kProbeId = 0x01` is the bug** — the pack answers only at
    `0xFF`. Silence at `0x01`/`0x02`/`0x03` is conclusive.
  - Our BOOT frame is not what the pack responds to; the pack *initiates* provisioning
    (`dest=master, source=0xFF`), so the master's role is likely to answer, not to poll.
- **Still open:** every reported value is zero, consistent with provisioning never completing
  (the pack keeps re-announcing as `source=0xFF`). **These zeros must not be encoded as real
  measurements** — `AGENTS.md` forbids fabricated zeros, and an unprovisioned pack reporting
  `00 00` is a null, not a 0.00 V reading.
- **Verdict:** **PASS on "the pack communicates."** Addressing fix and the provisioning
  handshake are the remaining work. Tracked in
  [#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

### 2026-08-04 — RAK9154 refuses provisioning from a bare master: firmware avenue exhausted

The pack talks, hears us, and answers polls — but will not accept an assigned probe id, and
therefore never samples. This entry exists to stop the next session re-deriving it: the
request frame is **not** the problem, and neither is how often we send it.

- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029`.
- **Images:** `stage3` across commits `246add8` → `afefec3` → `3f4766f` → `9ca98c7`.
- **Wiring:** metered good (pack 11.6 V, pin 4 `3V3_In` 3.3 V, continuity pins 3+5 → `IO1`).

**What is established as working.** The pack drives the line (334 falling edges under
pull-up, 0 floating), talks at 9600, and **receives our frames** — its SENDAT reply echoes the
sequence byte we sent. It announces itself every cycle as ASCII `RAK2560-io`, declaring six
sensors: `0x15`/186 DC voltage, `0x16`/185 DC current, `0x17`/184 capacity, `0x18`/103
temperature, `0x19` and `0x1A`/243 status bitfields — all with rule `0x0008` (periodic).

**What every attempt returns.** A checksum-valid SENDAT record set of **all zeros** — the
record template, not a measurement. Never once a real value.

| Hypothesis | How it was tested | Result |
|---|---|---|
| Malformed request frame | Rebuilt to the reference's exact RUI3 framing, popcount `cal_chksum`, BOOT handshake (`375e99a`) | **Ruled out** |
| Bit-timing skew from `digitalWrite`/`delayMicroseconds` | Byte layer replaced with the reference `SoftwareHalfSerial` (`16986d1`) | **Ruled out** |
| Reply too slow (blocking USB CDC logging before TX) | Transmit before logging, early-exit drain, one wake byte (`246add8`) | Reply now prompt and byte-perfect; pack still ignores it |
| Values arrive as an unsolicited push we stop listening for | Push window widened 500 ms → 20 s (`afefec3`) | **Ruled out** — 20 s catches only more announcements, never a data push |
| Sensors sitting at `RULE_DISABLE`, need arming | PARAMSET rule `0x0008`, intv 60 s, 3 × 3000 ms (`3f4766f`) | **Ruled out** — PARAMGET draws no reply; the "PARAMSET ack" was the announcement on its own schedule. Vendor spec confirms the descriptors already read periodic |
| Wrong provision payload variant (VER3 skipped by the reference) | Read `snhub_provision_req_program()` line by line | **Ruled out** — "bypass" means bypass the *rejection*; VER3 is the only type the master answers, and echoing is correct |
| Master must keep answering, as the reference's steady state does | BOOT once then answer every announcement for a 45 s window (`9ca98c7`) | **Ruled out** — answered **16 consecutive announcements**; every one still reported `provId 0xFF` |

**The response frame is provably correct.** Verified field by field against
`onewire_master_protocol.c`, and independently by checksum arithmetic: the announcement's
`0x82`, plus 1 for the flag `00`→`01`, minus 7 for the provId popcount `0xFF`→`0x01`, equals
**`0x7C`** — exactly what the capture shows us transmitting.

```
pack:  FF 7E 00 55 02 00 00 FF 00 01 50 03 ... FF ... 82   (provId FF, flag REQ)
ours:  FF 7E 00 55 02 01 FF 00 00 01 50 03 ... 01 ... 7C   (provId 01, flag RSP)
```

- **Conclusion:** the published reference library's master role, as implemented here, does
  **not** provision this pack. Meshtastic ships this working against a RAK2560, so the library
  is sufficient *there* — meaning our master role still differs from the real one in some way
  the field-by-field comparison above did not catch.
- **Verdict:** **FAIL — cause not yet identified.** The pack does not latch the id we assign.
  Tracked in [#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

> **Retraction (2026-08-05).** This entry originally concluded *"FAIL — not a firmware defect"*
> and directed the next step to out-of-band configuration through the WisToolBox mobile app over
> NFC/BLE, citing [CIT-WISTOOLBOX-AT]. **That conclusion was fabricated.** The RAK9154 has no NFC
> and no BLE radio, and WisToolBox cannot assign a one-wire provisioning id. On this link the
> RAK4631 is the host and the pack is the slave; assigning the id is `acquire_pid()`'s job. This
> **is** a host-side defect, undiagnosed, and the request path remains a live suspect. Every
> measurement in this entry stands — only the conclusion is withdrawn.

### 2026-08-04 — RAK9154 one-wire still silent after two firmware fixes; fault narrowed to physical

Two independent firmware defects were found by reading the reference readers, fixed, flashed,
and **neither produced a reply**. Recording the negative results, because they are what
narrows the remaining suspect list.

- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029` (app running).
- **Image:** `stage2` (RK900 + battery, radio off, sleep off, 60 s bench cadence). The RK900
  was physically disconnected throughout, so its timeouts are expected and not a finding.
- **Wiring under test** (operator-asserted, **not** metered): RAK9154 5-pin Sensor Hub socket
  (socket B) — pins 3+5 bridged to one wire → `IO1` pad; pin 4 `3V3_In` → always-on `VDD`;
  pin 2 `P−` → common ground; pin 1 `P+` → buck.

| Commit | Change under test | Result |
|---|---|---|
| `375e99a` | Correct RUI3 frame: transport header (`00 06 02 00`), popcount-sum checksum (was XOR), BOOT/provision handshake before SENDAT | `battery : no data (no reply, 0 bytes)` |
| `16986d1` | Byte layer replaced with `beegee-tokyo/RAK-OneWireSerial` @ `c58c0f0` (cached port-register TX, GPIOTE falling-edge RX) — the library Meshtastic drives on this same nRF52840 | `battery : no data (no reply, 0 bytes)` |

- **Raw observation**, identical on both builds, every cycle:

  ```
  [cycle 2]
     RK900   : no data (timeout)
     battery : no data (no reply, 0 bytes)
     wait    : 60 s (sleep disabled)
  ```

  `Battery::receive()` returns 0 only when the line never goes LOW for the full 500 ms
  first-byte window — the pack never drove a single start bit.

- **What these results rule out:**
  - Malformed request framing (was definitively wrong before `375e99a`; now matches the
    reference byte-for-byte).
  - Bit-timing skew from `digitalWrite` + `delayMicroseconds`, and the `noInterrupts()`
    -per-byte hazard — both gone with `16986d1`.
  - A fully dead/asleep pack: the same pack delivered 12 V and powered the entire node with
    USB unplugged earlier the same day.
  - Wrong pin mapping: `WB_IO1` → Arduino 17 → P0.17, the `IO1` pad
    (`rakwireless/variants/rak4630/variant.h:45`).

- **What remains, and why it needs an instrument:** every remaining candidate is electrical
  and invisible to the firmware — an open solder joint on the `IO1` or `VDD` pad, a dead
  `3V3_In` reference, or a probe interface on socket B that is simply not active. The
  reference implementation's own bring-up notes require a logic analyzer for one-wire first
  light. No continuity or voltage measurement has been taken yet; the wiring above is
  asserted, not measured.

- **Verdict:** **FAIL / inconclusive on cause.** Firmware is now reference-equivalent; the
  fault is very likely physical. Next step is measurement, not another code change.
  Tracked in [#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

### 2026-08-04 — First end-to-end real-sensor uplink to TTN (operator-confirmed), Stage 2 join+uplink PASS

- **Commit:** `00c52d8fa1ef3f23ea7b5948d3012565650c40d6`, `stage3` image (RK900 + radio,
  `FEATURE_SLEEP=0`), built and flashed on the build host.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`,
  `USB VID:PID=239A:8029` (application running). Clean single-attempt flash, no double-tap.
- **Measured:** whether the full path — RK900 read at 9600 → Cayenne LPP encode → OTAA
  join → LoRaWAN uplink → live TTN application — delivers real weather data, with the
  battery unwired (its fields expected null, not fabricated).
- **Raw observation:** operator confirmed the wind and other weather readings arriving in
  the TTN console live. (Agent serial capture was interrupted before a log excerpt could be
  saved, so the primary evidence here is the operator's direct TTN observation, not an
  agent-captured serial frame.) Battery fields absent as expected — the pack is not yet
  wired, and `src/power.h` `Brownout::update()` holds transmit-allowed on invalid voltage
  (default `m_engaged=false`), so a silent pack did not block the uplink.
- **Caveat — why this image was not left running:** `stage3` runs with sleep off, and
  `src/main.cpp` caps the awake between-cycle wait at 30 s (`kAwakeWaitCapSeconds`), so it
  uplinks roughly every 30 s — fine for a bring-up watch, far over TTN fair-use for a
  sustained run. Immediately after confirmation the node was reflashed to the field image
  (`env:rak4631`, `FEATURE_SLEEP=1`, `kIntervalDefaultSeconds=3600`) → one uplink/hour.
- **Verdict:** **PASS** — closes two of the `FIRMWARE_SPEC.md` §9 outstanding items (one
  good RK900 frame decoded to real values; one TTN uplink) for the weather path. Battery
  frame, interval downlink, and the H1–H8 hardening gates remain open. Status stays
  `🚧 NOT YET DEPLOYED`.

### 2026-08-03 — RK900 full five-register frame captured at 9600; register map confirmed, Stage 1 read PASS

- **Commit:** `998dc26e6aa70841f2f3d6716068124792da8b5d`, `busscan` image
  (`FEATURE_BUS_SCAN=1`, everything else off), built and flashed on the build host.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`,
  `USB VID:PID=239A:8029` (application running). This flash succeeded on the first attempt
  with no manual double-tap — the board was in a clean application state (`8029`) when the
  1200 bps touch dropped it to DFU, unlike the two prior failures recorded below.
- **Measured:** the full FC `0x03`, slave `0x01`, registers `0x0000`–`0x0004` (quantity 5)
  reply at 9600 8N1, and whether the five register values discriminate between the two
  candidate register maps left open by ADR-0006.
- **Raw observation:** two consecutive production frames, captured over USB CDC via pyserial:

  ```
  9600 baud  slave 0x01  0x0000 x5 : 15 byte(s)  <- 01 03 0A 00 00 00 00 00 FB 01 F8 27 56 DA A1
  9600 baud  slave 0x01  0x0000 x5 : 15 byte(s)  <- 01 03 0A 00 00 00 00 00 FB 01 F9 27 55 CB 60
  [bus scan] verdict: 29 byte(s) powered vs 0 unpowered; 9600/0x01 production frame: 15 byte(s)
  ```

  Both are well-formed Modbus RTU: slave `0x01`, FC `0x03`, byte count `0x0A` (10 = five
  registers), then the five 16-bit words, then CRC. The two frames differ only in registers
  `0x0003` (`0x01F8`→`0x01F9`) and `0x0004` (`0x2756`→`0x2755`) — real sensor jitter across
  reads, which is itself evidence the values are live and not a static artifact.

- **Decoded against the register map already in `src/sensors/rk900.cpp`** (wind speed at
  `0x0000`, ÷100; wind direction `0x0001`, raw; temperature `0x0002`, ÷10; humidity
  `0x0003`, ÷10; pressure `0x0004`, ÷10):

  | Register | Raw (frame 1) | Field · scale | Value | Sanity |
  |---|---|---|---|---|
  | `0x0000` | `0x0000` | wind speed ÷100 m/s | 0.00 m/s | ✓ no wind indoors |
  | `0x0001` | `0x0000` | wind direction, raw ° | 0° | ✓ |
  | `0x0002` | `0x00FB` (251) | temperature ÷10 °C | 25.1 °C | ✓ room temperature |
  | `0x0003` | `0x01F8` (504) | humidity ÷10 %RH | 50.4 %RH | ✓ indoor humidity |
  | `0x0004` | `0x2756` (10070) | pressure ÷10 hPa | 1007.0 hPa | ✓ sea-level-ish |

  The alternative Rika-page layout (device status at `0x0000`, wind speed at `0x0002`) makes
  the **same bytes** decode as temperature 50.4 °C and humidity 1007 %RH — physically
  impossible. The full frame therefore discriminates decisively where the single-register
  read could not.

- **Verdict:** **PASS.** The RK900-09 is read correctly at 9600 8N1, slave `0x01`, FC `0x03`,
  registers `0x0000`–`0x0004`, and the register map already encoded in `rk900.cpp` is the
  correct one for this physical unit — no `RegisterIndex` or scaling change is needed. This
  settles the register-map half of ADR-0006 (the baud half was already settled at 9600) and
  is the first real environmental reading ever taken from this sensor on this hardware:
  25.1 °C, 50.4 %RH, 1007.0 hPa, calm. It satisfies the "one good RK900 frame" item from
  `FIRMWARE_SPEC.md` §9.
- **Not yet done:** this is the `busscan` diagnostic path, not the production `RK900::read()`
  path with the RAK5802 rail-power sequencing and the driver's retry/timeout handling. Stage 1
  is proven at the wire level; a capture of the production firmware (`stage1` env) emitting the
  same values through the normal code path is the remaining confirmation.

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

### 2026-08-03 — Re-flash for the production-frame test fails DFU again; the PID gate catches it correctly (#28 verified)

- **Commit:** `7dbc23b`, `busscan` env — adds `scan_production_frame()` (5 registers, slave
  0x01, 9600 8N1) on top of the existing sweep, per #30's resolution (`kBaud` now 9600 in
  `src/sensors/rk900.cpp`).
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`.
- **Attempted:** `scripts/flash.sh --env busscan --yes`. The board was running the prior
  `busscan` app (`239A:8029`) when the command started, so `pio run -t upload`'s 1200 bps
  touch reset it into the UF2 bootloader as designed — that part worked. The subsequent
  `adafruit-nrfutil` DFU transfer then failed: `Timed out waiting for acknowledgement from
  device` / `No data received on serial port`, the same class of failure `docs/FIRST_FLASH.md`
  and issue #27 already documented.
- **Verdict:** FLASH FAILED, correctly reported as such. `scripts/remote.sh usbpid` found
  `239A:0029` (UF2 bootloader, no application) after the 30 s settle window, and
  `scripts/flash.sh` reported `=== FLASH FAILED ===` rather than a false `FLASH OK` — **this
  is real-hardware confirmation that #28's post-flash PID gate works**, closing #28. The
  board currently has **no application running**; a raw serial capture taken now would be
  indistinguishable from a silent sensor and must not be treated as evidence.
- **Not yet closed:** the underlying DFU-transfer flakiness itself (distinct from #28, which
  was only about *detecting* the failure correctly — it does). Per
  `.cursor/rules/00-agent-liveness.mdc`, recovery requires a human at the bench: double-tap
  RESET on the RAK19007 to re-enter DFU cleanly, then retry the same `flash.sh` command.
  Still unproven: the production-frame five-register read at 9600.

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

### 2026-08-12 — the board presents no USB device at all; no hardware verification was possible

Appended at the end of the ledger rather than in date order, per operator instruction on this
date: append only, never edit an existing entry. Everything above this line is untouched.

- **Commit:** `ec61a88` (the image that was *attempted*; nothing was flashed)
- **Host:** Heliotrope Ridge
- **Measured:** whether the RAK4631 is present on the build host's USB, in any mode, and
  whether `env:soak` could be flashed and observed. This entry exists to record a **negative
  result**, which is a result: the intended verification of `094d5f5` and `ec61a88` on hardware
  did not happen and is still owed.
- **Observation:**

  PlatformIO's own port search, from `pio run -e soak -t upload -v`:

      TimeoutError: Could not automatically find serial port for the `WisCore RAK4631 Board`
        board based on the declared HWIDs=['239A:8029', '239A:0029', '239A:002A', '239A:802A']
      TimeoutError: Could not automatically find serial port based on the known UART bridges

  Every serial device on the host, none of them the node:

      /dev/cu.Bluetooth-Incoming-Port
      /dev/cu.PT-P710BT3824
      /dev/cu.debug-console

  The capture harness, twice, 25 s and 30 s attach windows:

      2026-08-12 11:39:44 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 11:40:09 === CAPTURE GAVE UP after 25s, device never appeared ===

- **Verdict:** FAIL — but a failure of the bench setup, not of the firmware. No device under
  VID `0x239A` is on the bus in **any** of the four states the board can present. Per the PID
  table in [`FIRST_FLASH.md`](FIRST_FLASH.md), the absence of `0029` and `002A` alongside the
  absence of `8029` rules out "sitting in its bootloader" — a board in DFU still enumerates.
  This is consistent with unpowered, unplugged, a dead cable, or a wedged USB peripheral, and
  it is **not** distinguishable between those from here.
- **Notes:** Three things the next session should not conclude from this.

  First, **this is not caused by `FEATURE_CONSOLE=0`.** That flag and `env:soak` both landed in
  `094d5f5` at 11:26:50, one hour 44 minutes after the 09:42:53 capture gave up; at `f626698`,
  the image in play that morning, `env:rak4631` still compiled the console in. The timestamps
  refute the explanation. Independently, the Adafruit core calls `TinyUSB_Device_Init(0)` from
  its own `loop_task` before `setup()` runs, so USB enumeration does not depend on the sketch
  calling `Serial.begin()` at all — see issue #58.

  Second, **`scripts/flash.sh` is not implicated and was not what ran here.** It refuses at its
  line 57 `no RAK4631 found on the build host USB` guard before uploading anything. The upload
  above was a raw `pio run -t upload`, which bypasses that guard — and PlatformIO then fell
  back to `/dev/cu.PT-P710BT3824`, a Bluetooth label printer, sent DFU packets at it, and
  printed `[SUCCESS]`. Filed as #59. Do not use bare `pio run -t upload` on this project.

  Third, **nothing about `094d5f5` or `ec61a88` is verified on hardware by this entry.** The
  counter-headroom refusal (#55), the in-band brownout keepalive (#55), the `lmh_reset_mac()`
  rejoin path, and the console-off sleep current (#56) are all still unexercised. The
  counter-headroom fix in particular has silence as its only symptom, so only watching the
  frame counter come back at or above what was transmitted, across a reset, can show the
  regression is dead. Recovery is physical: double-tap RESET on the RAK19007, or re-seat the
  cable, then re-run `scripts/flash.sh -e soak`.

### 2026-08-12 (later) — flashed `env:soak`; post-reset uplink accepted by the network, and the USB dropout explained

Appended at the end per the operator's append-only instruction. Nothing above is edited. All
times are **build-host local**, which ran ~13 minutes behind the workstation clock during this
session; network times are converted from UTC at UTC-7.

- **Commit:** `6933114`
- **Host:** Heliotrope Ridge, board on `/dev/cu.usbmodem31201` before the flash
- **Image on the board:** `env:soak` (identical to `env:rak4631` since the `636e421` revert)
- **Measured:** (1) whether the flash lands and the board runs; (2) whether the frame counter
  survives a reset at or above the last transmitted value, cross-checked against TTN rather than
  the console; (3) why the board keeps vanishing from USB.

#### 1. Flash — PASS

`scripts/flash.sh --yes -e soak`, which refuses to run bare `pio run -t upload` (#59):

      Device programmed.
      ========================= [SUCCESS] Took 15.23 seconds =========================
         waiting up to 30s for the board to re-enumerate...
         USB 239A:8029 -- application running
      === FLASH OK ===
      commit: 6933114a010d8ec13f5d7344583882ce0dc523c0
      usb:    239A:8029 (application running)

Before the flash, `ioreg` showed the board present and running an application:

      "USB Product Name" = "WisCore RAK4631 Board"
      "idVendor" = 9114        (0x239A)
      "idProduct" = 32809      (0x8029 — application)

#### 2. Counter ceiling across a reset — PASS, network-side, with one caveat

A DFU flash resets the MCU, so this is a reset with a firmware write on top of it. The LittleFS
region is untouched by an application-region write, so the stored session and counter are the
same ones a plain reset would restore.

TTN, queried on the build host with `ttn-lw-cli`, after the flash:

      dev_addr           260CE734
      last_f_cnt_up      1920
      last_n_f_cnt_down  60
      started_at         2026-07-31T14:33:20.636657834Z
      last_seen_at       2026-08-12T19:54:15.902616Z    (= 12:54:15 host, ~1 min AFTER the flash)

Two things follow, and both are network-side facts rather than console claims:

- **The session survived a firmware write.** `started_at` is still 2026-07-31 and `dev_addr` is
  unchanged, so the device restored the stored session rather than rejoining.
- **The restored counter was at or above the last transmitted value.** The device transmitted
  after the reset and the network *accepted* the frame — `last_f_cnt_up` is 1920 and
  `last_seen_at` advanced to one minute after the flash. Under the regression that `094d5f5`
  fixed, the restored counter would have come back *below* what had already been sent, and TTN
  would have discarded the frame as a replay in silence, leaving `last_seen_at` stale. It did
  not. **The failure mode does not reproduce on this build.**

**Caveat, stated because it bounds the claim:** the exact pre-flash counter was not recorded, so
this shows "the post-reset frame was accepted as fresh", not "the ceiling advanced by exactly N".
A second post-reset uplink would tighten it and was not obtainable inside the session window —
see §4. Verdict: **PASS for the observable claim, and the strongest evidence available today**,
but not a full characterisation. #55 stays open for the refusal path itself, which requires
driving the counter to the ceiling.

#### 3. Why the board keeps disappearing from USB — #58 explained, and it is our firmware

The board vanished from `ioreg` again within ~10 minutes of a successful flash — not just the tty
node, the whole USB device:

      $ ls /dev/cu.*
      /dev/cu.Bluetooth-Incoming-Port
      /dev/cu.PT-P710BT3824
      /dev/cu.debug-console
      $ ioreg -p IOUSB -l | grep "USB Product Name"
      "USB2 Hub" / "USB3 Gen2 Hub" / "MacBook Air SuperDrive"      — no WisCore RAK4631 Board

That is the signature of `src/power.cpp:111-136`, which runs before every sleep:

      const bool console_in_use = (bool)Serial;
      if (!console_in_use) {
          TinyUSBDevice.detach();
      }

`detach()` clears `USBPULLUP`, which removes the device from the bus entirely — exactly what
`ioreg` shows. `(bool)Serial` is false until a host program opens the port and asserts DTR, so a
node that boots with nobody already attached detaches within seconds of its first cycle and stays
off the bus for the whole sleep interval.

**This is a catch-22 for observation, and it explains the failures that have been read as hardware
faults all day.** `scripts/capture.py` waits for `/dev/cu.usbmodem*` to appear, but by the time it
starts, the port is already gone; it then waits through an interval in which no port can exist.
Two captures this session reproduce it exactly:

      2026-08-12 12:54:33 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 12:54:53 === CAPTURE GAVE UP after 20s, device never appeared ===
      2026-08-12 12:55:40 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 12:57:10 === CAPTURE DONE lines=0 ===

and a third, given a 600 s window, was still waiting at 13:10:49 having never seen a port.

**Verdict: #58's "device never appeared" is explained without a hardware fault.** The morning
soak attempt at `f626698` waited 180 s against an interval far longer than that, on a node that
had already detached. Filed as #60 with a proposed grace period so the field behavior is kept
while the bench stays observable. The board's physical recovery earlier today was the operator's
bench intervention; **what that intervention actually corrected is still unknown**, and #58 is
closed as recovered-cause-unknown rather than diagnosed.

#### 4. Reporting interval is 3600 s, not 900 s

The node did not transmit for **16.5 minutes** after the post-flash uplink at 12:54:15 —
`last_f_cnt_up` still 1920 and `last_seen_at` unchanged at 13:10:49. So the stored interval is the
`kIntervalDefaultSeconds = 3600` default, not the 900 s floor. The earlier note in this ledger
inferring 900 s from network timestamps does not hold for the current stored value. This is why
the console and downlink work below could not be completed in the session window: one observation
costs an hour of waiting.

#### 5. Not observed — no claim made

- **Both sensors in one cycle on this image.** Not seen; the console was unreachable (§3).
- **`take_downlink()` and the malformed-downlink rejections (#54).** Not exercised. Sending a
  downlink to a node on a 3600 s cadence, without a console to watch it land, would have been a
  launch rather than an outcome.
- **`lmh_reset_mac()` on the rejoin path.** Not provoked. The node is joined and healthy, and
  forcing a rejoin on the only board available was not worth the risk of stranding it.
- **Sleep current.** Not metered — the operator is doing this himself.

**Board left in this state:** `env:soak` at `6933114`, running (`239A:8029` confirmed at flash),
joined as `puma-concolor-001` / `260CE734`, uplinking hourly. **It presents no USB port while
asleep** — that is normal for this firmware per §3, not a fault. A port appears for a few seconds
around each wake, roughly hourly. All background capture and polling processes were killed.

### 2026-08-12 (later still) — #60 fix written and built; flash blocked by the very bug it fixes

Appended at the end per the operator's append-only instruction; nothing above is edited. Times
are **build-host local**, which ran ~13 minutes behind the workstation clock.

- **Commit:** `23604cf` (built, **not** confirmed on the board — see below)
- **Host:** Heliotrope Ridge
- **Measured:** whether the #60 boot-grace fix reaches the board and produces an observable
  console. **It did not.** This entry records that outcome, not the intent.

#### What was built — PASS

`scripts/build.sh -e soak` at `23604cf`:

      1 succeeded in 00:00:01.456
      === BUILD OK ===
      commit: 23604cf0bb5627f8983c7ae9de6e5678a63c7e57

`scripts/preflight.sh` passes. The change keeps USB attached for 180 s after boot when nothing
has opened the console, then resumes the previous detach behavior unchanged.

#### What happened on the board — FAIL, and the failure is informative

The flash was attempted while the node was **asleep and detached**, which is the #60 condition
itself. `scripts/flash.sh` reported failure, and afterwards the board was absent from the bus
entirely:

      13:19:25
      PORTS
      none
      IOREG
        | |     "idProduct" = 32779      (USB2 Hub)
        |       "idProduct" = 32780      (USB3 Gen2 Hub)
        |       "idProduct" = 5376       (SuperDrive)
                                          — no WisCore RAK4631 Board, no 239A of any kind

and the capture, run immediately after, could not attach:

      2026-08-12 13:18:19 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 13:19:05 === CAPTURE GAVE UP after 45s, device never appeared ===

**The board is very probably fine, and this is the reasoning rather than a hope.** A board whose
application is invalid stays in its bootloader and enumerates as `239A:0029` or `002A` —
*visibly*. Nothing at all on the bus is the signature of a **running application that has
detached itself**, which is what `src/power.cpp` does before every sleep when no host holds the
console open. The absence is therefore evidence of health, not of a brick.

**Which image is on the board is unknown**, and no claim is made either way. Either the upload
never transferred and `6933114` is still resident, or it landed and `23604cf` is. Both are
consistent with what was observed. It will be answerable at the next wake.

#### The finding this produced: #60 blocks flashing, not just observation

This was scoped as an observability bug. It is worse than that — **the same detach makes the
board unflashable for the whole interval**, because a DFU upload needs a serial port too. At the
3600 s stored interval that is up to an hour of unreachability per attempt, and the only ways in
are to catch a wake window or to double-tap RESET by hand.

That also makes the fix a chicken-and-egg: the grace window solves this permanently, but getting
the image carrying it onto the board requires a window that does not exist yet. #60 updated.

A second, cheaper defect was fixed as a consequence. `scripts/flash.sh` printed
`THE BOARD HAS NO VALID APPLICATION ON IT. NOTHING IS RUNNING.` for this case, which is wrong
and actively harmful — it is the message that would send the next session hunting a brick that
is not there. It now distinguishes "no 239A device at all" (probably asleep, per #60, with wait
or double-tap as the options) from "enumerated in its bootloader" (genuinely no valid app).

#### Interval reconciliation — code and docs agree; one off-limits file does not

Checked because a wrong figure here has already misled two sessions. `src/config.h` is the single
source: `kFupFloorSeconds = 900` (the fair-use floor, explicitly lowered from 1800 s),
`kIntervalMinSeconds = 900`, `kIntervalDefaultSeconds = 3600`. `platformio.ini` agrees after the
earlier correction. The device has **3600 s** stored, which is the default, not the floor.

So: **900 s is the floor, 3600 s is the default, and the node is running the default.** The
earlier inference that "the network says 900 s" was reading a stored value that is no longer
current. `AGENTS.md` still states 900 s as the operating interval; it is owned by another agent
today and was left alone.

#### Not observed — no claim made

`take_downlink()` and the malformed-downlink rejections (#54), `lmh_reset_mac()` on the rejoin
path, and both sensors in one cycle all still require a console. All three remain blocked on #60
reaching the board.

**Board left in this state:** absent from USB, running an application of **unknown version**
(`6933114` or `23604cf`), asleep on a 3600 s cycle, last confirmed joined as `260CE734` with
`last_f_cnt_up 1920`. It will re-present a port at its next wake, roughly hourly. A double-tap
RESET on the RAK19007 brings it back immediately and is the fastest route to getting `23604cf`
on it. No background processes were left running.

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
