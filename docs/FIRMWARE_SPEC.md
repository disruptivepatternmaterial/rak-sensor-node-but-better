# Firmware spec — WisBlock RK900 + RAK9154 node

🚧 **NOT YET DEPLOYED.** No shipping firmware in this repo yet. Behavior below is the contract to implement against.

Cross-links (sibling repos on this machine / org):

- Hardware: [`HARDWARE.md`](HARDWARE.md)
- Plan: [`../plans/P0_HARDENED_NODE.md`](../plans/P0_HARDENED_NODE.md)
- Libraries / examples: [`LIBRARIES.md`](LIBRARIES.md)
- Field RAK2560 settings + Modbus maps: `forest-weather-machines/LoRaWAN/docs/RAK2560_weather_station_settings.md`
- RAK9154 bench reverse-engineering: `forest-weather-machines/rak-4-5-wire/`

## 1. Product identity

| Item | Value |
|---|---|
| Role | LoRaWAN Class A end node, US915 |
| Replaces | RAK2560 Sensor Hub electronics/firmware path |
| Keeps | RAK9154, RK900-09, site SP11 cabling where possible |
| MCU | RAK4631 (nRF52840 + SX1262), Arduino BSP preferred for P0 |
| Base | RAK19007 |
| RS-485 | RAK5802 in IO slot |

## 2. Sensors

### 2.1 RK900-09 (weather)

| Param | Value | Source |
|---|---|---|
| Interface | Modbus-RTU over RAK5802 | Field + RAK Weather Station Solution Manual |
| Baud | **4800** 8N1 | `RAK2560_weather_station_settings.md` §1 / §5 |
| Slave | **0x01** | Same |
| FC | 0x03 Read Holding | Same |

| Reg | Name | Type | Scale | Unit |
|---|---|---|---|---|
| 0x0000 | Wind speed | U16 | ×0.01 | m/s |
| 0x0001 | Wind direction | U16 | ×1 | ° raw (site offset applied in **decoder**, not here, unless downlink sets offset) |
| 0x0002 | Temperature | S16 | ×0.1 | °C |
| 0x0003 | Humidity | U16 | ×0.1 | %RH |
| 0x0004 | Pressure | U16 | ×0.1 | hPa |

**Null policy:** missing Modbus → omit field / encode null sentinel per payload schema. Never invent `0`. Wind direction may be null when wind speed is 0 (match existing decoder policy).

**Total-silence policy (proof of life).** When *neither* sensor yields a single field, the node
still transmits a zero-length uplink — immediately on the first such cycle, then every 8th
consecutive silent cycle. This is deliberate and is not a data uplink: it distinguishes a
node whose sensors have failed from a node that is gone, a flat pack, or a dead gateway, all
of which otherwise present identically as silence. It is also the only way to stay
commandable, since Class A permits a downlink only in the window following an uplink. The
counter resets as soon as any field is read. Implemented in `src/main.cpp`
(`kQuietCyclesPerHeartbeat`).

**Timeouts:** per-transaction max wait ≤ 1000 ms; max retries 2; then mark sensor fail and continue cycle.

### 2.2 RAK9154 (battery) — preferred path

**Preferred:** 4-pin **Gateway Load** SP11/P4 Modbus (same map as field Hub / `rak-4-5-wire`).

| Param | Value |
|---|---|
| Baud | **9600** 8N1 |
| Slave | **0x6E** (110) |
| FC | 0x03 |
| Span | one read `0x6000` length 21; unpack V/I/SoC/T |

| Reg | Field | Scale |
|---|---|---|
| 0x6000 | Pack V | ×0.01 V |
| 0x6001 | Current | ×0.01 A (signed; **negative = charging** per field docs) |
| 0x6002 | SoC | ×1 % |
| 0x6009 | Batt T | ×1 °C |

**Power:** P+/P− from 4-pin or 5-pin → **12 V→5 V buck** → WisBlock 5 V. Never feed P+ to `BAT`.

**Alternate:** 5-pin Sensor Hub Load one-wire (TXD+RXD bridged, 9600 half-duplex, IPSO TLV). Only if 4-pin is occupied or unavailable. Refs: Meshtastic `RAK9154Sensor`, `beegee-tokyo/RAK-OneWireSerial`, `forest-weather-machines/rak-4-5-wire/firmware/nanoc6-onewire-poll`.

**Bus conflict note:** RK900 is 4800; BMS Modbus is 9600. Same RAK5802 transceiver **must** reconfigure baud between polls (or use one-wire for BMS and 5802 only for RK900). Spec default: **5802 for RK900; BMS via one-wire on Serial1 half-duplex** OR **5802 baud-switch** — pick one in implementation and document in HARDWARE. Recommendation for P0: **one RAK5802 + baud switch** if both Modbus; else **5802@4800 for RK900 + one-wire for BMS**.

## 3. LoRaWAN

| Param | Value |
|---|---|
| Region | US915 |
| Activation | OTAA |
| Class | **A** only |
| ADR | ON |
| Confirmed uplink | OFF default |
| Join | backoff; never tight-loop join forever |
| Credentials | not in git (`secrets.h` / `secrets.example.h`) |

TTN app: reuse `my-app-tobi` unless ingest requires a new app (decision in plan open items).

## 4. Cadence & downlink

| Item | Value |
|---|---|
| Default interval | **3600 s** |
| Allowed | **1800–86400 s** inclusive |
| Downlink port | **fPort 10**. A fixed port so traffic on any other port cannot be mistaken for a configuration change |
| Downlink format | `opcode` uint8, then arguments |
| `0x01` set interval | followed by `interval_s` uint32 BE — 5 bytes total |
| `0x03` request status | no arguments — 1 byte. Answered by shortening the wait so the next scheduled uplink arrives promptly, rather than transmitting a second time |
| Apply | next wake after RX; persist to flash |
| Invalid downlink | ignore; keep previous |

**The minimum was raised from 300 s to 1800 s** to stay inside the network's fair-use
allowance of roughly 30 seconds of transmit time per device per day [CIT-TTN-FUP]. One
11-byte uplink at the slowest US915 data rate takes about 370 ms, so 300 s intervals would
mean 288 uplinks and roughly 107 seconds of airtime — over three times the allowance. At
1800 s it is under 18 seconds even at the slowest rate. Adaptive data rate puts the nodes
with the weakest coverage on that rate, which is exactly this deployment, so the worst case
is the one to design against.

### Bench cadence (`FEATURE_BENCH_INTERVAL`) — never in the field image

Bring-up needs a reading about once a minute; a half-hour field interval makes a wiring
change take half an hour to confirm. `FEATURE_BENCH_INTERVAL` lowers both the floor and the
default to **60 s**. It is defined only by the `stage1` and `stage2` environments in
`platformio.ini` and is `0` everywhere else, including `[env:rak4631]`.

| Property | Field image | Bench build |
|---|---|---|
| Minimum interval | 1800 s | 60 s |
| Default interval | 3600 s | 60 s |
| Maximum interval | 86400 s | 86400 s |
| Stored interval honored | yes | **no** — forced to 60 s, and never written back |
| `FEATURE_RADIO` | 1 | **must be 0** |

Three properties make this safe rather than merely documented:

- **`src/config.h` fails the build** when `FEATURE_BENCH_INTERVAL` and `FEATURE_RADIO` are
  both set. A 60 s cadence is roughly 1440 uplinks a day; at the slowest US915 rate that is
  well over an hour of airtime against an allowance of about 30 s [CIT-TTN-FUP]. There is no
  legitimate build combining the two on the shared network, so the number that can be
  produced is zero rather than one guarded at run time. Private-network bench testing with
  the radio is tracked separately — the fair-use limits do not apply there [CIT-TTN-FUP].
- **It is compile-time only.** No downlink and no stored setting can reach a 60 s cadence,
  so a node already in the woods cannot be talked into one. `set_interval_seconds()` still
  refuses anything outside the bounds compiled into the running image.
- **The stored interval is ignored on a bench build** and the bench value is never
  persisted. Every field interval (1800–86400 s) falls inside the bench build's widened
  range, so loading flash would quietly restore a half-hour cadence and read as the setting
  having been ignored.

With sleep compiled out the between-cycle wait is capped so bring-up is not spent watching
a blank screen; that cap rises to the bench interval on a bench build, or it would override
the cadence it is supposed to serve.

The leading opcode replaces an earlier plan for a bare 4-byte interval. A bare integer has
no room to express any other request, so adding one later would have meant guessing at a
message's meaning from its length — and a node already in the woods cannot be taught the
new rule. An unrecognized opcode is ignored, so a command added later reaches an older node
harmlessly instead of being misread.

## 5. Wake cycle (single iteration)

1. Pet WDT  
2. Power/enable RS-485 as needed  
3. Poll RK900 (timeouts)  
4. Poll BMS (timeouts)  
5. Build uplink; TX Class A; handle RX1/RX2 for downlink  
6. Persist any new interval  
7. `Serial.end` / radio sleep / MCU sleep until next interval  

Failing one sensor does not skip the other or skip uplink (uplink may be battery-only or weather-only with validity flags).

## 6. Payload (draft — freeze before soak)

Prefer **RAK Standardized / Cayenne LPP–style** types already decoded by `forest-weather-machines` `rak-wx-station-default.js` so ingest stays unchanged:

| Channel / type | Content |
|---|---|
| Wind 158/190 | speed |
| Wind dir 159/191 | direction |
| Temp 103 | air |
| Humidity 104/112 | RH |
| Pressure 115 | hPa |
| Cap 184 | SoC |
| Current 185 | pack I |
| Voltage 186 | pack V |
| Temp 103 (batt channel) | pack T if present |

Exact channel IDs must match decoder expectations; verify against live decoder before field.

## 7. Hardening requirements (release-blocking)

| ID | Requirement |
|---|---|
| H1 | Hardware WDT; Modbus/BMS hang → reset |
| H2 | Deep sleep between cycles; Class A radio sleep |
| H3 | Brownout: no flash thrash; skip TX if V critically low |
| H4 | Join/TX fail: bounded backoff; survive multi-day no-GW |
| H5 | Interval + keys path survives power loss |
| H6 | RK900 absent → no livelock |
| H7 | BMS silent → no livelock |
| H8 | Bench soak ≥24 h; field shadow ≥7 d before hike-in trust |

## 8. Explicit non-goals

GNSS, RTC module, Meshtastic field image, RAK13002 (IO breakout; conflicts with RAK5802), fabricating sensor zeros, Class C.

## 9. Verification commands (when code exists)

- Compile: PlatformIO env `rak4631` (board id `rak4630`, from the vendored RAK definition
  in `rakwireless/` — the RAK4631 is not in the PlatformIO board registry)
- Bench: one successful RK900 frame + one BMS frame + one TTN uplink
- Downlink: change interval; confirm next cycle timing
- Fault: unplug RK900; unplug BMS data; assert continued sleep/wake
