# Citation registry

Canonical sources for this project. **URLs live here and nowhere else** — code and docs
cite the key (`[CIT-TTN-FUP]`) so a moved link is fixed in one place.

Governed by [`.cursor/rules/20-citation-discipline.mdc`](../.cursor/rules/20-citation-discipline.mdc).
Validated by [`scripts/check_citations.py`](../scripts/check_citations.py), which fails the
build on a reference to an undefined key **and on any entry that is not verified**.

## Verification policy

**Every entry in this registry is verified. An unverified source does not get committed —
it gets verified or left out.** A registry that ships warnings trains people to ignore
warnings, which defeats the point of having a checker.

Verified means the URL was fetched and returned the expected document on the date shown.
Two access quirks are recorded inline rather than treated as a lesser status:

- Some RAKwireless and Nordic pages are JavaScript-rendered, so an automated fetch returns
  only the page title. The URL is confirmed live; open it in a browser for the tables.
- `modbus.org` PDFs reject `HEAD` requests and require a referer. They are confirmed as
  `application/pdf` via a ranged `GET`.

When adding a source: fetch it, confirm it states the fact in the "Establishes" column, and
record the date. Do not add a row from memory.

## Network policy and LoRaWAN specification

| Key | Title | Publisher | URL | Establishes | Status |
|---|---|---|---|---|---|
| `CIT-TTN-FUP` | Duty Cycle — Fair Use Policy | The Things Network | https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/ | Sandbox limits are **30 s uplink airtime per node per 24 h** and **10 downlink messages per node per 24 h**. Sets the airtime budget in rule 40. | VERIFIED 2026-07-30 |
| `CIT-TTN-FUP-EXPLAINED` | Fair Use Policy explained | TTN community forum | https://www.thethingsnetwork.org/forum/t/fair-use-policy-explained/1300 | Derives the 30 s figure as (8 channels × 86400 s × 5% gateway RX duty) ÷ 1000 nodes. A 10-byte payload allows ~20 msg/day at SF12 vs ~500 at SF7. The downlink cap **includes ACKs for confirmed uplinks**. | VERIFIED 2026-07-30 |
| `CIT-TTN-FREQ` | Frequency Plans | The Things Network | https://www.thethingsnetwork.org/docs/lorawan/frequency-plans/ | TTN US902-928: uplink 903.9–905.3 MHz at **SF7BW125 to SF10BW125** (plus 904.6 SF8BW500); RX1 downlink 923.3–927.5 SF7BW500–SF12BW500; **RX2 = 923.3 MHz SF12BW500**. The uplink floor is SF10, not SF12. | VERIFIED 2026-07-30 |
| `CIT-LW-LINK` | LoRaWAN L2 1.0.4 Specification (TS001-1.0.4) | LoRa Alliance | https://lora-alliance.org/wp-content/uploads/2021/11/LoRaWAN-Link-Layer-Specification-v1.0.4.pdf | §3.3 Receive Windows and §3 Class A: a Class A end-device's uplink is followed by **two short downlink receive windows** and there is no other downlink opportunity. §5.2 LinkADRReq/Ans defines the ADR mechanism. Free public PDF — no registration. | VERIFIED 2026-07-30 |
| `CIT-LORA-RP002` | RP002-1.0.3 Regional Parameters | LoRa Alliance | https://lora-alliance.org/wp-content/uploads/2021/05/RP002-1.0.3-FINAL-1.pdf | US915 channel plan, data rates, per-DR maximum payload sizes, and RX2 defaults. Free public PDF. RP002-1.0.4 is available via the resource hub: https://resources.lora-alliance.org/technical-specifications/rp002-1-0-4-regional-parameters | VERIFIED 2026-07-30 |
| `CIT-FCC-15247` | 47 CFR § 15.247 | Cornell Law LII | https://www.law.cornell.edu/cfr/text/47/15.247 | Frequency-hopping and digital-modulation rules for 902–928 MHz, including per-channel dwell limits. LII mirror is used because eCFR blocks automated access. | VERIFIED 2026-07-30 |

## Hardware — manufacturer documentation

| Key | Title | Publisher | URL | Establishes | Status |
|---|---|---|---|---|---|
| `CIT-RAK4631` | RAK4631 WisBlock LoRaWAN Module Datasheet | RAKwireless | https://docs.rakwireless.com/product-categories/wisblock/rak4631/datasheet/ | MCU/radio (nRF52840 + SX1262), pinout, electrical characteristics. JS-rendered. | VERIFIED 2026-07-30 |
| `CIT-RAK19007` | RAK19007 WisBlock Base Board Datasheet | RAKwireless | https://docs.rakwireless.com/product-categories/wisblock/rak19007/datasheet/ | Base board slots, power input, IO slot assignment, battery connector. JS-rendered. | VERIFIED 2026-07-30 |
| `CIT-RAK5802` | RAK5802 WisBlock RS485 Interface Module Datasheet | RAKwireless | https://docs.rakwireless.com/product-categories/wisblock/rak5802/datasheet/ | RS-485 transceiver, IO-slot conflict with RAK13002, enable/direction control. JS-rendered. | VERIFIED 2026-07-30 |
| `CIT-RAK9154` | RAK9154 Solar Battery Datasheet | RAKwireless | https://docs.rakwireless.com/product-categories/accessories/rak9154/datasheet/ | Pack electrical spec (5.2 Ah / 56.16 Wh, 12 V output) and load-socket definitions. Note the path is `accessories/`, not `wisnode/`. Overview: https://docs.rakwireless.com/product-categories/accessories/rak9154/overview/ | VERIFIED 2026-07-30 |
| `CIT-RK900` | RAK Weather Station Solution Datasheet (RK900-09) | RAKwireless | https://docs.rakwireless.com/product-categories/wisnode/weather-station/datasheet/ | RK900-09 Modbus register map, 4800 8N1, slave 0x01. This is the URL cited by the live TTN decoder header. JS-rendered. | VERIFIED 2026-07-30 |
| `CIT-NRF-POWER` | nRF52840 Product Specification — Power management | Nordic Semiconductor | https://docs.nordicsemi.com/bundle/ps_nrf52840/page/power.html | System ON vs System OFF sleep modes, RAM retention, and the fact that **entering sleep does not automatically disable peripherals**. JS-rendered. | VERIFIED 2026-07-30 |
| `CIT-NRF-PERIPH-SLEEP` | Can't reach low power mode on nRF52840 | TinyGo issue #4886 | https://github.com/tinygo-org/tinygo/issues/4886 | Measured evidence for the peripheral-shutdown requirement: **1.2 mA / 890 µA reduced to 10 µA** by disabling UART and SPI and disabling USBD. Corroborating Nordic DevZone thread: https://devzone.nordicsemi.com/f/nordic-q-a/45355/how-optimize-high-current-consumption-in-sleep-mode-using-nrf52840 | VERIFIED 2026-07-30 |
| `CIT-SX1262` | SX1262 Long Range Low Power LoRa Transceiver | Semtech | https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 | TX power settings and current consumption for the power budget. | VERIFIED 2026-07-30 |

## Protocols

| Key | Title | Publisher | URL | Establishes | Status |
|---|---|---|---|---|---|
| `CIT-MODBUS-APP` | MODBUS Application Protocol Specification V1.1b3 | Modbus Organization | https://www.modbus.org/file/secure/modbusprotocolspecification.pdf | Function code **0x03 Read Holding Registers** request/response framing and exception codes. Rejects `HEAD`; confirmed `application/pdf` by ranged `GET`. Index page: https://www.modbus.org/modbus-specifications | VERIFIED 2026-07-30 |
| `CIT-MODBUS-SERIAL` | MODBUS over Serial Line Specification and Implementation Guide | Modbus Organization | https://www.modbus.org/file/secure/modbusoverserial.pdf | RTU framing and the **3.5-character inter-frame silent interval** that bounds poll timing and bus turnaround. Rejects `HEAD`; confirmed `application/pdf` by ranged `GET`. | VERIFIED 2026-07-30 |
| `CIT-CAYENNE-LPP` | Cayenne Low Power Payload | myDevices | https://docs.mydevices.com/docs/lorawan/cayenne-lpp | Channel/type TLV encoding that the payload schema follows. | VERIFIED 2026-07-30 |

## Prior art and implementations

Prior art shows something works in practice. It never establishes correctness on its own —
pair it with a `datasheet` or `spec` citation (rule 20).

| Key | Title | Publisher | URL | Establishes | Status |
|---|---|---|---|---|---|
| `CIT-WISBLOCK-API2` | WisBlock-API-V2 | beegee-tokyo | https://github.com/beegee-tokyo/WisBlock-API-V2 | Event-driven Class A + power-save framework for RAK4631 handling LoRaWAN, BLE, and AT commands; the P0 stack recommendation in `LIBRARIES.md`. | VERIFIED 2026-07-30 |
| `CIT-SX126X-ARDUINO` | SX126x-Arduino | beegee-tokyo | https://github.com/beegee-tokyo/SX126x-Arduino | LoRaWAN MAC implementation for Semtech SX126x underlying the RAK4631 Arduino path. 282 stars. | VERIFIED 2026-07-30 |
| `CIT-ONEWIRE-SERIAL` | RAK-OneWireSerial | beegee-tokyo | https://github.com/beegee-tokyo/RAK-OneWireSerial | One-wire half-duplex serial protocol **as used by the RAK2560 SensorHub** — the alternate RAK9154 path. | VERIFIED 2026-07-30 |
| `CIT-MESHTASTIC-9154` | meshtastic/firmware | Meshtastic | https://github.com/meshtastic/firmware | `RAK9154Sensor` — a working RAK9154 reader; also nRF52 brownout lessons. | VERIFIED 2026-07-30 |
| `CIT-MODBUSMASTER` | ModbusMaster | 4-20ma | https://github.com/4-20ma/ModbusMaster | Arduino Modbus master with timeout handling. 646 stars. | VERIFIED 2026-07-30 |
| `CIT-RAK-SLEEP` | RAK4631 — deep sleep until reset | RAKwireless forum | https://forum.rakwireless.com/t/rak4631-deep-sleep-until-reset/7253 | Bernd Giesecke (WisBlock-API author): **`sd_power_mode_set()` sleeps the nRF52 but not the SX1262 — `Radio.sleep()` is also required**, and critically, **"if you initialize LoRaWAN but cannot connect to a LoRaWAN server, the MCU will never sleep, because it is retrying to join."** A reporter measured 6 mA in the failing configuration. Measurement method: Nordic PPK2 in source-meter mode on the base board battery connector. | VERIFIED 2026-07-30 |

## Sibling repositories (local)

Local paths move and these repos advance independently, so **cite a commit SHA**.

| Key | Repo | Path | Establishes | Pinned |
|---|---|---|---|---|
| `CIT-FWM-DECODER` | forest-weather-machines | `LoRaWAN/payload/rak-wx-station-default.js` | The live TTN uplink formatter — the other half of the payload contract. Channel/type map, `WIND_DIR_OFFSET = 230`, zero-wind null policy. | `efc0e3c` (2026-07-30) |
| `CIT-FWM-RAK2560` | forest-weather-machines | `LoRaWAN/docs/RAK2560_weather_station_settings.md` | Field-proven RK900 and BMS Modbus settings from the deployed Sensor Hub. | `efc0e3c` |
| `CIT-RAK45WIRE` | forest-weather-machines | `rak-4-5-wire/` | RAK9154 connector reference and bench reverse-engineering, including the one-wire POC. | `efc0e3c` |

Clone location on both machines: `~/Documents/GitHub/forest-weather-machines`.
Override for tooling with `FWM_REPO=/path/to/repo`.

## Corrections log

URLs that were wrong and are recorded so nobody re-derives them:

| Wrong URL | Problem | Correct |
|---|---|---|
| `modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf` | 404 — widely cited elsewhere, including by Modbus's own security spec, but dead | `www.modbus.org/file/secure/modbusprotocolspecification.pdf` |
| `modbus.org/docs/Modbus_over_serial_line_V1_02.pdf` | 404 | `www.modbus.org/file/secure/modbusoverserial.pdf` |
| `docs.rakwireless.com/product-categories/wisnode/rak9154/overview/` | 404 — RAK9154 is not under `wisnode/` | `.../accessories/rak9154/datasheet/` |
| `lora-alliance.org/resource_hub/rp2-regional-parameters/` | 404 — wrong host and slug | `resources.lora-alliance.org/...` or the direct PDF above |
| `lora-alliance.org/resource_hub/lorawan-specification-v1-0-4/` | 404 | direct PDF above |
| eCFR deep link for 15.247 | Blocks automated access with a CAPTCHA | Cornell LII mirror |
