# Adversarial review — 2026-08-12

**Scope:** read-only. No files edited, no build, no flash, no serial, no SSH.
**Reviewed at:** working tree as of 2026-08-12, `src/` = 5894 lines (28 files), `platformio.ini` = 233 lines.
**Time budget:** 25 minutes. Passes 1–3 completed; coverage gaps are listed in §4.

**Evidence discipline used here:** every claim below cites `file:line`. Where I am reasoning
about behaviour I could not observe (library internals, RTOS scheduling, the wire), the finding
says **INFERENCE** explicitly and states what would settle it. Nothing in §1 is inferred except
where marked.

---

## 1. PASS 1 — defects

Ranked by the deployment bar in `AGENTS.md`: *requires a hike* > *loses data* > *ugly*.

### D1 — HIKE — a persisted brownout hold can never arm the keepalive, so the node goes mute forever

**Evidence:** `src/power.cpp:180-190`, `src/power.cpp:204-206`, `src/power.h:173-180`, `src/power.h:199-200`, `src/power.cpp:153-165`.

`Brownout::update()` has two ways to engage. The voltage-backed path at `power.cpp:223-232`
sets `m_engaged` **and persists it**, but leaves `m_without_evidence` at its
declared default `false` (`power.h:199`). The no-evidence path at `power.cpp:196-211` is the
only place `m_without_evidence = true` is ever written (`power.cpp:205`), and it is guarded by
`if (m_engaged)` returning early at `power.cpp:183-189`.

So once the gate is engaged on a measured low voltage, and the pack subsequently stops
answering at all:

```
power.cpp:182  if (!voltage_valid) {
power.cpp:183      if (m_engaged) {
power.cpp:186          if (m_without_evidence && m_silent_cycles < kNoEvidenceKeepaliveCycles) {
power.cpp:187              ++m_silent_cycles;
power.cpp:189          return;          // <-- every subsequent silent cycle exits here
```

`m_without_evidence` is `false`, so `m_silent_cycles` never increments,
`engaged_without_evidence()` (`power.h:173`) is permanently false, and therefore
`keepalive_due()` (`power.h:177-180`) is permanently false. `main.cpp:255` computes
`keepalive = false` forever and `main.cpp:264` takes the hold branch on every cycle for the
rest of the node's life.

The only exit is `power.cpp:234` — a *valid* reading at or above `kTxResumeCentivolts`. If the
one-wire link is what failed, there will never be another valid reading.

**Why this is a hike.** `power.h:93-110` states the exact reasoning this keepalive exists to
satisfy: *"a permanently quiet node is also an uncommandable one — there is no route left to
tell it otherwise. Silent-forever and drained both end in a hike, and the silent one gives no
warning first."* The keepalive is switched off precisely in the compound failure it was
written for: low pack **then** dead link. Note also `power.cpp:161-164` restores the hold from
flash across a reset but does not restore or re-derive `m_without_evidence`, so a reset does
not clear it either.

**Not speculation:** the state machine is fully readable in `power.cpp`; no hardware needed.

### D2 — HIKE (conditional) — the sleep path has never been exercised by any environment that appears in the evidence trail

**Evidence:** `platformio.ini:110` (`stage1`), `:135` (`busscan`), `:163` (`owscan`), `:180`
(`stage2`), `:210` (`battdiag`), `:233` (`stage3`) — **every one sets `-D FEATURE_SLEEP=0`**.
Only `env:rak4631` (`platformio.ini:52-62`) leaves `FEATURE_SLEEP` at its default `1`
(`src/build_features.h:55-57`). `AGENTS.md` records stages 0–3 as run on hardware; those are
the sleep-off environments.

Inside that never-run path, `power::sleep_seconds()` (`power.cpp:73-151`) contains:

- `power.cpp:134-136` — a `for (i < seconds) delay(1000);` loop with **no `watchdog_feed()`**.
  This is safe only if `WDT_CONFIG_SLEEP_Pause` (`power.cpp:52`) genuinely stops the counter
  for essentially the whole interval. **INFERENCE:** with a non-tickless FreeRTOS tick the CPU
  wakes every tick and the WDT counts during each awake slice; if the awake duty exceeds
  ~3.3%, a 3600 s sleep accumulates the full 120 s (`main.cpp:48`) and the node resets every
  interval — losing the LoRaWAN session restore path and boot-looping in the field. This is
  settled by a bench soak, not by reading, and it is exactly gate H8.
- `power.cpp:121` — `TinyUSBDevice.detach()` is guarded only by the runtime
  `if (!console_in_use)`, **not** by `#if FEATURE_CONSOLE`, while the matching
  `TinyUSBDevice.attach()` at `power.cpp:148` **is** inside `#if FEATURE_CONSOLE`
  (`power.cpp:143-150`). The header is also only included under `#if FEATURE_CONSOLE`
  (`power.cpp:7-17`). A `FEATURE_CONSOLE=0` build therefore fails to compile, and if it were
  made to compile it would detach USB and never re-attach. No current environment sets
  `FEATURE_CONSOLE=0`, so this is latent, not live — but the guard asymmetry is real.

**Consequence in the woods:** the one image that ships is the one image with no bench hours on
its distinguishing feature.

### D3 — HIKE (narrow trigger) — RK900 leaves the switched 3V3_S rail on if the Modbus read never returns

**Evidence:** `src/rk900.cpp:63` `power_on()` → `:68` `bus.read_holding(...)` → `:70`
`power_off()`.

`power_off()` (`rk900.cpp:47-57`) is the only thing that drops `WB_IO2` and closes `Serial1`,
and it sits on the straight-line path after the read. It is **not** in a scope guard, and
`read_holding` has no `return` that bypasses it — I checked `modbus.cpp:151-172`, every exit is
a `return result` back to the caller. So the rail *is* dropped on all read outcomes. What is
not covered is a hang *inside* `transact()`: `modbus.cpp:93-104` is bounded by
`kReplyTimeoutMs` (`modbus.cpp:29`, 1000 ms) so it terminates, but there is **no
`power::watchdog_feed()` anywhere between `main.cpp:181` and `main.cpp:210`**. With
`retries` defaulted the worst case is bounded and well inside 120 s, so the watchdog does not
fire — but if the watchdog *did* fire mid-read, the reset happens with `WB_IO2` driven HIGH and
`Serial1` open. On reset the pin reverts to its POR state, so the rail drops; I could not
confirm the RAK5802's behaviour across that transition.

**Downgraded on self-attack (see §3):** I cannot demonstrate an unbounded path here. Recorded
as *worth a bench check*, not as a proven defect.

### D4 — DATA LOSS — `Radio`'s downlink flag and buffer are file-scope state that is never cleared per cycle

**Evidence:** `src/radio.cpp:89-95` (`s_have_downlink`, `s_rx_buf`, `s_rx_len`, `s_rx_port` at
file scope), `radio.cpp:111-121` (`on_rx` sets them), `radio.cpp:361-398`
(`take_downlink` is the *only* consumer and the only thing that clears `s_have_downlink`, at
`:366`), `src/main.cpp:301-302` (`take_downlink` is called **only inside**
`if (radio.send(payload))`).

Nothing clears `s_have_downlink` at the top of a cycle. Any downlink delivered by the MAC on a
cycle where `radio.send()` returns false — `radio.cpp:294-296` (not joined) or
`radio.cpp:304-325` (`lmh_send` error) — stays latched, along with its `s_rx_buf` contents,
until some later successful send consumes it as if it were fresh.

Bounds-checking of the payload itself is sound: `radio.cpp:368` rejects the wrong port,
`radio.cpp:379` requires `s_rx_len == 5` exactly for `kCmdSetInterval`, `radio.cpp:116-117`
clamps the copy to `sizeof(s_rx_buf)`, and `config.cpp:157-161` rejects out-of-range intervals
rather than clamping. So the damage is bounded to *replaying a stale but structurally valid
command*, e.g. re-applying an interval the operator has since changed, or a spurious
`request_status` shortening one sleep to `kIntervalMinSeconds` (`main.cpp:309`).

**INFERENCE on reachability:** I cannot see inside `LoRaWan-Arduino` to enumerate every moment
it may invoke the `on_rx` callback, so I cannot prove the window is ever hit in practice. What
I *can* state from the source is that the flag is never reset per cycle and the consume path is
conditional — that is the un-reset-static-state shape regardless of how often it fires.

**This is the closest match in the codebase to the `owscan.cpp` bug class.**

### D5 — DATA LOSS — the firmware and the test suite exercise two different payload encoders

**Evidence:** `src/payload.cpp:49-79` (`add(WeatherReading)`) and `:81-110`
(`add(BatteryReading)`) duplicate the channel/type table used by `:112-148` (`build`).
`src/main.cpp:229` and `:235` call **only** `build()`. Grep of the whole tree shows `add()` is
called **only** from `test/test_payload/test_payload.cpp` (lines 30, 41, 55, 70, 84, 97,
122-123, 151, 258) and never from `src/`.

Both functions encode the same nine fields against the same constants (`payload.cpp:7-16`), but
they are separate call sites. A channel or type edited in `build()` alone ships to the air with
a green test suite behind it, because the tests are validating `add()`. Rule 60 exists to stop
exactly this, and `scripts/check_decoder_parity.py` compares against `payload/schema.yaml` —
which does not distinguish which of the two encoders production uses.

**Consequence:** the decoder-parity gate has a blind spot. A drifted field makes the TTN
formatter throw and **discard the whole uplink**, per `payload.h:5-8`.

### D6 — ADR-0002 DEPENDENCY — battery current sign

**Evidence:** `src/payload.cpp:87-95` encodes `b.current.value` unmodified and explicitly defers
to ADR-0002. `src/sensors/battery.cpp:1427-1450` is the log line an operator would use to settle
the convention, and its sign handling is correct (magnitude split from sign before dividing).
`src/sensors/battery_frame.cpp:239` decodes it as `(int16_t)val16(...)` with byte order chosen
from the frame flag at `battery_frame.cpp:198`.

**Flagged, not resolved, per instruction.** Everything that depends on the unresolved sign:
`payload.cpp:94` (what goes on the air), `battery_frame.cpp:198` (a wrong `lsb_first` would
invert the sign *and* the magnitude), and `battery.cpp:1446-1450` (the console line the decision
will be made from). Note the byte-order choice at `battery_frame.cpp:198` is load-bearing for
the sign question and is itself sourced only from `[CIT-MESHTASTIC-9154]` plus one bench
capture — if that inference is wrong, the sign evidence collected from the console is wrong too.

### D7 — UGLY (but misleading at the bench) — the raw battery dump prints values for fields that were never read

**Evidence:** `src/sensors/battery.cpp:1471-1474` prints `out.voltage.value`,
`out.current.value`, `out.soc.value`, `out.temperature.value` **without testing `.valid`**,
unlike the block immediately above it (`:1443-1460`) which gates every field on `.valid`.

The values are structurally zero when invalid (`battery_frame.cpp:126` clears the struct on
entry), so this prints `raw v=0 i=0 soc=0 t=0` for a pack that said nothing. In a driver whose
entire discipline is *null stays null*, the one line an operator reads to settle the
temperature scale is also the one line that fabricates zeros. Console only — nothing encodes
from it.

### Things I attacked and could NOT break — recorded so the next reviewer skips them

- **Fabricated zeros in the uplink.** `battery_frame.cpp:126` clears `out` on entry;
  `:279` clears it on a truncated record; `:323-326` clears it and returns `Unsampled` when
  every physical record read zero. `main.cpp:221` then feeds `brownout.update()` with
  `pack.voltage.valid == false`, which is the *no-evidence* path, not a `0 V` reading. The
  obvious catastrophic bug — an all-zero template being read as a flat pack and latching a
  persisted brownout hold — **is correctly defended**.
- **Unbounded loops.** `modbus.cpp:93` (bounded by `kReplyTimeoutMs`), `radio.cpp:232`
  (bounded by `kJoinTimeoutMs`, 30 s), `battery.cpp:875` (bounded by `kProvWindowMs` and feeds
  the watchdog at `:883`), `battery.cpp:980` (bounded by `deadline_us` and feeds at `:987`),
  `owscan.cpp:176/222/309` (all `millis()`-bounded). I found no `while (!available())` shape
  anywhere.
- **Join hammering.** `radio.cpp:400-411` grows the backoff 60 s → 3600 s and holds. One join
  request per hour at the ceiling is far inside the TTN allowance.
- **Fair-use enforcement.** `config.h:36-38` `#error` on bench-interval + radio, and
  `config.h:146-153` `static_assert` on the *derived* effective cadence, is genuinely stronger
  than an enumerated flag check. This is good work and should not be touched.
- **`owscan.cpp` accumulators.** `owscan.cpp:343-346` now resets all four
  (`ow_edges_pulled`, `ow_edges_float`, `ow_bytes`, `ow_best_baud`). The reported bug is fixed.
  `owscan.cpp:291` `static uint8_t seq` still persists, which is harmless and arguably correct.
- **Session/frame-counter handling.** `session.cpp:176-180` refuses to keep a session whose
  counter the MAC rejects, and `session.cpp:220` stores a counter ahead of the live one. The
  silent-discard failure mode is handled.

---

## 2. PASS 2 — cruft inventory

`src/` totals **5894 lines**. A 10% reduction is **590 lines**.

The single dominant fact: **`src/sensors/battery.cpp` is 1477 lines, of which 917 are comment
and 462 are code** (measured: `rg -c '^\s*(//|\*|/\*)'`). It is 25% of `src/` and its comment-to-code
ratio is 2:1. Most of that commentary documents *code that is no longer in the file* — settled
negative results, superseded rationales, and hypotheses that were falsified. That history
belongs in `docs/EVIDENCE.md`, `CHANGELOG.md` and the ADRs, all of which already exist.

### Confident deletion candidates

| # | File | Lines | Count | Justification |
|---|---|---|---|---|
| C1 | `src/sensors/battery.cpp` | 142-183, 185-210, 212-225 | **81** | The PARAMSET/PARAMGET constant block. Nine of its constants are **declaration-only** — verified by grep: `kPldParamSnsrUpdate`, `kParamSid`, `kParamIntv`, `kParamRule`, `kParamBytes`, `kParamIntvMax`, `kParamAckTimeoutUs`, `kParamAttempts`, `kEnablePassBudgetMs` each appear exactly once in all of `src/`. The pass they served was deleted (see the comment at `:1221-1242` admitting it). Keep only `kRuleDisable`/`kRulePeriodic` (2 refs each, used at `:795-796`). |
| C2 | `src/sensors/battery.cpp` | 50-90 | **41** | Wake-count history. Argues the count down to 1 across ~25 lines of citation, then reverses itself at `:74` ("RESTORED to 4"). The live fact is one line: 4, because that is what the bench drew a reply with. The superseded half is a trap for the next reader. |
| C3 | `src/sensors/battery.cpp` | 817-864 | **48** | The 48-line essay justifying sustained announcement-answering as the last untested hypothesis. It is now known **not** to be the missing piece — `:939-947` and `:1221-1242` both say the real cause was turnaround timing. The essay argues for a conclusion the file elsewhere refutes. |
| C4 | `src/sensors/battery.cpp` | 1221-1242 | **22** | A comment block whose entire subject is a feature that has been deleted, explaining why it was deleted. `CHANGELOG.md`'s job. |
| C5 | `src/sensors/battery.cpp` | 13-27 | **15** | The raw-Modbus negative result. Already recorded in `build_features.h:102-105`, `CHANGELOG.md` and `docs/EVIDENCE.md` — this is the third copy. |
| C6 | `src/sensors/battery.cpp` | 331-349 | **19** | The removed `FEATURE_ONEWIRE_SPLIT` explainer, plus `link_for()` (`:349`) which is a bare pass-through to `bus()` (`:326-330`). Two functions where one does. |
| C7 | `src/sensors/battery.cpp` | 226-284, 415-475 | **~95** | Frame-offset and buffer-capacity derivations, each 10-20 lines of arithmetic narration for a single `constexpr`. The offsets are already re-derived in `battery_frame.h`/`.cpp` where they are used. Conservative estimate; needs a line-by-line pass to split the load-bearing citations from the narration. |
| C8 | `src/payload.cpp` + `src/payload.h` | `payload.cpp:49-110`, `payload.h:58-66` | **71** | `Payload::add()` ×2. Dead in firmware (D5). **Deleting requires porting `test/test_payload/test_payload.cpp` to `build()`** — which is the point: the tests would then cover the code that ships. |
| C9 | `src/power.h` | 60-130 | **~45** | 146 comment lines against 34 code lines. Three constants carry ~70 lines of prose that restates `docs/POWER_BUDGET.md` and ADR-0007 verbatim. Cite the ADR, delete the restatement. |
| C10 | `src/config.h` | 40-58, 96-133 | **~40** | 115 comment lines against 59 code lines. The fair-use arithmetic is reproduced in full here *and* in `docs/FIRMWARE_SPEC.md` §4 *and* in `platformio.ini:214-220`. |
| C11 | `src/main.cpp` | 240-247, 251-262, 277-294 | **~35** | Comments that narrate issue history (`Refs #24`, `#44`, `#45`) inside `loop()`. The issue tracker holds this; `loop()` is meant to be "deliberately boring" per `main.cpp:4`. |

**Running total, `src/` only: ~512 lines = 8.7% of 5894.**

### Outside `src/` — mechanical and safe

| # | File | Lines | Count | Justification |
|---|---|---|---|---|
| C12 | `platformio.ini` | `:54-61`, `:98-105`, `:122-129`, `:150-157`, `:169-176`, `:198-205` | **48** | Every one of the six board environments declares `extends = common` and *then* re-states all eight inherited keys (`platform`, `board`, `framework`, `build_src_filter`, `lib_archive`, `lib_deps`, `monitor_speed`, `monitor_filters`) as `${common.X}`. `extends` already does this. Purely redundant; deletion is verifiable by diffing `pio project config` before and after. |

**Grand total including `platformio.ini`: ~560 lines.**

### Getting to 10% honestly

I reached **8.7% within `src/`** with claims I can defend line by line. The remaining ~80 lines
are certainly present in `battery.cpp` — 917 comment lines against 462 code lines leaves a great
deal I did not have time to itemise — but I did not read every one of them closely enough to
name the range, and I will not pad the list to hit a number. **The honest statement is: 8.7%
itemised, and `battery.cpp` alone plausibly holds another 200+ lines of the same kind.** A
focused hour on that one file gets past 10% comfortably.

### DO NOT DELETE YET

- **`src/diagnostics/owscan.cpp` (412) and `src/diagnostics/busscan.cpp` (175).** They share
  obvious copy-pasted scaffolding — frame build, hex dump, baud sweep, per-phase byte totals —
  and deduplicating them looks attractive. **They are the only instruments the operator has for
  silent hardware right now**, and `owscan.cpp:81-84` states a deliberate reason for the
  duplication: *"a diagnostic that shares mutable state with the thing it is diagnosing is not
  a diagnostic."* That reasoning is sound. Leave both alone until bring-up closes.
- **`FEATURE_BATTERY_FAST` / `FEATURE_BATTERY_TURNAROUND_MS`** (`build_features.h:112-129`) and
  the `battdiag` environment (`platformio.ini:196-212`). `FEATURE_BATTERY_TURNAROUND_MS` is a
  live sweep knob; `battery.cpp:105-109` warns in terms that deserve to be taken literally —
  deleting the delay it feeds re-breaks battery telemetry with a symptom that looks like a
  wiring fault.
- **The `stage1`/`stage2`/`stage3` environments.** They look redundant against `rak4631` but
  they are the bring-up ladder, and D2 above means the ladder has not finished being climbed.
- **`kTurnaroundMs`, `kWakeCount`, `kProvWindowMs`, `kPushListenUs`** and their *bench* citations.
  Every one is held by measurement alone with no datasheet behind it. The prose around them is
  fair game (C2); the constants and their `CITE(bench)` lines are not.
- **`src/sensors/battery_frame.{h,cpp}`** (578 lines combined). High comment ratio, but it is
  the only part of the battery stack with host tests (`test/test_battery_frame/`), and the
  truncation bug it was extracted to fix (issue #42) cost days.
- **Anything I marked INFERENCE.** If I could not read it, I did not cost it.

---

## 3. PASS 3 — self-attack

What I knocked down or downgraded from my own first pass:

- **"The watchdog cannot cover the join wait."** Withdrawn. `radio.cpp:232` has no
  `watchdog_feed()`, but the worst-case span between the feed at `main.cpp:215` and the one at
  `main.cpp:324` is roughly 30 s (join) + ~6.5 s (`radio.cpp:343`, `rx_window_ms()`) plus
  encode — well inside the 120 s at `main.cpp:48`. No finding.
- **"An all-zero SENDAT template will latch a persisted brownout hold."** Withdrawn, and it was
  my best candidate. `battery_frame.cpp:323-326` returns `Unsampled` with `out` cleared, so
  `main.cpp:221` sees `valid == false`. Defended correctly.
- **"`m_seq` wraps and breaks response matching."** Withdrawn. `battery.cpp:537` increments a
  `uint8_t` and `battery_frame.cpp:107` compares equality against the same value from the same
  cycle. Wrapping is harmless.
- **"`modbus.cpp:78` can overflow `resp[]`."** Withdrawn. `read_holding` validates
  `count <= kMaxRegisters` at `modbus.cpp:154` before `transact()` is reachable, and `expected`
  at `:77` is then bounded by `kMaxFrame` at `:11`.
- **D3 downgraded** from "leaves the rail powered" to "worth a bench check" — I traced every
  exit from `read_holding` and `power_off()` is unconditionally reached.
- **D4 kept but honestly bounded.** I cannot prove the MAC ever delivers a downlink outside the
  consume window, so I have not claimed it does. The un-reset state is real; the exploit rate is
  unknown.
- **The cruft total was cut**, not padded, when I could not name specific line ranges.

**Surviving findings, ranked:**

| Rank | ID | Severity | One-line consequence in the woods |
|---|---|---|---|
| 1 | D1 | **Hike** | Low pack + dead one-wire link = permanently silent, permanently uncommandable node. |
| 2 | D2 | **Hike (unproven)** | The only image that ships is the only image never soaked; a WDT-vs-sleep interaction would boot-loop it. |
| 3 | D5 | Data loss | Decoder-parity gate tests an encoder production does not use; drift discards whole uplinks. |
| 4 | D4 | Data loss | Stale downlink replayed a cycle or more later; bounded to a valid-but-wrong interval. |
| 5 | D6 | Blocked | ADR-0002 sign, plus the byte-order inference at `battery_frame.cpp:198` it rests on. |
| 6 | D3 | Bench check | Rail state across a watchdog reset mid-Modbus-read, unconfirmed. |
| 7 | D7 | Ugly | Console prints `raw v=0 i=0 ...` for fields that were never read. |

---

## 4. What I could NOT verify, and why

- **Anything requiring hardware.** No build, no flash, no serial, no SSH — a concurrent agent
  owns the device. So: actual sleep current, whether `WDT_CONFIG_SLEEP_Pause` holds across the
  Arduino core's FreeRTOS idle path (D2), the real awake duty cycle, and the RAK5802 rail state
  across a reset (D3).
- **Library internals.** `LoRaWan-Arduino`, `SX126x-Arduino`, `SoftwareHalfSerial` and
  `Adafruit_TinyUSB` were not read. That limits D4 (when `on_rx` can fire),
  `radio.cpp:274` (`LoRaMacQueryTxPossible` behaviour when unjoined) and any claim about US915
  dwell-time enforcement, which I therefore did not make.
- **Decoder parity end-to-end.** I read `src/payload.{h,cpp}` and confirmed the two-encoder
  split by grep, but did not read `payload/reference/rak-wx-station-default.js`,
  `payload/schema.yaml` or `scripts/check_decoder_parity.py` against each other. D5 is a claim
  about *which encoder the tests cover*, not a claim that the wire format is currently wrong.
- **`docs/` cross-checking.** I read `AGENTS.md` and the rules in full. I did **not** read
  `FIRMWARE_SPEC.md`, `HARDWARE.md`, `POWER_BUDGET.md`, `EVIDENCE.md` or the ADRs line by line —
  they are cited above from their references within `src/`. So I have made **no findings about
  spec-versus-code drift or contradictions between documents**, which was part of the brief and
  is the largest uncovered area.
- **`src/reading.h`, `src/session.h`, `src/radio.h`, `src/sensors/battery.h`,
  `src/sensors/modbus.h`, `src/diagnostics/*.h`, `src/sensors/crc16.*`, `test/`, `tools/`,
  `scripts/`.** Read partially or not at all. The cruft inventory therefore under-counts.
- **`git log` / issue tracker.** Not consulted, so I cannot say whether D1 is a known-and-filed
  issue or new.
