# Power budget

🚧 **Worksheet, not a result.** Every figure below is `TBD` until it is either read from a
manufacturer datasheet (cited) or measured on the bench and recorded in
[`EVIDENCE.md`](EVIDENCE.md). **Do not fill these in from memory or from a blog post** —
an invented current figure produces a confident, wrong runtime estimate, and the cost of
being wrong is a hike.

Rules: [`.cursor/rules/50-power-management.mdc`](../.cursor/rules/50-power-management.mdc)

## Why this dominates the design

Success is months unattended with **no solar in the ordered BOM**
([`HARDWARE.md`](HARDWARE.md) — Unify 910406, no solar). At the default 3600 s interval the
node is awake for a few seconds per hour, so **sleep current sets the runtime**, not the
radio.

A worked example of the asymmetry: at a 3600 s interval, a 5-second wake at 30 mA costs
about 0.042 mAh per cycle. One stray milliamp of sleep current costs about 1 mAh over the
same hour — **roughly 25× more**. This is why `Serial.end()` before sleep is a
release-blocking requirement and not a micro-optimization.

## Model

Average current over one cycle:

```
I_avg = (I_sleep × t_sleep + Σ(I_phase × t_phase)) / t_cycle
runtime_hours = usable_pack_capacity_mAh / I_avg_mA
```

## Inputs — fill from datasheet, then confirm by measurement

| Phase | Duration | Current | Source | Status |
|---|---|---|---|---|
| Sleep (System OFF class) | ~`t_cycle` | TBD | [CIT-NRF-POWER] + measured | ⬜ |
| Sleep **with radio left awake** (defect case) | — | ~6 mA observed | [CIT-RAK-SLEEP] | reference |
| Failed-join retry loop (defect case) | unbounded | never sleeps | [CIT-RAK-SLEEP] | reference |
| MCU wake + init | TBD | TBD | measured | ⬜ |
| RS-485 enabled, RK900 poll | ≤ 1 s per txn, ≤ 2 retries (`FIRMWARE_SPEC.md` §2.1) | TBD — includes RAK5802 transceiver | [CIT-RAK5802] + measured | ⬜ |
| BMS poll | ≤ 1 s per txn | TBD | measured | ⬜ |
| LoRa TX | depends on DR/SF and payload | TBD at US915 TX power | [CIT-SX1262] | ⬜ |
| RX1 + RX2 windows | per Class A | TBD | [CIT-LW-LINK] + [CIT-SX1262] | ⬜ |
| Flash write (interval change only) | rare | TBD | [CIT-NRF-POWER] | ⬜ |

| Pack | Value | Source |
|---|---|---|
| RAK9154 usable capacity | TBD | [CIT-RAK9154] |
| Buck 12 V → 5 V efficiency | TBD | buck module datasheet (part not yet selected) |

The buck converter's **quiescent draw is a load 24/7** and is easy to forget. A buck with
poor no-load efficiency can dominate the entire budget regardless of firmware quality.
Select the part with this in mind and record the figure here.

## Interval sensitivity

Complete once `I_sleep` and per-phase costs are measured. The interval is downlink-settable
across 300–86400 s (`FIRMWARE_SPEC.md` §4), so runtime must be understood across the range,
not just at the default.

| Interval | Cycles/day | I_avg | Projected runtime |
|---|---|---|---|
| 300 s (minimum) | 288 | TBD | TBD |
| 3600 s (default) | 24 | TBD | TBD |
| 86400 s (maximum) | 1 | TBD | TBD |

Note the interaction with airtime: 288 uplinks/day at the 300 s minimum pushes hard against
the TTN Fair Use budget of 30 s/day [CIT-TTN-FUP]. On TTN's US915 plan the slowest uplink is
SF10BW125 [CIT-TTN-FREQ], and a 10-byte payload at the slow end of the range allows only
tens of messages per day within the budget [CIT-TTN-FUP-EXPLAINED]. **At the short end of
the interval range the binding constraint is airtime, not battery.** Compute both and
report whichever is tighter.

## Brownout thresholds

| Threshold | Value | Behavior |
|---|---|---|
| TX inhibit | TBD | Skip uplink, keep sleeping (H3) |
| Flash-write inhibit | TBD | Never write during sag — corrupts stored interval/keys (H3, H5) |
| Recovery hysteresis | TBD | Prevents oscillating at the threshold |

Pack voltage, current, and SoC are readable over the BMS link (`FIRMWARE_SPEC.md` §2.2), so
these are measurable rather than guessed. The **battery current sign convention is
unresolved** — see [ADR-0002](decisions/ADR-0002-payload-contract-conflicts.md). Do not
write charge/discharge logic until it is closed.

## Changing anything power-related

1. Cite the manufacturer figure for the claim.
2. Recalculate this page.
3. Record a **measured** sleep current in [`EVIDENCE.md`](EVIDENCE.md) before calling it done.
