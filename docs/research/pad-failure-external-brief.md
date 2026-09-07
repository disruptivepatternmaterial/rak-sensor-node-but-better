# nRF52840 GPIO pads failing short-to-ground on a battery-pack data line

**External brief, 2026-09-06; scope note added 2026-09-07.** Written to be shared outside the
project. Self-contained: it assumes no knowledge of this repo. The underlying measurements and
their raw capture paths are in [`../EVIDENCE.md`](../EVIDENCE.md); the resulting design is in
[`../HARDWARE.md`](../HARDWARE.md) § "The data-line front end".

> ### Read the numbers below with these three limits
>
> **1. Every capture was taken on two pins joined together.** The pack's connector has a `TXD`
> (pin 3) and an `RXD` (pin 5); this harness bridges them, and the probe was on the bridge. So
> **−7.84 V, +4.33 V, 8.6 kΩ and 0.9 mA describe the pair, not a conductor.** The source impedance
> in particular may be two different sources in parallel — the mating transient measured ≈ 8.6 kΩ
> while the pack's own HIGH driver measured ≈ 4.5 kΩ on what was assumed to be one net. RAK's
> master-side datasheet defines pin 3 as `Reserved / Not defined` and specifies the one-wire link
> on pin 5 alone, so the bridge appears to be a build error. **The capture that separates the two
> pins has not been run yet**, and it may move every impedance figure here.
>
> **2. "This destroys a pad" is not claimed and is not supported by this data.** Nine pads died;
> this is the only out-of-spec condition anyone has found; the two have not been causally linked.
> The § "Where this is stuck" section below is the honest state of it — the measured energy looks
> far too small for the damage observed.
>
> **3. The proposed fix is not built.** No relay has been fitted, bench-qualified, or driven by
> firmware; the parts are on order. Nothing here is a report on a working circuit.

## System

Solar-powered LoRaWAN weather node, deployed unattended. A Nordic nRF52840 (RAK4631 module on a
RAK19007 carrier) reads a RAK9154 12 V battery pack over a **single-wire half-duplex 9600 8N1**
link.

One 5-pin circular connector (SP11) carries all of it: pack `P+` (~12 V), pack return, the data
wire (the pack's TXD and RXD pins joined), and a 3.3 V reference that the **node supplies to the
pack**. The same 12 V feeds the buck converter that powers the node, so unmating the connector
also powers the node down.

## Symptom

**Nine GPIO pads destroyed** over the project's life. In every case the dead pad was the one
carrying the pack data line; pads on the same parts never connected to the pack stayed healthy.

A dead pad reads permanently LOW as an input and measures **~3.9 Ω to ground**. The rest of each
chip keeps working. A 1 kΩ series resistor was inline for at least one of the deaths.

The mechanism has never been established.

## What was measured

Saleae Logic Pro 8, 1.5625 MS/s analog, four channels, with **the analyzer's ground on the node's
ground pad**. Every earlier capture in this project grounded at the pack, which structurally
cannot reveal a pack-to-node offset — moving that one clip is what made the effect visible. No MCU
pad was connected to the data wire during any capture.

| Condition | Data conductor, relative to node ground |
|---|---|
| ~20 mate/unmate cycles, node unpowered | **−7.84 V** min, **+4.33 V** max |
| Mate with the node powered and the pack driving | **−2.62 V** min |
| Node ground excursion, same events | ≤ 224 mV |
| Node-supplied 3.3 V reference excursion | ≤ 9 mV |

Sixty excursions past −1 V and seventeen past −6 V in a single plugging session. Individual events
last **1.6–10.6 ms**. The −7.84 V figure is inside the analyzer's ±10 V analog range, so it is a
measured value rather than a clipped floor.

Adding a **1 kΩ shunt** from the data wire to node ground collapses the excursion to −0.82 V and
the event duration to 3–4 µs. Treating the unloaded figure as open-circuit gives a **source
impedance of ≈ 8.6 kΩ** and an **available current of ≈ 0.9 mA**. Separately, loading the line the
same way while the pack drives HIGH puts the **pack's driver at ≈ 4.5 kΩ** source impedance.

The nRF52840's I/O absolute maximum is **−0.3 V to VDD+0.3 V**, and 0.3 V total when the device is
unpowered [CIT-NRF-GPIO]. The wire therefore runs 26× past the negative limit, through the
**lower** clamp — which is consistent with pads failing short to ground rather than short to VDD.

**Ground offset is ruled out.** Node ground moved at most 224 mV and the node-supplied reference at
most 9 mV, so the data conductor itself is being driven.

## Where this is stuck

0.9 mA for a few milliseconds is roughly **5 µJ per event**. That is orders of magnitude below the
HBM energy the part is qualified to survive, and the 1 kΩ series resistor that was fitted when one
pad died would have limited the fault to about 0.75 mA.

So the transient we can measure looks too feeble to be the thing destroying pads — yet it is the
only out-of-spec condition found so far, and it is out of spec in exactly the direction the
failures point.

## Proposed fix

A **normally-open optically-isolated MOSFET relay** in series with the data wire — Panasonic
`AQY212EH`, 60 V bidirectional contacts, 1 µA maximum off-state leakage
[CITE(datasheet): Panasonic AQY212EH product data](https://industry.panasonic.com/global/en/products/control/relay/photomos/number/aqy212eh).
Its LED is sunk by a GPIO rather than sourced, so the switch is open at boot, throughout sleep, and
whenever the node has no power at all — which covers every connector mating.

A Schottky from the MCU side of the relay to ground backs it up, because the relay's off-state
**output** capacitance is not the tabulated LED-to-output isolation figure — that is I/O
capacitance, a different parameter. Output capacitance appears only as a curve against applied
voltage in the characteristics graphs and reads at roughly **80 pF** near 0 V, which will pass a
fast edge straight through an open switch. Two `BAT85S` in parallel rather than one, because a
single diode at the measured 0.9 mA clamps near −320 mV against a −300 mV limit; the cold-weather
forward-voltage rise is unresolved, since the datasheet characterises VF at 25 °C only.

This is a structural response to an unproven mechanism: it removes the pad from a whole class of
unknown insults. It is not a diagnosis, and fitting it does not clear the harness.

## Questions worth an outside opinion

1. What drives a floating signal line to −7.84 V behind ~8.6 kΩ when the adjacent conductor in the
   same harness is a 12 V hot-plug? Capacitive coupling from the buck's inrush is the working
   guess, but the 1.6–10.6 ms event duration does not fit a simple RC with any plausible harness
   capacitance.
2. Can ~0.9 mA of repeated millisecond-scale forward clamp current realistically destroy an ESD
   diode over dozens of cycles, or is this the wrong signal to be chasing?
3. Is a normally-open switch the right answer while the mechanism is unproven, or is the effort
   better spent establishing the mechanism first?
4. The backup clamp has almost no margin: two paralleled `BAT85S` sit near −280 mV against a
   −300 mV limit at room temperature, and Schottky forward voltage rises as it cools — this node
   overwinters outdoors. Is there a better clamp for a −300 mV budget at −20 °C, or is the right
   move to stop treating the clamp as a backstop and rely on the open switch alone?

CITE(datasheet): [CIT-NRF-GPIO] nRF52840 Product Specification §5, Absolute maximum ratings —
I/O pin voltage −0.3 V to VDD+0.3 V for VDD ≤ 3.6 V.
CITE(datasheet): [CIT-SALEAE-LOGICPRO8] Logic Pro 8 analog range −10 V to +10 V, which is why
−7.84 V is a measurement and not a saturated reading.
CITE(bench): [`../EVIDENCE.md`](../EVIDENCE.md) 2026-09-06, captures 7 through 11 — every number in
this brief, with raw capture paths.
