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
| Pinned commit | `058bd69acf07552c90da81122eaad2274a85a42a` |
| SHA-256 | `717afcebeebd0a3d219aad5249bee04c0ddbcfd43059dae2a792bede4e91058b` |
| Copied | 2026-08-28 |

The same commit and hash are recorded in [`../schema.yaml`](../schema.yaml) under
`decoder:`. They must stay in agreement — and now something checks, rather than trusting
the sentence above. `scripts/check_decoder_parity.py` hashes this copy against
`pinned_sha256` on every run, including runs that read the live formatter and have no
other reason to open this file.

That check exists because the agreement broke. `6af5964` re-pinned `../schema.yaml` to
`058bd69` / `717afceb…` after the formatter was restructured upstream, and did not refresh
this copy. Every machine with the sibling repo cloned kept reporting `PREFLIGHT OK` from
the live file; CI, which is the only place that falls back to this copy, failed on every
run for a day. Worse than the red build: the field-by-field gate in CI was comparing the
schema against the **superseded** decoder and printing `checked 9 fields against 19
decoder types` while doing it.

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
