# Adversarial review, round 3 — regression hunt on today's fixes

- **Date:** 2026-08-12
- **Scope:** `origin/main` `8994d02..f4075c0`, read-only, no build/flash/SSH.
- **Caveat on line numbers:** `src/` was being edited by another agent during this read.
  Every citation below is against the working tree as of `f4075c0`. Line numbers may have
  shifted by the time this is read; the symbol names are stable.
- **Method:** `git diff 8994d02..HEAD` on `src/session.{cpp,h}`, `src/radio.cpp`,
  `src/power.cpp`, then ranged reads of the surrounding functions, `src/main.cpp` loop body,
  `src/payload.cpp`, `scripts/check_decoder_parity.py`. Prior reports grepped, not read.

---

## 1. Regression introduced by today's fixes

### R1 — The `session.cpp` brownout gate makes the frame counter go backwards. **This is the headline.**

`s_saved_counter_ceiling` is assigned at `src/session.cpp:241`, which is **after** the new
gate returns at `src/session.cpp:227-230`. So a gated save does not merely skip the write —
it also leaves the ceiling at its old value while the live MAC counter keeps advancing.

The path, all cited:

1. Hold engages on the no-evidence path (`src/power.cpp:228-230`), so
   `flash_write_allowed()` is false (`src/power.h:166`) and the gate installed at
   `src/main.cpp:185` rejects every save.
2. The keepalive still transmits *while holding* — that is its whole purpose
   (`src/main.cpp:295-316`, `src/power.h:177-180`). Each successful send reaches
   `session::maybe_save_counter()` at `src/radio.cpp:342`.
3. `maybe_save_counter()` compares the live counter to the stale ceiling
   (`src/session.cpp:263`). With `kCounterMargin = 32` (`src/session.h:40`), the 32nd
   keepalive after the last good save crosses the ceiling. From then on every cycle calls
   `save()` and every call is refused.
4. Any reset now restores the **stored** counter (`src/session.cpp:180`), which is now
   *below* what was already transmitted.

The comment at `src/session.cpp:174-179` describes exactly this failure and why it is
unacceptable — "the network would discard the uplinks silently while the node reported
success." The gate reintroduces it. The gate's own comment claims the cost is "a rejoin
after the next reset" (`src/session.cpp:223`); that is wrong. There is no rejoin, because
`restore()` **succeeds** — the stored record is intact and self-consistent, just stale.

Why the node cannot notice: the uplinks are unconfirmed (`LMH_UNCONFIRMED_MSG`,
`src/radio.cpp:313`), so a network-side replay rejection returns `LMH_SUCCESS`. `m_failures`
stays 0, so the rejoin-after-3-failures escape at `src/radio.cpp:328-333` — the only path to
`session::forget()` — is never reached.

Exit path, and it is slow: the node recovers only by re-climbing the counter, at one frame
per transmitted uplink. While the hold persists that is one frame per
`kNoEvidenceKeepaliveCycles = 24` cycles (`src/power.h:130`) — roughly 12 h per lost frame at
the 1800 s interval. Once the pack recovers and the hold clears (`src/power.cpp:258-262`) it
catches up at one per cycle.

**Rank: silent data loss measured in days-to-weeks. No hike.** It self-corrects, eventually,
invisibly.

*Note on prior coverage:* `docs/reviews/2026-08-12_spec_drift.md:112-126` flagged the
*absence* of the gate and explicitly declined to trace the ceiling ("*I did not trace
whether the keepalive's counter always advances past `s_saved_counter_ceiling`*"). The
interaction above is the consequence of the fix that closed that gap, and is not in either
prior report.

### R2 — `radio.cpp` downlink clear: clean for the current cycle; one narrow residual

The clear at `src/radio.cpp:304-306` sits after the `!m_joined` guard and before `lmh_send`,
and `main` calls `take_downlink()` on the straight-line path immediately after `send()`
returns (`src/main.cpp:312-319`), i.e. after `delay(rx_window_ms())` at
`src/radio.cpp:353`. **There is no path where a downlink belonging to the current cycle is
cleared before it is read.** Reporting this plainly: the radio fix does not regress.

Residual, **labelled inference**: a downlink whose callback lands *after* `delay(rx_window_ms())`
returns (`rx_window_ms()` = RxDelay2 + `kRxWindowMarginMs` 1500 ms, `src/radio.cpp:368`;
fallback 7000 ms at `:365`) previously survived and was applied one cycle late. It is now
discarded at `src/radio.cpp:304`. I could not verify from source whether the LoRaMac RX2
callback can fire outside that window, so I cannot say this is reachable. **Rank: untidy.**

### R3 — `power.cpp` keepalive: engages correctly on both traced paths

Traced every entry. `begin()` zeroes `m_invalid_reads` (`src/power.cpp:157`), and the
voltage-engage path zeroes it at `src/power.cpp:239` before `set_engaged` at `:252`, so the
new counter at `:196-198` always starts from 0 and cannot fire early on a measured-low hold.
Both routes the comment names — restored-from-flash and reading-went-stale — reach
`m_without_evidence = true` after `kInvalidReadsBeforeInhibit = 4` cycles (`src/power.h:91`).
**No regression found.**

One behavioural consequence worth stating, not a defect: a hold taken on a *measured* low
voltage now earns a keepalive transmit 28 cycles after the pack goes quiet. `src/power.h:112-114`
still asserts a measured-low hold has "no keepalive." That sentence is now stale with respect
to `src/power.cpp:195-208`. Doc drift, not code.

---

## 2. The worst thing a malicious or corrupted downlink can do

**Set the interval to 86400 s and have it persisted, reducing the node to one commandable
moment per day.** `src/radio.cpp:389` accepts opcode `0x01` with a 4-byte big-endian value;
`src/config.cpp:157` clamps only to `[kIntervalMinSeconds, 86400]` (`src/config.h:83-92`) and
`src/config.cpp:173` writes it to flash, so it survives every reset. Being Class A, the RX
windows open only after an uplink (`src/radio.cpp:344-353`) — so undoing it requires hitting
a ~1.5 s window that opens once per 24 h.

Cheaper harassment, same authority: `0x03` sets `sleep_for = kIntervalMinSeconds`
(`src/main.cpp:323-327`), so repeating it pins the node at the 900 s floor indefinitely and
multiplies its duty cycle.

Two mitigations that matter, and they are real: the frame is authenticated by the MAC, so a
*corrupted* frame cannot reach `take_downlink()` at all — this is an app-key-holder attack,
not an RF attack. And the exact-length check at `src/radio.cpp:389` plus the port-10 check at
`:378` mean nothing malformed is acted on. Unknown opcodes ignored at `:406` is correct and
not an attack surface.

Not found: no downlink can erase the session, change keys, reset the counter, or write
arbitrary flash. There is no bricking opcode. **Rank: strands the node's commandability; a
hike only if the operator cannot catch the daily window.**

*Previously noted:* the 86400 s ceiling is called out in
`docs/reviews/2026-07-30-DOWNLINK-AND-RESILIENCE.md:138,196` and
`docs/reviews/2026-08-12_spec_drift.md:137`. Repeated here only because it is the answer to
the question asked.

---

## 3. Unrecoverable state

**U1 — A persisted brownout hold on an intermittently-answering pack: mute, uncommandable,
and no exit the node can take by itself.**

`src/power.cpp:239-245` resets `m_invalid_reads`, `m_without_evidence` **and**
`m_silent_cycles` on *any* valid reading, whatever it says. A pack that answers at least once
every 4 cycles while reporting between `kTxInhibitCentivolts` 960 and `kTxResumeCentivolts`
1020 (`src/power.h:63-64`) therefore never accumulates to the no-evidence state, so
`keepalive_due()` (`src/power.h:177`) is never true and `transmit_allowed()` stays false
(`src/main.cpp:281`). The hold was persisted to flash at `src/power.cpp:252`, so it survives
reset and is restored at `src/power.cpp:159-163`. No uplink means no RX window means no
downlink, and `src/main.cpp:320` would refuse a `set_interval` during the hold anyway.

The only exit is `src/power.cpp:258-262`, which requires the pack itself to report ≥ 1020.
**Nothing the node does can cause that.** If the reported voltage is wrong — a one-wire
decode landing a plausible-looking low value — the node is silent until someone walks out.
This is not a regression from today; it is the pre-existing shape of the design, and today's
`power.cpp` change deliberately created an escape for the *silent*-pack case while leaving
the *lying*-pack case with none. **Rank: hike.**

**U2 — R1's counter regression compounds across resets.** While the hold holds, the gate
blocks the corrective save, so a second reset repeats the regression from the same stale
ceiling (`src/session.cpp:227` before `:241`). Bounded by hold duration; recovers on pack
recovery. **Rank: data loss.**

---

## 4. What the decoder parity check does not catch

Read `scripts/check_decoder_parity.py` gates 1, 2, 3b. Real holes:

1. **The encoder's scale factor is never checked.** Gate 2 compares `divisor` between schema
   and decoder (`:330-335`), but `check_encoder()` compares only channel, type, size and
   signedness (`:437-456`) — `divisor` does not appear in that loop. A firmware change to the
   fixed-point scale (0.1 vs 0.01) ships a 10× wrong value with every gate green. This is the
   worst hole because it is silent in both directions: nothing throws.
2. **`_CALL_RE` (`:379`) requires a matched `kChX, kTyX` pair** via a backreference. A
   cross-wired emit — `put_u16(kChHumidity, kTyPressure, …)` — matches nothing and is skipped
   entirely; a name not in `consts` is silently `continue`d at `:413-414`. The gate reports
   "N emitted field(s) checked" (`:465`) and never says which N.
3. **Only `put_u8/u16/s16` exist in `_EMITTERS` (`:382-386`).** Any future wider or raw
   emitter is invisible.
4. **No total-length check.** Nothing sums field widths against
   `kMinDataRatePayloadBytes` / `max_payload()` (`src/radio.cpp:267-284`). In practice
   `Payload::build()` honours the budget (`src/payload.cpp:112-115`), so this is latent, not
   live — but the gate is not what makes it safe.
5. **The two emit orders in `src/payload.cpp` are indistinguishable to the gate.** `:55-108`
   and `build()` at `:119-147` emit the same fields in different order; `encoded[name]` at
   `:417` overwrites, so a divergence between the two paths would not be seen.

**ADR-0002 dependency, not resolved here, every path listed:** the battery-current sign
convention is carried by `put_s16(kChBattAmps, kTyBattAmps, b.current.value)` at
`src/payload.cpp:94` **and** `src/payload.cpp:143` (both orders). The parity gate can only
compare the `signed` boolean (`:450-456`) — it cannot see whether charge is positive. Both
call sites and the schema row for that channel are what a resolution has to touch.

---

## 5. What I could not verify

- Anything requiring hardware: no build, flash, serial, or SSH. All findings are source-level.
- Whether the LoRaMac RX2 callback can fire after `delay(rx_window_ms())` returns — this is
  what would make R2's residual real or vacuous.
- Whether a corrupted one-wire read can produce a *plausible* low `centivolts` and so trigger
  U1's persisted hold. `centivolts` does not appear in `src/sensors/battery.cpp`, so the
  conversion and any range sanity check live elsewhere; I ran out of budget before finding
  them. U1's mechanism does not depend on this — a genuinely sagging pack reaches the same
  state — but the likelihood does.
- Whether TTN's actual replay window is strict `fcnt >` or tolerant. R1's severity scales
  with that; its existence does not.
- `src/sensors/battery.cpp`, `battery_frame.{h,cpp}` and `src/diagnostics/owscan.cpp`: not
  reviewed. The three files named as priority consumed the budget.

---

## Self-attack — what I dropped

- *"The gate blocks the post-join save at `radio.cpp:244`, so the node loses its session."*
  **Dropped.** Losing the record makes `restore()` fail and the node joins fresh
  (`src/session.cpp:144-152`) — correct, not a defect. R1 is the opposite and worse case: the
  record survives and is *wrong*.
- *"`send()` can truncate a payload mid-TLV and make the decoder throw."*
  **Dropped.** `src/radio.cpp:308` truncates against `sizeof(s_tx_buf)`, but `build()` is
  already capped at `max_payload()` (`src/main.cpp:246`, `src/payload.cpp:115`), so the
  truncation is unreachable. Kept only as gate hole #4.
- *"The `m_invalid_reads` counter is shared and can fire the new branch early."*
  **Dropped.** Zeroed at `src/power.cpp:239` and `:157` on every path into a hold.
- *"An attacker can set the interval to 0 or overflow the sleep timer."*
  **Dropped.** `src/config.cpp:157` rejects anything outside `[900, 86400]`.

### Ranking of survivors

| # | Finding | Cost |
|---|---|---|
| 1 | U1 — persisted hold + lying/intermittent pack, no self-exit (`src/power.cpp:239-245,258-262`) | **Hike** |
| 2 | R1 — gated save leaves the ceiling stale, counter goes backwards (`src/session.cpp:227` before `:241`) | **Data loss, silent, days-to-weeks** |
| 3 | Downlink `0x01` → 86400 s, persisted (`src/radio.cpp:389`, `src/config.cpp:173`) | **Commandability; hike if the window is missed** |
| 4 | Parity gate never checks the encoder's divisor (`scripts/check_decoder_parity.py:437-456`) | **Data corruption, silent** |
| 5 | Parity gate blind to cross-wired `kCh`/`kTy` pairs (`:379,413-414`) | Data corruption, silent |
| 6 | `src/power.h:112-114` now contradicts `src/power.cpp:195-208` | Untidy (doc drift) |
| 7 | R2 residual — late downlink now discarded (`src/radio.cpp:304` vs `:353`) | Untidy, possibly vacuous |
