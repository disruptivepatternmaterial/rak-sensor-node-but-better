# Node build procedure

This is the assembly procedure. Follow it in order.

`HARDWARE.md` holds pin research, measurements, rejected explanations, and circuit history. It is
not a build sequence. If the two files appear to conflict, **stop**: this file controls assembly,
and the conflict must be resolved before hardware is connected.

## Current stopping point

**Do not install a RAK4631 Core and do not connect the pack data wire to any GPIO.**

The safe procedure currently ends at step 23. Steps after that do not exist yet because the
steady-state capture of [ADR-0011](decisions/ADR-0011-plug-moves-only-with-no-core-fitted.md)
§ "Exit criteria" is not in `EVIDENCE.md`
([#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102)).

**The one rule** (measured 2026-09-05, `EVIDENCE.md`): when the 5-pin plug is mated or unmated,
`P−` lands ~180 ms after `P+` and the data pin, and the node's supply current returns through the
data wire at −8.1 V — that is what killed every pad. It harms nothing unless a core is in the
board. So: **the plug is mated before the core is fitted, and is never moved with a core in the
board.** In the field the plug is never touched; a pack swap is a bench trip, core out first.

## The build, in one picture

Every wire in the node, one per row. The numbered steps below are these wires in build order.

| # | From | To |
|---|---|---|
| 1 | pack pin 1 `P+` | buck `VIN+` |
| 2 | pack pin 1 `P+` | Pololu relay, one big terminal |
| 3 | Pololu relay, other big terminal | RK900 12 V |
| 4 | pack pin 2 `P−` | base-board `GND` pad |
| 5 | pack pin 2 `P−` | buck `VIN−` |
| 6 | pack pin 2 `P−` | RK900 GND |
| 7 | pack pin 2 `P−` | Pololu relay `GND` |
| 8 | pack pins 3 + 5, joined | RAK5802 `SDA` clip |
| 9 | pack pin 4 `3V3_In` | base-board `VDD` pad |
| 10 | base-board `VDD` pad | Pololu relay `VIN` |
| 11 | RAK5802 `SCL` clip | Pololu relay `EN` |
| 12 | RK900 A | RAK5802 `A/RX` |
| 13 | RK900 B | RAK5802 `B/TX` |
| 14 | buck output | board USB-C |

```mermaid
flowchart TB
    P1["pin 1  P+"] --> BUCK["buck"]
    P1 --> RELAY["Pololu relay"]
    RELAY --> RK["RK900 12 V"]

    P2["pin 2  P−"] --> GND["GND pad · buck VIN− · RK900 GND · relay GND"]

    P35["pins 3+5"] --> SDA["SDA clip"]

    P4["pin 4  3V3_In"] --> VDD["VDD pad"]
    VDD --> RELAY
    SCL["SCL clip"] --> RELAY
```

| | Rule |
|---|---|
| The plug | Mated before the core goes in. Never moved with a core in the board. |
| `K1` | High-side in the RK900 branch only. Control: `VIN` from the `VDD` pad, `GND` to node ground, `EN` from the `SCL` clip. Firmware for it does not exist yet ([#117](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/117)). |
| `SDA` and `VDD` (red / pin 4) | **Not landed until step 24 exists.** Core stays out of the board until then. |
| `VDD` pad | Two wires: pack pin 4 and `K1 VIN`. Never the RAK5802 `3V3` clip. |
| Ground | One common ground: pack pin 2 → base-board `GND` pad, buck `VIN−`, RK900 `GND`, `K1 GND`. |
| Not connected | 4-pin Gateway Load socket; RAK5802 `BAT`, `GND`, `3V3`, `AIN` clips; enclosure lid panel. |

## Required equipment

- RAK19007 WisBlock base board
- RAK5802 RS-485 module with spring terminals
- RK900 weather sensor
- RAK9154 solar battery pack and one SP11 plug mating its 5-pin `Sensor Hub Load` socket
  (`SP1110/P5`) [CIT-RAK-WX-MANUAL]
- Pololu 5426 (`K1`) — in hand for 002/003
- 12 V-to-5 V buck converter
- multimeter
- Saleae Logic Pro 8 for any unqualified signal
- current-limited 3.3 V bench supply for the pack-data qualification

Keep the candidate/donor RAK4631 CPU/radio Core somewhere outside the assembly area until step 23
passes.

## A. Assemble only the non-Core wiring

1. Disconnect USB.
2. Unmate the RAK9154 plug.
3. Disconnect the buck from its input.
4. Confirm with the meter that the buck output, base-board `VDD`, pack pin 4, and the free data
   lead are not energised. Record the readings; do not proceed on a non-zero reading.
5. Remove the RAK4631 Core if one is fitted.
6. Fit the RAK5802 in its documented WisBlock IO slot.

**Ground.**

7. Wire pack pin 2 (`P−`) to the base-board `GND` pad, and from that same node ground to the
   buck input negative, the RK900 `GND`, and `K1 GND`. Solder and heat-shrink the pin-2 junction.

**Power.**

8. Wire pack pin 1 (`P+`) to the buck input positive and to one `K1` load slot.
9. The other `K1` load slot goes to the RK900 12 V. `K1 VIN` → the base-board `VDD` pad (the pad pack pin 4 will
   share when step 24 exists); `K1 EN` → the RAK5802 `SCL` clip
   ([#117](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/117)).
10. Do not jumper `K1 EN` to `K1 VIN`; do not take `K1 VIN` from the RAK5802 `3V3` clip; do not
    switch the RK900 ground.
11. Confirm the pack plug is still unmated. It stays unmated until step 19 has passed.

**Data.**

12. Join pack pins 3 and 5 at the plug. Terminate the resulting data lead so it cannot touch
    anything. **Do not put it in `SDA`, `A1`, `IO1`, or any other node terminal.**
13. Leave pack pin 4 (`3V3_In`) disconnected and insulated.

**Checks and the RS-485 pair.**

14. Short the meter probes together and record the lead-resistance reading.
15. Measure pack pin 2 to the base-board `GND` pad. Record the resistance. It must remain stable
    while the harness and each termination are moved gently; an overload/open or changing reading
    fails this step.
16. Measure pack pin 2 to the RK900 `GND` and to the buck input negative in the same way.
17. Measure pack pin 1 to the buck input positive and to the `K1` load slot: lead resistance from
    step 14. Record both.
18. Connect RK900 `A` to RAK5802 `A/RX` and RK900 `B` to RAK5802 `B/TX`.

### Checkpoint after step 18

Your wiring must match § "The build, in one picture" with the red `SDA` clip and pin 4 still
unconnected, the Core not fitted, the plug not mated, and the buck output not connected.

## B. Qualify the actual base board and pack path

19. With every source still disconnected and no Core fitted, meter from the base-board
    edge-header `BAT` pad to each of:
    - `IO1`;
    - `A1`;
    - RAK5802 `SDA`.

    Record all three displays. Each must show the meter's open/overload indication. Any finite or
    unstable reading fails the board; do not install a Core.

20. If this is the same pack and harness as capture 13 in `EVIDENCE.md`, record that existing
    pack-side qualification in the build sheet and continue; a new pack or a new plug means
    repeating steps 21–22.

21. Qualify a changed pack or harness with **no Core and no base board in the measurement loop**:
    - bench supply OFF;
    - bench-supply negative to pack pin 2;
    - bench-supply positive to pack pin 4;
    - Saleae ground to pack pin 2;
    - Saleae analog channel 0 to the joined pin 3+5 data lead;
    - nothing else on the data lead;
    - set the supply to 3.3 V with a 50 mA current limit;
    - energise pin 4 and capture at least 60 seconds.

    This repeats the setup that produced capture 13. Do not substitute a coreless RAK19007 `VDD`
    pad: with the Core removed that pad is open-circuit — the base board's `3V3` reaches it only
    through the Core's connector
    ([ADR-0010](decisions/ADR-0010-rak19007-vdd-source-conflict.md), [CIT-RAK4631-SCH]).

22. Export the analog capture and run `scripts/owprobe.py <analog.csv>`. Record its output and raw
    capture path in `EVIDENCE.md`.
    - Exit 0 qualifies only the pack-side voltage under this captured condition.
    - Exit 1 is inconclusive; correct the measurement setup and repeat.
    - Exit 2 fails; isolate the lead and stop.

## C. Stop and record

23. Confirm all of the following:
    - the RAK4631 Core is still not fitted;
    - the pack data lead is insulated and reaches no node terminal;
    - the pin-1 readings from step 17 are recorded;
    - both ground-path readings are recorded;
    - all three `BAT`-isolation readings are recorded;
    - the applicable pack-side analyzer result is recorded.

    Then **mate the pack plug** — no core is in the board, so nothing can be harmed — and stop.
    From here on the plug does not move until a core has been removed again.

## What must be added before step 24 can exist

[ADR-0011](decisions/ADR-0011-plug-moves-only-with-no-core-fitted.md) § "Exit criteria", both in
`EVIDENCE.md` with host and SHA:

1. **Steady state, no core:** plug mated (step 23), buck output connected so the board runs from
   the pack exactly as it will in the field, RK900 powered. Analyzer ground on the base-board
   `GND` pad, analog channel on the joined pins 3+5 lead. ≥ 10 minutes. **Pass = the lead stays
   within −0.3 V … +3.6 V.** The 2026-09-05 session already cleared USB-C re-plug, bench-cable
   re-plug and RESET in this condition; the mating excursion cannot occur because the plug does
   not move.
2. **First core, plug already mated:** meter the incoming core's `IO1`, `A1`, `SDA` to `GND`
   (megohms), fit it, land the data lead in `SDA` and pin 4 on `VDD`, run 24 h, re-meter the pad.

Until those results are in `EVIDENCE.md`, there is no step that says to install the donor Core.
The isolation switch of #101 (`SN74CBTLV1G125`) is no longer a prerequisite: it guards the
unpowered-node case, which is not the measured mechanism, and an ESD-class part is not rated for
183 ms of reverse conduction.

## Build record

Copy this block into the bench record:

```text
Date:
Base board identifier (or NOT IDENTIFIED):
Pack/harness identifier (or NOT IDENTIFIED):
Meter lead resistance:
Pack pin 2 -> base-board GND:
Pack pin 2 -> RK900 GND / buck VIN-:
Pack pin 1 -> buck VIN+ / K1 load:
Plug mated at step 23 with no core fitted? (must be YES):
BAT -> IO1:
BAT -> A1:
BAT -> SDA:
Pack-side capture/evidence entry:
Step 23 PASS/FAIL:
```
