# Libraries & example projects (research)

Use these as **implementation references**, not as “copy Meshtastic into the woods.” Prefer small, auditable deps. Verified via GitHub API / known RAK docs / sibling `forest-weather-machines` work (2026-07-22).

## Stack recommendation (P0)

| Layer | Choice | Why |
|---|---|---|
| LoRaWAN + sleep framework | **beegee-tokyo/WisBlock-API-V2** (+ SX126x-Arduino underneath) | Built for RAK4631 event-driven Class A + low power |
| Alt LoRaWAN | RUI3 Arduino examples (RAK4631-R) | If we switch to RUI3 core SKU later |
| Modbus master | **4-20ma/ModbusMaster** or **emelianov/modbus-esp8266** (API patterns) | Timeouts; battle-tested |
| RAK9154 one-wire (alt) | **beegee-tokyo/RAK-OneWireSerial** + Meshtastic `RAK9154Sensor` | Protocol already reverse-engineered |
| RAK9154 Modbus | Own thin client using map in FIRMWARE_SPEC | Already field-proven on Hub |
| WDT | **adafruit/Adafruit_SleepyDog** (nRF52 path) or Nordic WDT direct | Hard reset on hang |
| Payload | CayenneLPP / RAK standardized types | Matches existing TTN decoder |

## 20+ repos / sources

### WisBlock / RAK4631 (primary)

1. [RAKWireless/WisBlock](https://github.com/RAKWireless/WisBlock) — official examples, RAK5802 samples, pin maps  
2. [beegee-tokyo/SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) — SX1262 LoRaWAN/LoRa for Arduino (RAK4631 default stack)  
3. [beegee-tokyo/WisBlock-API-V2](https://github.com/beegee-tokyo/WisBlock-API-V2) — **event-driven API, AT, BLE, LoRaWAN, power-save**  
4. [beegee-tokyo/WisBlock-API](https://github.com/beegee-tokyo/WisBlock-API) — V1 predecessor; patterns still useful  
5. [beegee-tokyo/WisBlock-Sensor-For-LoRaWAN](https://github.com/beegee-tokyo/WisBlock-Sensor-For-LoRaWAN) — full sensor→LoRaWAN firmware example on RAK4631  
6. [beegee-tokyo/WisBlock-Seismic-Sensor](https://github.com/beegee-tokyo/WisBlock-Seismic-Sensor) — woods-adjacent hardened sensor node example (Arduino + RUI3 variants)  
7. [Kongduino/RUI3_RAK4631_LoRaWan_RAK1901_RAK12010](https://github.com/Kongduino/RUI3_RAK4631_LoRaWan_RAK1901_RAK12010) — RUI3 Class A sensor uplink pattern  

### Battery / SensorHub protocol

8. [beegee-tokyo/RAK-OneWireSerial](https://github.com/beegee-tokyo/RAK-OneWireSerial) — SensorHub IPSO / half-duplex UART  
9. [meshtastic/firmware](https://github.com/meshtastic/firmware) — `RAK9154Sensor.cpp` (production-ish 9154 reader); also nRF52 solar brownout lessons ([issue #9699](https://github.com/meshtastic/firmware/issues/9699))  
10. [meshtastic/firmware PR #4117](https://github.com/meshtastic/firmware/pull/4117) — RAK9154 bring-up history  
11. Sibling: `forest-weather-machines/rak-4-5-wire/` — **local** Modbus map + clean-room one-wire POC (`nanoc6-onewire-poll`)  

### Modbus / RS-485

12. [4-20ma/ModbusMaster](https://github.com/4-20ma/ModbusMaster) — classic Arduino Modbus master  
13. [arduino-libraries/ArduinoModbus](https://github.com/arduino-libraries/ArduinoModbus) — official; heavier  
14. [emelianov/modbus-esp8266](https://github.com/emelianov/modbus-esp8266) — richest timeout/API patterns (port ideas to nRF52)  

### LoRaWAN MAC / TTN ecosystem (reference, not necessarily link)

15. [Lora-net/LoRaMac-node](https://github.com/Lora-net/LoRaMac-node) — Semtech reference MAC (duty cycle, Class A state machine)  
16. [mcci-catena/arduino-lmic](https://github.com/mcci-catena/arduino-lmic) — LMIC; US915 lessons; not first choice on SX1262 WisBlock  
17. [mcci-catena/arduino-lorawan](https://github.com/mcci-catena/arduino-lorawan) — friendlier LMIC wrapper  
18. [TheThingsNetwork/lorawan-devices](https://github.com/TheThingsNetwork/lorawan-devices) — device repo / codec patterns  
19. [ElectronicCats/CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) — LPP encoder  

### Hardening / low power / field test

20. [adafruit/Adafruit_SleepyDog](https://github.com/adafruit/Adafruit_SleepyDog) — watchdog  
21. [rocketscream/Low-Power](https://github.com/rocketscream/Low-Power) — sleep patterns (AVR-oriented; ideas only on nRF52)  
22. [disk91/WioLoRaWANFieldTester](https://github.com/disk91/WioLoRaWANFieldTester) — field RF / margin testing mindset  
23. RAK forum: [RAK4631 deep sleep / Serial1.end()](https://forum.rakwireless.com/t/rak4631-deep-sleep-until-reset/7253) — ~~**must `Serial.end()` before sleep** or current stays ~mA~~ **CORRECTED 2026-08-12: do not apply this to USB `Serial`.** `Adafruit_USBD_CDC::end()` only calls `clearConfiguration()` — it does not stop the `usbd` task, disable `NRF_USBD`, or release HFCLK, so it removes no current and does discard the descriptor the host enumerated. The USB peripheral is gated by **VBUS**, not by `begin()`/`end()` (`NRF_USBD->ENABLE` is set only from the VBUS event handler, `dcd_nrf5x.c:927`), and `TinyUSBDevice.detach()` in `src/power.cpp:135` is the correct lever. This line is still valid for the **UART** `Serial1`, which is a different peripheral. See [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md) and the `CIT-RAK-LOWPOWER` content correction in [CITATIONS.md](CITATIONS.md#corrections-log) — this entry, read as applying to USB, is what justified a field build that was reverted the same day.  

### Platform

24. [adafruit/Adafruit_nRF52_Arduino](https://github.com/adafruit/Adafruit_nRF52_Arduino) — core often underlying WisBlock Arduino  
25. PlatformIO `nordicnrf52` + board `wiscore_rak4631` — build system of record for this repo  

## Do not use as field firmware

- Full **Meshtastic** image (mesh ≠ our TTN weather contract)  
- **RAK13002** breakout as a substitute for RAK5802  
- Unconfirmed Class C examples  

## Ingest alignment

Existing decoder: `forest-weather-machines` → `payload/rak-wx-station-default.js` (and standardized payload helpers). New device should emit types that decoder already understands unless a paired decoder PR ships.
