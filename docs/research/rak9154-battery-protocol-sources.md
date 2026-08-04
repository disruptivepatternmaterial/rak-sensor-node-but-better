# RAK9154 battery protocol — source evidence

Research note backing the diagnosis of the zero-byte battery reads on `stage3`
(2026-08-04). Every URL below was fetched and confirmed live; local files are pinned
to a commit SHA. Sibling repo `forest-weather-machines` HEAD `efc0e3c`; this repo HEAD
`aa5365c`.

## The conjectures

- **A.** RAK9154 is read over Modbus RS-485, slave `0x6E`, 9600 8N1, holding registers
  from `0x6000` (~21 registers). This is the hardware-proven path.
- **B.** There is no official RAKwireless-documented *one-wire* battery-telemetry protocol
  for the RAK9154 — the IPSO-TLV bit-banged scheme is reverse-engineered, not a vendor
  interface.
- **C.** The one-wire codec in `src/sensors/battery.cpp` is a clean-room reimplementation.
  **Correction (see note):** the underlying protocol IS validated on silicon upstream; it
  is *our port on the RAK4631 5-pin socket* that has never read a real pack.
- **D.** Supporting hardware facts (pinouts, WB_IO1 = P0.17, connectors).
- **E.** Modbus normative specs.

## Sources (20 verified)

| # | Source | Where | Category | Status | Conj. | Establishes |
|---|---|---|---|---|---|---|
| 1 | RAK9154 Solar Battery Lite — product page | https://store.rakwireless.com/products/rak-battery-lite-solar-power-solution-rak9154 | datasheet | VERIFIED | A,D | Smart battery: BMS + heater, solar, 5.2 Ah / 10.8 V, one 12 VDC Sensor Hub output; status over LoRaWAN. |
| 2 | RAK9154 datasheet (docs) | https://docs.rakwireless.com/product-categories/accessories/rak9154/datasheet/ | datasheet | VERIFIED (JS-rendered) | D | Canonical datasheet; path is `accessories/`. |
| 3 | Meshtastic firmware PR #4117 (WisMesh Hub RAK2560/RAK9154) | https://github.com/meshtastic/firmware/pull/4117 | prior-art | VERIFIED | C | **Merged 2024-06-16**, author beegee-tokyo; RAK9154 protocol on nRF52840, shipping product with bench telemetry — protocol validated on silicon. |
| 4 | beegee-tokyo/RAK-OneWireSerial — repo | https://github.com/beegee-tokyo/RAK-OneWireSerial | prior-art | VERIFIED | B,C | One-wire half-duplex lib "used by RAK2560 SensorHub." 0 stars / 0 forks — near-zero external validation. |
| 5 | RAK-OneWireSerial — source tree | https://raw.githubusercontent.com/beegee-tokyo/RAK-OneWireSerial/main/src/SoftwareHalfSerial.cpp | prior-art | VERIFIED | C | The bit-banged half-duplex UART + IPSO codec our `battery.cpp` reimplements. |
| 6 | RAK5802 RS485 module datasheet | https://docs.rakwireless.com/product-categories/wisblock/rak5802/datasheet/ | datasheet | VERIFIED (JS-rendered) | D | RS-485 transceiver module carrying the Modbus path. |
| 7 | RAK19007 base board datasheet | https://docs.rakwireless.com/product-categories/wisblock/rak19007/datasheet/ | datasheet | VERIFIED (JS-rendered) | D | Base board IO-slot / connector reference. |
| 8 | RAK4631 module datasheet | https://docs.rakwireless.com/product-categories/wisblock/rak4631/datasheet/ | datasheet | VERIFIED (JS-rendered) | D | nRF52840 + SX1262 host module. |
| 9 | MODBUS Application Protocol V1.1b3 | https://www.modbus.org/file/secure/modbusprotocolspecification.pdf | spec | VERIFIED (PDF) | E | Normative FC 0x03 Read Holding Registers framing. |
| 10 | MODBUS over Serial Line v1.02 | https://www.modbus.org/file/secure/modbusoverserial.pdf | spec | VERIFIED (PDF) | E | RTU framing, 3.5-char inter-frame gap. |
| 11 | RAK Weather Station Solution User Manual | https://downloads.rakwireless.com/LoRa/SensorHub/Sensor%20Hub%20Solutions/Weather%20Station%20Solution%20User%20Manual.pdf | datasheet | VERIFIED (PDF) | A,C | SensorHub Generic-Probe-IO flow + IPSO type codes (Temp 103, etc.). |
| 12 | RAK2560 WisNode Sensor Hub — overview | https://docs.rakwireless.com/product-categories/wisnode/rak2560/overview/ | datasheet | VERIFIED (JS-rendered) | A,B | The Sensor Hub that natively reads the RAK9154 over Modbus RS-485. (`/datasheet/` 404 — use `/overview/`.) |
| 13 | sibling `rak-4-5-wire/docs/01-connector-reference.md` | `~/Documents/GitHub/forest-weather-machines/rak-4-5-wire/docs/01-connector-reference.md` @ `ddfebfb3` | sibling | LOCAL | A,D | SP11/P4 + P5 pinouts; RAK9154 BMS = Modbus slave `0x6E`, 9600 8N1; 5-pin pins 3/5 = one-wire 9600 8N1, pin 4 = 3V3_In. |
| 14 | sibling `rak-4-5-wire/docs/05-onewire-alternative.md` | same repo @ `ddfebfb3` | sibling | LOCAL | B,C | One-wire is closed-source RAK IPSO-TLV; our codec clean-room, "works but only POC-validated"; RS-485 primary. Types 184/185/186/103. |
| 15 | sibling `rak-4-5-wire/README.md` | same repo @ `ddfebfb3` | sibling | LOCAL | A,C | Whole thing a "bench POC"; RS-485/Modbus FC 0x03 read-only is the validated recommendation. |
| 16 | sibling `nanoc6-onewire-poll/src/onewire_protocol.cpp` | same repo @ `ddfebfb3` | sibling | LOCAL | C | The clean-room one-wire codec (ESP32-C6, POC) — direct ancestor of our `battery.cpp`. |
| 17 | sibling `LoRaWAN/docs/RAK2560_weather_station_settings.md` | same repo @ `efc0e3c` | sibling | LOCAL | A | Field-deployed twin: RAK9154 BMS Modbus 9600, slave `0x6E`; §5d register map is the byte-accurate production reference. |
| 18 | `rakwireless/variants/rak4630/variant.h:45` | this repo @ `abc716c` | datasheet | LOCAL | D | `WB_IO1 = 17` → nRF52840 P0.17. |
| 19 | `src/sensors/battery.cpp:186` | this repo @ `aa5365c` | prior-art | LOCAL | C | Codec's own words: reply checksum "not yet confirmed against hardware… prior-art never checked the reply." |
| 20 | `docs/CITATIONS.md` registry | this repo @ `aa5365c` | registry | LOCAL | A–E | Registers `CIT-ONEWIRE-SERIAL` and `CIT-MESHTASTIC-9154`; both root URLs resolve. |

## Strength assessment

- **A (Modbus 0x6E / 9600 8N1)** — STRONG (two sibling docs + a field-deployed twin). Gap:
  the specific `0x6000` start + ~21 registers is asserted in `AGENTS.md`; nail it by citing
  the line-level map in `rak-4-5-wire/docs/02-rs485-modbus-map.md` / settings §5d.
- **B (no official one-wire spec)** — STRONG. The 5-pin interface is real, but the wire
  protocol is closed-source; only reverse-engineered/POC implementations exist publicly.
- **C (our codec unproven)** — STRONG with a nuance: the protocol itself is proven upstream
  (see correction); our RAK4631 5-pin-socket reimplementation is what has never read a pack.
- **D (hardware facts)** — STRONG.
- **E (Modbus specs)** — STRONG (both normative PDFs live).

## Correction to the earlier claim (important)

Earlier I said the one-wire path "never worked on any hardware." That over-stated it.
**Meshtastic PR #4117 was merged and ships on real silicon** (RAK2560 WisMesh Hub reads the
RAK9154 over this exact SensorHub one-wire protocol). So the protocol and beegee-tokyo's
implementation ARE hardware-validated. What is genuinely unproven is **this firmware's
clean-room reimplementation on the discrete RAK4631 + 5-pin socket** — it has never returned
a frame from a pack, and `battery.cpp` still flags the reply checksum as an untested
assumption.

## What this points to

The Modbus RS-485 path (slave `0x6E`) is the only path with a byte-accurate, field-deployed
reference in-hand. Reading the pack there — reusing the existing `busscan` RS-485 sweep,
which already probes `0x6E` — is the decisive, low-cost test of whether the pack is alive,
and is ADR-0004's own documented fallback.

## Registry follow-up

- No active `docs/CITATIONS.md` entry is dead. RAK datasheet pages are JS-rendered (live
  200, title-only to automated fetch — known quirk).
- Suggested add: register the specific Meshtastic **PR #4117** URL (higher-value than the
  repo root currently under `CIT-MESHTASTIC-9154`).
