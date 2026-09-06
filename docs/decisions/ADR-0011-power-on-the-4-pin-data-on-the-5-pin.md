# ADR-0011 — All supply current on the 4-pin socket; the 5-pin socket carries data and ground only

- **Status:** Proposed 2026-09-05 — accepted for the wiring plan in `HARDWARE.md`; becomes
  *Accepted* when the harness described here passes the mating test in § "Exit criteria"
- **Date:** 2026-09-05
- **Tracks:** #102, #101, #99, #113, #94

## Context

Nine nRF52840 pads are dead, every one shorted to ground. On 2026-09-05 the cause was measured
with no core in the circuit: re-mating the pack's 5-pin `Sensor Hub Load` plug puts the joined
pins 3+5 data wire at **−8.1 V** relative to node ground for **~183 ms**, because pin 2 (`P−`)
lands after pin 1 (`P+`) and the data pin, and for that interval the node's entire supply
current — buck, RK900, and anything else on that plug — has no return but the data wire
[CITE(bench): `docs/EVIDENCE.md` 2026-09-05, runs 6 and 7, ~25 matings, two packs](../EVIDENCE.md).
Through a fitted core that current runs backwards through the pad's ground clamp, which is the
one direction that leaves a pin shorted to ground [CITE(prior-art): CIT-NRF-GNDLOSS](../CITATIONS.md)
[CITE(prior-art): CIT-NRF-GNDLIFT](../CITATIONS.md).

Nothing on the node side prevents it: it happened with the core removed, and the SP11 series has
no first-mate ground contact [CITE(datasheet): CIT-WEIPU-SP11](../CITATIONS.md). Two remedies
were on the table: a powered-off isolation switch in the data line (#101), or moving the supply
current off the plug that carries the data.

The pack offers exactly that split. Its 4-pin `Gateway Load` socket is `P+`, `P−`, `RS485A`,
`RS485B` and its 5-pin socket is `P+`, `P−`, `TXD`, `3V3_In`, `RXD`
[CITE(datasheet): CIT-RAK9154-RAW](../CITATIONS.md) [CITE(datasheet): CIT-RAK-WX-MANUAL](../CITATIONS.md);
both negatives are the battery negative, and the 4-pin `Load` is the battery rail through the
pack's current protection while the 5-pin `12 V out` is a boost stage
[CITE(datasheet): CIT-RAK9154-ELEC](../CITATIONS.md).

## Decision

1. **Every load — the node's supply, the RK900's 12 V, and the muon-wx — takes `P+` and `P−`
   from the 4-pin `Gateway Load` socket.** The RS-485 pins of that socket stay unconnected
   (the BMS is read over one-wire, ADR-0004).
2. **The 5-pin `Sensor Hub Load` plug has no `P+` conductor.** It carries pin 2 `P−` (a second,
   redundant ground), pins 3+5 joined (one-wire data), and pin 4 `3V3_In`. With no supply
   current on the plug there is nothing to divert down the data wire when pin 2 lands last.
3. **Mating order is a rule, not a ritual: 4-pin first, 5-pin second; unplug in reverse.** With
   the 4-pin mated, node ground and pack ground are already one net before any 5-pin contact
   touches, so the order the 5-pin contacts land in no longer matters. Mating the 5-pin first
   recreates a smaller version of the fault (node `3V3` into the pack's IO MCU through pin 4,
   returning through pin 5), so the order is load-bearing and goes on the harness label.
4. The isolation switch (#101) is **not** adopted. It addresses the unpowered-node case, which
   is not the measured mechanism, and it cannot survive the measured one (an ESD-class part is
   not rated for 183 ms of reverse conduction — see `CIT-LRC399-04AT1G` for what such parts are
   rated for). #101 closes as superseded when this ADR is accepted.
5. The redundant-ground and series-resistor mitigations already in `HARDWARE.md` stay as
   belt-and-braces; neither is the fix and neither is credited as one.

## Consequences

- A second SP11 cable per node: an `SP1110/P4`-mating plug for power, alongside the existing
  5-pin. Node 001 is retrofitted the same way when it is next on a bench; it has survived by
  luck that is unmeasured, not by design.
- The 2 A rating of the 4-pin contacts is the ceiling for node + RK900 + muon combined
  [CITE(datasheet): CIT-RAK-WX-MANUAL](../CITATIONS.md); `POWER_BUDGET.md` keeps the sum.
- `BUILD.md`'s stopping point moves: the pre-core gate becomes "split harness built, mating
  test passed" instead of "isolation and contention unresolved".

## Base board: RAK19010 (SKU 110086) + RAK19016, or keep the RAK19007

Asked 2026-09-05. The answer is in two parts because the question has two parts.

**Does the base board change the pad problem?** No. The mechanism lives in the pack plug; the
same −8.1 V appears on any base board, and the fix above is the same on any base board.

**Is RAK19010 + RAK19016 a better power front end than RAK19007 + hobby buck + USB-C?** Yes,
on the evidence, with one real cost.

| | RAK19007 as built | RAK19010 + RAK19016 |
|---|---|---|
| Pack 12 V enters via | external MP1584-class module → 5 V → **USB-C** → `D2` Schottky → `TP4054` | **3-pin screw terminal** `VCC_IN` → PMOS reverse-polarity gate → `SGM61230` → 4.43 V → `TP4054` [CITE(datasheet): CIT-RAK19016-SCH](../CITATIONS.md) |
| Input range | module-dependent; 5.5 V absolute max at the board | **5–24 V** [CITE(datasheet): CIT-RAK19016-RAW](../CITATIONS.md); pack is 9–13.2 V |
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

What does **not** change with the new base: the pack rule above, `3V3_In` from the `VDD` pad
(RAK19010 has the same header and the same `VDD` = looped `3V3` arrangement,
`CIT-RAK19010-RAW`), the `SDA` clip as the one-wire landing, and the 2 A connector ceiling.

## Exit criteria

Accepted when all three are in `EVIDENCE.md` with host and SHA:

- [ ] Meter, pack alone: 4-pin `P+`→`P−` reads pack voltage; 4-pin `P−` ↔ 5-pin `P−` continuity.
- [ ] Split harness on a base board with **no core**, analyzer on the pins 3+5 wire against node
  ground: ≥ 10 matings in the specified order, **no excursion below −0.3 V** (runs 6/7 repeated
  with the only variable changed). Then ≥ 5 matings in the *wrong* order, recorded, to size the
  residual path in decision 3.
- [ ] A meter-checked core fitted, one mating in the specified order, pad still reads megohms to
  ground afterwards.
