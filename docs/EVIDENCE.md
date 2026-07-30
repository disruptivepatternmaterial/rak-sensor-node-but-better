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
| H5 | Interval + keys survive power loss | Set interval, cut power, confirm retained | ⬜ none |
| H6 | RK900 absent → no livelock | Unplug sensor → cycle continues | ⬜ none |
| H7 | BMS silent → no livelock | Unplug BMS data → cycle continues | ⬜ none |
| H8 | Bench soak ≥24 h, field shadow ≥7 d | Soak log + TTN ingest history | ⬜ none |

Also outstanding, from [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §9: one good RK900 frame, one
good BMS frame, one TTN uplink, one interval downlink applied.

## Power budget

Projections live in [`POWER_BUDGET.md`](POWER_BUDGET.md). A projection is a hypothesis;
only a measurement recorded here closes it.

## Log

Newest first.

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
