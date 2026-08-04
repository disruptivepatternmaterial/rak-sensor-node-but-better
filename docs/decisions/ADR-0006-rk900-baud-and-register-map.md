# ADR-0006 — RK900-09 line rate: keep 9600 for now, register map unchanged

- **Status:** Accepted. Baud settled at 9600 by measurement; register map confirmed
  (RAKwireless/sibling/beegee map) by the full five-register frame on 2026-08-03 — see the
  Update section at the foot of this ADR and `docs/EVIDENCE.md`.
- **Date:** 2026-08-03
- **Closes:** #30 (baud), documents the register-map question `rk900.cpp`'s raw-dump comment
  refers to (previously an undocumented forward-reference — no ADR-0006 existed until this one)
- **Affects:** `src/sensors/rk900.cpp` (`kBaud`, register-index enum), `docs/CITATIONS.md`

## Context

Three independent baud claims exist for the RK900-09 on this exact unit's RS-485 pair:

1. **RAKwireless datasheet** (`CIT-RK900`) and the sibling fleet's live field configuration
   for a **currently-deployed** twin system — `forest-weather-machines`
   `LoRaWAN/docs/RAK2560_weather_station_settings.md` at `ddfebfb3`, TTN device
   `9181010k6063240022`, "RAK Weather Sensor 002", active since 2026-05-18 — both say **4800
   8N1**, and that doc is explicit: "The RK900-09 weather station probe (00886) uses baud
   4800. The RAK9154 battery probe (00336) uses baud 9600. Each probe's RS485 settings are
   independent." This is not a spec claim — it is an operating field unit with real wind data
   flowing to TTN today.
2. **The Rika manufacturer datasheet** and the open-source `beegee-tokyo/RUI3-RS485-Wind-Sensor`
   project, which uses this exact sensor on RUI3/RAK3172, both state the **factory default is
   9600**. Rika's own RS485 config commands (`>CUS <rate> 8-N-1`) accept `2400`–`115200`, so 4800
   is a supported, settable rate — not a fixed value the sensor ignores.
3. **Direct bench observation**, `CIT-RK900-BAUD-2026-08-03`: this physical unit replies at 9600
   and gives zero bytes at 4800 across four consecutive sweeps.

Two different, unrelated things are being asked at once and this ADR was written to stop
conflating them:

- **What does this unit answer at right now?** Settled — 9600, by direct measurement.
- **What line rate should the fleet standardize on?** Open — the one other real weather
  station running this exact sensor+battery combo runs 4800, and Rika's own tool supports
  reprogramming a unit's baud in place (see Reprovisioning below), so "the sensor is stuck at
  9600" is not a real constraint if 4800 is later preferred for fleet consistency.

There is a **second, separate conflict** on the register map itself, which is why
`rk900.cpp`'s `RK900::read()` now prints the raw `0x0000`–`0x0004` words ahead of any
interpretation (see the "hold production 9600 change" merge, `6ea7488`):

- RAKwireless datasheet, the sibling's field-deployed reference doc, and
  `beegee-tokyo/RUI3-RS485-Wind-Sensor` all agree: `0x0000` wind speed (×0.01), `0x0001` wind
  direction, `0x0002` temperature (×0.1), `0x0003` humidity (×0.1), `0x0004` pressure (×0.1).
  This is a three-source consensus and matches the enum already in `rk900.cpp`.
- A Rika manufacturer page pasted into this session by the operator instead showed `0x0000` as
  a device-status word with wind speed starting at `0x0002` — a **different register layout**,
  not just a different baud. No fetch of that exact page succeeded in this session to pin it as
  a citation, so it is recorded here as an unresolved conflict rather than cited.

The single-register busscan reply (`0x0000` = `0x0000`) is consistent with *both* candidate
maps — "wind speed 0.00 m/s, sensor idle indoors" and "device status 0 = OK" are both
plausible — so it does not discriminate between them. Only the full five-register frame can.

## Options considered

| Option | Pros | Cons |
|---|---|---|
| A. Firmware stays at 9600 (current state) | No further physical action needed; matches what this specific unit already does; unblocks the full-frame read immediately once DFU is out of the way | Diverges from the one real fleet precedent (4800); if this unit is later swapped or reset, "works at 9600" may not generalize |
| B. Reprogram this sensor to 4800 to match the field-deployed twin | Matches the only real prior-art fleet config; keeps `CIT-RK900`'s 4800 citation truthful for this unit too | One more physical/firmware step before any read succeeds; the exact command sequence (below) has not been exercised against this specific unit yet |
| C. Block on resolving both conflicts before any read | Maximally certain | Exactly the paralysis this ADR exists to end — the full-frame read is evidence *for* resolving the register map, not something that has to wait on it |

## Decision

**Option A for right now.** Leave `kBaud = 9600` as the concurrent session set it
(`8ca5299`), get the full five-register frame at 9600 next — that is the next physical
action already queued in `docs/EVIDENCE.md` ("operator double-taps RESET… re-run
`scripts/flash.sh --yes --env busscan`") — and use the actual register values to settle the
map conflict by inspection (a plausible wind speed / temperature / humidity / pressure
quadruple confirms the RAKwireless/sibling/beegee-tokyo map; a small enum-like value in
register 0 with a real wind speed one register over would confirm the Rika-page map instead).

**Whether to then reprogram this unit to 4800 for fleet consistency (Option B) is left open**
and should be revisited once the frame is read — it is a five-minute firmware job, not a
hardware limitation, so there is no urgency to decide it before the read.

## Rationale

The four-sweep busscan result is real measurement and the sibling's field doc is real
prior art; neither is wrong, they are answering different questions. Changing `kBaud` back
to 4800 right now would re-brick the read against a unit that has only ever answered at
9600, purely to match a citation, which is the guess this project's citation discipline
exists to prevent from going the other direction too. Reading the full frame first costs
nothing extra — the busscan image is already built for 9600 — and produces the evidence
that makes the register-map call non-arbitrary.

## Consequences

- `src/sensors/rk900.cpp` keeps the raw hex dump ahead of the typed fields (added in
  `6ea7488`) until the map is confirmed — do not remove it as "debug noise."
- If the full frame confirms the Rika-page map instead of the current enum, `RegisterIndex`
  in `rk900.cpp` and the scaling in the `LOGF` line both need to change together, and this
  ADR gets a status update rather than a new ADR (same conflict, same evidence gate).
- If a future decision reprograms this unit to 4800, the reprovisioning sequence below is the
  answer — no WisToolBox step exists for it because WisToolBox's `RK900-09_weather_station.json`
  probe profile only configures the **RAK2560 hub's own** RS485 port
  (`ATC+IO_CFG={PRB_ID}:RS485:4800:8:1:0`); it contains no command that reaches the RK900-09's
  own UART, and that hub doesn't exist on this RAK4631 + RAK5802 stack anyway.

### Reprovisioning sequence (not yet exercised — for the Option B follow-up)

Per the Rika RK900-series communication protocol (`CIT-RK900-PROTO`), sent over the same
RS-485 pair at the sensor's *current* baud, each line within the 15 s settings-mode window:

```
>*\r\n                 // enter settings mode; expect "\n>CONFIGURE MODE\r\n"
>CUS 4800 8-N-1\r\n    // set baud/framing; expect ">CMD IS SET\r\n"
>!\r\n                 // exit settings mode (required before reset, per the manual's own
                        // documented sequence: One -> Two -> Five -> Four)
>RESET\r\n              // soft reset; new baud takes effect after this
```

## Evidence

- `docs/EVIDENCE.md` — 2026-08-03 busscan entries (single-register reply at 9600; full-frame
  DFU failure; and the successful full-frame capture that resolves the register map).
- The full five-register frame — the test that closes the register-map half of this ADR — was
  captured 2026-08-03 at commit `998dc26`. See the Update below.

## Update — 2026-08-03: register map confirmed, both conflicts resolved

The full five-register frame was captured (`docs/EVIDENCE.md`, commit `998dc26`, busscan
image, Heliotrope Ridge). Slave `0x01`, FC `0x03`, `0x0000`–`0x0004` at 9600 8N1 returned,
across two consecutive reads:

```
01 03 0A 00 00 00 00 00 FB 01 F8 27 56 <crc>   -> regs 0,0,251,504,10070
01 03 0A 00 00 00 00 00 FB 01 F9 27 55 <crc>   -> regs 0,0,251,505,10069
```

Decoded with the enum already in `rk900.cpp` this is 0.00 m/s wind, 0° direction, 25.1 °C,
50.4 %RH, 1007.0 hPa — a physically plausible calm-indoor-bench quadruple. The Rika-page
layout would decode the same bytes as 50.4 °C / 1007 %RH, which is impossible.

**Decision, now non-provisional:** the RAKwireless / sibling / beegee-tokyo register map — the
one already coded — is correct for this unit. `RegisterIndex` and the `LOGF` scaling stay as
they are; the raw hex dump added in `6ea7488` can remain as belt-and-braces but is no longer
load-bearing. Option B (reprogramming this unit to 4800 for fleet consistency) remains the
only genuinely open item and is still not urgent — the node reads correctly at 9600 today.

## Citations

- CITE(datasheet): [CIT-RK900] RAKwireless RK900-09 datasheet — 4800 8N1, register map with
  wind speed at `0x0000`.
- CITE(datasheet): [CIT-RK900-PROTO] Rika RK900-series Modbus-RTU communication protocol —
  factory default 9600, settings-mode ASCII commands to change baud/address in place.
- CITE(sibling): [CIT-FWM-RK900-FIELD] `forest-weather-machines`
  `LoRaWAN/docs/RAK2560_weather_station_settings.md` at `ddfebfb3` — live deployed twin
  system, RK900-09 at 4800, RAK9154 at 9600, same register map as this repo.
- CITE(prior-art): [CIT-BEEGEE-RS485-WIND] `beegee-tokyo/RUI3-RS485-Wind-Sensor` — independent
  open-source RK900-09 integration; confirms 9600 factory default and the same register map.
- CITE(bench): [CIT-RK900-BAUD-2026-08-03] `docs/EVIDENCE.md` 2026-08-03 busscan — CRC-valid
  reply at 9600 only.
