# ADR-0007 — There is no second voltage source, so the brownout hold is bounded instead

- **Status:** Accepted
- **Date:** 2026-08-05
- **Refs:** [#45](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/45),
  [#38](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/38),
  [ADR-0004](ADR-0004-bms-one-wire-path.md)

## Context

`9b17102` fixed #38 by making the brownout gate **fail closed**: after
`kInvalidReadsBeforeInhibit` consecutive cycles with no readable pack voltage, the node stops
transmitting. That was the right direction. Transmitting blind into a sagging pack can drive it
to its protection cutoff, and a disconnected pack may not restart on panel current at all —
that failure is unrecoverable without a hike, and possibly not recoverable even then.

But the gate is **single-sourced on the RAK9154 one-wire link**, which is the least reliable
element in this build and the one that has actually failed repeatedly during bring-up
(`docs/EVIDENCE.md`). A broken wire, a bad connector, or the pack's own comms dying now
silences a node whose pack is full — permanently. Because the node is Class A, a downlink can
only follow an uplink, so a permanently silent node is also **uncommandable**: there is no
remote route left to override the hold. Silent-forever and drained both end in a hike, and the
silent one gives no warning first.

#45 asked for the single point of failure to be removed rather than the gate loosened. The
right way to do that is a second, independent voltage measurement.

## The second source does not exist

The obvious candidate is the nRF52840 SAADC reading the RAK19007 base board's battery divider
(the `PIN_VBAT` / `WB_A0` path used by RAKwireless' own battery examples). It does not work
here, for a wiring reason rather than a firmware one.

The base board's battery divider observes exactly one node: the `BAT` connector. In this build
the pack never touches it. `docs/FIRMWARE_SPEC.md` §2 is explicit:

> **Power:** P+/P− from 4-pin or 5-pin → **12 V→5 V buck** → WisBlock 5 V. Never feed P+ to
> `BAT`.

`docs/HARDWARE.md` says the same thing at wiring level and explains why: `BAT` and `3V3` on
the WisBlock side are **outputs** at 4.2 V and 3.3 V intended to power a sensor, not supply
inputs, and a ~12 V pack on `BAT` would be applied straight across a 4.2 V single-cell charging
circuit. So the divider reads an unpopulated connector.

Everything downstream of the buck is regulated. The 5 V rail and the 3V3 rail hold their value
across the pack's entire usable range and only move once the buck itself drops out — which
happens somewhere below the pack's protection cutoff, i.e. after the event this gate exists to
prevent. There is no monotonic mapping from any rail this chip can measure back to pack
terminal voltage.

The other candidate, the RAK5802's `AIN` terminal, is a bare analog input with nothing wired to
it. Using it would need a physical divider from `P+` that does not exist on the bench or in the
field unit.

**Conclusion: no independent voltage source is available in the current hardware.** Adding a
scaled ADC read anyway would mean inventing a mapping from a regulated rail to a pack voltage —
a fabricated reading presented as a measurement, which `AGENTS.md` forbids and which is worse
than no reading, because the brownout gate would then act on it.

## Decision

Bound the silence rather than fake a second source.

`power::kNoEvidenceKeepaliveCycles` = 24. After that many consecutive cycles held on the
**no-evidence** path, the node transmits one uplink regardless, then resumes holding for
another full interval. At the default hourly interval that is about one uplink per day.

Three properties make this different from reopening what #38 closed:

1. **It applies only to the no-evidence path.** When the pack answers and reports a low
   voltage, there is no keepalive. The evidence says spending energy is the wrong move, and
   staying quiet is exactly right. #38 exists because that used to be ignored.
2. **Flash writes stay blocked** on a keepalive cycle. Transmitting blind is a bounded risk;
   a half-written config file survives every reset afterwards.
3. **The cost is negligible and the benefit is not.** One short uplink per day is roughly 5% of
   the TTN sandbox airtime allowance [CITE(policy): `CIT-TTN-FUP`] and a rounding error against
   a pack measured in amp-hours. What it buys is that the node's silence stops being
   indistinguishable from its death, and that the Class A downlink route reopens once a day so
   the node can still be commanded.

## Consequences

- A healthy pack with a dead one-wire link is heard from about daily instead of never. This is
  the acceptance condition #45 named, and it is met by mitigation, not by removing the single
  point of failure — the single point of failure is a wiring fact and cannot be removed in
  firmware.
- **Residual gap, deliberately left:** a hold that engaged on a *measured* low voltage and was
  then restored from flash across a reset gets no keepalive, even if the link later dies. The
  last actual evidence was a low pack, and betting against that evidence is the wrong side to
  err on.
- **Not measured.** Neither this path nor #38's engage/restore-across-reset path has run on
  hardware. Closing it needs a bench test with the one-wire lead physically pulled from a full
  pack, observing the hold engage, ~24 cycles of silence, then one uplink. No H1–H8 gate closes
  until that is in `docs/EVIDENCE.md`. Status stays `🚧 NOT YET DEPLOYED`.

## Alternatives rejected

| Option | Why not |
|---|---|
| ADC read of `PIN_VBAT` scaled to pack voltage | The divider observes the unpopulated `BAT` connector, not the pack. Any scaling would be fabricated. |
| ADC read of the 3V3/5 V rail as a proxy | Regulated — flat across the pack's whole usable range, so it carries no information about pack state until after the failure it would need to predict. |
| Loosen the gate back toward fail-open | Reintroduces #38. An unrecoverable pack is worse than lost data. |
| Add a hardware divider from `P+` to the RAK5802 `AIN` terminal | The genuinely correct fix, and the only one that removes the single point of failure. Requires a physical change to a built unit; recorded as the follow-up rather than assumed. |
