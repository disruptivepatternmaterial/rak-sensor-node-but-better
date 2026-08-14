# ADR-0002 — Payload contract conflicts with the live TTN decoder

- **Status:** **Accepted** — all three conflicts decided. Conflict 1 closed 2026-08-13; the
  payload freeze required by `FIRMWARE_SPEC.md` §6 is no longer blocked by this record.
- **Date:** 2026-07-30 · **conflict 1 decided 2026-08-13**
- **Decider (conflict 1):** the operator
- **Affects:** `payload/schema.yaml`, `docs/FIRMWARE_SPEC.md` §2.2 and §6, the encoder (WP6)
- **Amended 2026-07-30:** conflict 3's decision was made when the pack was expected to be
  read over Modbus. ADR-0004 moved it to the one-wire path, which changes the answer — see
  the amendment at the end of this record.
- **Resolved 2026-08-13:** conflict 1 is decided — **positive = charging, negative =
  discharging.** See "Resolution — conflict 1" below, which also corrects a mistaken
  assumption about which side of the two-repo contract had to move.

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

**Conflict 1: originally deferred to bench measurement (Option C).** `batt_current` was
marked `status: BLOCKED` in the schema, and no charge/discharge or brownout logic was
permitted to depend on the sign. **Superseded 2026-08-13 — see "Resolution — conflict 1"
below.** The deferral is left in place as the record of why it was open for six weeks.

## Rationale

The decoder is the authority on what ingest will accept, so where the disagreement is about
*decoding* (conflicts 2 and 3), the decoder wins by definition. Conflict 1 is different: it
is a claim about the **physical BMS register**, and neither source is a measurement — one
is a field note, the other a code comment. Guessing here would be exactly the
unsourced-constant failure the citation rule exists to prevent, so it stays open rather
than being resolved by preference.

## Consequences

- The payload cannot be frozen (`FIRMWARE_SPEC.md` §6) until conflict 1 closes. **Closed
  2026-08-13** — this record no longer blocks the freeze.
- `FIRMWARE_SPEC.md` §6 should be corrected to state channel numbers, not just type IDs —
  the decoder keys on `name_channel`, so a right type on a wrong channel silently misses
  its consumer.
- `scripts/check_decoder_parity.py` reports the BLOCKED field on every build, so this
  cannot be quietly forgotten.

## Evidence

**Needed to close conflict 1:** with the pack under a known load and then under charge,
read `0x6001` directly and record the observed sign in `docs/EVIDENCE.md` alongside the
pack's actual state. Cross-check against `rak-4-5-wire` bench captures [CIT-RAK45WIRE].

## Amendment — conflict 3 after ADR-0004

Conflict 3 was decided against `FIRMWARE_SPEC.md` §2.2 register `0x6009`, which is a Modbus
register carrying whole degrees. Multiplying by ten was the right answer for that source.

ADR-0004 then moved the pack onto the one-wire path, and that path does not expose Modbus
registers at all — it returns IPSO records, the same encoding the pack uses in its own
LoRaWAN uplinks, where type 103 already means tenths of a degree. Applying the ×10 to a
value that is already scaled would put the reading ten times high, which is the same class
of defect this record was written to prevent, in the opposite direction.

**Amended decision.** The ×10 applies only if the pack is read over Modbus. On the one-wire
path the value passes through unscaled. The encoder does this today and says so at
`src/payload.cpp`, and `src/reading.h` declares the field in tenths accordingly.

**Still an assumption.** Nothing has confirmed the one-wire record's scale against hardware;
it is inferred from the IPSO type. The first pack reply settles it — a value near 210 at
room temperature means tenths and the code is right, while a value near 21 means whole
degrees and the ×10 comes back. Tracked in issue #4.

## New measurement — 2026-08-12 (does NOT resolve conflict 1)

Recorded here because it bears directly on the battery-current sign and would otherwise be
lost, **not** because it decides anything. The ADR stays open.

On `4510763`, Heliotrope Ridge, across 20 consecutive `battdiag` cycles and again in the
`rak4631` field image, the pack reported:

```
08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
08:04:47 battery : raw v=1212 i=-1 soc=91 t=230
```

The raw current register is `i=-1`, i.e. **-0.01 A on a pack at 91% that is not connected
to a charger** — the RK900's 12 V feed on pack pin 1 is deliberately unconnected, and the
node is running from the pack.

Why it is suggestive and still not sufficient:

- A pack that is **discharging** (which this one is, at a small rate — it is powering the
  node) reporting a **negative** current is consistent with `FIRMWARE_SPEC.md` §2.2's
  "negative = charging" being **wrong**, and with the live decoder's "positive = charging".
- But -0.01 A is one LSB. At that magnitude the reading is not distinguishable from a zero
  offset or a rounding artifact, and the sign of a single-LSB value is exactly the thing a
  calibration offset gets wrong. **One LSB is not a sign convention.**

What would settle it, and what nobody has run: put a real charge current into the pack —
connect the panel, or a bench supply — and read the sign at a magnitude well clear of the
LSB. A discharge under a deliberate load would do equally well.

**Superseded 2026-08-13.** Conflict 1 was closed by an operator decision rather than by this
measurement — see "Resolution — conflict 1". The confirming charge-current observation
described above has still never been run, and this entry is still not evidence of the sign.

CITE(bench): docs/EVIDENCE.md 2026-08-12 — 20 consecutive cycles at `12.12 V, -0.01 A,
  91%, 23.0 C`, plus the same values in the `rak4631` field image.

## Resolution — conflict 1, 2026-08-13

**Decision: positive current means the pack is CHARGING. Negative means DISCHARGING.**

Decided by the operator on 2026-08-13. This is the sign convention the pack itself reports,
so the firmware does not invert a hardware-reported value on its way to the record.

### Rationale

The pack's own telemetry is the primary source, and it is the only one of the three sources
that is a measurement rather than a note about a measurement. Adopting it means no code
anywhere in the path applies a sign transform: the pack's two's-complement word travels to
TTN unmodified, and the number in the database means what the hardware said it meant. Any
convention other than the pack's would require an inversion somewhere, and an inversion is a
place the record can silently go backwards — which is precisely the failure this record was
opened to prevent.

`FIRMWARE_SPEC.md` §2.2's "negative = charging per field docs" is the claim that loses. It
was a field note, never a measurement, and it most likely described the RAK2560 Hub's
re-encoding of the pack rather than the pack's raw register. It is corrected in the same
change as this resolution.

### Which side of the two-repo contract actually had to move: **neither**

This was expected to require a decoder change. Read from code, it does not, and it is worth
recording why so nobody "fixes" the decoder later and inverts the record by doing so.

| Side | What the code actually does today | Agrees with the decision? |
|---|---|---|
| Pack → firmware parse (`src/sensors/battery_frame.cpp:239`) | `out.current.set((int16_t)val16(...))` — takes the pack's two's-complement word verbatim, no sign normalisation | yes, by construction |
| Firmware → wire (`src/payload.cpp:94` and `:143`) | `put_s16(kChBattAmps, kTyBattAmps, ...)` — emits the parsed value unmodified | yes |
| Live decoder (`rak-wx-station-default.js` @ `efc0e3c`) | type 185 is `{ size: 2, name: "dc_current_batt", signed: true, divisor: 100 }`; `arrayToDecimal` sign-extends and divides. **No sign transform of any kind.** Its header comment at `:16` documents "signed; positive = charging" | yes — and it already says so in words |

So the decoder's behavior *and* its documented convention already match the decision. The
only artifact that disagreed with the pack was our own spec line. **No wire format changes,
no encoder changes, no decoder changes.**

### Sequencing, and why the usual two-repo hazard does not arise here

Rule 60 exists because a drifted encoder does not lose one field — the decoder throws on the
first unrecognized type byte and `decodeUplink` discards the **entire** uplink. That hazard
is real whenever the bytes on the wire change. Here **no byte on the wire changes and no
decoded value changes**, so there is no window in which the two repos can disagree, and no
cutover to sequence. The node kept transmitting every 15 minutes throughout this change and
its uplinks decode exactly as before.

Had the decision gone the other way — adopting the spec's "negative = charging" — the safe
order would have been decoder-first and tolerant: widen the decoder to accept both, let it
run until every in-flight uplink was drained, then narrow it. That is not needed and was not
done, because a sign flip on either side alone would have *created* the disagreement this
record exists to prevent.

### Blast radius: the record, not the node

Per [`docs/reviews/2026-08-12_spec_drift.md`](../reviews/2026-08-12_spec_drift.md) §2.2, which
enumerates the six code paths whose meaning depends on this sign, **none of them is a control
decision.** No charge detection, no brownout threshold, and no TX gating reads the sign — the
six sites are one parse, two encode calls, one wire-format comment, one console print and one
field declaration. That is why this was safe to leave open for six weeks, and it is equally
why it is safe to close now: resolving it wrongly would have inverted the record without
stranding the node. The `Do not write charge/discharge logic against the sign` prohibition in
rules 50 and 60 is lifted, but note that no such logic exists today and adding any is a
separate change with its own gates.

### What this resolution does NOT claim

**Nothing here was observed on hardware under charge.** The 2026-08-12 measurement below is
still one LSB on a discharging pack and still settles nothing on its own; it is consistent
with the decision, not proof of it. This is a decision about which convention the project
adopts and records, made by the operator, not a bench result. The confirming observation —
a real charge current well clear of the LSB, with the panel or a bench supply connected —
has still never been run, and no evidence entry claims otherwise.

CITE(spec): [CIT-CAYENNE-LPP] Cayenne LPP — channel/type TLV encoding; type 185 is a signed
  16-bit value in 0.01 A units, so the wire format carries the sign and the convention is a
  question of meaning, not of encoding.
CITE(sibling): [CIT-FWM-DECODER] `forest-weather-machines/LoRaWAN/payload/rak-wx-station-default.js`
  @ `efc0e3cf25b3f9288ff1b9a1a60849b8d425cc32` — `WX_TYPES[185]` is `signed: true, divisor: 100`
  with no sign transform in `arrayToDecimal` (`:118-130`), and the header at `:16` documents
  "positive = charging". Confirms the decoder needs no change.
CITE(bench): docs/EVIDENCE.md 2026-08-12 — a discharging pack reported negative current,
  consistent with this decision at one-LSB magnitude. Suggestive, not confirming.

## Citations

- CITE(sibling): live TTN formatter `rak-wx-station-default.js` @ `efc0e3c` [CIT-FWM-DECODER]
- CITE(sibling): RAK9154 connector and register reverse-engineering @ `efc0e3c` [CIT-RAK45WIRE]
- CITE(datasheet): RAK9154 pack documentation [CIT-RAK9154]
- CITE(spec): Cayenne LPP channel/type TLV encoding [CIT-CAYENNE-LPP]
