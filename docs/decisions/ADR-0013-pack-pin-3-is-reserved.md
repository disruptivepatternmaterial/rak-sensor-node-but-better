# ADR-0013 — The one-wire link lands on pack pin 5 only; pins 3 and 5 are not joined

- **Status:** Accepted for the interface definition; the electrical attribution below is a
  hypothesis awaiting bench evidence
- **Date:** 2026-09-07
- **Supersedes:** nothing. The 3+5 bridge was never an ADR — it entered the build from the pack
  datasheet's pin names and was never decided.

## Context

Two RAK documents describe the two halves of the **same mated SP11 connector** and they disagree
about pin 3.

The pack side, from RAK's own datasheet for the RAK9154:

| Pin | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Name | `P+` | `P−` | **`TXD`** | `3V3_In` | **`RXD`** |

[CITE(datasheet): RAK9154 "Panel Connector Definition" — CIT-RAK9154-RAW](../CITATIONS.md)

The master side, from the datasheet for the RAK2560 Sensor Hub — the device this pack is built to
power and be read by:

| Pin | Name | Type | Description |
|---|---|---|---|
| 1 | `Vin` | PI | 12 V, input 5–16 V |
| 2 | `GND` | — | Ground |
| 3 | **`Reserved`** | IO | **Not defined — reserved for future use** |
| 4 | `Vcc_Probe` | PO | Power supply for the probe, 3.3 V |
| 5 | **`One-wire UART`** | IO | Communication with probe |

[CITE(datasheet): RAK2560 Sensor Hub, "Pin Definition" — CIT-RAK2560](../CITATIONS.md)

`TXD` and `RXD` on the pack side read like a full-duplex UART, which is why joining them for a
half-duplex link looked correct.

**What the master-side datasheet actually says is narrower than "nobody drives pin 3".** It
defines pin 3 as `Reserved | IO | Not defined` and names pin 5 alone as the one-wire UART. That is
a statement about the published interface, not about the Hub's internal connection: RAK does not
say what pin 3 is wired to inside the hub, only that it is not defined for use. An earlier
revision of this ADR wrote "no shipping master drives pin 3", which the source does not support —
what it supports is that **no shipping master is documented to use pin 3, and the one-wire link is
specified on pin 5 alone**. The decision below rests on that weaker, sourced claim, which is
sufficient for it: a pin the interface leaves undefined is not a pin to bridge a signal onto.

The two documents agree on everything else, including the direction of pin 4: the master **sources**
the probe rail and the pack **sinks** it (`Vcc_Probe` is `PO`; the Probe-IO datasheet lists the same
pin as `PI`). So this node supplying 3.3 V to pin 4 is correct and unchanged by this ADR.

**None of this is new information.** `CIT-RAK2560` has been in the citation registry since it was
verified, and it already states that a hub "uses pin 5 only and leaves pin 3 unconnected, so this
node's bench harness bridging pins 3+5 has no counterpart on any shipping master." The finding was
recorded and never acted on.

## Decision

1. **The one-wire link is pack pin 5 only.** Pin 3 is left unconnected and insulated.
2. Pins 3 and 5 are **not** joined, in any harness, bench or field.
3. Pin 4 continues to be supplied with 3.3 V from the node, per the master-side direction above.
4. Where the two datasheets conflict, the **master-side** document governs the interface, because
   it describes what a working system actually drives.

## The hypothesis this raises, which is not yet evidence

The 2026-09-06 captures ([`../EVIDENCE.md`](../EVIDENCE.md)) measured **−7.84 V** on the data
conductor during connector mating. Every one of those captures was taken on the **joined 3+5**
wire, so they cannot say which pin sourced it.

Two observations make pin 3 a candidate rather than a curiosity:

- The mating transient's source impedance measured **≈ 8.6 kΩ** while the pack's HIGH driver
  measured **≈ 4.5 kΩ**. Two different impedances on what was assumed to be one net is what two
  sources tied together looks like.
- Every one of the nine destroyed pads carried this joined conductor, and the harness was rebuilt
  the same way each of the three times it was replaced — so a wiring error present from the start
  survives "we replaced the harness" as an explanation.

This is a candidate, not a cause. It does not retire the relay in
[`../HARDWARE.md`](../HARDWARE.md) § "The data-line front end": that protects the pad from whatever
is on the wire, and remains correct whichever pin turns out to misbehave.

## Exit criteria

Separate the two pins and capture each independently, analyzer ground on the node's ground, no
Core pad in the loop, across at least ten mate/unmate cycles:

- **Pin 3 carries the excursion and pin 5 is clean** — the bridge was the fault. Record it, and
  re-evaluate whether the relay is still needed or is now belt-and-braces.
- **Pin 5 carries it too** — pin 3 is exonerated as the source and the relay stands on its own.
- **The 9600-baud traffic is on pin 3, not pin 5** — the outcome that would make this decision
  expensive, and the reason the capture probes both pins rather than only the suspect one. The
  pack datasheet labels pin 3 `TXD`, so "the pack transmits on 3 and listens on 5" is consistent
  with its own naming; a harness built to this ADR would then be **deaf**, and the symptom would
  be a silent pack that looks exactly like a dead one. If the capture shows protocol edges on
  pin 3, stop and supersede this ADR before rebuilding any harness — do not bridge the pins back
  together as a workaround, because that restores the configuration nine pads died on.

The first two outcomes leave the decision unchanged, because it rests on the interface definition
rather than on the electrical result. The third replaces it.

Record the numbers in [`../EVIDENCE.md`](../EVIDENCE.md) with date, host, and raw capture paths.
