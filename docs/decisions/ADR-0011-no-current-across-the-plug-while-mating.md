# ADR-0011 — No current crosses the pack plug while it is being mated: a load disconnect on `P+`

- **Status:** Proposed 2026-09-05 — written into the wiring plan in `HARDWARE.md`; becomes
  *Accepted* when the harness passes the mating test in § "Exit criteria"
- **Date:** 2026-09-05
- **Tracks:** #102, #101, #99, #113, #94

## Context

Nine nRF52840 pads are dead, every one shorted to ground. On 2026-09-05 the cause was measured
with no core in the circuit: re-mating the pack's 5-pin `Sensor Hub Load` plug puts the joined
pins 3+5 data wire at **−8.1 V** relative to node ground for **~183 ms**, because pin 2 (`P−`)
lands after pin 1 (`P+`) and the data pin, and for that interval the node's entire supply
current — buck, RK900, and anything else on the plug — has no return but the data wire
[CITE(bench): `docs/EVIDENCE.md` 2026-09-05, runs 6 and 7, ~25 matings, two packs](../EVIDENCE.md).
Through a fitted core that current runs backwards through the pad's ground clamp, which is the
one direction that leaves a pin shorted to ground [CITE(prior-art): CIT-NRF-GNDLOSS](../CITATIONS.md)
[CITE(prior-art): CIT-NRF-GNDLIFT](../CITATIONS.md).

Nothing on the node side prevents it: it happened with the core removed, and the SP11 series has
no first-mate ground contact [CITE(datasheet): CIT-WEIPU-SP11](../CITATIONS.md). The pack's
5-pin socket is the only one in play for this node; its `P+` (12 V boost), `P−`, `TXD`, `3V3_In`,
`RXD` [CITE(datasheet): CIT-RAK-WX-MANUAL](../CITATIONS.md) all arrive on the same plug, and
that does not change.

Three things could stop the current: change the connector (not possible — it is the pack's),
block the data wire (#101: an isolation switch — wrong mechanism, and an ESD-class part is not
rated for 183 ms of reverse conduction, see `CIT-LRC399-04AT1G` for what those parts *are* rated
for), or **make sure no current is flowing when the contacts land.**

## Decision

1. **A manual load disconnect `S1` on `P+`, inside the enclosure, immediately downstream of the
   plug.** Everything that draws power — the node's supply, the RK900, the muon-wx — is on the far
   side of it.
2. **`S1` is open for every mate and every unmate.** Procedure: `S1` OFF → plug in, tighten the
   nut → `S1` ON. Unplugging: `S1` OFF → unplug. With `S1` open nothing draws through pin 1, node
   ground cannot be pulled toward `P+`, and the data wire has no current to carry, whatever order
   the contacts touch. No settling wait is needed after opening `S1`: the buck's and sensors'
   input capacitors discharge into their own loads on the node side of the switch, and that
   current does not cross the plug.
3. **`S1` requirements:** breaks `P+` only, never `P−`; rated ≥ 2 A at 13.2 V DC (the plug's
   contact ceiling [CITE(datasheet): CIT-RAK-WX-MANUAL](../CITATIONS.md)); reachable with the
   lid open and nothing unplugged; labelled `OFF before plugging or unplugging the pack`. A toggle
   or rocker switch, or an inline blade-fuse holder with the fuse pulled — the latter also gives the
   harness the overcurrent protection it has never had. **No part is chosen in this ADR**; the one
   chosen gets a `CITATIONS.md` row before it goes on the BOM.
4. **What is unchanged:** pin 2 `P−` to all load negatives and the base-board `GND` pad, plus a
   second conductor to the RAK5802 `GND` clip; pins 3+5 joined to the `SDA` clip; pin 4 to the
   `VDD` pad. The redundant ground and the series resistor stay as belt-and-braces; neither is
   the fix and neither is credited as one.
5. The isolation switch (#101) is **not** adopted, and closes as superseded when this ADR is
   accepted.

## What this does not do

`S1` depends on a hand. Mate with it closed and the 2026-09-05 fault is back, unchanged. There is
no interlock; the label and the procedure are the whole guard. That is the cost of a one-plug
design and it is stated here rather than hidden. The residual on the *data* side is small and
bounded: while `S1` is open the node is unpowered, `VDD` is dead, pin 4 is dead, the pack's IO MCU
has no level-shifter rail, and the line sits at pack ground (capture 13 measured 20 mV in that
condition [CITE(bench): `docs/EVIDENCE.md` capture 13](../EVIDENCE.md)) — against a node ground
that is already common through pin 2 by the time `S1` closes.

## Consequences

- One new part per node (`S1`), one label, one habit. Node 001 gets `S1` when it is next on a
  bench; it has survived by luck that is unmeasured, not by design.
- `BUILD.md`'s stopping point moves: the pre-core gate becomes "harness with `S1` built, mating
  test passed" instead of "isolation and contention unresolved".
- `S1` also gives the field a clean power-cycle without touching the plug.

## Base board: RAK19010 (SKU 110086) + RAK19016, or keep the RAK19007

Asked 2026-09-05. The answer is in two parts because the question has two parts.

**Does the base board change the pad problem?** No. The mechanism lives in the pack plug; the
same −8.1 V appears on any base board, and `S1` is the fix on any base board.

**Is RAK19010 + RAK19016 a better power front end than RAK19007 + hobby buck + USB-C?** Yes,
on the evidence, with one real cost.

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

**Recommendation.** Use RAK19010 + RAK19016 for the field build and keep a RAK19007 on the bench
as the programming/soak fixture — the core moves between them, which is the workflow RAK itself
describes for these modules. The field node is reflashed over BLE OTA anyway (`HARDWARE.md`
§ "Reflashing a sealed field node"), so losing USB in the woods costs nothing; losing it on the
bench would, and the RAK19007 avoids that. The buck module, the USB-C-as-power-input, and the
backfeed rule all leave the field build. RAK19012 (USB + LiPo + solar) is not recommended: it is
the RAK19007 power topology on a plug-in card and keeps the buck.

What does **not** change with the new base: `S1` and the plug rule above, `3V3_In` from the `VDD`
pad (RAK19010 has the same header and the same `VDD` = looped `3V3` arrangement,
`CIT-RAK19010-RAW`), the `SDA` clip as the one-wire landing, and the 2 A connector ceiling.

## Exit criteria

Accepted when all three are in `EVIDENCE.md` with host and SHA:

- [ ] `S1` fitted, harness on a base board with **no core**, powered from the pack exactly as in
  the field, analyzer on the pins 3+5 wire against node ground: ≥ 10 mate/unmate cycles by the
  procedure in decision 2, **no excursion below −0.3 V**. Runs 6 and 7 are the control — same
  harness, no `S1`, every mating below −0.3 V; this is that test with one variable changed.
- [ ] The same, ≥ 3 cycles with `S1` deliberately **closed**, recorded, to confirm the switch is
  the variable and not the day.
- [ ] A meter-checked core fitted, one cycle by the procedure, pad still reads megohms to ground
  afterwards.
