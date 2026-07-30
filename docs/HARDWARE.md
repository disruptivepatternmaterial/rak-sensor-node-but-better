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
| Enclosure | Unify 150×100×45 M8+RP-SMA **no solar** | **910406** |
| Buck | 12 V → 5 V | (separate) |

**Not used:** RAK13002 (conflicts with 5802 IO slot), GNSS, RTC, Solar Unify **910421**, AS923 kit **119012**.

## RAK9154 ports (critical)

The pack has **two** load sockets. Connector reference:
`forest-weather-machines/rak-4-5-wire/docs/01-connector-reference.md` [CIT-RAK45WIRE] —
the sibling repo is cloned at `~/Documents/GitHub/forest-weather-machines`, not beside this
repo, so it is cited rather than linked. See [`CITATIONS.md`](CITATIONS.md).

### A — 4-pin Gateway Load (SP11/P4) — BMS Modbus **preferred**

| Pin | Signal |
|---|---|
| 1 | P+ (~12 V) → buck only |
| 2 | P− → GND |
| 3 | RS-485 A |
| 4 | RS-485 B |

Modbus slave **0x6E**, **9600** 8N1. Register map: FIRMWARE_SPEC §2.2 / RAK2560 settings §5d.

### B — 5-pin Sensor Hub Load (SP11/P5) — power + optional one-wire

| Pin | Signal | Notes |
|---|---|---|
| 1 | P+ (~12 V) | Buck VIN+ only |
| 2 | P− | GND |
| 3 | TXD | Half-duplex data (bridge to pin 5 for one-wire) |
| 4 | 3V3_In | Level ref / probe rail — tie carefully to 3V3, **never 5 V** |
| 5 | RXD | Bridge to TXD for one-wire |

**Not** full-duplex UART to RX1/TX1 as two independent lines without bridging — Hub protocol is half-duplex one-wire @ 9600. See Meshtastic / RAK-OneWireSerial / `rak-4-5-wire`.

## RK900

4-wire: V+, GND, A, B → RAK5802 @ **4800**. Probe IO optional as junction box only.

## P0 wiring recommendation

1. Buck from P+ (either socket) → WisBlock 5 V.  
2. RAK5802 → RK900 @ 4800.  
3. Battery: either (a) one-wire on 5-pin to a single GPIO/UART half-duplex, or (b) Modbus on 4-pin with **baud switch** on same 5802 between 4800/9600. Document the chosen option in the PR that implements it.

## Enclosure

Unify **910406** (no solar) or reuse Sensor Hub shell (2× SP11 already fitted).
