# Hardware — WisBlock RK900 + RAK9154 node

🚧 **NOT YET BUILT.** Bench/field status: none.

Authoritative behavior: [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md). Libraries: [`LIBRARIES.md`](LIBRARIES.md).

## Mission

Class A LoRaWAN US915 end node: poll **RK900-09** + **RAK9154**, uplink on downlink-settable interval, woods-hardened.

## BOM (ordered path)

| Role | Part | SKU |
|---|---|---|
| Core | RAK4631 US915 | **116000** |
| Base | RAK19007 | **110082** |
| RS-485 | RAK5802 | **100003** |
| Antenna | Blade 915 RP-SMA (if needed) | **926019** |
| Enclosure | Unify **solar** variant — the no-solar 910406 was out of stock | **910421** (confirm) |
| Buck | 12 V → 5 V | (separate) |
| Power source | RAK9154 Solar Battery Lite, **large-panel variant** | — |

**Not used:** RAK13002 (conflicts with 5802 IO slot), GNSS, RTC, AS923 kit **119012**.

### The enclosure that arrived has its own solar panel — and we do not want to use it

Planning assumed the no-solar 910406. Only the solar variant was available, so the shell in
hand has panel mounting and, unlike 910406, is roomy enough for the buck converter with
space to spare. Both of those are fine. The panel is the problem, and it is a decision
rather than a detail:

**The RAK9154 must remain the power source, and the enclosure's panel should be left
unconnected.** Half the firmware exists to read that pack — voltage, current, state of
charge, and temperature come over the one-wire link on its 5-pin socket
([ADR-0004](decisions/ADR-0004-bms-one-wire-path.md)), the low-voltage gate in
`src/power.h` decides whether to transmit based on what it reports, and four of the nine
uplink fields come from it. Powering the node from the enclosure panel instead would mean
no pack to interrogate: `BatteryReading` would be permanently absent, the brownout gate
would have nothing to act on, and the node would lose the ability to tell anyone it was
running out of energy.

Wiring both panels into the pack's charge input is also not the answer — the RAK9154 has
its own 18 V charge controller expecting one array, and a second uncontrolled source on the
same input is a good way to damage it.

So the enclosure's panel is dead weight here. That is acceptable; it was a stock
substitution, not a design change. Do not "make use of it" without revisiting this.

**The RAK9154 is solar-recharged in its own right:** 56.16 Wh pack, integrated 18 V charge
controller, BMS, and heater, with a 10 W panel in the regular variant [CIT-RAK9154-SOLAR].
This deployment uses the large panel. The system was always solar; only the shell was not.
This was previously stated wrong in [`POWER_BUDGET.md`](POWER_BUDGET.md) and matters a great
deal to the design — see that page.

**To confirm on the bench:** the exact SKU, how many cable entries the shell actually has,
and whether the lid panel terminates in a bare lead or a connector. Cable entry planning is
issue #20 and the premise changed with the shell.

**Select the buck on its no-load quiescent current.** It is a 24/7 load in parallel with
everything the firmware does, and a part drawing several milliamps at idle would exceed the
node's entire average draw on its own.

## RAK9154 ports (critical)

The pack has **two** load sockets. Connector reference:
`forest-weather-machines/rak-4-5-wire/docs/01-connector-reference.md` [CIT-RAK45WIRE] —
the sibling repo is cloned at `~/Documents/GitHub/forest-weather-machines`, not beside this
repo, so it is cited rather than linked. See [`CITATIONS.md`](CITATIONS.md).

### A — 4-pin Gateway Load (SP11/P4) — BMS Modbus — **fallback only**

Superseded by [ADR-0004](decisions/ADR-0004-bms-one-wire-path.md): the battery uses the
one-wire path on socket B. This socket stays unused and available if one-wire proves
unreliable on the bench.

| Pin | Signal |
|---|---|
| 1 | P+ (~12 V) → buck only |
| 2 | P− → GND |
| 3 | RS-485 A |
| 4 | RS-485 B |

Modbus slave **0x6E**, **9600** 8N1. Register map: FIRMWARE_SPEC §2.2 / RAK2560 settings §5d.

### B — 5-pin Sensor Hub Load (SP11/P5) — power + one-wire — **chosen**

**Mating part: `SP1110/P5-N` plug** (SP11 series, IP67, screw-locking circular, 2 A, 0.75 mm
contacts × 5). The socket on the pack is `SP1110/P5`; the cable-end plug that mates with it is
the `-N` variant [CIT-RAK9154]. Buying the plug is preferable to cutting the supplied cable,
which keeps the assembly weatherproof and reversible.

The connector visible **inside** a Sensor Hub shell is the board-side end of that bulkhead and
is a different, unpublished part — RAK does not document it, so it cannot be ordered by part
number. Measure the pin pitch before assuming a JST family (1.0 mm SH, 1.25 mm GH, 1.5 mm ZH,
and 2.0 mm PH all look alike in a photograph). Mating it saves a gland but ties the build to an
undocumented part.

| Pin | Signal | Notes |
|---|---|---|
| 1 | P+ (~12 V) | Splits two ways: buck VIN+, **and** the RK900's 12 V supply |
| 2 | P− | GND — shared return for the buck, the RK900, and the RS-485 reference |
| 3 | TXD | Half-duplex data (bridge to pin 5 for one-wire) |
| 4 | 3V3_In | Level ref / probe rail — tie carefully to 3V3, **never 5 V** |
| 5 | RXD | Bridge to TXD for one-wire |

**Not** full-duplex UART to RX1/TX1 as two independent lines without bridging — Hub protocol is half-duplex one-wire @ 9600. See Meshtastic / RAK-OneWireSerial / `rak-4-5-wire`.

## RAK5802 terminal blocks — what each one actually is

Two 4-way spring terminals, silkscreened `BAT GND A/RX B/TX` and `SCL SDA 3V3 AIN`.

| Terminal | Direction | Notes |
|---|---|---|
| `BAT` | **output** | Battery rail, 2.6–4.2 V, for powering a sensor [CIT-RAK5802]. Not an input. |
| `GND` | — | Common ground. Worth landing the RS-485 ground here, not just A/B. |
| `A/RX` | RS-485 A | Non-inverting |
| `B/TX` | RS-485 B | Inverting |
| `3V3` | **output** | On the **switched** `3V3_S` rail — see the warning below |
| `SCL` / `SDA` / `AIN` | — | I²C and one analog input. Unused here. |

**`BAT` and `3V3` are outputs to power a sensor, not supply inputs.** Neither can run the RK900,
which needs 12 V — that comes from the pack, through the buck, and never through this module.

**Do not take the pack's `3V3_In` (pin 4) from the RAK5802's `3V3` terminal.** That terminal sits
on `3V3_S`, which `WB_IO2` switches, and `src/sensors/rk900.cpp` deliberately drops `WB_IO2` LOW
when it finishes reading the weather station in order to power the transceiver down. The battery
is read *after* the weather station, so the pack's reference would be dead exactly when it is
needed, and the symptom would be a battery that never replies — easy to misread as a wiring or
protocol fault. Take pin 4 from the always-on `VDD` pad on the base-board header instead.

## RK900

4-wire: V+, GND, A, B → RAK5802 @ **4800**. Probe IO optional as junction box only.

## P0 wiring — decided

Per [ADR-0004](decisions/ADR-0004-bms-one-wire-path.md). The two sensors are on **separate
buses**, so neither can interfere with or block the other.

### The two power rules, stated plainly

> **12 V positive (`P+`) goes to BOTH the buck VIN+ AND the RK900's 12 V input.**
>
> **12 V negative (`P−`) goes to ALL THREE: the buck negative, `GND` on the RAK5802, and the
> RK900's negative.**

Both rails split. Nothing is daisy-chained through the RAK5802, and nothing is exclusive.

### The two data rules, stated plainly

> **Pack pins 3 and 5 (TXD and RXD) are joined together, and that single joined wire goes to
> the `IO1` pad on the base board.**
>
> **Pack pin 4 (`3V3_In`) goes to the `VDD` pad on the base board. Not to the RAK5802's `3V3`
> terminal, and never to 5 V.**

Both pads are on the 2.54 mm header along the edge of the RAK19007, silkscreened
`BAT IO2 IO1 A1 IN1` and `SDA SCL TX1 RX1 GND VDD BOOT0`. Neither signal appears on any screw
terminal, so both are solder joints.

### The whole harness on one screen

| From (pack, 5-pin SP11) | To | Why |
|---|---|---|
| Pin 1 `P+` (~12 V) | buck VIN+ **and** RK900 12 V | both, in parallel |
| Pin 2 `P−` | buck negative **and** RAK5802 `GND` **and** RK900 negative | all three |
| Pins 3 + 5 joined | `IO1` pad | one-wire half-duplex to the pack |
| Pin 4 `3V3_In` | `VDD` pad | always-on 3.3 V reference |

| From (RK900) | To |
|---|---|
| RS-485 A | RAK5802 `A/RX` |
| RS-485 B | RAK5802 `B/TX` |
| 12 V + | pack `P+` |
| Negative | pack `P−` |

1. `P+` feeds **two** loads, not one: the buck's VIN+ (→ WisBlock 5 V) and the RK900's 12 V
   directly. The RK900 is a 12 V device and the RAK5802 cannot supply it — that module's `BAT`
   and `3V3` terminals are sensor outputs at 4.2 V and 3.3 V. Size the buck for the WisBlock
   alone and select it on no-load quiescent draw; the RK900 does not pass through it.
2. RAK5802 → RK900 **data only** (A/RX, B/TX, and GND for the reference), fixed **4800** 8N1,
   slave `0x01`. No baud switching. Power comes from step 1, not from this module.
3. RAK9154 → **one-wire half-duplex** on the 5-pin socket, TXD/RXD bridged, via the SP11
   adapter cable. Watch pin 4 (`3V3_In`): tie to 3V3, **never 5 V**.
4. Leave the 4-pin Gateway Load socket unused — it is the documented fallback.

The earlier shared-bus option with a 4800/9600 baud switch is **rejected**; the rationale
is in the ADR.

## Enclosure

Unify **solar variant** (the no-solar 910406 was out of stock — see the BOM note above). Its
lid panel stays unconnected.

### Mounting the board — the baseplate holes do not line up

The Unify shells use a removable mounting plate rather than bosses in the shell itself, and the
plate that ships with the enclosure is not drilled for the RAK19007. Three ways out, best first:

1. **Buy RAK's mounting plate.** Sized `137 × 87 × 6.8 mm` for the 150×100×45 shell, ABS
   UL94V-0, and it comes with the self-tapping screws. It holds a RAK19001 *and* a RAK19007
   simultaneously, and builds in the required 3 mm standoff between the plate surface and the
   PCB underside — [store.rakwireless.com](https://store.rakwireless.com/products/unify-enclosure-mounting-plate).
   Confirm the size against the shell in hand first; the solar variant was not the planned one.
2. **Print RAK's blank plate and add your own bosses.** `Medium-Blank.step` / `.stl` in
   [Awesome-WisBlock](https://github.com/RAKWireless/Awesome-WisBlock/blob/main/Unify-Enclosure/README.md).
   This is likely the better route here, because the buck converter and the pack's field wiring
   also need somewhere to live, and the bought plate only accounts for WisBlock boards. The same
   folder has drill guides for the USB-C cutout and for gland/antenna holes, which is directly
   useful for the second cable entry (issue #20).
3. **Ready-made prints.** A finished RAK19007 plate by pdxlocs on
   [Printables](https://www.printables.com/model/622358-rak-unify-enclosure-simple-rak19007-mounting-plate)
   (drawn for the smaller shell — check before printing), and a dimensionally accurate RAK19007
   board model by Radish on [Printables](https://www.printables.com/model/1694959-rak-wireless-wisblock-19007-baseboard)
   for laying out hole positions in your own design.

Board facts for any custom plate: the RAK19007 is `30 × 60 mm`, mounting-hole locations are in
Figures 12–13 of its datasheet, and the board ships with `M2.5 × 4` screws for exactly this
[CIT-RAK19007]. Keep the 3 mm standoff — the underside carries the sensor-slot connectors and
the pad header being soldered to.
