# Code-deletion plan — verified pass, 2026-08-12

Read-only review. Nothing in this pass was deleted, edited, or moved. The only file written is
this one. No build, no flash, no SSH, no serial.

Baseline measured this pass (`wc -l` over `src/**/*.{c,cpp,h}`, 28 files):

**`src/` total = 6081 lines.** 10% = 609 lines.

The prior pass measured 5894. The delta is other agents' in-flight edits, not a counting error.
Every line range below was read at HEAD `b436aa9` with `src/diagnostics/owscan.cpp` being
modified concurrently — **re-verify each range against the file before cutting.** Ranges are
given newest-last so an executor working bottom-up in each file does not invalidate later ones.

---

## 1. Verification of the prior pass's claims

| Claim | Verdict | Measurement |
|---|---|---|
| `battery.cpp` is 917 comment / 462 code | **CONFIRMED, exactly** | 1477 total = 98 blank + 917 `//` + 0 block + 462 code. Comments are 62.1% of the file. |
| Nine constants declaration-only and dead | **CONFIRMED — all nine** | Repo-wide `grep -rw` (src, scripts, tests, ini, md) returns exactly 1 hit each: the declaration. |
| "roughly another 200 lines of the same kind" | **REJECTED as stated** | The dead-experiment comment mass is ~120 lines, not ~200, and ~50 of those are `CITE(...)` lines that rule 20 protects. See §4. |
| 48 redundant lines in `platformio.ini` | **CONFIRMED (49)** | 7 envs each carry `extends = common` *and* restate 7 inherited keys. 7 × 7 = 49. |
| ~512 removable lines in `src/` (8.7%) | **REJECTED** | Only reachable by counting whole comment blocks including their citations and bench facts. The honest figure is far lower. See §6. |

### The nine dead constants — named and individually confirmed

All in `src/sensors/battery.cpp`. Each has exactly one repo-wide occurrence (its own declaration).
All nine are remnants of the deleted PARAMGET/PARAMSET "sensor enable pass".

| Line | Constant | Repo-wide refs |
|---|---|---|
| 168 | `kPldParamSnsrUpdate` | 1 |
| 180 | `kParamSid` | 1 |
| 181 | `kParamIntv` | 1 |
| 182 | `kParamRule` | 1 |
| 183 | `kParamBytes` | 1 |
| 185 | `kParamIntvMax` | 1 |
| 209 | `kParamAckTimeoutUs` | 1 |
| 210 | `kParamAttempts` | 1 |
| 225 | `kEnablePassBudgetMs` | 1 |

**Correction to the prior pass's neighbourhood:** `kRuleDisable` (169) and `kRulePeriodic` (170)
look like part of the same dead set and are **not**. Both are live at
`battery.cpp:795-796`, where `provision()` prints whether each announced sensor descriptor reads
`periodic` or `DISABLED`. That log line is the evidence that falsified the enable-pass
hypothesis in the first place. **Do not delete 169-170.**

Also checked and found live, so not candidates: `kPldBoot` (used at :595), `kPayloadSendData`
(:604), `kProvCapacity` (`static_assert` at :1142), `kInterByteTimeoutUs` (:1001),
`kProvSnsrOffset` (:783).

No dead functions exist. `battery_result_name`, `dump`, `send_boot`, `ladder_allowed`,
`provision`, `acquire_pid` all have live call sites.

`FEATURE_BATTERY_MODBUS`, `FEATURE_BATTERY_PARAM_PASS` and `FEATURE_ONEWIRE_SPLIT` are **not**
dead feature flags — they appear only inside comments, as names of things that were deliberately
never built or already removed. There is no `#if` on any of them and no environment sets them.
They are narrative, and they are handled in §3/§4 as comment text, not as code.

---

## 2. Deletion order and running total

Tier 1 is unconditional: no `CITE(...)` line, no bench fact, no datasheet figure is touched.
Tier 2 requires a relocation step and is only safe if that step is performed.

### Tier 1 — unconditional, zero citations touched

| # | File | Lines | Count | Justification |
|---|---|---|---|---|
| 1 | `src/sensors/battery.cpp` | 142-155 | 14 | Prose describing how to drive the deleted enable pass (`set.param(pid, sid, enable, intv)`). Describes an operation the file no longer performs. CITE lines 156-167 are **kept** (Tier 2 decides their home). |
| 2 | `src/sensors/battery.cpp` | 168 | 1 | `kPldParamSnsrUpdate` — dead constant. |
| 3 | `src/sensors/battery.cpp` | 171-175 | 5 | Blank + prose sizing `SNHub_Api_Param_Snsr_t`, the wire struct of the deleted pass. CITE 176-179 kept. |
| 4 | `src/sensors/battery.cpp` | 180-186 | 7 | `kParamSid/Intv/Rule/Bytes` (180-183), blank, `kParamIntvMax` (185), blank — five dead constants. |
| 5 | `src/sensors/battery.cpp` | 187-200 | 14 | Prose on ack budget and retry count for a write the driver no longer sends. CITE 201-208 kept. |
| 6 | `src/sensors/battery.cpp` | 209-211 | 3 | `kParamAckTimeoutUs`, `kParamAttempts`, blank — two dead constants. |
| 7 | `src/sensors/battery.cpp` | 212-221 | 10 | Prose budgeting a pass that does not exist; the watchdog arithmetic it performs is for 54 s of writes never issued. CITE 222-224 kept. |
| 8 | `src/sensors/battery.cpp` | 225 | 1 | `kEnablePassBudgetMs` — dead constant. |
| 9 | `src/sensors/battery.cpp` | 1221-1237 | 17 | "There is deliberately no parameter-write phase here" — prose narrating a falsified hypothesis and its removal. CITE 1238-1242 kept in place. **Lower confidence — see §5.** |

**Tier 1 subtotal: 72 lines. Running total 72 / 6081 = 1.18%.**

All nine dead constants are covered by items 2, 4, 6, 8.

### Tier 2 — safe only with relocation

These are the `CITE(...)` blocks left standing by Tier 1. They document the RUI3 PARAMSET wire
format and RAK's retry budget — real, verified, expensive knowledge — attached to code that no
longer exists. Rule 20 forbids deleting them. They may leave `src/` **only** by being moved
verbatim, citation keys intact, into a new ADR (suggested:
`docs/decisions/ADR-00NN-no-parameter-write-pass.md`) which the surviving three-line comment at
the old site links to.

| # | File | Lines | Count | Content |
|---|---|---|---|---|
| 10 | `src/sensors/battery.cpp` | 156-167 | 12 | `CIT-ONEWIRE-SERIAL` — `PLD_PARMGSET_TYPE_E` enum and `api_set_snsr_param()`. |
| 11 | `src/sensors/battery.cpp` | 176-179 | 4 | `CIT-ONEWIRE-SERIAL` — `SNHub_Api_Param_Snsr_t` field widths, `verify_snhublen()`. |
| 12 | `src/sensors/battery.cpp` | 201-208 | 8 | `CIT-WISTOOLBOX-AT` retries/timeout, `CIT-ONEWIRE-SERIAL` NULL `.req`/`.rsp`. |
| 13 | `src/sensors/battery.cpp` | 222-224 | 3 | `CIT-NRF-WDT` — watchdog cannot be stopped. **Consider keeping in place**; it constrains any future long-running path, not just the deleted one. |
| 14 | `src/sensors/battery.cpp` | 1238-1242 | 5 | `CIT-MESHTASTIC-9154` + `CIT(bench)` — the reference never writes parameters; descriptors read 0x0008. |
| 15 | `src/sensors/battery.cpp` | 332-348 | 17 | The `FEATURE_ONEWIRE_SPLIT` two-wire experiment narrative, incl. 2 CITE lines. Abandoned approach; carries wiring context. **Verify against `battery.h:103`, which still references the name, before cutting.** |

**Tier 2 subtotal: 49 lines. Running total 121 / 6081 = 1.99%.**

### Outside `src/` — not counted in the percentage

| # | File | Lines | Count | Justification |
|---|---|---|---|---|
| 16 | `platformio.ini` | 54-57, 59-61 | 7 | `[env:rak4631]` restates 7 keys already inherited via `extends = common`. |
| 17 | `platformio.ini` | 98-101, 103-105 | 7 | `[env:stage1]` — same. |
| 18 | `platformio.ini` | 122-125, 127-129 | 7 | `[env:busscan]` — same. |
| 19 | `platformio.ini` | 150-153, 155-157 | 7 | `[env:owscan]` — same. |
| 20 | `platformio.ini` | 169-172, 174-176 | 7 | `[env:stage2]` — same. |
| 21 | `platformio.ini` | 198-201, 203-205 | 7 | `[env:battdiag]` — same. |
| 22 | `platformio.ini` | 223-226, 228-230 | 7 | `[env:stage3]` — same. |

**49 lines, 21% of `platformio.ini`.** Mechanical and behaviour-preserving: `extends = common`
already supplies every one of these values. Confirm with a `pio project config` diff before and
after — that is a build-host operation for the executing agent, not this pass.

Do **not** touch `[env:native]` (75-88): its `platform`, `build_flags` and `build_src_filter`
are genuinely different, and the comment above it records the C++11 aggregate-initialisation bug
from `df15867` that a green host suite once hid.

---

## 3. Final figure

| Scope | Lines | % of `src/` (6081) |
|---|---|---|
| Tier 1 (unconditional) | 72 | **1.18%** |
| Tier 1 + Tier 2 (with relocation to an ADR) | 121 | **1.99%** |
| `platformio.ini` (separate file, not `src/`) | 49 | — |

**The honest number is 1.99% of `src/`, and only 1.18% without a relocation step.**

---

## 4. Why the 10% target is not honestly reachable

The 8.7% figure is arithmetically achievable only by counting the comment blocks in
`battery.cpp` in full. Having read them, that is the wrong call. The file's 917 comment lines
break down roughly as:

- **~120 lines** documenting removed or falsified experiments — the material in §2 above.
- **~350 lines** of `CITE(datasheet)` / `CITE(prior-art)` / `CITE(bench)` — the RUI3 struct
  layouts, the `SoftwareHalfSerial` port-register rationale, the captured announcement byte
  offsets, the GPIOTE start-bit timing. Rule 20 protects these outright.
- **~400 lines** of negative knowledge: constraints the code cannot express. `kTurnaroundMs`
  exists because a reply sooner than 2 ms is not heard. `kWakeCount` is four because three
  milliseconds of "free" latency broke the link. `VER3` is the only accepted provision type
  because every other value returns `RET_ERROR`. Deleting any of these re-opens a bug that cost
  bench days. These are the most expensive lines in the repo per byte, and they read as verbose
  precisely because the constraint is not visible in the code.

I found essentially **no** comments that merely narrate the next line. That class of cruft — the
easy 10% — is not present in this codebase. What is present is a very high ratio of
constraint-documentation, which is a different thing and is load-bearing.

There is also no dead code beyond the nine constants: no unreferenced functions, no `#if` on a
flag no environment sets, no commented-out code blocks. `src/` is small and dense.

**Reaching 10% (609 lines) would require deleting roughly 490 lines of citations and measured
hardware facts.** That trade is bad: the comment volume is the asset, not the debt. Reported
figure is 1.99%.

If the 10% goal is about repo weight rather than `src/` specifically, the productive direction
is relocation, not deletion — moving the experiment archaeology and the long derivation
narratives out of `battery.cpp` and into `docs/decisions/` ADRs, where they remain citable and
searchable but stop sitting between a reader and 462 lines of code. That could plausibly move
300-400 lines out of `src/` (5-7%) with nothing lost, but it is a *move*, and it should be
authorised as such rather than smuggled in under a deletion budget.

---

## 5. Do-not-delete

**`src/diagnostics/` in its entirety** — `owscan.cpp` (565), `owscan.h` (45), `busscan.cpp`
(175), `busscan.h` (34), and the `[env:battdiag]` build path. 819 lines, and the single largest
apparent "unused code" block in the repo. It is not unused. These are the only instruments for
debugging silent hardware, they are in use this hour, and `owscan.cpp` is being edited
concurrently by another agent. A diagnostic that no production path calls is not dead code. The
duplicated scaffolding across `owscan` and `busscan` is also **not** a deduplication target:
sharing a helper between two diagnostics means a bug in the shared helper lies to you on both
buses at once, which defeats the purpose of having two.

**Every `CITE(...)` line**, every datasheet figure, every `CITE(bench)` observation. Rule 20.
Tier 2 above moves five such blocks and loses none of them.

**`kRuleDisable` / `kRulePeriodic` (`battery.cpp:169-170`)** — look dead, are live at :795-796.

**The negative-knowledge comments**, specifically: `battery.cpp` 13-26 (no raw-Modbus path),
49-90 (`kWakeCount`, wake-byte run), 93-126 (`kTurnaroundMs`, the 2 ms rule), 227-242 (VER3 is
the only accepted type), 244-320 (provision struct offsets, port-register bit timing), 1095-1120
(no rail to raise, direction of the exchange). Each documents a constraint that is invisible in
the code and was learned by failing.

**`platformio.ini` 1-17 and 63-88** — the vendored-board rationale and the C++11 host-suite trap.

### Lower confidence, flagged rather than recommended

- **Item 9 (`battery.cpp` 1221-1237).** This is archaeology, but it is *warning* archaeology: it
  tells a future agent not to re-implement a falsified parameter pass. Deleting the prose and
  keeping only the CITE lines at 1238-1242 may not carry the warning. Safer alternative: compress
  to four lines pointing at the ADR. I do not recommend a straight cut without that.
- **Item 15 (`battery.cpp` 332-348).** `battery.h:103` still names `FEATURE_ONEWIRE_SPLIT` in its
  own comment. Cutting one and leaving the other creates a dangling reference. Handle both or
  neither.
- **Item 13 (`battery.cpp` 222-224).** The `CIT-NRF-WDT` fact constrains any future long-running
  awake path, not only the deleted pass. Keeping it in place costs three lines.
- **`battery_frame.h` / `battery.h` (121 and 136 comment lines).** Not examined line-by-line
  within budget. Both looked citation-dense on sampling. Not proposing anything there.
- **`radio.cpp`, `power.cpp`, `main.cpp`, `session.cpp`** — comment ratios of 24-35%, which is
  normal. Sampled the archaeology greps; hits were all "deliberately no X" guard comments, which
  are load-bearing. No candidates proposed.

---

## 6. Execution notes for the next agent

1. Work bottom-up within `battery.cpp` (item 9 first, then 8 → 1) so earlier line numbers stay valid.
2. Re-run the ranges against the live file first: `sed -n '142,155p' src/sensors/battery.cpp` etc.
   The file may have moved under concurrent edits.
3. Do Tier 2 only after the ADR exists and contains the blocks verbatim, keys intact.
4. `scripts/preflight.sh` gates this — `scripts/check_citations.py` will fail if a CITE is dropped
   rather than moved. That is the safety net; let it do its job.
5. `platformio.ini` items 16-22 should land as their own commit, verified with a
   `pio project config` diff on the build host.
