# Power budget

🚧 **Worksheet, not a result.** Every figure below is `TBD` until it is either read from a
manufacturer datasheet (cited) or measured on the bench and recorded in
[`EVIDENCE.md`](EVIDENCE.md). **Do not fill these in from memory or from a blog post** —
an invented current figure produces a confident, wrong runtime estimate, and the cost of
being wrong is a hike.

Rules: [`.cursor/rules/50-power-management.mdc`](../.cursor/rules/50-power-management.mdc)

## The system is solar-recharged — correcting an earlier error

An earlier version of this page said there was "no solar in the ordered BOM." **That was
wrong**, and it mattered: it framed the whole budget as a countdown to an empty pack.

The confusion was between two different things. The *enclosure* (Unify 910406) has no
solar. The **RAK9154 does** — it ships with a 10 W panel (34 × 27 cm), an integrated
18 V-input charge controller, a BMS, and a heater [CIT-RAK9154-SOLAR]. The deployment uses
the large-panel variant.

## Why this dominates the design

The goal is **indefinite unattended operation**, not a runtime figure. That changes the
question from "how long until it dies?" to:

> Can the pack carry the node through the longest plausible stretch with no meaningful
> solar harvest, and still be above the level it can recover from?

Three things follow, and they reorder the priorities:

**1. The node's own draw is almost certainly not the binding constraint.** A well-behaved
node averaging ~1 mA at 12 V uses roughly 0.3 Wh/day against a 56.16 Wh pack — about six
months of pure reserve with zero sun. A 10 W panel replaces that in minutes of decent
light. Sleep-current discipline still matters, but the margin is comfortable.

**2. What kills it is a defect that outruns the panel, during a stretch when the panel is
covered.** Snow, ice, or needle litter on a 34 × 27 cm panel under forest canopy can mean
weeks of near-zero harvest. That is survivable on reserve alone — *unless* firmware is
also draining abnormally. The failure mode is compound: no harvest **and** a stuck node.
The documented worst case is the join loop, where the node never sleeps at all
[CIT-RAK-SLEEP]. **This is the single most important number in this document, and it is a
firmware defect rather than a power parameter.**

**3. The pack must never reach empty, because empty may not be self-recoverable.** RAK
warns of malfunction "arising from a cold start with an empty battery"
[CIT-RAK9154-SOLAR]. For a node that is a hike away, a state requiring a physical visit to
clear is a total failure. The low-voltage cutoff therefore exists to **guarantee
recoverability**, not to squeeze out a last uplink: stop transmitting early, keep sleeping,
let the panel bring it back. Losing a day of data is free. Losing the node is not.

**The heater is a load nobody budgets for.** The RAK9154 has an integrated heater
[CIT-RAK9154-SOLAR], present so the pack can charge below freezing — lithium cells cannot
safely accept charge when cold. In a woods winter it may draw more than everything else
combined, exactly when harvest is worst. Its draw is unknown to us and is **not** under
firmware control. Measure it before trusting any winter projection.

### The sleep-vs-wake asymmetry still holds

At a 3600 s interval, a 5-second wake at 30 mA costs about 0.042 mAh per cycle. One stray
milliamp of sleep current costs about 1 mAh over the same hour — **roughly 25× more**.
Continuous small drains dominate brief large ones, which is why `Serial.end()` before sleep
is release-blocking and not a micro-optimization.

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

| Pack / harvest | Value | Source |
|---|---|---|
| RAK9154 rated energy | 56.16 Wh (5.2 Ah at 10.8 V nominal) | [CIT-RAK9154] |
| RAK9154 usable fraction | TBD — cutoff-dependent, see brownout thresholds | measured |
| Battery efficiency | 0.9 | [CIT-RAK9154-SOLAR] |
| Solar panel | 10 W regular (34 × 27 cm); large-panel variant deployed | [CIT-RAK9154-SOLAR] |
| Winter harvest under canopy | TBD — **must be measured on site, not modelled** | field |
| Integrated heater draw | TBD — not firmware-controllable; may dominate in winter | [CIT-RAK9154-SOLAR] + measured |
| Buck 12 V → 5 V efficiency | TBD | buck module datasheet (part not yet selected) |
| Buck no-load quiescent | TBD — a 24/7 load; select the part on this figure | buck module datasheet |

The buck converter's **quiescent draw is a load 24/7** and is easy to forget. A buck with
poor no-load efficiency can dominate the entire budget regardless of firmware quality.
Select the part with this in mind and record the figure here.

## Interval sensitivity

Complete once `I_sleep` and per-phase costs are measured. The interval is downlink-settable
across 300–86400 s (`FIRMWARE_SPEC.md` §4), so the figures must hold across the range, not
just at the default.

The useful column is **no-harvest survival**: how long the node runs on reserve alone with
a snow-covered or fully shaded panel, down to the recoverable cutoff — not to empty.

| Interval | Cycles/day | I_avg | No-harvest survival | Airtime vs TTN budget |
|---|---|---|---|---|
| 300 s (minimum) | 288 | TBD | TBD | **over budget at SF10** — see below |
| 3600 s (default) | 24 | TBD | TBD | TBD |
| 86400 s (maximum) | 1 | TBD | TBD | comfortable |

Also compute the **defect** rows, because these are what actually kill the node — the
interval barely matters once one of them is happening:

| Scenario | I_avg | No-harvest survival |
|---|---|---|
| Join loop, MCU never sleeps [CIT-RAK-SLEEP] | ~6 mA reference | TBD |
| Radio left awake across sleep [CIT-RAK-SLEEP] | TBD | TBD |
| Peripherals left enabled across sleep [CIT-NRF-PERIPH-SLEEP] | 0.89–1.2 mA reference | TBD |

Note the interaction with airtime: 288 uplinks/day at the 300 s minimum pushes hard against
the TTN Fair Use budget of 30 s/day [CIT-TTN-FUP]. On TTN's US915 plan the slowest uplink is
SF10BW125 [CIT-TTN-FREQ], and a 10-byte payload at the slow end of the range allows only
tens of messages per day within the budget [CIT-TTN-FUP-EXPLAINED]. **At the short end of
the interval range the binding constraint is airtime, not battery.** Compute both and
report whichever is tighter.

## Brownout thresholds

Set these to **guarantee the pack can always recover on its own**, not to maximize uptime.
A node that stops reporting for a week and then resumes is a minor inconvenience; a node
that has to be walked to is a failure of the entire design goal.

| Threshold | Value | Behavior |
|---|---|---|
| TX inhibit | **9.60 V** (3.2 V/cell) | Skip uplink, keep sleeping and reading (H3). Implemented in `power::Brownout` |
| Flash-write inhibit | **9.60 V** | Same trigger. A half-written config or session file survives the reset and breaks every boot after (H3, H5) |
| Deep-idle floor | Not separate | Below the inhibit the node already does nothing but wake, read, and re-check. A further tier would only add a state to get stuck in |
| Recovery hysteresis | **10.20 V** (3.4 V/cell) | Without a gap the pack transmits, sags below the limit, recovers, and transmits again — spending the remainder on the oscillation |

**These are inferred, not measured.** The 10.8 V nominal rating implies three lithium cells
in series, which puts full at about 12.6 V and empty near 9.0 V, with the pack's own
protection circuit somewhere below that. Neither the cell count nor the actual cutoff has
been confirmed against the hardware.

The asymmetry is deliberate. Missing a day of readings is an inconvenience; letting the
pack reach protection cutoff is a hike, because a disconnected pack may not restart from
panel current alone. A transmit burst is the largest current the node ever draws, so it is
the most likely thing to push a tired pack over that edge — which is why transmission is
the first thing to go.

Confirm the cutoff voltage on the bench and revisit both numbers. Tracked in
[`../TODO.md`](../TODO.md).

Pack voltage, current, and SoC are readable over the BMS link (`FIRMWARE_SPEC.md` §2.2), so
these are measurable rather than guessed. The **battery current sign convention is
unresolved** — see [ADR-0002](decisions/ADR-0002-payload-contract-conflicts.md). Do not
write charge/discharge logic until it is closed.

## Changing anything power-related

1. Cite the manufacturer figure for the claim.
2. Recalculate this page.
3. Record a **measured** sleep current in [`EVIDENCE.md`](EVIDENCE.md) before calling it done.
