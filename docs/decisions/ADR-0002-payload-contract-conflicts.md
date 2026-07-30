# ADR-0002 — Payload contract conflicts with the live TTN decoder

- **Status:** **Open** — blocks the payload freeze required by `FIRMWARE_SPEC.md` §6
- **Date:** 2026-07-30
- **Affects:** `payload/schema.yaml`, `docs/FIRMWARE_SPEC.md` §2.2 and §6, the encoder (WP6)

## Context

Building `payload/schema.yaml` from the live TTN formatter surfaced three disagreements
between our spec and the decoder that is actually running in ingest. Each is the kind of
defect that produces plausible-looking wrong data rather than an obvious failure.

Decoder of record: `forest-weather-machines/LoRaWAN/payload/rak-wx-station-default.js`
@ `efc0e3c` [CIT-FWM-DECODER].

### Conflict 1 — battery current sign (**blocking**)

| Source | Claim |
|---|---|
| `FIRMWARE_SPEC.md` §2.2 | `0x6001` is signed, **negative = charging** ("per field docs") |
| Decoder header | type 185 is signed, **positive = charging** |

Both cannot hold. The consequence of choosing wrong is inverted charge/discharge logic and
a brownout threshold that behaves backwards — on a pack with no solar in the BOM.

### Conflict 2 — humidity type is not interchangeable

`FIRMWARE_SPEC.md` §6 lists humidity as "104/112" as though either works. In the decoder
they are different fields:

- Type **112** → name `humidity_prec` → key `humidity_prec_4` → mapped to `wx_humidity` ✅
- Type **104** → name `humidity` (1 byte, divisor 2) → key `humidity_4` → **not in
  `CHANNEL_NAMES`**, so it falls through as a raw key and misses every consumer ❌

### Conflict 3 — battery temperature scaling

`FIRMWARE_SPEC.md` §2.2 reads BMS register `0x6009` at scale ×1 (whole °C). The payload
carries it as type 103, whose decoder divisor is 10. Encoding the raw value produces a
reading **10× low** — 21 °C arrives as 2.1 °C.

## Options considered

For conflict 1 only; conflicts 2 and 3 have a single correct answer.

| Option | Pros | Cons |
|---|---|---|
| A. Trust `FIRMWARE_SPEC.md` (negative = charging) | Matches our field notes | Field notes may describe the Hub's re-encoding, not the raw BMS register |
| B. Trust the decoder header (positive = charging) | Matches what ingest assumes today | The header is a comment, not a measurement |
| C. Resolve on the bench | Definitive | Needs hardware, which is on order |

## Decision

**Conflicts 2 and 3: decided now.**

- Humidity is **type 112 on channel 4**. Type 104 is not an acceptable substitute.
- Battery temperature is **multiplied by 10 before encoding** to match the type-103 divisor.

Both are recorded in `payload/schema.yaml`.

**Conflict 1: deferred to bench measurement (Option C).** `batt_current` is marked
`status: BLOCKED` in the schema. No charge/discharge or brownout logic may depend on the
sign until this is closed.

## Rationale

The decoder is the authority on what ingest will accept, so where the disagreement is about
*decoding* (conflicts 2 and 3), the decoder wins by definition. Conflict 1 is different: it
is a claim about the **physical BMS register**, and neither source is a measurement — one
is a field note, the other a code comment. Guessing here would be exactly the
unsourced-constant failure the citation rule exists to prevent, so it stays open rather
than being resolved by preference.

## Consequences

- The payload cannot be frozen (`FIRMWARE_SPEC.md` §6) until conflict 1 closes.
- `FIRMWARE_SPEC.md` §6 should be corrected to state channel numbers, not just type IDs —
  the decoder keys on `name_channel`, so a right type on a wrong channel silently misses
  its consumer.
- `scripts/check_decoder_parity.py` reports the BLOCKED field on every build, so this
  cannot be quietly forgotten.

## Evidence

**Needed to close conflict 1:** with the pack under a known load and then under charge,
read `0x6001` directly and record the observed sign in `docs/EVIDENCE.md` alongside the
pack's actual state. Cross-check against `rak-4-5-wire` bench captures [CIT-RAK45WIRE].

## Citations

- CITE(sibling): live TTN formatter `rak-wx-station-default.js` @ `efc0e3c` [CIT-FWM-DECODER]
- CITE(sibling): RAK9154 connector and register reverse-engineering @ `efc0e3c` [CIT-RAK45WIRE]
- CITE(datasheet): RAK9154 pack documentation [CIT-RAK9154]
- CITE(spec): Cayenne LPP channel/type TLV encoding [CIT-CAYENNE-LPP]
