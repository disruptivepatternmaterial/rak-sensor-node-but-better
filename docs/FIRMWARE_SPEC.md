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
| Baud | **9600** 8N1 — this unit | [ADR-0006](decisions/ADR-0006-rk900-baud-and-register-map.md), bench-measured 2026-08-03 |
| Slave | **0x01** | Same |
| FC | 0x03 Read Holding | Same |

**On the baud.** The datasheet and the sibling fleet run 4800 — [CITE(datasheet): RK900-09 register map / RAK Weather Station Solution Manual](CITATIONS.md) — but the unit on this node answers only at 9600, which is where `src/sensors/rk900.cpp` is set. 4800 may be reinstated later for fleet consistency; until then the spec records what the hardware does, not what the sheet says. [CITE(bench): full 5-register read at 9600, `998dc26`](EVIDENCE.md)

| Reg | Name | Type | Scale | Unit |
|---|---|---|---|---|
| 0x0000 | Wind speed | U16 | ×0.01 | m/s |
| 0x0001 | Wind direction | U16 | ×1 | ° raw (site offset applied in **decoder**, not here, unless downlink sets offset) |
| 0x0002 | Temperature | S16 | ×0.1 | °C |
| 0x0003 | Humidity | U16 | ×0.1 | %RH |
| 0x0004 | Pressure | U16 | ×0.1 | hPa |

**Null policy:** missing Modbus → omit field / encode null sentinel per payload schema. Never invent `0`. Wind direction may be null when wind speed is 0 (match existing decoder policy).

A CRC-valid reply is not by itself a measurement. A span in which **all five registers read
`0`** is refused whole (`ModbusResult::Unsampled`) and contributes no fields, and a **`0`
pressure register** is omitted on its own even when the rest of the span is plausible — `0.0`
hPa is a vacuum, not weather. Genuine zeros survive: `0` m/s is calm, `0`° is due north, and
`0.0` °C is an ordinary winter temperature, all of which pass as long as one other register is
non-zero. This mirrors the battery path, which has refused the pack's all-zero record template
since `b6bbf31` (`src/sensors/battery_frame.cpp`, `BatteryResult::Unsampled`).

**Total-silence policy (proof of life).** When *neither* sensor yields a single field, the node
still transmits a zero-length uplink — immediately on the first such cycle, then every 8th
consecutive silent cycle. This is deliberate and is not a data uplink: it distinguishes a
node whose sensors have failed from a node that is gone, a flat pack, or a dead gateway, all
of which otherwise present identically as silence. It is also the only way to stay
commandable, since Class A permits a downlink only in the window following an uplink. The
counter resets as soon as any field is read. Implemented in `src/main.cpp`
(`kQuietCyclesPerHeartbeat`).

**Timeouts:** per-transaction max wait ≤ 1000 ms; max retries 2; then mark sensor fail and continue cycle.

### 2.2 RAK9154 (battery) — implemented path

**Implemented: 5-pin Sensor Hub Load one-wire** (TXD+RXD bridged, 9600 half-duplex, IPSO TLV),
chosen in [ADR-0004](decisions/ADR-0004-bms-one-wire-path.md) and working on hardware since
2026-08-05. This is the socket to go to when battery reads fail.
[CITE(prior-art): Meshtastic `RAK9154Sensor`, `beegee-tokyo/RAK-OneWireSerial`, `forest-weather-machines/rak-4-5-wire/firmware/nanoc6-onewire-poll`](CITATIONS.md)
[CITE(bench): pack latches pid `0x01` and reports 12.23 V / 98 % / 23.0 °C, `1a203d3`](EVIDENCE.md)

**Not used — held in reserve:** 4-pin **Gateway Load** SP11/P4 Modbus (same map as field Hub /
`rak-4-5-wire`). Raw Modbus at slave `0x6E` over the one-wire line was proven dead (0 bytes every
cycle; the adapter does not bridge it) and that path was removed in `b6bbf31`. The register map
below is retained only so a future 4-pin harness does not have to rediscover it.

| Param | Value |
|---|---|
| Baud | **9600** 8N1 |
| Slave | **0x6E** (110) |
| FC | 0x03 |
| Span | one read `0x6000` length 21; unpack V/I/SoC/T |

| Reg | Field | Scale |
|---|---|---|
| 0x6000 | Pack V | ×0.01 V |
| 0x6001 | Current | ×0.01 A (signed; **positive = charging**, negative = discharging — [ADR-0002](decisions/ADR-0002-payload-contract-conflicts.md), decided 2026-08-13) |
| 0x6002 | SoC | ×1 % |
| 0x6009 | Batt T | ×1 °C |

**Recovery when the pack stops answering its latched id.** The expensive half of the ladder (the
5 s announcement window, the 20 s push listen) is paid for while the failure is new and then
rate-limited, and the vendor protocol's `BOOT` — which is a *reboot* verb, not a re-latch verb —
is sent at most once per failure episode: only after three consecutive cycles with no reading of
any kind, re-armed by the next genuine reading rather than by an MCU reset, and never twice
inside 96 cycles (24 h at 900 s). A pack that is answering is never rebooted, and a single missed
probe is not treated as silence. Both thresholds are chosen engineering margins sourced to bench
observation, not specified values.
[CITE(bench): one transient probe miss in 20 live cycles, `65f8615`](EVIDENCE.md)
[CITE(policy): never let the pack reach a state it cannot recover from by itself](POWER_BUDGET.md)

**Power:** P+/P− from 4-pin or 5-pin → **12 V→5 V buck** → WisBlock 5 V. Never feed P+ to `BAT`.

**Bus conflict note — HISTORICAL, resolved.** An earlier draft proposed sharing one RAK5802
between the RK900 and the BMS by switching baud between polls. [ADR-0004](decisions/ADR-0004-bms-one-wire-path.md)
rejected it: the RAK5802 is dedicated to the RK900, and the BMS talks one-wire on its own line.
Two buses, no baud switching, one fewer failure mode. Recorded here only so the option is not
re-proposed as though it were still open.

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
| Allowed | **900–86400 s** inclusive |
| Downlink port | **fPort 10**. A fixed port so traffic on any other port cannot be mistaken for a configuration change |
| Downlink format | `opcode` uint8, then arguments |
| `0x01` set interval | followed by `interval_s` uint32 BE — 5 bytes total |
| `0x03` request status | no arguments — 1 byte. Answered by shortening the wait so the next scheduled uplink arrives promptly, rather than transmitting a second time |
| Apply | next wake after RX; persist to flash |
| Invalid downlink | ignore; keep previous |

**The minimum is 900 s, and it is an airtime limit, not a power one.** The energy a cycle
costs is irrelevant at any interval in this range, so the fair-use allowance of roughly 30
seconds of transmit time per device per day is the only thing that sets the floor
[CIT-TTN-FUP].

The history matters, because the floor has moved twice. It was originally 300 s. That is
288 uplinks a day, and one 11-byte uplink at the slowest US915 rate takes about 370 ms —
roughly 107 seconds of airtime, over three times the allowance. It was raised to 1800 s,
which is under 18 seconds even at the slowest rate. It is now **900 s**, to give the
operator 15-minute reporting.

900 s is 96 uplinks a day. The airtime that costs depends entirely on the data rate the
network has settled the node at:

| Data rate | Spreading factor | ~Airtime per uplink (11–15 B) | 96 uplinks/day | Against the 30 s allowance |
|---|---|---|---|---|
| DR0 | SF10BW125 | ~370 ms | ~36 s | **over** |
| DR3 | SF7BW125 | ~60 ms | ~6 s | comfortably under |

So **900 s is fair-use compliant at DR3 or better and marginal at DR0.** This is a
coverage-dependent condition rather than a fixed guarantee, and recording it that way is
the point — the earlier 1800 s floor held at every rate, and 900 s does not.

Adaptive data rate is enabled with an initial `DR_3` (`src/radio.cpp`), so a node with
usable gateway coverage settles high enough for 900 s to be legal, while a node at the edge
of coverage stays near DR0 and does not. The operator accepts that trade for 15-minute
reporting; if a deployed node is observed sitting at DR0, the interval is the thing to
raise. Data rate to spreading factor mapping is per [CIT-LORA-RP002].

### Bench cadence (`FEATURE_BENCH_INTERVAL`) — never in the field image

Bring-up needs a reading about once a minute; a half-hour field interval makes a wiring
change take half an hour to confirm. `FEATURE_BENCH_INTERVAL` lowers both the floor and the
default to **60 s**. It is defined only by the `stage1` and `stage2` environments in
`platformio.ini` and is `0` everywhere else, including `[env:rak4631]`.

| Property | Field image | Bench build |
|---|---|---|
| Minimum interval | 900 s | 60 s |
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
  persisted. Every field interval (900–86400 s) falls inside the bench build's widened
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
7. USB detach / radio sleep / MCU sleep until next interval  

Step 7 detaches the USB device with `TinyUSBDevice.detach()` and re-attaches on wake. It must **not** clear `NRF_USBD->ENABLE` and must **not** call `Serial.end()`. Neither is reversible from application code: the core re-runs the USBD enable sequence only from its VBUS power-event handler, and `Serial.end()` clears the configuration descriptor with no re-enumeration to reconcile the host's view. Either one leaves the console dead for the rest of the boot while the application keeps running and transmitting — indistinguishable from a hung node from the bench, and the reason hardware verification was blocked repeatedly ([CITE(prior-art): Adafruit TinyUSB detach/attach](https://github.com/adafruit/Adafruit_TinyUSB_Arduino), [CITE(datasheet): nRF52840 USBD](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/usbd.html)).

**The console ships in the field image**, and omitting `Serial.begin()` would save nothing. `env:rak4631` builds `FEATURE_CONSOLE=1`; `env:soak` is byte-identical to it and deliberately carries no build difference, so a soak is evidence about the shipped image without an argument that one differing flag was harmless.

The reason is that **the USB device task exists regardless of application code.** The core calls `SerialTinyUSB.begin(115200)` and creates the `usbd` task before `setup()` runs, so our `Serial.begin()` returns immediately having done nothing ([CITE(prior-art): Adafruit nRF52 core, `Adafruit_USBD_Device.cpp:262,265` / `Adafruit_USBD_CDC.cpp:95` — `CIT-TINYUSB-CORE`](CITATIONS.md)). RAK's own BSP defines `-DUSE_TINYUSB` unconditionally with no opt-out, so **RAK's `MAX_SAVE` builds create the same task** ([CITE(prior-art): RAK-nRF52-Arduino `platform.txt:72` — `CIT-RAK-NRF52-CORE`](CITATIONS.md)). Omitting the call removes no task, no peripheral and no clock; it buys only that the node does not print.

**The lever is VBUS and `detach()`, not `Serial`.** The residual true claim is that the USBD peripheral and its HFXO cost energy — but `NRF_USBD->ENABLE` is set only from the VBUS power-event handler, so **in the field, with no cable, the peripheral is never enabled at all**, whatever was built. With a cable present, `detach()` drops `USBPULLUP` and stops host polling, which step 7 already does.

> **Do not re-derive the removal.** This paragraph asserted RAK's "**MUST NOT** be initialized / background task that never sleeps" mechanism as fact until 2026-08-12. It was acted on in `094d5f5` and reverted the same day in `636e421` after the mechanism was refuted from the core's source: the task blocks on `xQueueReceive(..., portMAX_DELAY)` rather than spinning, and SOF interrupts stay disabled for a CDC-only device. Full reasoning in [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md); the withdrawn claim and its counter-citations are in the `CIT-RAK-LOWPOWER` content-correction table in [CITATIONS.md](CITATIONS.md). **Prior art is not authority** — the primary source won, per `.cursor/rules/20-citation-discipline.mdc`. Whether a console-free build saves any measurable current is a magnitude question only, still unmeasured, tracked in issue #47.

Failing one sensor does not skip the other or skip uplink (uplink may be battery-only or weather-only with validity flags).

An uplink is also withheld when the persisted frame counter cannot be kept ahead of the counter actually transmitted — see H3 below. That is a refusal to transmit, not a failure: the alternative is a frame the network discards as a replay while `lmh_send()` reports success, which is indistinguishable from health from every vantage point the node has.

The reserve that makes those withheld writes affordable is finite, and the hold that consumes it need not end. A hold resting on a pack that has stopped answering is lifted only by a valid reading at or above the resume threshold, and the keepalive uplink is the only thing keeping the node reachable in the meantime — so once the reserve is spent, refusing the keepalive too would leave a permanently mute node, and being Class A ([CITE(spec): LoRaWAN L2 1.0.4 §3, Class A — `CIT-LW-LINK`](CITATIONS.md)) a permanently uncommandable one. **A keepalive the brownout gate itself armed is therefore allowed exactly one counter checkpoint write, gate or no gate.** Never an ordinary uplink, and never a hold backed by a measured low voltage, where staying quiet is the correct answer. The write rate is one per `session::kCounterMargin` keepalives — about monthly at the default cadence — and a supply that collapses mid-write cannot produce a plausible-looking wrong record, because the filesystem commits atomically ([CITE(prior-art): littlefs README, "atomic, even in event of power-loss" — `CIT-LITTLEFS-DESIGN`](CITATIONS.md)) and the in-RAM ceiling advances only after the write has returned.

## 6. Payload (draft — freeze before soak)

Prefer **RAK Standardized / Cayenne LPP–style** types already decoded by `forest-weather-machines` `rak-wx-station-default.js` so ingest stays unchanged:

| Channel / type | Content |
|---|---|
| Wind 158/190 | speed |
| Wind dir 159/191 | direction |
| Temp 103 | air |
| Humidity — ch **4**, type **112** only | RH. Type 104 is a single byte and decodes to key `humidity_4`, which is absent from the formatter's `CHANNEL_NAMES` and is therefore **silently discarded**. See [ADR-0002](decisions/ADR-0002-payload-contract-conflicts.md). |
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
| H3 | Brownout: no flash thrash; skip TX if V critically low. **One exception, by design:** an authorized keepalive may take a single frame-counter checkpoint write while the gate is engaged — see §5 below. |
| H4 | Join/TX fail: bounded backoff; survive multi-day no-GW |
| H5 | Interval + keys path survives power loss |
| H6 | RK900 absent → no livelock |
| H7 | BMS silent → no livelock |
| H8 | Bench soak ≥24 h; field shadow ≥7 d before hike-in trust |

### 7.1 Safety holds — every state where the node withholds an action

Three separate defects have had the same shape: a hold taken for a good reason that also
disabled the only mechanism able to lift it ([#61](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/61),
the brownout keepalive counter-headroom defect, and
[#74](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/74)/[#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68)).
Three times is a pattern, so the enumeration stops being implicit ([#66](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/66)).

**Class A is what makes this category dangerous.** A downlink can only follow an uplink, so
any hold that stops transmission also removes the only route by which the hold could be
countermanded. "Mute" and "uncommandable" are the same state, and the node is expected to run
unattended for a year.

Deliberately **not** a runtime mechanism. Firmware has no general way to know what would lift
an arbitrary hold, so a "watchdog for holds" would either override safety decisions blindly —
which is what #38 exists to prevent — or be a hand-maintained table pretending to be a
mechanism. Every real fix has been specific to its hold.

| Hold | Withholds | Lifted by | Depends on an action it suppresses? | Bound |
|---|---|---|---|---|
| Brownout inhibit, measured low (`≤ kTxInhibitCentivolts`) | Uplinks, flash writes | A reading `≥ kTxResumeCentivolts` | **No** — needs pack voltage to recover, which the node neither causes nor suppresses | **No uplink on a measured-low cycle, deliberately.** The pack has said spending energy is wrong (#38). The cycle still advances the shared hold clock: if a later reading is inside the hysteresis band, the keepalive may run there. Without that rule, alternating below/above the floor resets the clock forever and permanently mutes the node (#45). If the link dies, it degrades into the no-evidence hold below |
| Brownout hold, no evidence (`kInvalidReadsBeforeInhibit` = 4 unreadable cycles) | Uplinks, flash writes | Any valid reading | **No**, once bounded — but it *would* be terminal unbounded, since the pack may be full and the link simply broken | `kNoEvidenceKeepaliveCycles` = 24 cycles, then one uplink regardless (#45) |
| Brownout hold, inside the hysteresis band (valid reading between inhibit and resume) | Uplinks, flash writes | A reading `≥ kTxResumeCentivolts` | **No**, once bounded. Reads as well-evidenced while being just as inescapable — a solar pack hovering in-band through short winter days parked the node permanently | Same keepalive. The clock measures total held cycles and is not reset by intervening measured-low cycles; transmission remains suppressed until the current reading is in-band |
| Flash-write withholding under H3 | Config and session writes | The brownout hold lifting | **No.** Clearing a persisted hold is itself a write, but `Config` does not consult the gate, and a failed clear self-heals on the next valid reading | One documented exception: the counter checkpoint behind an armed keepalive |
| **Frame-counter ceiling refusal** | Every uplink | A successful `write_session()` advancing the stored ceiling | **Yes — this was the terminal one.** The only exit was the write that was failing. Needs no brownout and no dead pack: a healthy node with a worn page reaches it ~32 uplinks after its last save, about 8 h at 900 s | Three failures, then `session::forget()`; if the removal also fails, transmit anyway — see below |
| Join backoff | Nothing; lengthens sleep only | A successful join | **No.** Checked because #66 flagged it as the likeliest offender: it does not gate the keepalive, and `note_keepalive_sent()` fires only after an uplink reaches the air, so a failed join cannot consume one | `kBackoffMaxSeconds` = 3600, the default reporting interval |
| Quiet-cycle uplink suppression | Uplinks with no sensor data | Any sensor field, or the heartbeat | **No** | `heartbeat_due()` — first quiet cycle, then every `kQuietCyclesPerHeartbeat` |
| Battery fallback-ladder suppression | The expensive re-latch ladder, not the read | The brownout hold lifting | **No.** `read()` issues its direct query *before* consulting the gate, so the read that detects recovery always happens | Inherits the hold's bound |

**Why the ceiling refusal ends in transmitting rather than staying silent.** The two bad
outcomes are not symmetric. Staying mute is terminal: nothing in the field repairs a worn
flash page, and an uncommandable node cannot be told anything. Transmitting past the ceiling
costs nothing until a reset happens, and after one the restored counter is behind what the
network has already accepted, so frames are discarded until the counter climbs back past the
highest value sent — bounded, then it heals unaided. A temporary outage beats a permanent one.

`session::forget()` returns whether the file is actually gone, and that return is load-bearing:
removal is itself a write, so on the broken filesystem that makes the escape necessary it is
the operation most likely to fail. Clearing the in-RAM state anyway would let the node run past
a ceiling a reset still resumes from — the silent replay the ceiling exists to prevent,
reintroduced by the code escaping it.

**Adding or widening a hold requires a row here** and an answer to the dependency column. See
[`.cursor/rules/30-change-workflow.mdc`](../.cursor/rules/30-change-workflow.mdc).

## 8. Explicit non-goals

GNSS, RTC module, Meshtastic field image, RAK13002 (IO breakout; conflicts with RAK5802), fabricating sensor zeros, Class C.

## 9. Verification commands

Every item below has now been observed at least once on hardware — see the §9 table in
[`EVIDENCE.md`](EVIDENCE.md) for which commit closed each. **That is first light, not
hardening.** §7's H1–H8 is the release gate and none of it has closed.

- Compile: PlatformIO env `rak4631` (board id `rak4630`, from the vendored RAK definition
  in `rakwireless/` — the RAK4631 is not in the PlatformIO board registry)
- Bench: one successful RK900 frame + one BMS frame + one TTN uplink
- Downlink: change interval; confirm next cycle timing
- Fault: unplug RK900; unplug BMS data; assert continued sleep/wake
