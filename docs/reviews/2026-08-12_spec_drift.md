# Spec-versus-code drift audit — 2026-08-12

Read-only audit. No source touched. Working tree as of 2026-08-12 07:58 local; `src/` was
being edited concurrently, so line numbers are as-read and may shift.

Scope: `FIRMWARE_SPEC.md` §7 (H1–H8), `docs/decisions/ADR-*.md`, `docs/EVIDENCE.md`.
Excluded by instruction: `power.cpp:180-190` brownout keepalive, `radio.cpp:89-95` downlink
leak, `owscan.cpp` accumulators, the battery one-wire provisioning frame.

---

## 1. H1–H8 — implemented versus aspirational

`FIRMWARE_SPEC.md:224-233` is the gate table. Every gate below is judged on **source
presence only**; none of them is judged closed, because §7 asks for measured behaviour and
`docs/EVIDENCE.md:34-45` already records all eight as `⬜ none` or `🟡 partial`.

| Gate | Implementing code | State |
|---|---|---|
| **H1** hardware WDT; hang → reset | `src/power.cpp:35-58` arms `NRF_WDT` (`CRV` at `:54`, `TASKS_START` at `:56`); armed at 120 s from `src/main.cpp:48,151`; fed at `src/main.cpp:181,210,215,324,337` and inside the long sensor paths (`src/diagnostics/owscan.cpp:220,288,330,417`, `src/diagnostics/busscan.cpp:128`) | **Implemented in source.** Caveat below. |
| **H2** deep sleep + Class A radio sleep | `src/power.cpp:73-151`: `Radio.Sleep()` `:79`, `SPI_LORA.end()` `:86`, `TinyUSBDevice.detach()` `:121`, wait loop `:134-136` | **Partial.** Radio/peripheral sleep is real. "Deep sleep" is not: `:134-136` is a `delay(1000)` loop on the FreeRTOS idle task, and the comment at `:129-133` states plainly it is not the chip's deepest state. |
| **H3** brownout: no flash thrash; skip TX when low | Class at `src/power.cpp:153-268`; thresholds `src/power.h:63-64` (960/1020 cV); `transmit_allowed()` gates TX at `src/main.cpp:264`; `flash_write_allowed()` gates the interval write at `src/main.cpp:303`; no-write-if-unchanged at `src/config.cpp:185-187` | **Partial — see finding 3.2.** One flash writer is ungated. |
| **H4** bounded backoff; survive multi-day no-GW | `src/radio.cpp:410-421` (doubling, clamped to `kBackoffMaxSeconds`); applied at `src/main.cpp:318-320` | **Implemented in source.** The only gate the repo's own 2026-08-04 audit calls implemented (`docs/EVIDENCE.md:771`). |
| **H5** interval + keys survive power loss | Interval: `src/config.cpp:89-145` over `InternalFS`. Session: `src/session.cpp:210-231`, counter at `:233-247`, called from `src/radio.cpp:244,342`. Keys: compiled in from `src/secrets.h` / `src/secrets.example.h` (`src/radio.cpp:12-14`) | **Implemented in source**, but "keys survive power loss" is trivially true only because keys are in the image, not in flash storage. Interval survival is untested (`docs/EVIDENCE.md:42`). |
| **H6** RK900 absent → no livelock | Bounded read: `src/sensors/modbus.cpp:29` (`kReplyTimeoutMs = 1000`), loop bounded at `:93`, returns `Timeout` at `:107,133`; caller tolerates failure at `src/sensors/rk900.cpp:72-75` (returns all-invalid, does not retry forever) | **Implemented in source.** |
| **H7** BMS silent → no livelock | Bounded read: `src/sensors/battery.cpp:351-352` (`kFirstByteTimeoutUs = 500000`, `kInterByteTimeoutUs = 5000`), frame loop at `:962-1001`; provisioning window bounded at `:875` (`kProvWindowMs`) | **Implemented in source.** Worst case is long — `docs/EVIDENCE.md:627` records `acquire_pid()` measured at 45.4 s of a 50.5 s wake — but it is bounded and the WDT window is 120 s. |
| **H8** bench soak ≥24 h; field shadow ≥7 d | **Nothing in code implements this and nothing can.** It is a process gate. No soak log exists; `docs/EVIDENCE.md:45` records `⬜ none` | **Aspirational, correctly so.** |

### 1.1 H1 caveat — the watchdog is paused across sleep (by design, but worth stating)

`src/power.cpp:51-52` sets `WDT_CONFIG_SLEEP_Pause`, so the watchdog does not count while
the CPU sleeps. `src/power.h:6` documents this: "The watchdog guards the awake part of the
cycle only." That matches H1's wording ("Modbus/BMS hang → reset"), since both hangs are
awake-path. **Not drift** — recorded so a future reader does not mistake it for a defect.
*Inference:* a hang that somehow occurred inside the sleep loop would not be caught; I did
not find a mechanism by which `delay()` hangs, so I am not raising this as a finding.

---

## 2. ADR compliance

| ADR | Status in file | Code match |
|---|---|---|
| **ADR-0001** build/flash on build host (`:3` Accepted) | Process ADR — no code surface. `scripts/` not audited (read-only constraint on that directory). | n/a |
| **ADR-0002** payload contract conflicts (`:3` **Open**) | Open by design. Code paths enumerated in §2.2 below. | Consistent — code carries the conflict unresolved and says so at `src/reading.h:56`. |
| **ADR-0003** framework (`:3` Superseded in part by ADR-0005) | `src/radio.cpp:9` includes `LoRaWan-Arduino`, not the WisBlock-API-V2 event framework; `main.cpp` owns its own loop. | Matches the **superseding** ADR-0005, not ADR-0003's literal text. Correct. |
| **ADR-0004** BMS on one-wire, RAK5802 dedicated to RK900 (`:3` Accepted) | Two separate buses confirmed: RK900 on hardware `Serial1` at `src/sensors/rk900.cpp:44,65`; battery on a bit-banged `SoftwareHalfSerial` on its own pin, `src/sensors/battery.cpp:964,1114-1115`. `src/sensors/battery.cpp:1100-1101` explicitly notes the pack is *not* routed through the `WB_IO2` rail that `rk900.cpp` drops. No shared bus, no baud switching. | **Matches.** ADR-0004's own verification section (`:74-76`) still says "Not yet verified on hardware" — that text is now stale; `docs/EVIDENCE.md:518` records a working one-wire read 2026-08-05. Documentation lag, not code drift. |
| **ADR-0005** direct SX126x (`:3` Accepted) | `src/radio.cpp:98,103,144,150,184-185` use `lmh_*` directly; join/backoff/sleep/watchdog owned in-repo per ADR-0005:50. | **Matches.** |
| **ADR-0006** RK900 baud/register map (`:3` Accepted, 9600) | `src/sensors/rk900.cpp:16` `kBaud = 9600`; register span `:21-22` (`0x0000`, 5 regs); map `:24-30`. | **Matches. `docs/HARDWARE.md:183` does not — see §3.1.** |
| **ADR-0007** no second voltage source (`:3` Accepted) | Brownout is driven solely from the pack reading: `src/main.cpp:221` `brownout.update(pack.voltage.valid, pack.voltage.value)`. No `PIN_VBAT` / `WB_A0` / `AIN` read found in `src/`. | **Matches.** |

### 2.2 ADR-0002 — code paths whose correctness depends on the current-sign resolution

Not resolved here, per instruction. The conflict (`ADR-0002:23-24`): the spec says negative
= charging, the live decoder's type 185 says positive = charging.

Every path that would change behaviour or meaning depending on which way it resolves:

1. `src/sensors/battery_frame.cpp:239` — `out.current.set((int16_t)val16(i + 2))`. The
   parse. Takes the pack's two's-complement word verbatim with no sign normalisation. If
   the pack's convention is the spec's, this value reaches TTN inverted.
2. `src/payload.cpp:94` — `put_s16(kChBattAmps, kTyBattAmps, b.current.value)` inside
   `Payload::add(const BatteryReading &)`. Channel 22, type 185 (`src/payload.cpp:14`).
   Emits the raw value; the encoder performs **no** sign conversion.
3. `src/payload.cpp:143` — the same `put_s16` call again inside `Payload::build(...)`. This
   is a **second, independent** emission site for the same field. Any sign fix applied to
   one and not the other silently diverges the two build paths.
4. `src/payload.cpp:45` — `put_s16`'s comment "the decoder sign-extends from 16 bits",
   i.e. the wire format is settled and only the *meaning* of the sign is not.
5. `src/sensors/battery.cpp:1446-1450` — the console print. Renders `-`/`+` from the raw
   value, so **bench logs inherit whichever convention is wrong**. A bench operator reading
   `+0.00 A` cannot tell charge from discharge until this closes.
6. `src/reading.h:56` — the field declaration, correctly annotated
   `// sign convention unresolved, see ADR-0002`.

Nothing in `src/` makes a *control* decision from the current sign (charge detection,
brownout, TX gating) — I grepped every `.current` use and found only the six above. So the
blast radius is **data interpretation, not node behaviour**. That is the useful half of the
answer: resolving it wrongly corrupts the record, it does not strand the node.

---

## 3. Other spec-versus-code drift

### 3.1 `docs/HARDWARE.md:183` still specifies 4800 — the document is stale, the code is right

- `docs/HARDWARE.md:183`: "RAK5802 → RK900 **data only** … fixed **4800** 8N1, slave `0x01`.
  No baud switching."
- `docs/decisions/ADR-0006-rk900-baud-and-register-map.md:1,3`: accepted at **9600**,
  settled by direct measurement (`:27-28`: this physical unit "replies at 9600 and gives
  zero bytes at 4800 across four consecutive sweeps").
- `src/sensors/rk900.cpp:16`: `constexpr uint32_t kBaud = 9600`.
- `docs/EVIDENCE.md:1011` and `:1115` record the 9600 capture and the original 4800 failure.

**`HARDWARE.md` is the stale one. Code and ADR agree at 9600.** ADR-0006:139 leaves
"reprogram this unit to 4800 for fleet consistency" open, so the 4800 figure is not
*wrong forever* — but it is wrong today, and `HARDWARE.md` presents it as settled fact with
"No baud switching" alongside it. **Cost if unfixed: a bench session.** Anyone wiring from
`HARDWARE.md` and then debugging a silent bus at 4800 repeats a failure this project has
already paid for once.

Same paragraph, `docs/HARDWARE.md:188-189`, describes the rejected shared-bus option with a
"4800/9600 baud switch" — that line is still accurate and is not the problem.

### 3.2 One flash writer is not gated by the brownout hold (H3, "no flash thrash")

`src/main.cpp:303` correctly gates the interval write:
`if (cmd.set_interval && brownout.flash_write_allowed())`.

`src/session.cpp:233-247` `maybe_save_counter()` also writes flash (`save()` at `:247`,
`InternalFS` at `:55-68`). It is called from `src/radio.cpp:342` after every send, and
`src/radio.cpp:244` calls `session::save()` directly after a join. **Neither call site
consults `brownout.flash_write_allowed()`** — I grepped `src/session.cpp` and `src/radio.cpp`
for it and found no reference; the only two consumers are `src/main.cpp:303` and the
declaration at `src/power.h:166`.

Bounded, not unbounded: the brownout hold suppresses most transmits (`src/main.cpp:264`), so
the post-send writer is largely starved, and `src/session.cpp:243` skips writes when the
counter has not advanced. The exposed path is the keepalive transmit
(`src/main.cpp:255,299`) — which by design fires *while* the hold is engaged, sends, and
therefore reaches `src/radio.cpp:342`.

*Inference, labelled as such:* I did not trace whether the keepalive's counter always
advances past `s_saved_counter_ceiling`, so I cannot say how many writes per keepalive this
is. The structural gap — a flash writer with no brownout gate, reachable on the one path
that runs during brownout — is fact.

**Rank: data/longevity, not a hike.** H3's letter says "no flash thrash"; this is a hole in
that claim, not a node-stranding defect.

### 3.3 Cadence and downlink contract — verified clean, no drift

Checked because a mismatch here is a silent field defect. All match:
`fPort 10` command port (`src/radio.cpp:22` vs `FIRMWARE_SPEC.md:113`) and it is genuinely
enforced (`src/radio.cpp:378` rejects any other port); opcodes `0x01`/`0x03`
(`src/radio.cpp:28-29,389,398`); exact-length validation at `src/radio.cpp:389`
("exact lengths, not minimums", `:385-388`); interval default 3600 s and max 86400 s
(`src/config.h:83-93`); 900 s fair-use floor with a compile-time assertion defending it
(`src/config.h:62`, assertion text at `:150`); uplink on port 2 (`src/radio.cpp:23`), which
the spec does not constrain.

### 3.4 `FIRMWARE_SPEC.md:200` USB rule — code complies

The spec forbids clearing `NRF_USBD->ENABLE` and forbids `Serial.end()` on the sleep path.
`src/power.cpp:99-122` does neither; it detaches only. Comments at `:103-114` restate the
rule. **Compliant.** Noted because §4.1 below shows the ledger still claims otherwise.

---

## 4. `docs/EVIDENCE.md` versus reality

The **live gate table** (`docs/EVIDENCE.md:30-45`) is honest. It marks H1, H2, H3, H4, H7,
H8 as `⬜ none` and H5, H6 as `🟡 partial` with the partiality spelled out. It does not
overclaim. `:32` states "None of these can be closed by inspection," which is the correct
posture and is consistent with everything in §1 above.

One overclaim, in a dated log entry rather than the gate table:

### 4.1 `docs/EVIDENCE.md:770` (2026-08-04 audit) describes a sleep path the code no longer has

That row reads: "H2 sleep | `SPI_LORA.end()`, `Serial.end()`, `NRF_USBD->ENABLE = 0` all
present on the sleep path".

Two of those three are now **forbidden** by `FIRMWARE_SPEC.md:200` and absent from
`src/power.cpp:73-151`. The later entry at `docs/EVIDENCE.md:471` ("USB CDC death
root-caused in source") is the reversal. The log is append-only and newest-first, so this is
arguably working as intended — but the row is phrased as a standing source audit, and it is
the exact claim a future reader would grep for when re-checking H2.

**Rank: untidy.** No hardware consequence. Worth a correction line because it contradicts a
release-blocking spec clause.

### 4.2 Dating and provenance — clean

Spot-checked the entries the assignment targets. Each carries a date in the heading, and the
substantive ones carry a commit SHA and host: `:598` ("`b6bbf31`, same host, same day"),
`:518` (Stage 3, 12.23 V, seven cycles), `:1011` (RK900 five-register frame at 9600),
`:143` (`stage3` at `9c35e2f`). `:53-58` documents that ordering is by commit date, not
heading date. `:709` is an explicit **retraction** of a fabricated external blocker, and
`:762-792`'s audit labels itself "source-verified only" with the verdict "no gate closes
here." **I found no measured claim missing a date, and no undated SHA-less measurement in
the sections I read.** I did not read all 1476 lines — see §6.

### 4.3 One stale doc outside EVIDENCE

`docs/decisions/ADR-0004-bms-one-wire-path.md:74-76` — "Verification: Not yet verified on
hardware. Stage 3 must demonstrate: one good BMS frame over one-wire…". `docs/EVIDENCE.md:518`
records exactly that, on 2026-08-05. The ADR was never updated. **Untidy** — the risk is a
reader concluding Stage 3 is unproven and re-running a bench test that already passed.

---

## 5. Self-attack — what I dropped

Findings considered and **discarded** for lack of a specific file and line, or because they
dissolved on inspection:

- *"The watchdog is paused during sleep, so a sleep hang is unrecoverable."* Real register
  behaviour (`src/power.cpp:51-52`) but explicitly documented as intended at `src/power.h:6`
  and consistent with H1's wording. Not drift. Downgraded to the note at §1.1.
- *"H2 is unimplemented because it is not System OFF."* Overstated. Radio and SPI sleep are
  real and are the dominant term; the spec says "deep sleep" without naming a chip state.
  Downgraded to "Partial" with the code's own admission cited (`src/power.cpp:129-133`).
- *"`get_battery_level()` returns 255, so the network never sees pack state."* Deliberate
  and documented at `src/radio.cpp:126-131`. Not drift.
- *"Two `put_s16` current call sites is a bug."* It is a duplication hazard, not a defect —
  both currently emit the same thing. Kept only as item 3 in the ADR-0002 dependency list.
- *Anything about `scripts/` or CI gates.* Read-only constraint plus budget; not opened.

### Ranking of surviving findings

| Finding | Requires a hike? | Loses data? | Merely untidy? |
|---|---|---|---|
| §3.1 `HARDWARE.md:183` 4800 vs code 9600 | No | No — **costs a bench session** | No |
| §3.2 `session.cpp` flash write ungated by brownout | No (bounded) | Flash wear during brownout; possible session loss if a write is torn | No |
| §2.2 ADR-0002 sign, 6 dependent paths | No | **Yes, if resolved wrongly** — record is inverted, node behaviour unaffected | No |
| §1 H2 "deep sleep" is idle sleep | No | No — power-budget accuracy only | Partly |
| §4.1 EVIDENCE 2026-08-04 H2 row | No | No | Yes |
| §4.3 ADR-0004 "not yet verified" | No | No — risks a redundant bench run | Yes |

---

## 6. Not verified, and why

- **Nothing was built, flashed, run, or measured.** Every H1–H8 judgement in §1 is source
  reading, which `docs/EVIDENCE.md:774` itself calls "the weakest form of evidence this repo
  accepts." No gate is closed by this document.
- **`docs/EVIDENCE.md` read in part, not whole** (1476 lines; budget). I read `:1-58`,
  `:762-792`, and the headings index. §4.2's "no undated claims" therefore covers the gate
  table and the H1–H8 audit entry, **not** all 40-odd log entries.
- **`scripts/` and `platformio.ini` not opened** — read-only constraint. So the CI/preflight
  gates (`check_citations.py`, `check_decoder_parity.py`) are unaudited, and I cannot say
  whether `HARDWARE.md:183`'s stale 4800 would be caught by a citation check.
- **Decoder parity not checked against the live TTN formatter** — it lives in the
  `forest-weather-machines` sibling repo, outside this workspace. So §2.2's channel/type
  claims rest on `src/payload.cpp:14` and `FIRMWARE_SPEC.md:208-218`, not on the decoder.
- **`src/` was being edited concurrently** by another agent during this audit
  (`power.cpp`, `radio.cpp`, `build_features.h`, `main.cpp` all had timestamps inside the
  session window). Line numbers should be re-confirmed before any of these are cited in a
  commit.
- **Worst-case awake time not measured**, so H1's 120 s window at `src/main.cpp:48` is
  unvalidated against `battery.cpp`'s bounded-but-long provisioning path
  (`docs/EVIDENCE.md:627` puts `acquire_pid()` at 45.4 s of a 50.5 s wake — inside the
  window, with less margin than the number suggests once a full RK900 poll is added).
