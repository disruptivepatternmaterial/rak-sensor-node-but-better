# Power budget

## What is measured — read this before calling anything "unmeasured"

This project has taken current measurements repeatedly. An agent that says "the draw is
unmeasured" without checking this list is wrong and wastes the operator's time. On record:

| Measurement | Value | When / where |
|---|---|---|
| Whole board, awake peak over a 900 s cycle | **~40 mA ± 10 mA** (meter likely missed the ~50 ms TX burst) | bench meter 2026-08-13, [`EVIDENCE.md`](EVIDENCE.md) |
| Whole board, idle between bursts | **< 10 mA** — meter floor, not a value | same session |
| Station overnight draw (node + RK900 + muon + buck, from the pack) | **−0.05 A, 89 → 85 % SoC over 4 h** | field telemetry 2026-08-30, `puma-concolor-001` |
| Pack charging sign | **+0.01 A in daylight** — confirms positive = charging | same field window |
| Pack telemetry resolution | **10 mA/LSB** — cannot resolve sleep-scale questions | 2026-08-12, `4510763` |
| RK900-09 draw | **0.4 W** (~33 mA at 12 V), continuously powered — not duty-cycled | operator bench measurement, recorded 2026-08-30; exact date/instrument unrecorded |
| RK900 12 V duty-cycle switch | **on order** — Pololu Isolated SSR #5426 ([CIT-POLOLU-5426](CITATIONS.md)) | operator, 2026-08-31; firmware + GPIO selection wait on arrival ([#113](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/113)) |

The **one** open number is the sleep-state current at the ~1 mA-and-below scale, where both
instruments used so far bottom out ([#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8)).
That gap matters for the node-alone reserve claim, **not** for station-level questions — even
the worst documented sleep defect (~6 mA) is a tenth of the measured overnight station draw.

🚧 **The tables below remain a worksheet.** A figure marked `TBD` is filled only from a cited
datasheet or a measurement recorded in [`EVIDENCE.md`](EVIDENCE.md) — never from memory or a
blog post. An invented current figure produces a confident, wrong runtime estimate, and the
cost of being wrong is a hike.

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

## The station, not the node — the pack feeds three loads

**Added 2026-08-30 ([#113](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/113)).**
The deployed pack does not power just this node. One RAK9154 feeds **this node, the RK900
(continuously), and a muon-wx air station** (Particle M-SoM with an SEN55 PM sensor and a
cellular radio — `particle-devices/muon-weather`), and four such combined stations are
planned. Every runtime conclusion in this document must be read at station level.

**Measured, whole station** ([`EVIDENCE.md`](EVIDENCE.md) 2026-08-30, f_cnt 3644–3660):
overnight draw **−0.05 A** on pack telemetry (5 LSB — a reading, not a floor), pack falling
**~1 %/h** (89 → 85 % over ~4 h). Against 56.16 Wh that is roughly **4 days of zero-harvest
reserve for the whole station**, hitting the 9.60 V TX-inhibit floor sooner.

This **corrects the reach of point 1 above**: "the node's own draw is not the binding
constraint" remains true of the node (~1 mA class), but the *six months of pure reserve*
arithmetic applies to the node alone and **not to the station**, which measures at ~50×
that. The binding constraint is the station's combined draw under snow cover.

**Attribution is the open work** — which of the four loads (node sleep, RK900 continuous,
buck idle, muon duty cycle) owns the 50 mA is unmeasured. The muon self-instruments: it
carries an INA228 and publishes measured `current_mA` each cycle, and its duty cycle is
spec'd (60 min PROD cadence, STOP sleep, 30 s SEN55 warm-up per wake). Per-load
measurements stay tracked in
[#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8),
[#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47),
[#9](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/9);
the station roll-up is [#113](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/113).

**Cross-protection gap:** this node inhibits TX from measured pack voltage at 9.60 V; the
muon's low-battery policy keys off its own PMIC and does not know the pack exists, so it
keeps buying cellular handshakes from the reserve this node is protecting. Open in #113.

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
| Sleep (chip's lighter sleep state, not its deepest) | ~`t_cycle` | TBD | [CIT-NRF-POWER] + measured | ⬜ |
| Sleep **with radio left awake** (defect case) | — | ~6 mA observed | [CIT-RAK-SLEEP] | reference |
| Sleep **with the radio's bus left running** (defect case) | — | ~0.9 mA observed | [CIT-NRF-PERIPH-SLEEP] | reference — closed in `power.cpp` |
| Failed-join retry loop (defect case) | unbounded | never sleeps | [CIT-RAK-SLEEP] | reference |
| MCU wake + init | TBD | TBD | measured | ⬜ |
| RS-485 enabled, RK900 poll | ≤ 1 s per txn, ≤ 2 retries (`FIRMWARE_SPEC.md` §2.1) | TBD — includes RAK5802 transceiver | [CIT-RAK5802] + measured | ⬜ |
| BMS poll | ≤ 1 s per txn | TBD | measured | ⬜ |
| LoRa TX | depends on DR/SF and payload | TBD at US915 TX power — datasheet reference **92 mA @ 17 dBm**, **125 mA @ 20 dBm** | [CIT-SX1262] + [CIT-RAK4631-RAW] | ⬜ |
| Whole board, peak over one 900 s cycle | burst | **~40 mA ± 10 mA observed** — coarse; the meter likely missed the TX burst | bench meter, 2026-08-13 ([EVIDENCE.md](EVIDENCE.md)) | coarse |
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

## Sleep current cannot be measured from the pack's telemetry — use a meter

Attempted 2026-08-12 (`4510763`) on the operator's instruction to try the RAK9154's own
current reading before setting up a meter. **It cannot answer the question**, for two
independent reasons, either of which alone is fatal:

1. **Resolution.** The pack reports current in units of 0.01 A — the raw register reads
   `i=-1` for the observed `-0.01 A`, so one LSB is **10 mA**. The number this budget turns
   on is **~1 mA** (see the interval-sensitivity note above: one stray milliamp costs ~25×
   what the wake costs). Even the documented defect cases — 0.89–1.2 mA for peripherals
   left enabled, ~6 mA for the radio left awake — sit **at or below a single LSB**. A
   sensor whose smallest step is ten times the threshold cannot distinguish a healthy
   sleep from the worst failure mode in this table.
2. **The bench node is not powered from the pack.** With USB attached the board draws from
   USB, so the pack sees essentially no load and its current reading says nothing about
   node draw. Consistent with observation: the reading sat at `-0.01 A` unchanged across
   20 consecutive `battdiag` cycles and again in the field image, awake and transmitting,
   where a pack actually supplying a ~30 mA awake node should have read about `-0.03 A`.
   Detaching USB removes the confound and also removes the console, which is the only way
   the reading gets off the board.

**Resolution floor: 10 mA. Required resolution: ~1 mA. A meter is the only way.** No sleep
current figure is recorded here, and none should be quoted from pack telemetry.

### …and not just any meter — a 10 mA bench meter fails for the identical reason

Attempted 2026-08-13 (`572bcfa`) with an inline USB current meter between the host supply and the
board. Its display resolves to **0.01 A**, so **one digit is 10 mA** — exactly the pack telemetry's
LSB, and exactly ten times the figure this page turns on. It read a peak of **0.04 A** and a
minimum of **`0`**.

**That `0` is a resolution floor, not a measurement.** It establishes only that idle draw is
*below 10 mA*, which every defect case in the table above already satisfies: 0.89–1.2 mA for
peripherals left enabled and ~6 mA for the radio left awake would each display as `0`. The
module's own datasheet sleep figure is **2.0 µA** [CIT-RAK4631-RAW], roughly 5000× below a single
digit. **No sleep-current figure exists, and this measurement did not produce one.**

The 40 mA peak is a genuine — if coarse — ceiling on peak draw, and it rules out a subsystem stuck
in the hundreds of milliamps. It is **not** a transmit measurement: it sits *below* the datasheet's
92 mA at 17 dBm [CIT-RAK4631-RAW], which points at the meter's slow sampling missing the ~50 ms
burst rather than at a radio drawing less than spec.

Closing this needs µA-capable instrumentation — a Nordic PPK2 (the method behind
[CIT-RAK-SLEEP]), a shunt on a scope, or coulomb counting over a long window. Tracked in
issue [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8).

This leaves the `delay()`-based sleep at `src/power.cpp:134-136` unmeasured, exactly as its
own comment at `:129-133` states. It parks the task in FreeRTOS and lets the idle task drop
the CPU to a lighter low-power state; it does **not** reach system-off, deliberately, since
system-off resets the nRF52 on wake and would force a rejoin every interval. The watchdog
is configured `WDT_CONFIG_SLEEP_Pause` (`src/power.cpp:52`), so it is **paused across
sleep, not fed**. Whether the residual draw is nearer 1 µA or 1 mA is open, and issue #47
(residual USB peripheral draw) is part of the same measurement.

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

Confirm the cutoff voltage on the bench and revisit both numbers. Tracked in issue #7.

Pack voltage, current, and SoC are readable over the BMS link (`FIRMWARE_SPEC.md` §2.2), so
these are measurable rather than guessed. The **battery current sign convention is decided**:
positive = charging, negative = discharging, matching the pack's own telemetry — see
[ADR-0002](decisions/ADR-0002-payload-contract-conflicts.md) (2026-08-13). It has not yet been
confirmed against a real charge current, so treat charge/discharge logic written against it as
resting on a decision rather than a measurement.

## Changing anything power-related

1. Cite the manufacturer figure for the claim.
2. Recalculate this page.
3. Record a **measured** sleep current in [`EVIDENCE.md`](EVIDENCE.md) before calling it done.
   The procedure — measured inline on the USB-C line between buck and board, or on the
   buck's 12 V input for buck-plus-node, with **no computer attached** because
   `power.cpp` keeps the console alive when a host has opened the CDC port — is in
   [`SOAK.md`](SOAK.md). Not at the battery JST: this design feeds the board over USB-C
   and never uses that connector (`FIRMWARE_SPEC.md` §2).
