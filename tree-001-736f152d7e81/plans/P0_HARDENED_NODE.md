# P0 — Hardened WisBlock weather node (woods)

**Status:** planning + specs only. Parts on order. No firmware in-tree.  
**Specs:** [`docs/FIRMWARE_SPEC.md`](../docs/FIRMWARE_SPEC.md) · [`docs/HARDWARE.md`](../docs/HARDWARE.md) · [`docs/LIBRARIES.md`](../docs/LIBRARIES.md)

**Success bar:** hike-in, months unattended — no brick, no Modbus hang, no airtime death spiral.

## Goal

Replace RAK2560 Sensor Hub path with WisBlock firmware that polls RK900 + RAK9154, Class A US915, downlink interval, woods-hardened.

## Architecture

```
RAK9154 -- power (P+/P-) --> buck 12V→5V --> RAK19007
       \-- BMS (Modbus 4-pin @9600 OR one-wire 5-pin) --> MCU
RK900   -- RS485 @4800 --> RAK5802 --> MCU
MCU = RAK4631 --> LoRaWAN US915 --> TTN --> forest-weather-machines ingest
```

## Work packages

| WP | Deliverable | Gate |
|---|---|---|
| WP1 | PlatformIO skeleton `wiscore_rak4631`, secrets example, CI compile | L2 |
| WP2 | RK900 Modbus on RAK5802 + timeouts | L3 |
| WP3 | RAK9154 (Modbus or one-wire) + timeouts | L3 |
| WP4 | WisBlock-API-V2 (or chosen stack) OTAA + downlink interval | L4 24h |
| WP5 | H1–H8 hardening in FIRMWARE_SPEC | L5 |
| WP6 | TTN device + decoder parity with `rak-wx-station-default.js` | L4 live ingest |

## Open decisions (resolve in first firmware PR)

1. BMS path: 4-pin Modbus+baud-switch vs 5-pin one-wire.  
2. Hub shell vs Unify 910406.  
3. Keep Probe IO junction vs direct RK900.  
4. Arduino WisBlock-API-V2 vs RUI3.  
5. Same TTN app `my-app-tobi` vs new app.

## Cloud-agent notes

- Do not invent pinouts; cite HARDWARE + `rak-4-5-wire` + RAK datasheets.  
- Do not fabricate sensor values.  
- No secrets in git.  
- Prefer libs listed in LIBRARIES.md.  
- Mark anything unverified `🚧 NOT YET DEPLOYED`.
