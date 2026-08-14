# Issue audit — 2026-08-14

**Why:** the tracker carried 41 open issues after a week in which a great deal was proven on
hardware. A tracker holding already-solved items hides the ones that matter, and `AGENTS.md`'s
"either solve it or file it" only works if solved things get closed.

**Authority:** [`docs/EVIDENCE.md`](../EVIDENCE.md) alone. Every disposition below cites an entry
in it. Where the evidence is partial, the issue stays open with a comment saying exactly what is
now known and what is still missing — that is a legitimate outcome and it is most of this audit.

**Repo state at audit:** `origin/main` at `dd92dee`, tagged `v0.4.3`. A 24 h soak is running on
`1c2df3c`; the node ships to the field today. This audit is tracker-only — nothing was flashed,
reset, or attached to.

---

## A correction that must not propagate

The brief for this audit stated a bench figure of **0.14 A peak during transmit**, measured
2026-08-14. **That number does not exist in `docs/EVIDENCE.md`.** A grep of the ledger for `0.14`
returns nothing, and there is no 2026-08-14 current-meter entry at all.

What the ledger actually records is the **2026-08-13** entry
[_inline USB current meter_](../EVIDENCE.md): peak **0.04 A (40 mA)**, minimum reads `0`, display
resolution 0.01 A. That entry explicitly warns against reading 40 mA as a transmit figure — the
meter's slow sampling most likely missed the ~50 ms burst, and the datasheet gives 92 mA at
17 dBm. Verdict on it was **INCONCLUSIVE**.

This is exactly the failure `AGENTS.md` describes, where a claim travels because each document
trusts the last one instead of the log. **At the time of this audit, no transmit-current figure was
on the record.** Nothing in this audit rests on one.

**Resolved after the audit — the observation was real, it was just never written down.** The
operator had in fact reported **0.14 A** on his bench meter on 2026-08-14, twice, ~7 h apart. It is
now recorded: `docs/EVIDENCE.md`, _"the meter finally caught a transmit-shaped peak"_, attributed to
`572bcfa` because both readings (07:50Z and 15:05Z) precede the `1c2df3c` flash. So the corrected
statement is **not** "the number is wrong" but "**the number existed only in chat, which is not a
record**." The audit's own point survives intact and sharpens: the defect was the missing ledger
entry, and the fix was to write it. **Sleep current remains unmeasured** — the same meter's minimum
still reads `0` at a 10 mA resolution floor — and the 0.14 A peak is "consistent with the
datasheet," not a measured transmit figure.
[#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8) stays open.

---

## Disposition summary

| Disposition | Count | Issues |
|---|---|---|
| Closed with evidence | 3 | #10, #11, #51 |
| Commented, left open | 7 | #12, #13, #14, #40, #48, #55, #62 |
| Left untouched | 31 | all others |
| **Open after audit** | **38** | |

---

## Closed with evidence

### #10 — Prove the downlink round trip on hardware

**Acceptance was two steps:** (1) send an interval change and confirm it applies; (2) reset the
board, then send another, proving a restored session still hears downlinks.

**Both met**, `docs/EVIDENCE.md` 2026-08-13 _downlink command matrix_. Host Heliotrope Ridge,
`/dev/cu.usbmodem31201`, image `stage3`, commit **inferred** `f15a983` (banner predates `033b584`,
so the SHA is consistent-with, not asserted — recorded as inferred, per the ledger's convention).

- **Step 1.** Case b, cycle 18: `radio : downlink — set interval 900 s` → `config : interval now
  900 s`, and cycle 19's wait line drops 1800 s → 900 s. The applied value is visible twice, from
  two independent lines. It then **persisted across a reflash** (2026-08-14, `1c2df3c` banner:
  `config : interval 900 s`).
- **Step 2.** The whole matrix ran on a **restored** session — `session : restored 0x260CE734,
  counter 2080`, no rejoin, and the entry confirms no reset occurred during the run. A node
  resuming from flash therefore demonstrably hears downlinks, across all eight cases.

All eight cases passed, each on a console line emitted from inside `Radio::take_downlink()` —
observed on hardware for the first time, on all five branches.

**Not claimed:** `FEATURE_SLEEP=0` on that image. The downlink code is shared with the field
image; the power path is not.

### #11 — Prove session persistence survives a reset on hardware

**Acceptance:** reset the board and confirm it resumes without a second join.

**Met, and cross-confirmed from two independent observers**, `docs/EVIDENCE.md` 2026-08-14
_`1c2df3c` read back from the board's own banner_. Host Heliotrope Ridge, commit **asserted from
the device** (`commit : 1c2df3c`, no `-dirty`).

The operator pressed RESET once at 17:50:12Z with a capture held open. The board printed:

```
session : restored 0x260CE734, counter 2496
```

Nine seconds later the soak's own `events.log`, reading TTN and not the console, logged
`f_cnt=2496 ... resets=1`. Serial and network agree on the counter across a reset, from two
observers that share no code path. **Restored, not rejoined** — no join event anywhere in the
capture.

Independently observed twice more: `d568574` (`restored 0x260CE734, counter 2112`) and during the
2026-08-13 downlink matrix (`counter 2080`).

**Not claimed:** H5 is not closed by this. Session/DevAddr restore is proven;
interval-survives-power-loss is a separate half and stays `🟡 partial` in the ledger.

### #51 — H3 hole: `session.cpp` writes flash with no brownout gate

**Acceptance:** the ungated flash-write path found by the 2026-08-12 spec-vs-code audit is gated.

**Met.** Closed by `378384e`, _"fix(session): gate flash writes behind the brownout hold (H3)"_.
Verified in the tree at `dd92dee`: `src/main.cpp:77-79` defines `session_flash_write_allowed()`
delegating to `brownout.flash_write_allowed()`, and `src/main.cpp:226` installs it via
`session::set_flash_write_gate()`. The keepalive transmit path — the one that runs *during* a
hold, which is what made this bite — is covered by the installed gate. `docs/EVIDENCE.md`'s H3
row records the same conclusion: _"the ungated session writer found by the audit was closed in
`378384e`."_

**Closed as a code defect, verified the same way it was found — by inspection.** This is
deliberately *not* a claim that gate H3 passes. H3's own status stays `⬜ none`: it needs a
supply sag and an observed skip, which no code change can supply. That measurement is tracked
under #14 / H8.

---

## Commented, left open

### #12 — Confirm sleep is actually low-power, not a spinning delay

**What is now known.** The field image reaches the real sleep path and wakes on schedule:
2026-08-14, `1c2df3c`, six consecutive unattended cycles (3–8) closing `sleep : 900 s` at
908/908/907/908/908 s wake-to-wake against a 900 s target. Not `wait : N s (sleep disabled)`.

**What is still missing, and it is the whole question.** The issue asks whether the FreeRTOS tick
wakes the CPU every millisecond — which would show up as *current*, not as timing. A spinning
`delay()` keeps perfect time; that is why "reaches sleep and wakes on schedule" cannot answer it.

The only current data on the record is 2026-08-13's inline USB meter: minimum reads `0` at a
**10 mA display resolution**. The nRF52840's active-run current sits *below* one digit of that
display, so the meter cannot distinguish a spinning delay from real sleep. The RAK9154 pack
telemetry has the same 10 mA LSB, so the two coarse readings agreeing is not confirmation.

**Needs:** µA-resolution instrumentation (PPK2, shunt on a scope, or coulomb counting) — the same
sitting as #8, #2 and #47.

### #13 — Freeze the payload schema

**The blocker named in the issue is gone.** `batt_current`'s sign question is closed by ADR-0002,
and `scripts/preflight.sh` reports `PASS — no payload field is BLOCKED`. The freeze is therefore
**performable**. It has not been performed here: it is a contract change across two repos and is
the operator's call.

**Performing it requires, precisely:**

1. Change the header of `payload/schema.yaml:7` from `Status: DRAFT` to frozen, and flip the nine
   `status: proposed` field entries (`wind_speed`, `wind_direction`, and the seven that follow) to
   a frozen state.
2. Re-verify parity against the live TTN formatter and **re-pin** `decoder.pinned_commit` /
   `pinned_sha256` (currently `efc0e3c…` / `9c58c2b…`, pinned 2026-07-30) — re-pinning to silence
   the gate is the exact failure that gate exists to prevent, so this means re-running
   `scripts/check_decoder_parity.py` against a re-read formatter, not editing the hash.
3. **Settle `batt_temperature` first, or freeze a known factor-of-ten risk into the contract.**
   `schema.yaml:167-173` still carries `status: proposed` with the scale *inferred from the IPSO
   type, not confirmed against hardware*. This is #4, still open. A raw reading near 210 confirms
   tenths; near 21 means a ×10 belongs in `src/payload.cpp`. The ledger's `d568574` and `65f8615`
   captures print `raw ... t=240 (t scale UNCONFIRMED)` — so the firmware itself is still flagging
   it. Freezing the schema over an unconfirmed scale bakes the defect into a two-repo contract.
4. Decide what to do with the two `requires_formatter_change` entries still `status: open`
   (`schema.yaml:183-195`) — firmware version in the uplink (#17) and per-sensor validity flags
   (#18). Both need a paired formatter change; a freeze either admits them now or defers them past
   the freeze.

**Recommendation, not an action:** #4 is the one that should move first. Everything else is
bookkeeping; #4 is a factor of ten.

### #14 — 24-hour bench soak, then a 7-day shadow deployment

**H8 is NOT met, on either half.** Stating the true state, because two figures in circulation are
each true of a different image and neither is 24 h.

- **Bench: the longest run is 19.03 h**, `docs/EVIDENCE.md` 2026-08-14. Tree `572bcfa` (`v0.4.2`),
  run dir `~/soak-runs/20260813T202735Z_ttn_rc-v0.4.2/`, preserved. 68511 s of 86400 s, **76
  uplinks**, `f_cnt` 2398 from a baseline of 2272, **0 anomalies**, 1 query failure (a TTN API
  read, not a missed uplink). **Stopped deliberately** at ~15:30Z so the `#75` BOOT-allowance fix
  could ship instead. 19.03 h is not 24 h, and **a partial run on one image cannot be topped up by
  another**.
- **Bench, current run:** started 16:09:16Z on `1c2df3c` (pid 28695, label `rc-v0.4.3-1c2df3c`,
  baseline `last_f_cnt_up=2464`). It has a **deliberate RESET inside its window** at 17:50:12Z —
  the operator's single press to make the board print its banner — logged by the soak itself as
  `resets=1`, `f_cnt=2496`, `anomalies=0`. **So this is not 24 h of uninterrupted runtime either
  and must never be read as such.** Its outcome gets written when it ends, not before.
- **Also on `1c2df3c`, and not a soak:** six unattended 900 s cycles (3–8), ~1 h 15 m, both
  sensors live every cycle, `sleep : 900 s` every cycle. Real, and not 24 h.
- **Field shadow: today is day zero of seven.** The node goes to the woods 2026-08-14. A beginning,
  not an achievement. The earlier 2026-08-12 attempt at `f626698` never attached — 140 bytes in
  180 s, cause unestablished.

Status stays `🚧 NOT YET DEPLOYED`.

### #40 — bench checks: USB re-enumeration after sleep; watchdog tick accumulation on long sleeps

**Half 1 (USB re-enumeration after sleep) is answered — yes, it re-enumerates.** `docs/EVIDENCE.md`
2026-08-14: `scripts/capture.py` reattaches across the sleep-time USB drop and, run for 1500 s,
caught full cycles; the 81-line capture spans eight cycles across multiple 900 s sleeps on
`1c2df3c`. The technician-with-a-laptop failure the issue describes does not occur. This is
consistent with the fix in #47, which replaced the unrestored `NRF_USBD->ENABLE = 0` with
`TinyUSBDevice.detach()`/`attach()`.

**Half 2 (watchdog accumulation on long sleeps) is not answered at the interval that matters.**
Six 900 s sleeps produced no watchdog reset — cycles 3–8 are monotonic with no boot banner between
them — so `WDT_CONFIG_SLEEP_Pause` holds at 900 s. The issue's concern is `kIntervalMaxSeconds`
= 86400 and the 3600 s default, neither of which has been run. Leaving open on half 2.

The two minor items in the issue (rejoin delayed up to 8 cycles when both sensors are silent;
`Serial.end()` outside the `FEATURE_CONSOLE` guard) are untouched by anything this week.

### #48 — soak harness: serial reattach path unverified against real hardware

**Adjacent evidence exists but does not meet the acceptance.** The acceptance is
`scripts/soak.sh start 30m --label smoke` on the real board, then `port_reattaches` close to
`cycles_seen`, the banner in `serial.log`, and no missing cycle.

What now exists: `scripts/capture.py` — a *different* tool — demonstrably reattaches across a real
RAK4631 TinyUSB detach/attach and captured eight consecutive cycles across real sleeps on
2026-08-14. So the underlying hardware behaviour the harness depends on is confirmed working.
`scripts/soak.sh`'s own serial half is still proven only against the synthetic pty fixture, and
the run in flight is `soak_ttn.sh`, the TTN-side poller, which reads network ingest and never
opens the port.

One harness fact worth folding in: `scripts/capture.py` **refuses** a port already held and names
the holding pid, rather than producing a truncated log. Port contention has cost this project real
time and a second reader silently producing an interleaved log is the failure mode.

### #55 — Verify on hardware: counter-headroom refusal and in-band brownout keepalive (`094d5f5`)

**Still compile-verified only. Both paths have now *run* on hardware without either being
*exercised*** — the #75 distinction, and the reason this is not a close.

A grep of the 81-line 2026-08-14 `1c2df3c` capture for `brownout`, `keepalive`, `silent at` and
`no confirmed latch` returns **nothing**. The pack sat at 11.75–11.76 V all afternoon, far above
`kTxInhibitCentivolts` = 960 (9.60 V), so no hold engaged, no keepalive was armed, and every cycle
produced a real uplink so none was due. The 19.03 h / 76-uplink soak on `572bcfa` likewise closed
with `anomalies=0` and no hold.

Neither acceptance condition in the issue has been met:

- `counter_headroom_ok()` — the console line `session : uplink withheld — counter N has reached
  the stored ceiling M…` has never been printed, and no frame has been observed withheld.
- In-band keepalive — `power : … a keepalive is armed (in 24 cycles)` has never appeared, and no
  keepalive uplink has followed one. The 960–1020 cV band the issue names has never been entered.

Both need a forced hold on `env:soak`, per the issue's own method. Nothing this week advances it.

### #62 — battery: the provisioning ladder does not re-latch a pack that has dropped back to pid `0xFF`

**Still open and still unproven — recorded here specifically so a future reader does not mistake
this week's clean captures for proof.**

`docs/EVIDENCE.md` 2026-08-14 names this issue directly: no `provId 0xFF` appears anywhere in the
capture, and **the pack kept its `0x01` latch straight through the deliberate RESET**, so the
re-latch path was never entered. The 2026-08-13 downlink matrix says the same — `pack answered at
0x01 — skipping provisioning` in every one of its cycles.

The last time the precondition genuinely existed (2026-08-12, #61 gate fix) the pack kept
announcing `0xFF` across **four full ladder attempts** and only recovered its latch across the
reset that came with the next reflash. That is a pack-side behaviour, and it is the open question.

The `v0.4.1` change that stopped rebooting the pack on every re-latch attempt addressed the root
cause identified in that entry; it has never met its own failure condition on hardware.

---

## What is left, grouped

38 open. Grouped by what would actually move each one, since that is the shape the operator needs.

### 1 — Needs an instrument he does not have (7)

Blocked on µA-resolution current measurement. All of these are one PPK2 sitting.

| Issue | Question |
|---|---|
| #8 | Measure sleep current — the whole power budget rests on it |
| #12 | Is sleep low-power or a spinning delay (see above) |
| #2 | Buck no-load quiescent current; a 20× spread on the whole budget |
| #47 | Residual USB sleep current now that USBD stays enabled across sleep |
| #7 | Pack cutoff voltage and cell count — inferred from the nominal rating |
| #67 | `kTxInhibitCentivolts` / `kTxResumeCentivolts` are unsourced |
| #9 | Heater draw — also needs it to be cold enough for the heater to run |

#9 and #7 additionally need conditions, not just the meter.

### 2 — Needs time to elapse (3)

| Issue | State |
|---|---|
| #14 | H8: ≥24 h bench + ≥7 d shadow. 19.03 h on a superseded image; day zero today |
| #48 | Soak harness serial reattach against the real board |
| #40 | Watchdog accumulation at 3600 s / 86400 s intervals |

### 3 — Needs physical / enclosure work (2)

| Issue | State |
|---|---|
| #20 | Enclosure entries — five conductors, one M8 fitted. Leaning option C then A |
| #21 | Breather vent — not optional for a sealed outdoor box |

### 4 — Payload contract, needs a two-repo decision (6)

| Issue | State |
|---|---|
| #13 | Freeze the schema — now **performable**; see the four requirements above |
| #4 | Battery temperature scale inferred — **blocks a safe freeze**, factor of ten |
| #5 | Battery reply checksum convention assumed |
| #17 | Firmware version in the uplink — `requires_formatter_change`, `status: open` |
| #18 | Per-sensor validity flags — `requires_formatter_change`, `status: open` |
| #16 | No acknowledgement that a downlink was applied — pairs with #17 |

### 5 — Code hardening, failure paths never exercised (11)

Each has a fix or a concern in source; none has met its defect condition on hardware. This is the
largest group and it is the #75 pattern repeated.

| Issue | Concern |
|---|---|
| #45 | A dead one-wire link silences a healthy node after 4 cycles |
| #55 | Counter-headroom refusal and in-band keepalive, both unexercised |
| #62 | Pack re-latch from `0xFF` never demonstrated |
| #66 | Audit every safety hold for whether it disables its own exit |
| #68 | A failed counter checkpoint write still mutes the node permanently |
| #74 | A permanently failing session write mutes a HEALTHY node, no keepalive escape |
| #71 | No way to nudge a pack that goes mute after the one BOOT is spent |
| #70 | `lmh_reset_mac()` is only `ResetMacCounters()`; comments overstate it |
| #72 | preflight null-policy heuristic flags counter resets as fabricated zeros |
| #73 | Soak and capture harnesses do not record the banner commit SHA |
| #76 | `soak_ttn.sh` counts an implausible forward `f_cnt` jump as one clean uplink |

#68 and #74 are the same failure class as #45 — a write failure or a missing reading turning into
permanent silence with no escape. Worth treating as one piece of work.

### 6 — Tooling and tech debt (2)

| Issue | State |
|---|---|
| #59 | `pio run -t upload` picks an unrelated device and reports `[SUCCESS]` on failure |
| #52 | Tech-debt pass — 10% `src/` reduction shown unreachable; deferred by the operator |

On #59: `scripts/flash.sh` deliberately does **not** pin `--upload-port` (see its comment at
:116-118 — the 1200 bps touch renames the port), and instead verifies the outcome, printing
`=== FLASH OK ===` plus the `239A:8029 (application running)` check. The 2026-08-13 `d568574`
flash was accepted on eleven rows of progress marks and an `ioreg` check rather than on
`[SUCCESS]`. So the hazard is handled in practice by the wrapper; the bare `pio` invocation is
still a trap for anyone not using it. Left open.

### 7 — Needs a decision from the operator (7)

Nothing technical is blocking these; they need a call.

| Issue | Decision |
|---|---|
| #15 | Allow shorter intervals when the data rate affords them — loosens a fair-use guard |
| #69 | The node cannot see its own data rate, so the DR0 airtime remedy needs a human |
| #22 | Move to LoRaWAN 1.0.4 once DevNonce is persisted |
| #23 | Register `puma-concolor-002` and `003` with MSB-order keys |
| #25 | Bench-only 60 s cadence for stage bring-up builds |
| #26 | Bench radio-cadence testing needs a private network, not a FUP exception |
| #78 | Confirm the battery-current sign against a real charge current |

#78 is a decision only in the sense that it needs the pack to actually be charging — ADR-0002
closed the sign question on reasoning, and a real charge current would settle it empirically. It
has never been captured.

---

## What this audit deliberately did not do

- **Did not close #12 on the sleep evidence.** Reaching sleep and waking on schedule is not
  drawing low current, and the only meter on the record cannot tell the difference.
- **Did not close #55, #62, #45, #66, #68 or #74.** Every one of them shipped in an image that ran
  cleanly on hardware this week. None of them had its defect condition arise. A fix that runs
  without being exercised is `believed correct, unobserved`.
- **Did not perform the #13 freeze.** It is a contract change across two repos and #4 should move
  first.
- **Did not touch the hardware.** A soak is running and the node ships today.
