# Pinned copy of the TTN formatter

**This is a byte-identical copy of someone else's file. Do not edit it.** The authoritative
formatter lives in `forest-weather-machines`; editing this copy changes nothing in TTN and
breaks the hash pin that makes it trustworthy.

## Why a copy exists

The payload is a two-repo contract, and a drifted encoder does not lose one field — the
decoder throws on the first unrecognized type byte and the **entire uplink is discarded**.
`scripts/check_decoder_parity.py` exists to make that impossible to ship, which means it
has to be runnable everywhere.

It was not. `forest-weather-machines` is a **private** repository, and the default GitHub
Actions token is scoped to this repository alone, so CI could never see the decoder. The
gate that was supposed to protect the payload contract was instead just failing every run —
and a gate that always fails gets switched off, which is how the contract would have
silently broken later.

## Provenance

| Field | Value |
|---|---|
| Source repo | `disruptivepatternmaterial/forest-weather-machines` (private) |
| Path | `LoRaWAN/payload/rak-wx-station-default.js` |
| Pinned commit | `efc0e3cf25b3f9288ff1b9a1a60849b8d425cc32` |
| SHA-256 | `9c58c2b92193d412958401c9182109495ff748ff502b9d76369dc1f93a85777c` |
| Copied | 2026-07-30 |

The same commit and hash are recorded in [`../schema.yaml`](../schema.yaml) under
`decoder:`. They must stay in agreement.

## What a run against this copy does and does not prove

| Question | Answered here? |
|---|---|
| Does every field in `schema.yaml` match the decoder's channel, type, size, sign, and divisor? | **Yes** |
| Are there fields the decoder cannot represent, needing a formatter change? | **Yes** |
| Has the formatter moved upstream since it was pinned? | **No** |

The last one needs a live checkout, so the checker prefers one whenever it can find it and
prints `source: live` or `source: pinned` on every run. Any machine with the sibling repo
cloned — the workstation and the Heliotrope Ridge build host both do — answers all three.

## Re-pinning

When the upstream formatter legitimately changes:

1. Reconcile `../schema.yaml` against the new decoder **by hand**. This is the whole point
   of the gate; re-pinning without reconciling defeats it.
2. Copy the new file here.
3. Update the commit and SHA-256 above and in `../schema.yaml`
   (`scripts/check_decoder_parity.py --repin` prints the hash).
4. Re-run the gate on a machine with the sibling cloned, so it reports `source: live`.
