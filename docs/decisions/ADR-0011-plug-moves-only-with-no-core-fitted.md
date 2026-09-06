# ADR-0011 — The pack plug is mated before the core is fitted, and never moved with a core in the board

- **Status:** Proposed 2026-09-05 — written into `BUILD.md`; becomes *Accepted* when the steady-state
  capture in § "Exit criteria" is in `EVIDENCE.md`
- **Date:** 2026-09-05 (revised the same evening — the first version put a switch inside a box that
  is sealed and cannot be opened in the field)
- **Tracks:** #102, #101, #99, #113, #94

## Context

Nine nRF52840 pads are dead, every one shorted to ground. On 2026-09-05 the cause was measured
with no core in the circuit: mating the pack's 5-pin `Sensor Hub Load` plug puts the joined
pins 3+5 data wire at **−8.1 V** relative to node ground for **~183 ms**, because pin 2 (`P−`)
lands after pin 1 (`P+`) and the data pin, and for that interval the node's entire supply
current — buck, RK900 — has no return but the data wire
[CITE(bench): `docs/EVIDENCE.md` 2026-09-05, runs 6 and 7, ~25 matings, two packs](../EVIDENCE.md).
Through a fitted core that current runs backwards through the pad's ground clamp, which is the
one direction that leaves a pin shorted to ground [CITE(prior-art): CIT-NRF-GNDLOSS](../CITATIONS.md)
[CITE(prior-art): CIT-NRF-GNDLIFT](../CITATIONS.md). Unmating reverses the order and does the same.

Nothing on the node side prevents it: it happened with the core removed, and the SP11 series has
no first-mate ground contact [CITE(datasheet): CIT-WEIPU-SP11](../CITATIONS.md). The pack's
5-pin socket is the only one in play; `P+`, `P−`, `TXD`, `3V3_In`, `RXD`
[CITE(datasheet): CIT-RAK-WX-MANUAL](../CITATIONS.md) all arrive on the same plug, and that does
not change. The enclosure is sealed in the field; nothing inside it can be operated.

The excursion harms nothing unless a pad is on the data wire while it happens. Node 001 was
mated once, then had its core fitted, and its pad is alive after many months; node 002 was
re-mated dozens of times during bring-up with a core in the board and lost three pads.

## Decision

1. **The pack plug is mated before the core goes into the base board, and is not unmated while
   a core is in the board.** Build order in `BUILD.md`: harness → mate the plug → qualify →
   fit the core → land the data wire → seal.
2. **In the field the plug is never touched.** A pack swap or a station move means the node comes
   back to the bench, the core comes out first, then the plug moves.
3. **No switch, no disconnect, no extra part.** The first version of this record specified a
   load-disconnect switch `S1` inside the enclosure; the enclosure cannot be opened in the field,
   so it was withdrawn the same day and is not on the BOM.
4. **What is unchanged:** pin 2 `P−` to the base-board `GND` pad, common with the buck and the
   RK900; pins 3+5 joined to the `SDA` clip; pin 4 to the `VDD` pad; the Pololu relay in the
   RK900 12 V line (#117). No second ground wire and no series resistor — neither was ever built,
   and neither is credited with anything.
5. The isolation switch (#101) is **not** adopted and closes as superseded when this record is
   accepted: it guards the unpowered-node case, which is not the measured mechanism, and an
   ESD-class part is not rated for 183 ms of reverse conduction (`CIT-LRC399-04AT1G` is what
   such parts *are* rated for).

## What this does not do

It depends on order, not on a part. Mate or unmate with a core fitted and the 2026-09-05 fault is
back, unchanged. There is no interlock. The cost is that a field pack swap is a bench trip —
acceptable, because the deployment goal already assumes nobody hikes out to touch the node
(`POWER_BUDGET.md`).

## Consequences

- Zero new parts. One rule in `BUILD.md`, one rule in `DEPLOY.md`.
- `BUILD.md`'s stopping point moves: the pre-core gate becomes "plug mated, node running from the
  pack, data wire quiet under the analyzer" instead of "isolation and contention unresolved".
- Node 001 already complies by accident. Node 002/003 comply by following the order.

## Appendix — base board: RAK19010 (SKU 110086) + RAK19016, evaluated and not adopted

Asked 2026-09-05 as an idea; the operator kept the hardware as built (RAK19007, buck, USB-C) the
same day. The evaluation is kept so it is not redone.

**Does the base board change the pad problem?** No. The mechanism lives in the pack plug; the
same −8.1 V appears on any base board, and the mating order is the fix on any base board.

**Is RAK19010 + RAK19016 a better power front end than RAK19007 + hobby buck + USB-C?** On the
drawings, yes, with one real cost.

| | RAK19007 as built | RAK19010 + RAK19016 |
|---|---|---|
| Pack 12 V enters via | external MP1584-class module → 5 V → **USB-C** → `D2` Schottky → `TP4054` | **3-pin screw terminal** `VCC_IN` → PMOS reverse-polarity gate → `SGM61230` → 4.43 V → `TP4054` [CITE(datasheet): CIT-RAK19016-SCH](../CITATIONS.md) |
| Input range | module-dependent; 5.5 V absolute max at the board | **5–24 V** [CITE(datasheet): CIT-RAK19016-RAW](../CITATIONS.md); pack is 12 V boost on this plug |
| Reverse polarity | none on USB-C | `Q1 NCE40P05Y` gate |
| Regulator protections | hobby module; sag → sawtooth to 4 V on a 3.3 V setpoint [CITE(bench): CIT-MP1584-SCOPE](../CITATIONS.md) | 5 ms soft-start, output OVP, current foldback, thermal auto-recovery [CITE(datasheet): CIT-SGM61230](../CITATIONS.md) |
| Idle draw of the front end | uncharacterized | 25 µA (`SGM61230`) + 450 nA (`SGM6036`) typical |
| 3.3 V stage | `SGM6036-ADJ` | **identical** `SGM6036-ADJ`, same divider |
| Runs with no battery fitted | yes (charger floats to 4.2 V) | yes — `VCC` → `D3` → `SGM6036`; `VBAT` = charger float, same as today |
| USB backfeed hazard when a bench cable is plugged in [CITE(prior-art): CIT-USB-BACKFEED](../CITATIONS.md) | present — buck and host share `VBUS` | **gone** — no USB on the power path |
| Programming / console | USB CDC | **none.** SWD (the CMSIS-DAP already on the bench) or a RAK5804 in the IO slot — which the RAK5802 occupies. No free UART reaches a header: `TXD1/RXD1` on the edge header is the RS-485 UART (`CIT-RAK4631-SCH`, `CIT-RAK19007-SCH-SLOTS`) |
| Parts | in hand | RAK19010 (110086) + RAK19016; RAK5802, RAK4631, antenna carry over |

**Not adopted.** If it is ever revisited: the workable shape is RAK19010 + RAK19016 in the field
with a RAK19007 kept on the bench as the programming/soak fixture, the core moving between them;
the field node is reflashed over BLE OTA (`FIELD_UPDATE.md`).
RAK19012 (USB + LiPo + solar) would gain nothing — it is the RAK19007 power topology on a card.
The plug rule, `3V3_In` on the `VDD` pad, and the `SDA` landing are the same on either base
(`CIT-RAK19010-RAW`).

## Exit criteria

Accepted when both are in `EVIDENCE.md` with host and SHA:

- [ ] **Steady state, no core:** plug mated, node running from the pack exactly as in the field
  (buck → USB-C, RK900 powered), analyzer ground on the base-board `GND` pad, analog channel on
  the joined pins 3+5 wire. ≥ 10 minutes including RK900 activity: **the wire stays within
  −0.3 V … +3.6 V** [CITE(datasheet): CIT-NRF-GPIO](../CITATIONS.md). The 2026-09-05 session
  already cleared USB-C re-plug, bench-cable re-plug, and RESET in this condition.
- [ ] **First core, plug already mated:** meter the incoming core's `IO1`, `A1`, `SDA` to `GND`
  (megohms), fit it, land the data wire in `SDA` and pin 4 on `VDD`, run 24 h, re-meter the pad.
