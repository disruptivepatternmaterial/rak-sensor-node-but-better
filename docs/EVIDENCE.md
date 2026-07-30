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

Newest first. No entries yet — hardware is on order and there is no firmware in-tree.

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
