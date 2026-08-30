# Evidence ledger

`AGENTS.md`: _"No aspirational 'deployed' claims without bench/TTN evidence."_ This file is
where that evidence lives. **If it is not written down here, it did not happen** — and the
project status stays `🚧 NOT YET DEPLOYED`.

## 2026-08-30 (fifth) — the pack's announcement frame decoded off the wire: it broadcasts unprompted, and identifies as "RAK2560-io"

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, capture 15,
digital + analog on the data line, `Async Serial` analyzer at **9600 8N1**. **Core:** none fitted.
**Nothing was transmitting** — pin 4 energised from the bench supply at 3.3 V, data wire to the
analyzer only, no MCU anywhere in the loop.

### Observation

**828 bytes decoded in 17.6 s. Nine frames, 92 bytes each, all nine byte-identical.**
Inter-frame period min 1.208 s, max 2.755 s, mean **2.186 s**.

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 33 09 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF 00 47 45 00 00
00 00 52 41 4B 32 35 36 30 2D 69 6F 00 00 00 00 00 00 00 00
00 00 00 00 00 00 06 15 BA 08 00 16 B9 08 00 17 B8 08 00 18
67 08 00 19 F3 08 00 1A F3 08 00 86
```

| Offset | Bytes | Reading |
|---|---|---|
| 0 | `FF` | wake byte — matches the four-wake-byte lead the driver already implements |
| 1 | `7E` | frame delimiter, HDLC-style |
| 42–51 | `52 41 4B 32 35 36 30 2D 69 6F` | **ASCII `"RAK2560-io"`** — the device names itself |
| 66 | `06` | count: six entries follow |
| 67–90 | `15 BA0800 · 16 B90800 · 17 B80800 · 18 670800 · 19 F30800 · 1A F30800` | six ID/3-byte-value triplets, IDs `0x15`–`0x1A` consecutive; values LE = 2234, 2233, 2232, 2151, 2291, 2291 |
| 91 | `86` | trailing byte, presumably the `cal_chksum()` popcount [CIT-ONEWIRE-SERIAL] |

### What this establishes

1. **The pack transmits without being asked.** Every ~2.2 s, with no master on the bus and no MCU
   connected at all. Previously the protocol was believed to be strictly request/response.
2. **9600 8N1 is confirmed on the pack side**, decoded rather than inferred. Independent of
   [ADR-0006](decisions/ADR-0006-rk900-baud-and-register-map.md), which established 9600 for the
   *RK900* on a different bus.
3. **The device identity is on the wire in plain ASCII**, confirming this is the RAK2560 Sensor Hub
   probe protocol and that [CIT-RAK-ONEWIRESERIAL] / [CIT-MESHTASTIC-9154] are the right
   references.
4. **Six consecutive IDs carrying six similar values.** Relevant to
   [#7](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/7), which
   records the pack's cell count as inferred from the nominal rating and never established. **Not
   concluded** — the unit of those values is unknown and six IDs need not mean six cells.

### What this does NOT establish

**All nine frames are identical, so this is an announcement, not telemetry.** No field varied
across 17.6 s. The `FF` at offsets 7 and 34 is consistent with an unprovisioned id awaiting
assignment (`provId FF`, the state `battery.cpp`'s provisioning ladder exists to leave). So
**listen-only cannot be assumed to yield battery readings** — the pack very likely has to be
answered at least once. Whether a *provisioned* pack then broadcasts telemetry unprompted is
untested and is the next cheap question.

### The consequence that matters today — and the line between fact and inference

**Fact:** the production battery read drives roughly **14 bytes** per cycle at a 900 s cadence.
`owscan` phase 0 drives **192 bytes** per cycle — 64 bytes of `0x55` at three baud rates — and
cycles in seconds. `0x55` toggles every bit.

**Inference, not established:** that this is what destroyed the seven pads. It is the leading
candidate and it fits, but no measurement has been taken of the node's own end of that wire while
it transmits, so the contention current is arithmetic rather than an observation. The diagnostic
was deleted on 2026-08-30 because driving an unqualified pad at 14× the production rate is wrong
on its own terms — **not** because it has been shown to be the cause.

The measurement that would settle it: an analyzer channel each side of the 1 kΩ series resistor
while the node transmits, giving contention current directly. Blocked on a flashable core
([#95](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/95)).

CITE(prior-art): [CIT-ONEWIRE-SERIAL] `RAK-OneWireSerial` @ `c58c0f0` — the wake-byte lead and
`cal_chksum()` this frame's first and last bytes are read against.
CITE(prior-art): [CIT-RAK-ONEWIRESERIAL] — RAK's own library, which names the RAK2560 Sensor Hub
probe protocol; the ASCII identity here confirms it applies.
CITE(bench): decoded bytes at `/tmp/rak-owprobe/decode/pack9600_c15.csv` on the build host, from
the capture preserved at `~/rak-captures/20260830_pack_announcement_9600.sal` alongside
`~/rak-captures/20260830_pack_levels_pin4_energised.sal`.

## 2026-08-30 (fourth) — the pack's data line MEASURED at last: 3.31 V idle, 0.087 V driven low, never out of pad spec

**The oldest unsourced electrical assumption in the project is now a measurement.** The RAK9154's
data-line logic level had never been established from a datasheet or a bench reading on any node
(prior reviews' "fact 4"). It has now.

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, capture 13,
three analog channels at 781.25 kS/s, 2,347,642 samples over 60.100 s, exported at downsample 20.
**Core:** none fitted. **Base board:** not in the loop. Pack pin 4 (`3V3_In`) energised from a
bench supply at 3.3 V, current limit 50 mA, supply negative bonded to pack pin 2 with the analyzer
ground. Data wire to the analyzer only.

### The valid window

Pin 4 was stable above 3.0 V for **57.11 s** (`t = 0.000 … 57.109 s`), median **+3.291 V**. It
dropped to ground for one contiguous 1.221 s run at `t = 57.132 … 58.353 s` — the supply being
switched off at the end of the run. All figures below are from the stable window only.

| Channel | Pack pin | min | median | max |
|---|---|---|---|---|
| 0 | 1 (`P+`) | +10.521 V | +10.521 V | +10.521 V (analyzer clipped, ≥ 10.52 V) |
| 1 | 4 (`3V3_In`) | +3.004 V | **+3.291 V** | +3.302 V |
| 2 | joined 3+5 (data) | **+0.014 V** | **+3.313 V** | **+3.318 V** |

### The data line, characterised

| Property | Measured |
|---|---|
| Idle / HIGH level | **+3.3118 V** mean, 96.56 % of samples (2,154,191) |
| Driven LOW level | **+0.0867 V** mean, 3.44 % of samples (76,644) |
| Absolute maximum seen | **+3.318 V** |
| Absolute minimum seen | **+0.014 V** |
| Midpoint (1.65 V) crossings | **9,520** in 57.11 s |

### Five conclusions

1. **The pack does not overdrive the pin. Overvoltage from the pack is RULED OUT.** Peak on the
   data line is +3.318 V against the nRF52840's `VDD + 0.3 V` = 3.600 V absolute maximum
   [CIT-NRF-GPIO] — **282 mV of headroom**. Not 5 V, not 12 V.
2. **The pack's driver is referenced to pin 4, not to `P+`.** Data HIGH sits at +3.3118 V while
   pin 4 sits at +3.291 V — **21 mV apart**, the same rail within measurement tolerance. Since the
   build feeds pin 4 from the node's own `VDD`, the pack **structurally cannot** drive the line
   above the rail the nRF52840's I/O runs on. This was the open question in the previous entry and
   it is now closed.
3. **No negative excursion.** Floor is **+0.014 V** against the pad's −0.3 V lower limit, across
   9,520 edges. The ground-offset / driven-negative mechanism
   ([CIT-NRF-GNDLIFT]) produces nothing here — though this test has no node ground return in it,
   so it constrains the pack, not the assembled system.
4. **The pack is an ACTIVE low-side driver, not a passive pull-down.** It pulls the line to
   +0.0867 V mean — a real driver, low impedance. **This strengthens
   [#99](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/99):**
   our end idles as a push-pull output driven HIGH, so an overlap is two active drivers in
   opposition with nothing between them but their on-resistances. Contention is now confirmed as
   physically available, not hypothetical.
5. **[#101](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/101)'s
   premise is confirmed by measurement for the first time.** The line idles at **+3.31 V**, and an
   *unpowered* nRF52840 pad's maximum is **0.3 V** [CIT-NRF-BACKPOWER]. A mated harness on a dark
   core is therefore **11× over** its absolute maximum — previously asserted from the nominal rail
   label, now measured.

### What is NOT concluded

The pack transmitting for 57 s at a safe level does not explain seven dead pads. It removes
candidates. Still open and untested: H0 on the **base-board header** (`BAT` sits on the same
2.54 mm row as the pads that die, and the board was not in this test), the contention path in
conclusion 4, and the unpowered-core exposure in conclusion 5.

CITE(datasheet): [CIT-NRF-GPIO] — the `VDD + 0.3 V` pad maximum the 282 mV headroom is measured
against.
CITE(prior-art): [CIT-NRF-BACKPOWER] — the 0.3 V unpowered maximum behind conclusion 5.
CITE(datasheet): [CIT-SALEAE-LOGICPRO8] — ±10 V analog range (why channel 0 clips), 4.88 mV per
LSB (why 21 mV in conclusion 2 is at the edge of resolution and read as "same rail").
CITE(bench): capture preserved at `/tmp/rak-owprobe/energised/analog.csv` on the build host.

## 2026-08-30 (later still) — the pack alone is harmless, and the harness has no 12 V bridge to the data line

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, capture 12,
three analog channels at 781.25 kS/s, 1,172,363 samples over 30.012 s, exported at downsample 20.
**Core:** none fitted. **Base board:** not in the loop. Only the pack and the analyzer, GND on
pack pin 2.

### Observation

| Channel | Pack pin | min | median | max | stdev | distinct ADC codes |
|---|---|---|---|---|---|---|
| 0 | 1 (`P+`) | +10.521 V | +10.521 V | +10.521 V | **0.000 mV** | **1** |
| 1 | 4 (`3V3_In`) | −0.046 V | −0.020 V | +0.011 V | 2.99 mV | 12 |
| 2 | joined 3+5 (data) | −0.017 V | +0.003 V | +0.045 V | 9.04 mV | 13 |

Zero samples on any channel sat more than 0.5 V from that channel's median. The pack transmitted
nothing for the full 30 s.

### Four conclusions, each with the number behind it

1. **The pack was live.** Channel 0 held +10.521 V on all 1,172,363 samples with *one* ADC code
   and *zero* variance — that is the analyzer clipping at the top of its ±10 V analog range
   [CIT-SALEAE-LOGICPRO8], not a measurement. So `P+` is **≥ 10.52 V**. This is the control the
   earlier 0 V capture lacked: **that reading was not a dead pack.**
2. **The pack does not drive pin 4.** It sits at −0.020 V with 2.99 mV of noise — far below the
   20 mV floating threshold, so genuinely connected and genuinely at ground. `3V3_In` is a
   passive input exactly as [CIT-RAK9154-DS] describes. **Hypothesis rejected: the pack does not
   present a rail on pin 4 that could back-feed the board's 3.3 V.**
3. **There is no `P+`-to-data-line bridge inside the harness.** `P+` was live at ≥ 10.52 V while
   the data wire read +0.003 V. The data wire's only loads were the analyzer's 2 MΩ and the pack's
   measured 15 kΩ pull-down, so a resistive bridge would appear as a divider; 3 mV across 15 kΩ
   bounds any such path at **> 50 MΩ**, i.e. absent. **This clears the harness of H0** (the 12 V
   rail reaching the data pad). H0 on the *base-board header* is untouched — the board was not in
   this test.
4. **With pin 4 unpowered the pack is mute and everything but `P+` is at ground.** So a mated
   harness on an unpowered node is not sitting there with a lethal level on the data wire.

### What it means for the mechanism

The pack's data driver is powered from pin 4, and pin 4 is fed from the node's own `VDD`. If pin 4
is genuinely that driver's rail, the pack **cannot** drive the data line above the node's 3.3 V,
because it is the same rail the nRF52840's I/O sits on — which would rule out overvoltage from the
pack entirely. That is not yet established: pin 4 could be a detect input while the driver
references `P+` internally. **Open, and the next measurement:** energise pin 4 from 3.3 V with no
Core fitted and watch what the data line rests at and drives to.

CITE(datasheet): [CIT-SALEAE-LOGICPRO8] — the ±10 V analog range that makes channel 0's
single-code reading a clip rather than a value, and the 2 MΩ input that makes the divider
argument in conclusion 3 valid.
CITE(datasheet): [CIT-RAK9154-DS] — SP11 pinout; `3V3_In` on pin 4, corroborated here by
measurement rather than by the label.
CITE(bench): capture preserved at `/tmp/rak-owprobe/pins/analog.csv` on the build host.

## 2026-08-30 (later) — the measurement gating #102 is void: it reads 0 V by construction

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8, serial `AF11F852CEC20A9`, Logic
2.4.46. **Core:** none — no RAK4631 was involved, and none was at risk. **Measured by the
analyzer**, not by the firmware.

[#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102) names
one discriminating measurement and blocks fitting another core until it passes: *"pack live,
harness unplugged from the node, meter the joined data wire against pack pin 2. Anything above
3.3 V is the pin killer."* It was run. **It cannot answer the question, and would have returned a
false all-clear.**

### Observation

| Capture | Setup | Samples | Span | min | max | median | stdev |
|---|---|---|---|---|---|---|---|
| 9 | nothing connected (instrument baseline) | 1,639 | 2.097 s | −0.192 V | +0.220 V | −0.009 V | **78.43 mV** |
| 11 | pack powered, harness unplugged from the node, ch0 on joined data wire (pins 3+5), GND on pack pin 2 | 786,429 | 20.133 s | −0.025 V | +0.001 V | −0.009 V | **0.74 mV** |

Capture 11 is **0.000 V, flat, for the full 20 seconds** — 775,463 of 786,429 samples (98.6 %) on
a single ADC code, six distinct codes in the whole file.

### Why that is a real reading and not a bad clip

Capture 11 is **105× quieter** than capture 9. A 2 MΩ input with nothing on it acts as an antenna
and wanders across 77 codes; a low-impedance tie to ground does not. So the probe was genuinely
on the wire and the wire was genuinely at ground.

### Why it proves nothing about the harness

**The pack's data-line reference is its own pin 4 (`3V3_In`), and pin 4 is fed from the node.**
With the harness unplugged from the node, pin 4 has no supply, the pack's line driver has no
rail, and the line rests at 0 V through the pack's measured 15 kΩ pull-down. That is the only
possible outcome of this setup — identical whether the harness is lethal or benign.

So the gate that has been blocking #102 is structurally incapable of discriminating, and the
obvious reading of its result ("≤ 3.3 V, therefore cleared") would have licensed fitting a fourth
core to an unqualified harness. **No harness has been cleared. #102 stays open.**

### What was changed as a result

- `scripts/owprobe.py` distinguishes the three near-zero cases by noise instead of magnitude:
  floating clip (> 20 mV stdev), de-energised driver (< 20 mV stdev), and a genuine driven low.
  It exits 1 on the first two rather than reporting a pass. Verified against both captures above
  plus four synthetic cases.
- [`HARDWARE.md`](HARDWARE.md) § "Qualifying the pack harness" records the void test and
  specifies the one that works: energise pin 4 from the base board's `VDD` **with no Core
  fitted**. The 3.3 V regulator is on the base board, not the Core — RAK diagnosed the identical
  symptom with the Core physically removed and `VDD` still dead [CIT-RAK19007] — so the pack gets
  its reference while no nRF52840 pad is exposed at all.

CITE(datasheet): [CIT-SALEAE-LOGICPRO8] — the ±25 V absolute maximum and 2 MΩ input that make
this measurable without a core; the ±10 V analog range that would have saturated on a 12 V line.
CITE(prior-art): [CIT-RAK19007] — the 3.3 V regulator sits on the base board.
CITE(bench): captures preserved on the build host at `/tmp/rak-owprobe/{baseline,live}/analog.csv`.

## 2026-08-30 — SDA/P0.13 is the fourth dead one-wire pad, and a 1 kΩ series resistor did not prevent it

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Core:** node 002's third core. **Image:**
`env:owscan_sda` at `afdaf48`. **Measured by the firmware**, not by meter, so it is repeatable.

### Observation — the pin cannot be pulled up, with both plausible external causes removed

Three readings, each with one more thing taken away:

| Pack connector | 2.2 kΩ pull-up | `INPUT_PULLUP` result |
|---|---|---|
| connected | fitted | `idle LOW : 0 falling edge(s), 1735755 of 1735755 samples LOW` |
| connected | lifted at one end | `idle LOW : 0 falling edge(s), 1746448 of 1746448 samples LOW` |
| **unplugged** | **lifted** | `idle LOW : 0 falling edge(s), 1749426 of 1749426 samples LOW` |

With nothing attached to the net and the chip's own ~13 kΩ internal pull-up enabled, the pin
still reads LOW on every sample. **P0.13 is shorted to ground.** The Saleae agrees
independently: 18.8 M analog samples over 12.024 s, median 0.121 V, range 0.106–0.205 V, and
**zero edges even while the firmware was bit-banging 64 bytes of `0x55` at three bauds.**

### Two hypotheses refuted here, both of them mine

- **The pull-up was miswired to GND.** A 2.2 kΩ to ground would pin the line near 0.4 V and read
  LOW, which fit. Lifting the resistor changed nothing, so it was not the cause.
- **A powered pack clamps the bus low.** The 15 kΩ the operator measured was on a disconnected
  pack, so it said nothing about the live case. Unplugging the pack changed nothing either.

### The count is seven pads across two cores, operator-stated

**Four on the core currently in node 002, three on the previous one.** Every one was the pin
carrying the pack's data line at the time; no pin used for anything else has failed on either
core. Earlier drafts of this entry said "four" because they counted only what the agent had
personally measured that day — that undercount is corrected here and should not be reintroduced.

**A 1 kΩ series resistor was inline when SDA died.** That is the mitigation `docs/HARDWARE.md`
requires and the one this ledger recommended after the A1 failure, and it did not prevent the
failure. Series resistance is therefore **refuted as sufficient protection** — which also means
the "REQUIRED protection network" section of `HARDWARE.md` oversells what it delivers and needs
revising rather than repeating
([#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102)).

### No firmware change since 001's image can explain it — established by diff, not argument

Node 001 has been in the woods since 2026-08-14 on `1c2df3c` with no pin damage. Diffing the
whole one-wire path from that commit to `afdaf48`:

| File | Change since `1c2df3c` |
|---|---|
| `src/sensors/battery.cpp` | **none** |
| `src/sensors/battery_frame.{h,cpp}` | **none** |
| `src/diagnostics/owscan.cpp` | 8 lines, **every one a comment or a log string** |
| `src/build_features.h` | the two new pin-select flags, and the version bump |
| `src/main.cpp` | `kBatteryPin` selection, the reset banner, a session-recovery callback |

So the one-wire driver in use where seven pads died is **functionally identical to the one
running without incident on 001.** The push-pull `pinMode(tx, OUTPUT)` in
`SoftwareHalfSerial::setTX()`, the four wake bytes, the provisioning ladder and the `0x55`
timing sweeps are all common to both nodes.

**This refutes the bus-contention hypothesis as an explanation for the difference between the
two nodes** — not as physics, but as a cause of *this* divergence, since both nodes run it. Any
future theory has to account for two cores failing on one harness while a third core runs the
same code safely on another.

### What that leaves

The variables never swapped are **node 002's harness and its pack**, present at all seven
failures across two cores. The cheapest discriminating measurement puts no MCU at risk: with the
**pack live and the harness unplugged from the node**, meter the joined data wire (pack pins
3+5) against pack pin 2. Anything above 3.3 V there is the pin killer. Until that is cleared,
**this harness must not be connected to another core.**

Note also that back-powering through a pad's ESD diode [CIT-NRF-BACKPOWER] should short a pin to
VDD, not to ground; Nordic's own thread raises that objection and leaves it unresolved
[CIT-NRF-PINSHORT]. Every dead pad here measures short to *ground*. **The cause is
unestablished. Only the correlation is solid.** Do not write it up as settled.

## 2026-08-30 — node 002 reads weather and transmits; opening the console resets the board

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Core:** node 002's third core, one-wire on
`SDA` (nRF P0.13), `1 kΩ` inline in the data wire. **Image:** field image at `7ad5daa`, then
`env:battdiag_sda`. **Pack:** physically disconnected for all of the below — operator-confirmed.

### Observation — the node works

```
2026-08-30 12:37:38    session : restored 0x260C1AF4, counter 1088
2026-08-30 12:37:38 [cycle 1]
2026-08-30 12:37:38    RK900   : raw 0x0000-0x0004 = 0000 0000 0107 01B5 275E
2026-08-30 12:37:38    RK900   : wind 0.00 m/s @ 0 deg, 26.3 C, 43.7 %RH, 1007.8 hPa
2026-08-30 12:38:05    battery : no data (no reply, 0 bytes)
2026-08-30 12:38:05    radio   : sent 20 bytes on port 2
2026-08-30 12:38:13    sleep   : 3600 s
```

RK900 returns plausible values, the uplink goes out at 20 bytes (wind-only, no battery block), and
the cycle closes on a normal sleep. **`battery : no data` is the correct result with no pack
attached, not a fault** — three sessions were spent treating it as one.

The DevAddr is `0x260C1AF4`, not the `0x260CE734` carried all month, so this core performed a
fresh OTAA join at some point today rather than restoring the old session.

### Observation — the interval downlink was rejected, so cadence never changed

```
2026-08-30 12:38:13    radio   : downlink — set interval 300 s
2026-08-30 12:38:13    config  : rejected interval 300 s (allowed 900-86400)
```

The node was asked for 300 s repeatedly through the afternoon and correctly refused every time;
the floor is 900 s. Every "why is it still on the slow cadence" question today has this answer.

### Inference (strong, 3/3 correlation) — the console attach is what reset the board

Every `[cycle 1]` in today's captures falls within one second of `scripts/capture.py` opening the
port: 12:35:47, 12:36:11, 12:37:38. The `f_cnt` steps read as fault evidence all afternoon —
`864 → 896 → 928 → 960 → 992 → 1024`, uniform `+32` — are the `kCounterMargin` signature of one
reset each, and the resets line up with capture attempts, not with transmits.

**Consequence: a counter jump observed while a capture is being started is not evidence of a
fault.** This is inference from timing correlation, not a measured reset cause: `power.cpp` read
`RESETREAS` and discarded everything but the watchdog bit, so nothing recorded why the node
reset. **Fixed in `d25f823`** — the boot banner now names the cause, which makes the next
occurrence a measurement instead of another inference.

### Observation — the failure ladder behaves as specified with the pack absent

13 consecutive `battdiag_sda` cycles with no pack: one BOOT per failure episode and not more
(`BOOT already spent this failure episode`), degradation to `probe only`, full-ladder retry every
second cycle, and the no-evidence hold arming its bounded escape —
`power : pack silent for 4 cycles — holding transmissions, no voltage evidence (keepalive in 24
cycles)`. This is the #45 / #71 hardening running correctly on hardware for the first time.

## 2026-08-30 — node 002's silent core is ALIVE: SWD answered, chip unlocked

**Host:** Heliotrope Ridge, CMSIS-DAP probe (serial `07000001006900394e...`), OpenOCD
0.12.0 from `~/.platformio/packages/tool-openocd`. **Core:** node 002's third core, the one that
stopped enumerating on USB.

### Observation — the core is not dead

One OpenOCD session connected and the target identified itself:

```
Info : Connecting under reset
Info : SWD DPIDR 0x2ba01477
Info : [nrf52.cpu] Cortex-M4 r0p1 processor detected
Info : [nrf52.cpu] target has 6 breakpoints, 4 watchpoints
0x00000001          <- CTRL-AP APPROTECTSTATUS
```

**`APPROTECTSTATUS = 0x00000001` means access port protection is NOT active** — the debug port is
open, so no mass erase is required to work on this core.

**A dead chip cannot do this.** It cannot return a valid DPIDR, and it certainly cannot report its
own core revision and its breakpoint and watchpoint counts. Combined with RAK's discriminator — a
missing SoftDevice kills USB while leaving SWD working, and "if you can't connect through JLink,
the device is damaged" [CIT-RAK-USBDEAD] — **this core is software-bricked, not damaged.**

The connection also required `connect_assert_srst`. Plain `init` failed at 100, 400 and 1000 kHz.
Holding reset while attaching is what worked, which indicates **there is firmware executing in
there** that interferes with the debug port when allowed to run.

### Not reproducible on demand — a physical-contact problem, not a silicon one

After that single success, **120 consecutive attempts failed** with `Error connecting DP: cannot
read IDR`, across three clock speeds, with and without `connect_assert_srst`, and with `halt`
removed from the command chain after it was found to abort the sequence early. Nothing was changed
on the software side between the success and the failures.

The probe initialises correctly every time — it reports its firmware version, its serial, and the
line states — so the failure is at the target end of the wires. **One good connection followed by
120 identical failures with no software change is the signature of marginal probe contact, not of a
chip that died in between.**

### What this retires

**"Another dead chip" is refuted.** The core answered. Any further reasoning about node 002's USB
silence must start from a live, unlocked nRF52840 running some firmware, with a broken USB
presentation — which is the bootloader/SoftDevice class of fault that RAK's forum corpus says every
documented RAK4631 no-enumeration case turned out to be.

### Not yet established

- What is actually in flash. The UICR bootloader pointer, the vector table at `0x0`, the
  application at `0x26000` and the bootloader at `0xF4000` were all queued for reading and **none
  of them were read** — every attempt after the first failed to connect. No conclusion about the
  bootloader or SoftDevice state is available yet.
- Whether reflashing bootloader + SoftDevice + application over SWD restores USB. The procedure
  that worked on a different core earlier the same day is in [`FIRST_FLASH.md`](FIRST_FLASH.md):
  OpenOCD with `configure -work-area-size 0`, and no resets between erase and program.
- Why the probe contact is marginal. Mechanical, and on the operator's side of the bench.


## 2026-08-30 — buck output measured at 4.98 V; the buck is cleared

**Host:** Heliotrope Ridge bench. **Core:** node 002's third core. **Meter:** operator's DMM.

### Observation

**Buck output: 4.98 V**, with the pack near full charge (~12.4–12.5 V at the buck input, from the
same pack that reported 12.41 V over the air at 18:13:20Z the same day).

### What this establishes

The buck is feeding a correct rail. 4.98 V is comfortably inside the RAK19007's `VBUS` absolute
maximum of 5.5 V [CIT-RAK19007-DS] and inside the nRF52840's own 5.8 V [CIT-NRF-GPIO-TOTAL].
**Overvoltage from the buck is refuted as the cause of the whole-core USB failure.**

This closes the check that [`FIRST_FLASH.md`](FIRST_FLASH.md) :25 has asked for since the project
began and that had never been recorded.

### A retracted hypothesis, recorded so it is not re-derived

I proposed that the buck's real hazard was **input sag** — that as the pack discharged, the buck
input would walk down toward the module's enable threshold and the output would enter a growing
sawtooth overshooting well above setpoint. **This does not apply to this build, and the operator
was correct to reject it.**

The measurement it rested on [CIT-MP1584-SCOPE] was taken on a module configured for a **3.3 V**
output whose sawtooth appeared near an **~11.8 V** input threshold specific to that board. An
MP1584 configured for **5 V** out needs only roughly 6 V in. **The pack's own protection cuts off
far above 6 V**, and this firmware stops transmitting at a 9.60 V inhibit long before that, so the
buck input never enters the regime where that behaviour was observed. Generalising one hobby
capture across a different setpoint and a different input range was wrong.

The related TI thread [CIT-TI-BUCK-OVERSHOOT] describes overshoot on **recovery from an output
short circuit**, which is a different event, not discharge sag. Conflating the two inflated the
concern further. Both citations stay in the registry because they are real and correctly describe
what they measured; neither supports a claim about this node.

### What is therefore still unexplained

**The whole-core USB failure on node 002 has no established cause.** The back-powering mechanism in
[#96](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/96) explains a
single dead pad and predicts the rest of the chip surviving — it does not predict a core that
vanishes from the host's USB tree entirely. With the buck cleared, no candidate remains that has
both a mechanism and evidence.

The next diagnostic is SWD, not more theory: a missing SoftDevice kills USB while leaving SWD
working, so **SWD reachability distinguishes a software-bricked core from a damaged one**
[CIT-RAK-USBDEAD]. Until that is run, "dead chip" is not a finding.

### Unrelated finding recorded the same day

RAK's own reference path for reading this pack **does not put the data line on a bare core GPIO**.
`beegee-tokyo/RAK-OneWireSerial`, the only published code that names the RAK9154, requires a
**RAK13002 WisBlock IO module** — "the Sensor Probe is connected with 3.3 V, GND and RXD1 only from
the RAK13002 module" — prefers `RX1` / P0.15, states that other GPIOs "should work… but it is not
tested", and is marked BETA [CIT-RAK-ONEWIRESERIAL]. This build's topology is outside anything RAK
documents, and the module skipped is the one sitting between the pack and the core.

The RAK9154 datasheet itself confirms the SP11 pinout in use but is **silent on every electrical
property** of the data lines — no logic level, no open-drain-versus-push-pull, no internal pull
resistance, and no statement of what `3V3_In` is for [CIT-RAK9154-DS]. The 15 kΩ measured on the
pack's data line is close to the nRF52840's own internal pull range of 11–16 kΩ
[CIT-NRF-GPIO-TOTAL], but nothing in RAK's documentation says what the pack presents.


## 2026-08-30 — RETRACTED: this entry claimed the cause was established. It is not.

> **Retraction, same day.** The heading below read "cause of the dead one-wire pads established:
> back-powering an unpowered core". That was wrong on two counts and is left in place, struck
> through by this note, because deleting a wrong claim hides that it was made.
>
> 1. **Direction.** Back-powering conducts through the pad's *upper* ESD diode and shorts a pin to
>    **VDD**. All measured pads here read short to **ground**. Nordic's own engineers hit this same
>    signature and say the remaining possibilities are a negative voltage on the pin or ground
>    lifted above it — not back-powering [CIT-NRF-GNDLIFT].
> 2. **Count.** The entry says four pads across four cores. The count is **seven pads across two
>    cores**.
>
> The mechanism that does match the signature is loss of the ground return, which makes the data
> pin carry the core's entire supply current [CIT-NRF-GNDLOSS]. That is a candidate too, not a
> proven cause. Tracking:
> [#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102).

## 2026-08-30 — (superseded heading) cause of the dead one-wire pads: back-powering an unpowered core

**Host:** research and analysis on the workstation; failure observations from the Heliotrope Ridge
bench earlier the same day. **Commit:** documented at the commit carrying this entry.
**Core:** node 002's third core (die ID never read — [#97](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/97)).

### Observation

**Count corrected: seven GPIO pads across two RAK4631 cores** — four on node 002's current core,
three on its predecessor (the "four cores" in the original text was wrong). In every case the dead
pad was **the pad carrying the RAK9154 one-wire link**. Pads on the same cores never connected to
the pack (`A0`, `SCL`) remained healthy. `SDA` (P0.13) is listed below as working; **it died later
the same day**, which is what took the count to seven.

A dead pad reads permanently LOW as an input and measures ~3.9 Ω to ground. The rest of each chip
kept working.

Separately, on 2026-08-30 node 002 stopped presenting on USB entirely: no `/dev/cu.usbmodem*`, no
UF2 bootloader volume, and **nothing in the host's `ioreg -p IOUSB` tree** — only the Mac's own
hubs and a SuperDrive. Not even a failed-enumeration entry.

### Inference, with sources

The dead-pad mechanism is documented by Nordic. On an **unpowered** nRF52840 the absolute maximum
on any GPIO is **0.3 V**, because the limit is VDD + 0.3 V and VDD is zero; above that the pad's ESD
protection diode conducts and back-powers the chip through the pin [CIT-NRF-BACKPOWER]. The damage
mode is current, and Nordic's phrasing for the result — in a thread titled *"SWDIO pin shorted to
ground"*, on a chip still running its last firmware — is that "the high current flow can
permanently damage the pin" [CIT-NRF-PINSHORT]. That thread's symptom is ours: one pad a few ohms
to ground, neighbours healthy.

The RAK9154 idles its data line at a level referenced to `3V3_In`, which this build takes from the
always-on `VDD` pad, and the pack has no way to know whether the core is powered. **So an unpowered
core with the harness mated sees roughly 3.3 V against a 0.3 V maximum — an order of magnitude
out.**

**Why node 002 and not node 001.** The buck powers the core *through the USB-C connector*, so the
buck and a host cable are mutually exclusive. Every swap between bench power and pack power
therefore contains a window with the core dark and the harness mated. Node 002 went through that
window roughly a dozen times during one day of bring-up. Node 001 was wired once, deployed, and has
never been through it — and `owscan`, including its transmit phases, drove 001's `IO1` on
2026-08-04 and 2026-08-12 with no harm. The difference is exposure to the swap window, not
firmware, not the protocol, and not the diagnostic tool.

**This supersedes the header-geography reading** recorded earlier the same day, which observed that
every dead pad sat on the `BAT IO2 IO1 A1 IN1` row while every healthy pad sat elsewhere. That
correlation is real but incidental — those pads were on that row because that is where the one-wire
pad happened to be. It has no mechanism and cannot explain `SDA` surviving. H0 (a 12 V bridge on the
header) is not refuted but is no longer load-bearing, and it never explained the three `IO1` cores
that died with no 12 V present.

### The whole-core failure is a different signature

Back-powering through one pad predicts one dead pad, not a chip that vanishes from the USB tree.
The RAK19007's USB-C `VBUS` absolute maximum is **−0.3 to 5.5 V**, feeding a TP4054 charger behind a
series diode, with **no documented overvoltage clamp** [CIT-RAK19007-DS]. The buck driving that
connector has never been metered on this project. Filed as
[#100](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/100). RAK's own
recommendation is that a regulated external supply goes to the **P2 "Green Power" connector, not
USB-C** [CIT-RAK-GREENPOWER] — which would also close the #96 swap window, because the buck and a
host cable would stop being mutually exclusive.

### Not yet established

- Whether this core is damaged or merely software-bricked. RAK's discriminator is SWD: a missing
  SoftDevice kills USB while leaving SWD working, so **SWD reachability is the test**
  [CIT-RAK-USBDEAD]. Not run at the time of writing. USB silence alone is not evidence of a dead
  chip — every documented RAK4631 no-enumeration case in the forum corpus turned out to be
  bootloader or SoftDevice.
- The buck's actual output voltage, loaded and unloaded ([#100](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/100)).
- Whether the ground-short *end state* is fully explained. [CIT-NRF-PINSHORT] carries an unresolved
  objection: back-powering through the clamp diode should short the pin to VCC, not to ground.
  Nordic did not close it. The damage attribution stands; the precise end state does not.

### Consequence for the build

Connector sequencing is now a documented rule in [`HARDWARE.md`](HARDWARE.md): the pack connector is
mated **last** and unmated **first**, always. The 1 kΩ series resistor is retained and now justified
by mechanism — it bounds the ESD-diode current to about 3 mA, converting a sequencing mistake from
fatal to harmless.


## What counts as evidence

An entry records something **observed**, not something expected. Every entry carries:

| Field | Why |
|---|---|
| Date | when it was observed |
| Commit SHA | which firmware — a result without a SHA cannot be reproduced |
| Host | which machine ran it ([`ENVIRONMENTS.md`](ENVIRONMENTS.md)) |
| What was measured | the specific claim under test |
| Raw observation | the actual output, reading, or log excerpt |
| Verdict | pass / fail / inconclusive |

"Inconclusive" is a legitimate verdict and more useful than an optimistic pass.

Measurement traps that invalidate an entry:

- **Sleep current measured over USB is meaningless.** Measure on battery and say so
  ([`.cursor/rules/50-power-management.mdc`](../.cursor/rules/50-power-management.mdc)).
- A bench run with the debugger attached is not a field run.
- A decoded TTN payload proves the formatter matches *that* build, not the schema —
  the parity gate proves the schema ([`.cursor/rules/60-decoder-parity.mdc`](../.cursor/rules/60-decoder-parity.mdc)).

## Getting the commit SHA off a running board

Every entry needs a SHA, and the firmware now supplies one. Since
[`033b584`](../CHANGELOG.md) the boot banner prints it, injected at build time by
`scripts/pio_git_rev.py`:

```
=== rak-sensor-node ===
firmware : 0.4.1
commit   : a7381e7
built    : Aug 13 2026 08:12:44
```

Read the `commit` line and record it verbatim. Three forms and what each means:

| Banner says | Means | What to record |
|---|---|---|
| `a7381e7` | built from that commit, working tree clean | the SHA, asserted |
| `a7381e7-dirty` | built from that commit **plus uncommitted work** | the SHA **and** `-dirty` — the tree is not recoverable from the SHA, so treat the build as unreproducible and say so |
| `unknown` | no git history was available at build time (tarball export, checkout without history) | `unknown`, and whatever else identifies the build — never substitute a guess |

**Reading an older capture, whose banner predates `033b584`:** those banners carry only
`firmware :` and `built :`, and a version plus a timestamp does not identify a commit — two
builds of the same version are byte-different and read identically. Do not resolve it by
picking the newest commit older than the build stamp. That is an inference, and it is wrong
whenever the build came from a tree that was dirty, from a branch, or from a checkout that was
not `main`.

Record the build timestamp verbatim, then mark the SHA **inferred** and name what the inference
rests on — as the 2026-08-13 `stage3` entry below does. A SHA that is inferred and labelled is
useful; a SHA that is inferred and asserted quietly poisons every later document that copies it,
which is exactly how a soak claim propagated through four documents on 2026-08-12. **Never
assert a SHA the board did not print.**

## Release gates awaiting evidence

From [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §7. **None of these can be closed by inspection.**

Two different questions get confused here, so the table separates them. **"In source"** means
the mechanism exists in `src/` and was read by a human — that is a precondition, not a pass,
and it is the weakest form of evidence this repo accepts. **"Status"** is the gate itself,
which only a measurement closes. A gate can be fully implemented and still `⬜ none`; H4 is
exactly that. The source column is from the read-only audit in
[`reviews/2026-08-12_spec_drift.md`](reviews/2026-08-12_spec_drift.md) §1, which names the
implementing lines for each gate.

| ID | Requirement | In source (2026-08-12 audit) | Evidence needed | Status |
|---|---|---|---|---|
| H1 | Hardware WDT resets a hung Modbus/BMS read | ✅ `NRF_WDT` armed at 120 s, fed on the long sensor paths. Paused across sleep by design — guards the awake path only | Induced hang → observed reset | ⬜ none |
| H2 | Deep sleep between cycles; radio sleeps | 🟡 radio + SPI + USB-detach sleep are real; the "deep" half is a `delay()` loop, not the chip's deepest state, and the code says so | Measured sleep current on battery — **and it must be a meter.** Pack telemetry cannot answer it: 10 mA LSB against a ~1 mA question (2026-08-12, `4510763`) | ⬜ none |
| H3 | Brownout: no flash thrash, no TX when low | ✅ thresholds, TX gate and flash gate all wired; the ungated session writer found by the audit was closed in `378384e` | Sag the supply → observed skip | ⬜ none |
| H4 | Bounded backoff; survives multi-day no-gateway | ✅ doubling, clamped | Gateway off ≥48 h → observed backoff | ⬜ none |
| H5 | Interval + keys survive power loss | ✅ interval and session over `InternalFS`; keys are compiled into the image, so "keys survive" is true trivially rather than by storage | Set interval, cut power, confirm retained | 🟡 partial — session restore observed 2026-07-31 and re-confirmed on node 002 at `9e2fcb4` on 2026-08-28, including TTN acceptance after reset; interval-survives-power-loss not yet isolated |
| H6 | RK900 absent → no livelock | ✅ bounded 1000 ms reply timeout; caller tolerates failure without retrying forever | Unplug sensor → cycle continues | 🟡 partial — silent-sensor bounded timeout observed 2026-07-31 and re-confirmed on sensor-absent node 002 at `9e2fcb4` (three attempts, then cycle continued to uplink and sleep); still needs connected-then-removed fault injection |
| H7 | BMS silent → no livelock | ✅ bounded first-byte and inter-byte timeouts, bounded provisioning window. Bounded but **long** — `acquire_pid()` measured at 45.4 s of a 50.5 s wake, inside the 120 s WDT window with less margin than it sounds | Unplug BMS data → cycle continues | 🟡 partial — sensor-absent node 002 at `9e2fcb4` completed the no-latch/no-reply path, sent proof of life, and slept; still needs connected-then-removed fault injection |
| H8 | Bench soak ≥24 h, field shadow ≥7 d | n/a — a process gate; no code implements it and none can | Soak log + TTN ingest history | 🟨 **started but not met, on both halves.** Bench: the longest run is **19.03 h on `572bcfa`, 76 uplinks, 0 anomalies**, 2026-08-13/14, **stopped deliberately** short of 24 h to ship the `#75` battery fix. The run on the shipping image `1c2df3c` had a **deliberate RESET inside its window** at 17:50Z (`resets=1`), so it is not an uninterrupted 24 h either. 19.03 h < 24 h, and a partial run on one image cannot be topped up by another. Shadow: the field deployment on **2026-08-14** is **day zero of seven — a beginning, not an achievement**. The earlier 2026-08-12 attempt at `f626698` never attached and logged 140 bytes in 180 s. **H8 stays open** |

The [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §9 first-light list is now **closed**, and closing
it changes nothing about the status above:

| §9 item | Closed by |
|---|---|
| One good RK900 frame | 2026-08-03, `998dc26` — full five-register read at 9600 |
| One good BMS frame | 2026-08-05, `1a203d3` / `b6bbf31` — and re-confirmed 2026-08-12 at `b436aa9`, 19 of 20 `battdiag` cycles live |
| One TTN uplink | 2026-07-31 join + accepted uplink; network-side session confirmed still advancing 2026-08-12 at `f4075c0` |
| One downlink applied | 2026-08-12 — a `0x03` status request delivered and drained across one uplink. **Half the surface only**: `take_downlink()` has never been observed on the console and malformed-downlink bounds checking is untested ([#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)) |

First light is not hardening. H1–H8 above is the gate that governs deployment.

## Power budget

Projections live in [`POWER_BUDGET.md`](POWER_BUDGET.md). A projection is a hypothesis;
only a measurement recorded here closes it.

## Log

Newest first, by the date the entry was committed (`git log --format='%ad' -- docs/EVIDENCE.md`),
not by the date embedded in its heading — two 2026-08-03 entries and two 2026-07-31 entries each
span more than one commit, so heading dates alone don't disambiguate order. If you add an entry,
add it at the top.

### 2026-08-30 — Node 002 moves to SDA/P0.13 after A1 dies; pack reads and the uplink lands

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`. **Commit:** `7ad5daa` (`env:rak4631_sda`,
`FEATURE_BATTERY_PIN_SDA=1`), asserted from build and DFU inputs — the field image sleeps and
drops USB before a capture can attach, so no banner was read back (`banner_commit=NOT OBSERVED`).

**Core identity: not captured.** Die ID not read. Pinned to the node (`puma-concolor-002`,
`DevEUI 42BB96EF76E200F2`) per `.cursor/rules/03-bench-claims.mdc`, not to a physical part.

#### What failed first

`A1` (P0.31) carried a complete pack reading at 15:08:48Z (12.54 V, entry above) and was
**held low** by 16:45Z. Measured with `env:owscan_a1`:

| Pin | Reading | Conditions |
|---|---|---|
| `A1` P0.31 | idle LOW, 1,823,490 of 1,823,490 samples | original harness, **a second harness**, and harness **fully removed** |
| `IO1` P0.17 | idle LOW, 1,822,564 of 1,822,564 samples | never wired; **and again with the RAK5802 removed**, 1,814,723 of 1,814,723 |
| `A0` P0.5 | idle HIGH, 0 of 1,852,145 low | never wired — validates the instrument |
| `SDA` P0.13 | idle HIGH, 0 of 1,848,823 low | never wired |

`A0` and `SDA` reading HIGH on the same core is what makes the LOW readings real rather than an
artefact: the census has also printed `idle HIGH` with 334 falling edges on a healthy bus
(2026-08-04 entry below).

**Pulling the RAK5802 did not lift IO1**, which refutes the hypothesis that a slot module was
holding it — `WB_IO1` is a `SLOT_A`/`SLOT_B` signal per `variant.h` :45, so that was worth testing
and it came back negative.

#### The pack is not an overvoltage source — measured, first time in this project

Operator meter readings, joined 3+5 wire **unplugged from the board**, pack live:

- **20 mV DC** to pack minus. So the pack's data line is not a 5 V or 12 V source, closing the
  oldest unsourced electrical assumption in the harness (previously flagged as never measured).
- **15 kΩ** to pack minus. A pull-down, not a driver clamping the line.

**Consequence worth acting on:** 15 kΩ against the nRF52840's ~13 kΩ internal pull-up puts the
idle line near 1.7 V, inside the undefined band between V_IL and V_IH. This bus has been marginal
by construction. An external 2.2 kΩ–4.7 kΩ pull-up to 3V3 puts idle near 2.9 V.

#### SDA baseline before the pack was connected

Qualified **before** any code selected it, unlike A1 which was chosen by decision:
`env:owscan_sda` after full assembly with the pack disconnected — `idle HIGH`, 0 of 1,847,336
samples low. This is the first "before" measurement the project has ever taken on a one-wire pad.

#### The pack reads on SDA, and the pad survived the connect

`env:battdiag_sda` at `7ad5daa`, 10 s cycles. Two separate sessions:

```
[cycle 9]  battery : sampling confirmed — pack is reporting live values
           battery : 12.44 V  +0.00 A  100%  24.0 C
           battery : sendat FF 7E 00 15 02 01 00 01 16 03 10 02 15 BA DC 04 16 B9 00 00
                     17 B8 64 18 67 F0 00 36
```

Cycles 9–12 clean. After the pack was unmated and remated, cycles 15–19 clean at
**12.43 V, −0.01 A, 100 %, 24.0 °C**. Latched at `0x01` both times.

Cycles 1–8 and 9–14 returned `all-zero records (pack not sampled)` — the documented warm-up,
longer here than the usual ~2 cycles. `AGENTS.md` already calls that line load-bearing.

#### Field image and TTN

`env:rak4631_sda` flashed at `7ad5daa`. Console showed `sleep : 3600 s`. TTN then reported
`updated_at 2026-08-30T17:56:24Z` — the boot second — with `last_f_cnt_up 832`, `dev_addr
260C1AF4`, `session started_at 2026-08-30T04:00:02Z`. **Session restored, not rejoined.**

**Not established:** a decoded payload for this cycle (no storage integration queried), any wind
reading on this image, a board-asserted banner SHA, and sleep current.

#### Why the pads failed: still unknown, and six hypotheses are dead

Recorded so none of them is re-derived. Full reasoning in
[`reviews/2026-08-30_onewire_pin_failures.md`](reviews/2026-08-30_onewire_pin_failures.md).

| Hypothesis | Status |
|---|---|
| Driver contention (push-pull TX, no current limiting) | **Real defect, not sufficient.** `owscan` transmitted into node 001's IO1 on 2026-08-04 and 2026-08-12; 001's IO1 works. Tracked as #99 |
| Ground-reference float on host USB | **Refuted.** The buck is non-isolated, and `P−` also lands directly on the base-board `GND` pad |
| `VDD` at 5 V overdriving the pack reference | **Refuted.** RAK19007 J12 `VDD` is 3.3 V [CIT-RAK19007] |
| Miswired replacement harness | **Refuted.** Operator confirmed pin-by-pin; 3+5 joined, pin 4 alone to `VDD` |
| Slot module holding IO1 low | **Refuted.** RAK5802 removed, IO1 still 100 % low |
| Electrostatic discharge during handling | **Weak.** Operator reports ~66 % ambient RH, no carpet, no pets |

Three cores have now shown a dead IO1 while node 001 — from the same order — reads its pack on
IO1 in the field. An empty baseboard measures open and two baseboards behaved identically with a
core installed, so the short is on the core side.

**The gap that matters: no core's IO1 has ever been measured before installation.** Every reading
is post-hoc, so "arrived shorted" cannot be told from "shorted here." Incoming test on every new
core — `IO1` to `GND` on the core connector, expect megohms — is the cheapest thing that would
close it.

**Node 002 is not cleared for the field.** One pack read and one uplink is not a soak, and the
pad-failure mechanism is unexplained.

### 2026-08-30 — Node 002 reads the RAK9154 over A1 and the uplink lands at TTN

**Host:** Heliotrope Ridge. **Commit:** `a48e996eac974670d6f722c06c0d150962f5a744`
(`env:rak4631_a1`, `FEATURE_BATTERY_PIN_A1=1`), asserted by the build and DFU inputs and by
`flash.sh`'s post-upload PID check — **not read back from a boot banner**, because the field
image sleeps and detaches USB before a capture can attach.

**Core identity: not captured.** The die ID was not read before the node went to sleep, so this
entry is pinned to the *node* (`puma-concolor-002`, `DevEUI 42BB96EF76E200F2`) and not to a
physical part. Said explicitly per `.cursor/rules/03-bench-claims.mdc` rather than implying a
continuity that was never established — see the correction below.

**Physical state:** RAK9154 pack wired to `WB_A1` (P0.31) per [`HARDWARE.md`](HARDWARE.md).
**RK900 not answering** — see below.

#### What the network recorded

```
puma-concolor-002  dev_addr 260C1AF4  f_port 2  f_cnt 448
2026-08-30T15:08:48Z  gateway 3356-gateway-002  RSSI -101  SNR 8  SF7  905.1 MHz
frm_payload  17b86415ba04e616b9fffe186700dc   (15 bytes)
```

Decoded: **12.54 V, −0.02 A, 100 %, 22.0 °C** — channel 21 `0xba` voltage, 22 `0xb9` current,
23 `0xb8` capacity, 24 `0x67` temperature.

This is the first RAK9154 reading this project has taken over **A1** rather than IO1, and it
went end to end: pack → one-wire on P0.31 → encoder → radio → TTN → decoder.

#### The RK900 answers once the harness is assembled

Later the same morning, with the node fully assembled, `env:stage1` (RK900 only, no radio, no
sleep) at commit `dd36e63d5f198195213530104d8b379afa9362f4`:

```
[cycle 2]
   RK900   : raw 0x0000-0x0004 = 0000 0000 0100 01BA 275F
   RK900   : wind 0.00 m/s @ 0 deg, 25.6 C, 44.2 %RH, 1007.9 hPa
   wait    : 60 s (sleep disabled)
```

Full five-register read at 9600 8N1, slave `0x01`, and the raw words corroborate the decode:
`0x0100` → 25.6 °C, `0x01BA` → 44.2 %RH, `0x275F` → 1007.9 hPa. **Wind 0.00 m/s at 0° is a real
reading from a still anemometer on a bench, not a fabricated zero** — registers `0x0000` and
`0x0001` are genuinely zero and the driver reported a successful read, which is a different
outcome from the null recorded below.

So **both sensors answer on this node**: the RAK9154 over A1 and the RK900 over RS-485. The node
was returned to `env:rak4631_a1` afterwards.

The console did not print a banner in either capture (`banner_commit=NOT OBSERVED`) because the
capture attached mid-cycle, so the commit above is asserted from the flash inputs rather than
read back from the board.

#### Earlier the same day, before assembly: the wind field was null, and the payload said so

The uplink is **15 bytes carrying 4 fields**; a full one is 35 bytes carrying 9. No wind channel
is present at all. That is the null policy behaving correctly — a failed RK900 read is omitted,
never encoded as zero — so the absence is itself a true reading. **The cause was simply that the
harness was not assembled yet**; once it was, the RK900 answered (above). Worth keeping as a
paired example: the same firmware omitted the field when the sensor could not answer and encoded
a genuine `0.00 m/s` when it could, which is exactly the distinction the null policy exists to
preserve.

#### Correction to the entry below

The entry below describes converting a RAK4631-R core from RUI3 over SWD. **That core was
discarded by the operator as unreliable and is not the core in node 002.** Its conversion
procedure and the mechanisms it established remain valid and are worth keeping
([`FIRST_FLASH.md`](FIRST_FLASH.md)); its board-level results describe a part that no longer
exists. Neither entry captured a die ID, which is exactly why the two could be conflated
([#97](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/97)).

**Not established here:** whether this core's IO1 is intact — A1 was chosen by decision, not by
measurement, and the three cores that failed on IO1 were never root-caused
([#96](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/96)).
Also not established: any wind reading, sleep current, or a board-asserted banner SHA.

### 2026-08-30 — A RAK4631-R (RUI3) core is converted to the Arduino bootloader over SWD and runs v0.4.4

**Host:** Heliotrope Ridge. **Commit:** `31805bad9f6470765a6ea4f6179c878ad3f70bbb` (`v0.4.4`),
asserted by the build and DFU inputs and by `flash.sh`'s post-upload PID check, **not read back
from the boot banner** — the field image sleeps and detaches USB before a capture can attach.
**Physical state:** a RAK4631-R module screwed to its base board, USB-C to the build host,
RAKDAP1 on SWD. **No RK900 and no RAK9154 attached** — no sensor claim is made here.

> **This core was later discarded.** It kept dropping off USB and SWD and the operator replaced
> it; it is **not** the core in node 002. No die ID was captured here, so nothing in this entry
> can be matched to a surviving part. The **procedure** below — the SWD conversion and the two
> mechanisms it establishes — is core-independent and remains valid. The board-level results are
> history ([#97](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/97)).

**What was established:** a board bought as RAK4631-R can be converted to this project's
bootloader in place, and the converted core runs the field image.

#### The conversion

The replacement core enumerated as **`1915:521F`** (RUI3), which neither `adafruit-nrfutil` nor
Nordic `nrfutil dfu` could program. The Arduino S140 bootloader was written over SWD:

```
** Programming Finished **
** Verify Started **
** Verified OK **
```

197 s at 1000 kHz. Two mechanisms had to be understood first, both recorded with their
citations in [`FIRST_FLASH.md`](FIRST_FLASH.md):

- OpenOCD's nRF5 driver executes its flash loader **on the target**, which a mass-erased part
  cannot do — it double-faults into lockup at `pc 0xfffffffe`. `configure -work-area-size 0`
  denies it the RAM scratch area and forces direct NVMC writes ([CIT-OPENOCD-NRF5], `nrf5.c:1143-1145`).
- The software half of APPROTECT re-arms on **every** reset unless running firmware writes
  `SwDisable`, so any reset between erase and program re-locks the part mid-recovery
  ([CIT-NRF-APPROTECT]).

UICR read back correct: `NRFFW[0] = 0x000F4000` (bootloader), `NRFFW[1] = 0x000FE000` (MBR
params). Post-conversion the board enumerated **`239A:0029`** — `WisBlock RAK4631`, UF2
bootloader, no application — confirming the conversion rather than inferring it.

#### Flash

```
=== FLASH OK ===
host:   Heliotrope Ridge
commit: 31805bad9f6470765a6ea4f6179c878ad3f70bbb
port:   /dev/cu.usbmodem31201
usb:    239A:8029 (application running)
```

#### A debug-power failure that reads as a dead chip

Between the conversion and the flash, every SWD attempt failed **after** `SWD DPIDR 0x2ba01477`
read cleanly:

```
Debug: DAP: wait CDBGPWRUPACK
Debug: DAP: poll 4 timeout
Debug: Command 'dap init' failed with error code -5
```

pyOCD failed the same way at the same point. `DPIDR` answering while `CDBGPWRUPACK` never
arrives means the debug port's always-on logic is alive but the die has no core power — and it
looks exactly like a destroyed chip, because the probe appears to be talking to something. **A
power cycle of the base board restored it**: on the 13th retry the same command returned
`Cortex-M4 r0p1 processor detected`, and the `239A` USB device reappeared on the same attempt.
No damage. Recorded because the misreading cost an hour and the correct test — power-cycle,
then retry — takes ten seconds.

**Not established here:** anything about sensors, IO1 continuity on this core, sleep current,
or a board-asserted banner SHA. `FEATURE_BATTERY_PIN_A1` defaults to 0, so this image drives the
one-wire link on WB_IO1; whether **this** core's IO1 is intact is untested and remains open in
[#96](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/96).

### 2026-08-28 — Node 002 runs v0.4.4 without sensors: bounded failures, session checkpoint/restore, TTN delivery, sleep

**Host:** Heliotrope Ridge. **Commit:** `9e2fcb4a14a6db21165ee2ce23502c544673d99d`
(`v0.4.4`), asserted by the build/DFU inputs and immediate pinned-port capture, **not read
back from the boot banner**. `capture.py` attached just after the banner and correctly reported
`banner_commit=NOT OBSERVED`; this entry therefore does not claim a board-asserted SHA.
**Physical state:** `puma-concolor-002` RAK4631 and LoRaWAN antenna only; no RK900, RAK9154,
or completed field wiring.

**What was measured:** the full `rak4631` image compiles with node 002's ignored credentials,
the credentials match both TTN EUIs without reversal, DFU leaves the RAK application PID
running, absent sensors do not livelock the wake path, a session checkpoint survives an
identical reflash/reset, the post-reset proof-of-life uplink reaches TTN, and the node reaches
sleep.

#### Build and flash

```
=== BUILD OK ===
host:   Heliotrope Ridge
commit: 9e2fcb4a14a6db21165ee2ce23502c544673d99d
36 test cases: 36 succeeded
rak4631 SUCCESS

Device programmed.
USB 239A:8029 -- application running
=== FLASH OK ===
host:   Heliotrope Ridge
commit: 9e2fcb4a14a6db21165ee2ce23502c544673d99d
```

Before the flash, the offline checker compared the build host's ignored `src/secrets.h`
against the DevEUI and JoinEUI read from TTN for `puma-concolor-002` and returned
`IDENTITY_OK`. No AppKey was printed or copied.

#### First sensor-absent cycle

```
config  : interval 3600 s, boot #1
session : restored 0x260CE002, counter 96
[cycle 1]
   modbus attempt 1/3 failed (timeout)
   modbus attempt 2/3 failed (timeout)
   modbus attempt 3/3 failed (timeout)
RK900   : no data (timeout)
battery : silent at its id 1 of 3 consecutive cycles — no BOOT yet
battery : no confirmed latch — proceeding unprovisioned
battery : no data (no reply, 0 bytes)
uplink  : proof of life — no sensor data for 1 cycle(s)
session : saved 0x260CE002, resume at 128
radio   : sent 0 bytes on port 2
sleep   : 3600 s
```

TTN remained at `last_f_cnt_up=96` after this cycle. That first frame reused the counter held
by the pre-v0.4.4 session state and was not delivered; the new image nevertheless wrote the
future ceiling 128 before transmitting.

#### Reset/restore cycle and network result

An identical DFU reset the board. The next pinned capture observed:

```
config  : interval 3600 s, boot #1
session : restored 0x260CE002, counter 128
[cycle 1]
   modbus attempt 1/3 failed (timeout)
   modbus attempt 2/3 failed (timeout)
   modbus attempt 3/3 failed (timeout)
RK900   : no data (timeout)
battery : silent at its id 1 of 3 consecutive cycles — no BOOT yet
battery : no confirmed latch — proceeding unprovisioned
battery : no data (no reply, 0 bytes)
uplink  : proof of life — no sensor data for 1 cycle(s)
session : saved 0x260CE002, resume at 160
radio   : sent 0 bytes on port 2
sleep   : 3600 s
```

TTN then reported `last_f_cnt_up=128`, proving that the restored reserve produced a
network-accepted post-reset uplink.

**Verdict: PASS for the software-only node 002 baseline.** This strengthens H5/H6/H7 evidence:
session checkpoint/restore and TTN acceptance occurred across reset; both absent sensor paths
were bounded; sleep was reached twice. It does **not** exercise measured-low brownout,
intermittent one-wire validity, failed LittleFS operations, connected sensors, sleep current,
the 24 h bench soak, or the 7 d field shadow. Those remain open under
[#55](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/55),
[#90](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/90), and H8.

### 2026-08-27 — Second node exists: `puma-concolor-002` registered, flashed, and OTAA-joined. Board-side behavior NOT observed

**Host:** Heliotrope Ridge — build, USB upload, and `ttn-lw-cli`, all on the build host.
**Commit:** `1bb18d7`, **asserted from the build host's checked-out HEAD at upload time, NOT
read back from the boot banner.** The serial capture returned 0 bytes (§4), so nothing on the
board confirmed which image is executing. Per the SHA table at the top of this file that makes
the identification an assertion about the host, not about the device.

**What this entry does and does not establish.** It establishes that a *second* physical node
now has its own network identity and can join. It establishes nothing about sensors, payload,
sleep, or power on that node — none of it was observed.

#### 1. TTN registration, mirroring `puma-concolor-001`

Created with `ttn-lw-cli end-devices create`, root keys generated by TTN (`--with-root-keys`)
and written directly into `src/secrets.h` on the build host. The AppKey was never printed to a
console, a chat, or a tracked file. `src/secrets.h` is gitignored; `git status --porcelain` on
the build host was empty afterwards.

```
device_id : puma-concolor-002
dev_eui   : 42BB96EF76E200F2
join_eui  : 0000000000000000
plan      : US_902_928_FSB_2 MAC_V1_0_3 PHY_V1_0_3_REV_A
app_key   : generated, 32 hex chars
```

Every setting matches `puma-concolor-001` as read back from TTN the same day — same JoinEUI,
same frequency plan, same MAC/PHY versions, same `nam1` Network/Application/Join servers, same
RX2 (DR8, 923.3 MHz). Only the DevEUI and the AppKey differ. `001` remains `...F1` and its
`secrets.h` was preserved to `~/rak-secrets/secrets.puma-concolor-001.h` on the build host
(outside the repo, so the tree stays clean for builds).

This closes the `002` half of
[#23](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/23);
`003` is still unregistered.

#### 2. Upload

`pio run -e rak4631 -t upload`, the full field image:

```
Auto-detected: /dev/cu.usbmodem31201
Forcing reset using 1200bps open/close on port /dev/cu.usbmodem31201
Uploading .pio/build/rak4631/firmware.zip
Activating new firmware
Device programmed.
========================= [SUCCESS] Took 22.65 seconds =========================
```

#### 3. The board is running an application

```
/dev/cu.usbmodem31201
Hardware ID: USB VID:PID=239A:8029 SER=DD1823E10F746C44 LOCATION=3-1.2
```

`8029` is the application product ID; a failed DFU would have left `0029` or `002A`. This is
the check that `pio run -t upload`'s own exit status cannot provide (issue #27). It proves an
application is running; it cannot prove *which*, since a previously resident image enumerates
identically.

- CITE(datasheet): RAK-nRF52-Arduino `boards.txt` [CIT-RAK-BOARDS-TXT] — `0x8029` application PID.
- CITE(prior-art): `Adafruit_nRF52_Bootloader` `board.h` [CIT-ADA-BOOTLOADER] — `0x0029` UF2 / `0x002A` CDC-only DFU IDs.

**New hardware serial on record:** `DD1823E10F746C44`. Use it to pin
`scripts/remote.sh usbpid` once both boards are ever on the same bus (issue #29).

#### 4. OTAA join — PASS, and it is the strongest observation here

```
dev_addr      : 260CE002
started_at    : 2026-08-27T21:47:19.090255124Z
last_f_cnt_up : None
last_n_f_cnt_d: None
```

A populated `session` and `mac_state` timestamped ~50 s after the upload completed means the
node joined **on its own**, with no console and no operator action. Since the keys, the radio,
and the running image all have to be correct simultaneously for a join to complete, this is
what makes §3's "an application is running" specifically *this* image, by inference from the
network side rather than from the banner.

`last_f_cnt_up` is absent, so **no data uplink has been counted yet** — the first one lands at
the end of the first reporting cycle. No payload has been decoded from this node, and decoder
parity for it is therefore untested end to end.

#### 5. Serial: 0 bytes over 45 s — expected, not a fault

`cat /dev/cu.usbmodem31201` for 45 s produced nothing. On this image that is the designed
behavior rather than a defect: the field build sleeps between cycles and detaches USB
(`FIRMWARE_SPEC.md` §5 step 7, issue #60). The consequence for this entry is concrete and
already stated in the header — **no banner was read, so the commit is asserted from the host,
not confirmed by the device.**

An earlier attempt with `pio device monitor` failed for an unrelated reason worth recording so
the next session does not read it as a board fault: without a TTY it dies in
`serial/tools/miniterm.py` with `termios.error: (25, 'Inappropriate ioctl for device')`. Use
`stty` plus `cat` over non-interactive SSH.

#### 6. The parity gate was bypassed. Deliberately, by hand, and it is still open

`scripts/preflight.sh` **failed** and `scripts/flash.sh` therefore refused to build. The upload
above was run directly with `pio` at the operator's explicit instruction, which skipped the gate.

The gate's finding is real: the formatter in `forest-weather-machines` moved
(`717afceb…` against the pinned `9c58c2b9…`) and `scripts/check_decoder_parity.py` cannot parse
its new shape, because `3cfd281` replaced the flat `CHANNEL_NAMES` string map with a `CHANNELS`
map of objects. Parity was checked **by reading the new map by hand**: every key this firmware
emits is still present and still maps to the same `decoded_key` (`wind_speed_1` →
`wx_wind_speed`, `dc_voltage_batt_21` → `batt_voltage`, and so on), and every type it emits is
still in `WX_TYPES` with matching size, signedness, and divisor.

**A hand check is not the gate.** It is recorded here as what it is — one person reading a file
once — and the checker fix, the corrections to the now-stale zero-wind notes in
`payload/schema.yaml` and `.cursor/rules/60-decoder-parity.mdc` (upstream `169e865` removed that
nulling), and the re-pin are tracked in
[#83](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/83).

#### 7. The device had NO uplink payload formatter. Now it does, and parity is gated again

Found later the same day, and it is the defect that would have mattered most: `ttn-lw-cli
end-devices get my-app-tobi puma-concolor-002 --formatters` returned **no `formatters`
block at all**, and `applications get my-app-tobi --formatters` returned **empty**. TTN
formatters on this application are set **per device** (the header of the formatter says so —
`WIND_DIR_OFFSET` differs per install), so there was no inheritance to fall back on. Every
uplink from `002` would have landed with `frm_payload` and **no `decoded_payload`**, from a
node that looked perfectly healthy.

`001` did have one, but a **stale** revision — its header is the old single-probe
"RAK2560 WisNode Sensor Hub + RK900-09" text, predating the unified formatter.

Set from the live file, by path rather than by shell interpolation
(`--formatters.up-formatter-parameter-local-file`), and read back:

```
source formatter sha256: 717afcebeebd0a3d219aad5249bee04c0ddbcfd43059dae2a792bede4e91058b
up_formatter           : FORMATTER_JAVASCRIPT
updated_at             : 2026-08-27T22:01:57.922922751Z
```

Read back **out of TTN** and compared byte-for-byte against the source file, rather than
trusting the write:

```
up_formatter        : FORMATTER_JAVASCRIPT
stored at TTN sha256: 717afcebeebd0a3d219aad5249bee04c0ddbcfd43059dae2a792bede4e91058b
local file    sha256: 717afcebeebd0a3d219aad5249bee04c0ddbcfd43059dae2a792bede4e91058b
BYTE IDENTICAL      : True
CHANNELS map present: True
WIND_DIR_OFFSET     : ['var WIND_DIR_OFFSET = 230;']
```

**That `230` is 001's site correction, not a statement about 002's orientation**, and it is
the one value in this file that is per-install. It only affects `wind_direction`
(formatter :219-221), and 002 has no RK900 attached so no heading has been recorded wrong —
but it must be set before 002 is installed or the heading arrives rotated and plausible.
Filed as [#84](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/84).

The parity gate that §6 recorded as bypassed is **no longer bypassed**. It was fixed rather
than re-pinned around, in this order:

1. `scripts/check_decoder_parity.py` now reads the `CHANNELS` object map *and* the legacy
   `CHANNEL_NAMES` string map, and reports an unparseable map as its own named failure — a
   gate that has stopped checking must not look like a gate that found nothing.
2. All **9** emitted fields re-verified against the live map: every `(channel, type)` keeps
   its `decoded_key`, every size/signedness/divisor still matches. **No firmware change was
   needed.**
3. `scripts/check_golden_vectors.py` PASSED **21 decoded values across 5 vectors** with
   `source: live` — real encoder bytes through this exact formatter in node v26. This is the
   check a field-by-field comparison cannot make, and it is why the re-pin is not a
   hand-check.
4. Only then re-pinned to `058bd69` / `717afceb…`. `scripts/preflight.sh` reaches
   `=== PREFLIGHT OK ===`, exit 0.

**The build host had no `forest-weather-machines` clone at all**, which is why its golden-vector
run reported `source: pinned` and could never have detected upstream drift. Cloned via `gh`
(not copied between machines) and confirmed byte-identical at `717afceb…`. Closes
[#83](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/83).

**Still NOT observed:** no uplink from `002` has been decoded, because none has been sent —
see the verdict row below. The formatter is correct by construction and by golden vector, not
by a decoded frame from this node.

#### 8. Why `002` has not uplinked — designed behavior, not a fault

`last_f_cnt_up` is still absent. The one stored record is the **join**, identifiable because
its inner `received_at` is byte-identical to the session `started_at`
(`2026-08-27T21:47:19.090255124Z`); it carries no `f_port`, `f_cnt`, or `frm_payload`.

The node is **incomplete by design at this stage — no RK900 and no RAK9154 pack attached.**
With no pack reading, `power::Brownout` engages without evidence after
`kInvalidReadsBeforeInhibit = 4` cycles and `main.cpp:373` holds the uplink
(`uplink : held — no pack voltage evidence`) until
`power::kNoEvidenceKeepaliveCycles = 24` cycles have passed, at which point it sends one
*empty* keepalive. A fresh board also has no stored interval, so it runs
`kIntervalDefaultSeconds = 3600 s`, not the 900 s of the field-configured `001`.

So: nothing to diagnose here, and **a decodable uplink from this node requires the pack to be
attached.** Recorded so a later session does not read the silence as a radio or key fault.

Link margin at join is worth noting for when it is installed: **RSSI −109 dBm, SNR −0.5 dB**
to `3356-gateway-002` at SF7 — joined fine, but far weaker than `001`'s field readings of
−59 dBm / 14.5 dB.

#### Verdict

| Claim | Verdict |
|---|---|
| `puma-concolor-002` registered on TTN, distinct identity, `001` unaffected | **PASS** |
| Uplink payload formatter present on `002` and byte-identical to the live formatter | **PASS** — set 22:01:57Z, `717afceb…` |
| Payload parity by gate, against the live formatter | **PASS** — 9 fields, plus 21 golden-vector values through node |
| Field image uploaded to the new board | **PASS** |
| Board running an application (`239A:8029`) | **PASS** |
| OTAA join by `puma-concolor-002` | **PASS** |
| Which commit is executing, from the board | **NOT OBSERVED** — banner never read |
| Sensors, payload, uplink contents, sleep, power on this node | **NOT OBSERVED** — no sensors attached; §8 |
| A decoded uplink from `002` | **NOT OBSERVED** — none sent, and none will be until the pack is attached (§8) |

**Status is unchanged: `🚧 NOT YET DEPLOYED`.** A second node that joins once is not a second
node that has been soaked, and none of the H1–H8 gates moved.

### 2026-08-15 — First >24 h continuous field runtime, ended by the operator picking the node up. Watcher log deleted; raw transcripts permanently lost

> **Corrected 2026-08-15, later the same day.** This entry first recorded the silence from
> 17:04:14Z as an unexplained field anomaly with four live hypotheses. **It is explained:** the
> operator packed the node into a bag and moved it at approximately that time, which ended its RF
> path to the gateway. The silence is operator handling, not node behavior, and **no firmware
> defect is implicated.** The observations below are unchanged and stand as recorded; the
> interpretation of the silence is corrected in place, because a wrongly-recorded unexplained
> field silence sitting in this ledger is exactly the kind of false claim `AGENTS.md` warns
> propagates. The one thing that remains genuinely unexplained is the **earlier** reboot at
> 2026-08-14T22:54:32Z, which predates any handling and is split out as
> [#82](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/82).

**Host:** Heliotrope Ridge, network side only — `ttn-lw-cli` against `my-app-tobi` /
`puma-concolor-001`. **Nothing was attached to the node**: it is at the field site and was not
flashed, reset, or read over serial for this entry. **Commit:** `1c2df3c` (`v0.4.3`), asserted
from the board's own boot banner 2026-08-14T17:50:12Z and unchanged since.

Three separate things are recorded here and they must not be collapsed into one another: a
completed 24 h watcher run, a **silence beginning 55 minutes after that run closed, caused by the
operator picking the node up**, and the loss of the raw logs.

#### 1. The 24 h network-side run completed. It did not die

The earlier reading that the watcher "fell over" is **wrong, and the correction matters** — the
process was absent because it **finished**, having written its summary. Verbatim from
`summary.md`, recovered before the loss described in §3:

```
### Soak (network side) — rc-v0.4.3-1c2df3c
- Device `puma-concolor-001` / app `my-app-tobi`, observed only at TTN
- Image: firmware `UNKNOWN`, banner commit `NOT OBSERVED`.
- Duration: 86405 s of 86400 s requested.
- Uplinks observed: 89 · frame counter 2464 → 2596
- Counter steps explained by a reset (≤ 32, the stored reserve): 3
- Anomalies: 1 · TTN query failures: 2
```

Header and closing line, verbatim:

```
2026-08-14T16:09:16Z === SOAK TTN START === label=rc-v0.4.3-1c2df3c duration=86400s
2026-08-14T16:09:16Z     tree       : 1c2df3c
2026-08-14T16:09:18Z     baseline   : last_f_cnt_up=2464
2026-08-15T16:09:21Z === SOAK TTN DONE === elapsed=86405s uplinks=89 resets=3 anomalies=1 query_failures=2 f_cnt=2464->2596
```

Every non-routine line in the 374-line log, verbatim — this is the complete set:

```
2026-08-14T17:50:21Z SOAK NOTE  counter-step +26 in 971s within the 32-frame reset reserve
2026-08-14T20:58:47Z SOAK WARN ttn query failed (1 so far) -- no statement about the node
2026-08-14T21:01:17Z SOAK WARN ttn query failed (2 so far) -- no statement about the node
2026-08-14T22:10:01Z SOAK ANOMALY silence 2788s with no counter advance (limit 2700s)
2026-08-14T22:56:29Z SOAK NOTE  counter-step +18 in 2789s within the 32-frame reset reserve
2026-08-15T15:24:53Z SOAK NOTE  counter-step +2 in 2005s within the 32-frame reset reserve
```

The `+26` at 17:50:21Z is the operator's single deliberate RESET, the same press that produced
the banner — already recorded in the 2026-08-14 entry below. **The `+18` at 22:56:29Z is not
explained by anything anybody did**, and unlike the 17:04Z silence it is **not** accounted for by
handling: it predates the packing by nearly 18 h. It follows a 5539 s silence, and the frame that
ended it (`f_cnt 2528`) was heard by `3356-gateway-002` at RSSI −59 dBm, SNR 14.5 dB — a strong
link, so the gap is not a coverage story.

The step size is fully accounted for by the stored reserve and is **not** 18 transmissions. The
node had restored at 2496 after the 17:50Z press, and its first uplink then wrote the ceiling at
2496 + `kCounterMargin` = **2528**. It transmitted through 2510, went quiet, and reappeared at
**exactly 2528** — the stored ceiling, i.e. the value a reset resumes from (`session.cpp:278`).
TTN's storage holds exactly one message at 2528, and frames never sent cannot be stored. So the
+18 is the counter-margin artifact of one reset, and **what is unexplained is the reset itself and
why the node took 5539 s — about six 908 s cycles — to reappear** rather than one. Carried forward
as [#82](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/82).

#### 2. Independent TTN record, and the silence — the part the watcher's clean exit hides

Pulled from TTN's storage integration, which is a separate record from the watcher's polling and
supersedes it for per-uplink history:

```
ttn-lw-cli end-devices storage get my-app-tobi puma-concolor-001 \
    --last 32h --type uplink_message --order received_at --limit 400 --stream-output

107 stored uplinks
first  2026-08-14T13:41:45Z  f_cnt 2391
last   2026-08-15T17:04:14Z  f_cnt 2600
span   98549 s = 27.37 h
```

Discontinuities across that whole span — again, the complete set:

```
gap=  96s  2026-08-14T15:57:56 f=2400 -> 15:59:32 f=2432  delta=32
gap= 197s  2026-08-14T15:59:32 f=2432 -> 16:02:49 f=2464  delta=32
gap=  89s  2026-08-14T17:48:44 f=2471 -> 17:50:13 f=2496  delta=25
gap=5539s  2026-08-14T21:22:13 f=2510 -> 22:54:32 f=2528  delta=18
```

The first three are bench-era: sub-200 s gaps with full-reserve steps, i.e. reflash/reset
turnarounds on the bench before the node was carried out. The fourth is the field event from §1.

**Session was restored, never rejoined,** for the entire period:

```
"dev_addr": "260CE734"
"started_at": "2026-07-31T14:33:20.636657834Z"
"last_f_cnt_up": 2600
```

A `started_at` of 2026-07-31 with the counter at 2600 is the definition of a restored session —
a rejoin would have reset both.

**Recency — this is the finding that matters most.** The newest uplink TTN holds is
**`f_cnt 2600` at `2026-08-15T17:04:14Z`**. Queried at **`2026-08-15T21:34:45Z`**,
`last_f_cnt_up` was **still 2600**. That is **4 h 30 m of silence, roughly 18 missed 900 s
cycles** — far past the 2700 s anomaly threshold and past the 24-cycle bar in F6.

The cadence immediately before it stopped was metronomic, which makes the stop look like an
event rather than a drift:

```
2026-08-15T15:18:14Z f_cnt 2593        2026-08-15T16:18:50Z f_cnt 2597
2026-08-15T15:33:27Z f_cnt 2594        2026-08-15T16:33:58Z f_cnt 2598
2026-08-15T15:48:35Z f_cnt 2595        2026-08-15T16:49:06Z f_cnt 2599
2026-08-15T16:03:43Z f_cnt 2596        2026-08-15T17:04:14Z f_cnt 2600   <-- last heard
2026-08-15T14:47:58Z f_cnt 2591  ...   ~908 s wake-to-wake throughout
```

The watcher's run ended at 16:09:21Z and the silence began at 17:04:14Z, **55 minutes after the
window closed.** The run's `anomalies: 1` is therefore true of its own window and says nothing
about what followed.

**Cause: operator handling — the node was packed into a bag and moved at approximately
17:04:14Z (10:04 local), which ended its RF path to the gateway.** Operator-confirmed. The node
was never at a field site during this period; it was in a vehicle. **No firmware defect is
implicated, and the brownout, watchdog-loop, backoff and counter-ceiling hypotheses are all
withdrawn.**

Two network-side observations recorded here are consistent with that and inconsistent with the
node having protected itself or failed:

- **The pack was healthy and charging at the moment it went quiet.** `f_cnt 2600` decodes to
  `batt_voltage 11.77 V`, `batt_capacity 78 %`, `batt_current +0.02 A`. The transmit-inhibit
  threshold in `src/power.h:63` is `kTxInhibitCentivolts = 960`, i.e. **9.60 V** — the pack was
  **2.17 V above it** and rising, so `power::Brownout` cannot have engaged. The four preceding
  frames read 11.75, 11.75, 11.76, 11.76, 11.77 V: flat, not sagging.
- **The next flash write was not due.** `session.cpp:278` stores `uplink_counter + kCounterMargin`
  (32), and `counter_headroom_ok()` only writes when the live counter reaches that ceiling. Ceiling
  writes therefore landed at `f_cnt` **2528, 2560 and 2592**, the last at 15:03:06Z; the next was due
  at **2624**. The node stopped at 2600, **24 frames short of any flash write**, so the permanent-mute
  paths in [#74](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/74)
  and [#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68) —
  both of which require a failing session write — were not reachable at that frame.

The link was also strong right up to the stop: `f_cnt 2600` was heard by `3356-gateway-002` at
**RSSI −76 dBm, SNR 13.75 dB, SF7BW125**, matching the preceding frames (−75 to −77 dBm, 9.75 to
14 dB). There was no degradation before the silence.

#### 3. What was lost — a monitoring failure recorded as an outcome

`$HOME/soak-runs` was deleted by the operator during a tidy-up, deliberately and reasonably:
project data had no business living in a home folder, where it is indistinguishable from junk.
The directory was found in `~/.Trash` and copied out intact — both run directories, the
abandoned duplicate, correct sizes and line counts. **It was then lost anyway**, and the honest
sequence is:

```
14:33Z  ~/.Trash/soak-runs copied to ~/soak-runs.recovered — intact, verified by listing
        20260813T202735Z_ttn_rc-v0.4.2         events.log 33745 B, 308 lines
        20260814T160916Z_ttn_rc-v0.4.3-1c2df3c events.log 43343 B, 374 lines, summary.md 864 B
        ABANDONED_duplicate_20260814T162332Z   events.log   404 B,   5 lines
~14:35Z the Trash was emptied
14:36Z  the recovered copies were removed by an agent before the copy into the repo was
        confirmed — the copy had failed, because its source in the Trash was already gone
```

Recovery was then attempted and **failed**: `tmutil listlocalsnapshots /` holds only
`com.apple.os.update-*` snapshots with no data snapshot, and `tmutil listbackups` returns
`Failed to mount destination`. **The raw `events.log` files are permanently gone.**

What survives, and it is most of the value:

| Artifact | State |
|---|---|
| `rc-v0.4.3-1c2df3c` summary, header, closing line, all 6 non-routine lines | **Preserved verbatim above** |
| Its ~360 routine `SOAK UPLINK` / `SOAK HEARTBEAT` lines | **Lost.** Superseded by the TTN storage series in §2, which is independent and finer-grained |
| `rc-v0.4.2` (`572bcfa`) 19.03 h run — 308-line `events.log` | **Lost in full.** Its measured result survives in the 2026-08-14 entry below (19.03 h, 76 uplinks, 0 anomalies); the transcript does not |
| TTN-side history | **Never at risk** — held by TTN, re-pullable |

The lesson is in the code now, not in a caveat: `scripts/soak_ttn.sh` writes to
`<repo>/soak-runs/` as of this commit, matching `scripts/soak.sh` and the existing `.gitignore`
entry. A prior worker documented the `$HOME` path as a gotcha instead of fixing it.

#### 4. Monitoring restarted

A 7 d field-shadow watcher was started from the repo-local path under `screen`, so it survives an
SSH disconnect, and **verified by reading its `events.log`, not by observing a live process** —
the check the 2026-08-12 false-start entry taught.

```
soak-runs/20260815T214053Z_ttn_field-shadow-1c2df3c/events.log

2026-08-15T21:40:53Z === SOAK TTN START === label=field-shadow-1c2df3c duration=604800s
2026-08-15T21:40:53Z     image      : firmware=v0.4.3 banner_commit=1c2df3c
2026-08-15T21:40:53Z     tree       : b063567
2026-08-15T21:40:54Z     baseline   : last_f_cnt_up=2600
2026-08-15T21:46:59Z === SOAK HEARTBEAT 1 === elapsed=365s of 604800s uplinks=0 f_cnt=2600 ...
```

The file grew from 5 lines to 6 across two reads six minutes apart, so it is recording. It also
now **names the image it is soaking** — `firmware=v0.4.3 banner_commit=1c2df3c` instead of the
`UNKNOWN` / `NOT OBSERVED` the previous run recorded.

**It had not yet logged an uplink when started:** `uplinks=0` with `f_cnt=2600` unchanged is the
watcher correctly reporting the §2 silence. A "prove it recorded a real uplink" check cannot pass
while the node is off the air, and claiming otherwise would be the exact failure this ledger
exists to prevent. Given the §2 correction, this watcher was started against a node that was in a
bag rather than at a field site — so **the 7 d field-shadow clock does not start from this run's
header.** F1 begins when the node is actually deployed and transmitting again.

`git status --porcelain` on the build host is **empty with this run live** — the artifacts are
inside the repo and ignored, not scattered and not committed.

#### Verdict

- **First >24 h of continuous runtime this project has had — real, and the headline of this
  entry.** 27.37 h of TTN-recorded uplinks on banner-verified `1c2df3c`, `f_cnt` 2391 → 2600,
  107 stored uplinks, cadence metronomic at ~908 s throughout, session `started_at 2026-07-31`
  restored and never rejoined. **It ended by being picked up, not by failing.**
- **No recency FAIL, because the run was ended by handling.** The silence from 17:04:14Z is the
  operator packing the node into a bag; F6 ("no period of silence longer than 24 cycles") is not
  assessable across an interval when the node was in a vehicle, and neither is F1. **The 7 d
  field-shadow half of H8 has not started** — day one has yet to be run with the node actually
  deployed.
- **One thing here remains unexplained and it is not the silence:** the reboot at
  2026-08-14T22:54:32Z. Split out as
  [#82](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/82) so it
  survives the closure of
  [#80](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/80).
- **The ≥24 h *bench* half of H8 is NOT met, and this run does not move it.** H8 wants ≥24 h on
  the bench *and* ≥7 d of field shadow. This was in the field, with nothing attached — so of the
  ten bench criteria in [`SOAK.md`](SOAK.md), only B1 (full duration) and part of B5 (frame
  counter continuity) can be evaluated at all. B2 watchdog resets, B3 unexpected reboots, B4
  cycles seen, B6 cycles without battery, B7 pack voltage and brownout, B9 keepalive — **all
  need the console, and there was no console.** B8 sleep current remains unmeasured
  ([#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8)).
  The prior bench run reached 19.03 h on `572bcfa`, a **superseded image**, and was stopped
  deliberately; a partial run on one image cannot be topped up by a different run on another.
  **Zero completed bench soak hours exist on `1c2df3c`.** Anyone reading "24 h" off this entry
  and calling H8's bench half closed is misreading it.
- **Project status stays `🚧 NOT YET DEPLOYED`.** Both halves of H8 are open: the bench half has
  zero completed hours on `1c2df3c`, and the field-shadow half has not begun.

CITE(spec): `docs/FIRMWARE_SPEC.md` §7 H8 — ≥24 h bench soak **and** ≥7 d field shadow before
field trust; this entry is measured against that bar and does not close it.
CITE(policy): CIT-TTN-FUP, [`CITATIONS.md`](CITATIONS.md) — the 900 s cadence observed here is
what keeps the node inside the 30 s/24 h uplink airtime budget.
CITE(prior-art): `src/session.h:41` `kCounterMargin = 32` — the reserve that makes a counter step
of +18 or +26 one uplink after a reset rather than 18 or 26 transmissions.
CITE(bench): `docs/EVIDENCE.md` 2026-08-15, this entry — `dev_addr 260CE734`, `last_f_cnt_up`
2464 → 2600 observed at TTN, and `f_cnt 2600` at 2026-08-15T17:04:14Z as the newest uplink.

### 2026-08-14 — `1c2df3c` **read back from the board's own banner**, and six unattended 900 s cycles

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`. **Commit, asserted from the device:**
`1c2df3c`. **Environment:** `env:soak` (byte-identical to `env:rak4631`,
[ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)). **Raw capture:** `/tmp/banner.log`
on the build host, 81 lines. **Verdict: pass — image identity confirmed, cadence confirmed, both
sensors confirmed.**

**This closes the one gap the entry below leaves open.** That entry recorded, correctly, that the
running image's SHA was asserted only by the build-and-flash tooling and never read off the
device — `239A:8029` proves *an* application is running, not *which*. The operator pressed RESET
**once** at 10:50:12 build-host local (UTC-7) = **17:50:12Z** while a capture was held open,
specifically to make the board print its banner. It did:

```
2026-08-14 10:50:12 === rak-sensor-node ===
2026-08-14 10:50:12 firmware : 0.4.3
2026-08-14 10:50:12 commit   : 1c2df3c
2026-08-14 10:50:12 built    : Aug 14 2026 09:00:34
2026-08-14 10:50:12 features : rk900=1 battery=1 radio=1 sleep=1 wdt=1
2026-08-14 10:50:12 interval : bench=0, bounds 900-86400 s, default 3600 s
2026-08-14 10:50:12 region   : US915 sub-band 2
2026-08-14 10:50:12    config  : interval 900 s, boot #3
2026-08-14 10:50:12    session : restored 0x260CE734, counter 2496
```

`commit   : 1c2df3c` **matches the SHA that was flashed**, with no `-dirty` suffix and not
`unknown`, so per the table at the top of this file the SHA is **asserted, not inferred**. This is
the **third distinct commit** ever to name itself off the board — after `d568574` (the first, and
the first entry here that did not have to infer a SHA) and `65f8615`. It is *not* the second; the
`65f8615` entry below is banner-asserted too, and any later document saying "second" is wrong.

**Six consecutive unattended cycles on the 900 s cadence.** Cycles 3 through 8 ran with nobody
touching the board, spanning 09:33:04 → 10:48:51 local (16:33:04Z → 17:48:51Z), about **1 h 15 m
of continuous correct cycling**. Wake-to-wake spacing measured from the `[cycle N]` stamps is
908, 908, 907, 908, 908 s against a 900 s target — the ~8 s excess is the cycle's own awake time,
which the `sleep : 900 s` line confirms is scheduled *after* the work, not inclusive of it. Two
representative cycles verbatim, first and last of the unattended run:

```
2026-08-14 09:33:04 [cycle 3]
2026-08-14 09:33:04    RK900   : raw 0x0000-0x0004 = 0000 0000 00FD 0254 2727
2026-08-14 09:33:04    RK900   : wind 0.00 m/s @ 0 deg, 25.3 C, 59.6 %RH, 1002.3 hPa
2026-08-14 09:33:04    battery : pack answered at 0x01 — skipping provisioning
2026-08-14 09:33:04    battery : 11.76 V  -0.01 A  78%  23.0 C
2026-08-14 09:33:04    radio   : sent 35 bytes on port 2
2026-08-14 09:33:12    sleep   : 900 s

2026-08-14 10:48:43 [cycle 8]
2026-08-14 10:48:43    RK900   : raw 0x0000-0x0004 = 0000 0000 00EB 0289 2728
2026-08-14 10:48:43    RK900   : wind 0.00 m/s @ 0 deg, 23.5 C, 64.9 %RH, 1002.4 hPa
2026-08-14 10:48:43    battery : pack answered at 0x01 — skipping provisioning
2026-08-14 10:48:43    battery : 11.75 V  -0.01 A  78%  23.0 C
2026-08-14 10:48:43    radio   : sent 35 bytes on port 2
2026-08-14 10:48:51    sleep   : 900 s
```

**Every one of the eight cycles in the capture closed `sleep : 900 s`** — the field sleep path,
never `wait : N s (sleep disabled)` — and every one sent `35 bytes on port 2`. The sensors track
a real afternoon rather than repeating a cached value: RK900 25.3 → 25.4 → 25.3 → 24.3 → 23.1 →
23.5 °C and 59.6 → 64.9 %RH across the six cycles, pressure steady at 1002.3–1002.4 hPa, wind
calm throughout. The pack reads 11.76 → 11.75 V, −0.01 A, 78 %, 23.0 °C, latched at `0x01` on
every single cycle.

**The RESET was deliberate and is recorded as an interruption, not as uptime.** The 24 h soak on
this image was already running when the button was pressed, so this run is **not** 24 h of
uninterrupted runtime and must never be read as such. The soak's own `events.log` caught the same
event independently:

```
2026-08-14T17:50:21Z SOAK UPLINK f_cnt=2496 delta=26 gap=971s total=7
2026-08-14T17:50:21Z SOAK NOTE  counter-step +26 in 971s within the 32-frame reset reserve --
2026-08-14T17:50:21Z     one uplink after a reset, not 26 transmissions (session.cpp:278); resets=1
2026-08-14T17:50:21Z === SOAK HEARTBEAT 20 === elapsed=6064s of 86400s uplinks=7 f_cnt=2496 resets=1 anomalies=0 query_failures=0
```

Two things fall out of that, both new. First, the network-side `f_cnt=2496` is the **same number**
the banner printed as `session : restored ... counter 2496` nine seconds earlier — serial and TTN
agree on the counter across a reset, from two independent observers. Second, **the frame-counter-step
fix `7b03d3a` is now observed on hardware**: it classified the +26 jump as one uplink after a reset
consuming the 32-frame reset reserve, and held `anomalies=0`, rather than reporting 26 phantom
transmissions. That fix was previously "believed correct, unobserved."

**Post-reset cycle 1 spent no BOOT on a healthy pack**, which is the behaviour `ec9725a` (the #75
fix) was written to produce:

```
2026-08-14 10:50:12 [cycle 1]
2026-08-14 10:50:12    RK900   : wind 0.00 m/s @ 0 deg, 23.6 C, 65.1 %RH, 1002.4 hPa
2026-08-14 10:50:12    battery : pack answered at 0x01 — skipping provisioning
2026-08-14 10:50:12    battery : sampling confirmed — pack is reporting live values
2026-08-14 10:50:12    battery : 11.75 V  -0.01 A  78%  23.0 C
2026-08-14 10:50:12    session : saved 0x260CE734, resume at 2528
2026-08-14 10:50:12    radio   : sent 35 bytes on port 2
2026-08-14 10:50:20    sleep   : 900 s
```

Note there is **no null cycle after this boot** — `sampling confirmed` on the very first cycle,
because the pack was already sampling from the preceding run. The "expect ~2 null cycles after
boot" note in `AGENTS.md` describes a pack that has just been powered, not one that has merely
been reset.

**What this run does NOT establish, and this is the honest limit of it.** A grep of the whole
81-line capture for `brownout`, `provId`, `BOOT this`, `no confirmed latch`, `Unsampled`,
`rejoin`, `keepalive` and `silent at` returns **nothing**. So:

- **The `#75` defect condition never arose.** Every cycle got `pack answered at 0x01`; there was
  not one transient probe miss in eight cycles. The run shows the healthy path working and the
  BOOT correctly *unspent*, which is consistent with `ec9725a` — it does **not** exercise the
  consecutive-miss counter the fix actually adds. The fix remains believed correct on its own
  failure gate.
- **The `#62` re-latch path was never entered.** No `provId 0xFF` anywhere, and the pack **kept**
  its `0x01` latch straight through the RESET, so no re-latch was ever needed. `#62` stays open
  and stays unproven.
- **The brownout path (`#61`'s fix) was never entered.** No `brownout engaged` line, because the
  pack sat at 11.75–11.76 V all afternoon. Nothing here speaks to it.
- **The rejoin and keepalive paths were never entered.** The session was *restored*, not rejoined,
  and every cycle produced a real uplink so no keepalive was due.

**`H8` is NOT met and is not advanced by this entry.** The requirement is a **≥24 h uninterrupted
bench soak** plus a **≥7 d field shadow**. The soak on this image stood at 6064 s (1.68 h) with a
deliberate reset inside the window at the last read, and 1 h 15 m of clean cycling is not 24 h.
The `572bcfa` run reached 19.03 h and was **stopped deliberately** to ship the `#75` fix — that
outcome is recorded in its own entry below and is **not** restated as new evidence here, nor can a
partial run on one image be topped up by another. The field deployment beginning today is the
**start** of the 7 d shadow, day zero of seven, not its completion. Status stays
`🚧 NOT YET DEPLOYED`.

**Process note worth one sentence.** The capture opens with the harness refusing rather than
producing garbage:

```
2026-08-14 09:26:55 === CAPTURE REFUSED /dev/cu.usbmodem31201 held by ...Python(31754) ===
```

Port contention has cost this project a great deal of time, usually by a second reader silently
producing a truncated or interleaved log that then gets believed. `scripts/capture.py` naming the
holding pid and declining is the correct behaviour; the fix is to kill the prior capture and
**confirm it is gone** before reading a quiet log as silence.

### 2026-08-14 — `1c2df3c` flashed as `env:soak`; both sensors and `sleep : 900 s` observed. Banner SHA still NOT read back.

**Host:** Heliotrope Ridge. **Commit built and flashed:** `1c2df3c`. **Environment:** `env:soak`
(byte-identical to `env:rak4631`, [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)).
**Verdict: flash confirmed, image identity NOT confirmed from the device.**

**Why this flash happened:** the operator is taking the node to the field today and chose to ship
the `#75` BOOT-allowance fix (`ec9725a`) rather than the longer-soaked `572bcfa`, accepting fewer
soak hours on the shipping image. This is the image that goes to the woods.

**What is established.** `scripts/flash.sh` completed `=== FLASH OK ===` at 15:59:31Z against
`/dev/cu.usbmodem31201`, and the board came back on the bus as an **application**, not a
bootloader:

```
=== FLASH OK ===
commit: 1c2df3c0d7f45b23f6feae65e4c87bdfd49330dc
usb:    239A:8029 (application running)
```

Re-checked independently ~10 min later, still `idProduct = 32809` (`0x8029`) with product string
`WisCore RAK4631 Board`. Per `docs/FIRST_FLASH.md` a board with no valid application stays in its
bootloader and enumerates `0029`/`002A`, so `8029` establishes that *an* application is running
and that the DFU write did not leave the board unprogrammed.

**What is NOT established, and this is the important line.** **The boot banner was never
captured, so the running image's SHA is asserted only from the build-and-flash tooling, not read
back from the device.** `AGENTS.md` treats a banner-asserted SHA as the standard precisely
because `8029` cannot distinguish the newly written image from a previously resident one. Three
capture attempts returned **0 bytes**:

- `pio device monitor` cannot be used non-interactively — it constructs a `miniterm` `Console()`
  and dies with `termios.error: (25, 'Inappropriate ioctl for device')` when stdout is redirected.
  Use a raw `cat` on the port instead.
- Two raw `cat` captures (100 s and 200 s) read nothing. The port stayed present throughout, and
  the firmware only writes during a cycle, so both windows fell inside the 900 s sleep. A capture
  intended to catch a cycle must therefore span **>900 s**, not the 180 s USB grace.
- A backgrounded capture launched over SSH with `nohup ... &` was **dead** when checked: it did
  not survive the session closing. Run the capture in the foreground, or under `screen`.

The banner prints only at boot (`src/main.cpp:143`) and there is no console command to re-request
it, so **settling this requires one single press of RESET while a >900 s capture is held open** —
a single press reboots the application; a double-tap enters DFU and would not run it. That press
was requested several times during this session and did not happen, so the entry is filed
honestly rather than left to imply a verification that was not performed.

**Correction, 16:17:56Z — both sensors and the sleep line have since been observed on this
image.** The paragraph that stood here said neither had been, which was true when written and is
now superseded by a capture that succeeded. The three failures above were diagnosed correctly: a
capture must span **>900 s** to catch a cycle. `scripts/capture.py` does exactly that — it
reattaches across the sleep-time USB drop — and run for 1500 s it caught a full cycle:

```
2026-08-14 09:17:56 [cycle 2]
2026-08-14 09:17:57    RK900   : raw 0x0000-0x0004 = 0000 0000 00FB 0255 2726
2026-08-14 09:17:57    RK900   : wind 0.00 m/s @ 0 deg, 25.1 C, 59.7 %RH, 1002.2 hPa
2026-08-14 09:17:57    battery : pack answered at 0x01 — skipping provisioning
2026-08-14 09:17:57    battery : 11.76 V  -0.01 A  79%  23.0 C
2026-08-14 09:17:57    radio   : sent 35 bytes on port 2
2026-08-14 09:18:04    sleep   : 900 s
```

So on the image now on the board: **both sensors read in one cycle** — RK900 at 25.1 °C /
59.7 %RH / 1002.2 hPa / calm, and the RAK9154 pack live at 11.76 V, −0.01 A, 79 %, 23.0 °C,
already latched at `0x01` — and **the cycle closes `sleep : 900 s`**, the field sleep path, not
`wait : N s (sleep disabled)`. The pack answered without a BOOT being spent, which is the
behaviour `ec9725a` was written to produce, though a healthy pack does not exercise the fix's
failure gates.

This capture also **cross-confirms the network record**: the serial cycle at 16:17:56Z is the
same event the soak logged from TTN as `f_cnt=2465` at 16:19:24Z. Two independent observers, one
cycle.

**It still does not identify the image.** The banner prints only at boot, and `[cycle 2]` shows
this was a *wake*, not a boot — the cycle counter carried over, so a wake does not reprint the
banner. Everything above would look identical on a resident older build. The banner remains the
one outstanding check, and it still needs a single RESET press.

**The flashed image does transmit.** A 24 h soak was started on it at 16:09:16Z
(`~/soak-runs/20260814T160916Z_ttn_rc-v0.4.3-1c2df3c/`, label `rc-v0.4.3-1c2df3c`, baseline
`last_f_cnt_up=2464`) and its first uplink was **read from `events.log`, not assumed from the
process running**:

```
2026-08-14T16:19:24Z SOAK UPLINK f_cnt=2465 delta=1 gap=607s total=1
2026-08-14T16:19:24Z === SOAK HEARTBEAT 2 === elapsed=607s of 86400s uplinks=1 f_cnt=2465 resets=0 anomalies=0 query_failures=0
```

That is a real uplink delivered to TTN after the flash, with `resets=0`, so the image on the board
joins and transmits. It still does not identify the image — a resident older build would also
transmit — which is why the banner remains the outstanding check. **The soak has minutes, not
hours: no soak-hour claim is made here, and `H8` is untouched by it.** Its outcome must be written
only once it ends.

**A second soak was started by mistake and has been stopped.** Two workers each launched
`soak_ttn.sh` against the same device, at 16:09:16Z (pid 28695) and 16:23:32Z (pid 31838).
Two pollers reading one device is not additive evidence — they double the TTN query rate and
each sees the other's uplinks, so the later run was killed and the surviving run is the **earlier
and longer** one, `~/soak-runs/20260814T160916Z_ttn_rc-v0.4.3-1c2df3c/`. The `latest-ttn` symlink
was repointed back to it, and the abandoned directory is kept as
`~/soak-runs/ABANDONED_duplicate_20260814T162332Z` rather than deleted. **Exactly one soak is
running.** The lesson is the same one `scripts/flash.sh` and `scripts/push.sh` already encode:
check for a running soak before starting work, because a `pgrep` taken minutes earlier is stale.

**Two process defects were found the expensive way** and both are fixed in this range. A flash
window was lost to a poller whose "port appeared" line was misread as empty. A second window was
lost to the new `scripts/flash.sh` soak guard **firing with no soak running** — it grepped
`remote.sh`'s own echoed command line, which contains the pattern string, and named
`Running on Heliotrope Ridge: pgrep -fl ...` as the offending process. Fixed in `1c2df3c` by
anchoring on a leading pid. A false refusal on a deadline is worse than no guard, because the
override becomes reflexive.

### 2026-08-14 — 19.03 h of clean soak on `572bcfa`, stopped deliberately at 76 uplinks and 0 anomalies

**Host:** Heliotrope Ridge. **Tree soaked:** `572bcfa` (`v0.4.2`). **Run directory:**
`~/soak-runs/20260813T202735Z_ttn_rc-v0.4.2/` — **preserved, not deleted.** **Verdict: partial
pass, superseded on purpose.**

**These are the first soak hours this project has ever accumulated.** The prior H8 row in this
file said "zero soak hours exist"; that is now wrong and is corrected above.

**What was measured:** whether the field image transmits on its 900 s cycle, unattended, for a
long run — the sleep, radio and power paths under wall-clock time rather than one cycle.

**Raw observation**, read from `events.log` (33745 bytes) at the moment of the stop, not
inferred from the process existing:

```
2026-08-13T20:27:35Z === SOAK TTN START === label=rc-v0.4.2 duration=86400s device=puma-concolor-001
2026-08-13T20:27:35Z     image      : firmware=0.4.2 banner_commit=dcd6807
2026-08-13T20:27:35Z     tree       : 572bcfa
2026-08-13T20:27:36Z     baseline   : last_f_cnt_up=2272
2026-08-14T15:29:28Z SOAK UPLINK f_cnt=2398 delta=1 gap=969s total=76
2026-08-14T15:29:28Z === SOAK HEARTBEAT 228 === elapsed=68511s of 86400s uplinks=76 f_cnt=2398 anomalies=0 query_failures=1
```

**Final numbers: elapsed 68511 s (19.03 h) of 86400 s, 76 uplinks, `f_cnt` 2398 from a baseline
of 2272, 0 anomalies, 1 query failure.** Zero anomalies across 19 h means no gap ever exceeded
the 2700 s silence threshold. The single query failure was a TTN API read, not a missed uplink.

**Why it was stopped short of 24 h:** the operator is taking the node to the field today and
chose to ship the `#75` BOOT-allowance fix rather than the image with the longer soak — an
explicit trade of soak hours for a battery-path fix, made with the alternative on the table. The
run was ended by `kill 76549` at ~15:30Z with its directory left intact, because 19 clean hours
on the sleep, radio and power paths is real evidence and those paths are unchanged in the
shipping image.

**What this does NOT establish.** `H8` requires ≥24 h, so **`H8` remains open** — 19.03 h is not
24 h, and a partial run cannot be topped up later by a different image. It says nothing about
the battery path in the shipping build: the `#75` fix is not in `572bcfa`. The soak reads TTN
ingest, so it proves uplinks *arrived*; it did not read the board's serial console and therefore
asserts nothing about which SHA was running beyond the `dcd6807` banner captured at the earlier
flash — the log flags this itself, and `572bcfa` is `dcd6807` plus documentation only.

**Unrelated but worth recording:** the build host dropped off the network entirely for ~50 min
(≈14:40Z–15:29Z, SSH and ICMP both, `uptime` afterwards showing 47 days so it never rebooted).
The soak was unaffected — it kept logging throughout and `query_failures` stayed at 1 — so the
outage was between the workstation and the host, not the host and TTN. A session that loses the
host cannot push at all, since the workstation's own `git push origin` is 403.

### 2026-08-12 (night) — Ten fixes landed. COMPILE-VERIFIED ONLY. No hardware ran any of them.

**Host:** Heliotrope Ridge. **Range:** everything after `2e89c93` through the `v0.4.1` release
commit. **Board state during this work:** RAK4631 asleep, USB detached, unreachable — no serial
port was opened and no image was flashed.

**What is claimed:** these commits compile. `pio run -e rak4631`, `-e soak` and `-e battdiag`
each returned `SUCCESS` in a throwaway worktree on the build host. That is the whole claim.

**What is NOT claimed:** nothing here has been observed running. Ten behavioural fixes — the
no-evidence brownout hold that disabled its own exit (#61), keepalive frame-counter starvation,
the ungated boot-counter flash write, sub-band re-selection after the rejoin escape, downlink
length checking (#63, #64), a set-interval downlink applied during a brownout hold (#65), the
backoff first step raised to the fair-use floor, the pack no longer rebooted on every re-latch
attempt (#62 root cause), empty pack records no longer counted as silence, and an all-zero RK900
span refused instead of encoded as weather, plus a bounded Modbus drain that feeds the watchdog
— are **reasoned from code and from the reference master, not measured.** Several of them touch
the sleep and brownout paths, which are exactly the paths that cannot be proven by compiling.

Each needs its own bench observation before it may be described as working. Until then the
honest status of every one of them is *believed correct, unobserved*.

**Hardware facts from earlier the same day, restated here because the release cites them and a
release note must not send the reader hunting:**

- `env:soak` and `env:battdiag` at `f6a897d` both boot into **application mode** — ioreg
  `idProduct 32809`, no `nRF UF2` MSC interface. Not DFU.
- The pack read `12.07 V  -0.01 A  89%  25.0 C`, **5 of 5 cycles at `b436aa9`** and **5 of 5
  again at `f6a897d`**.
- Earlier the same day, a **15-cycle episode of all-zero records with `source = 0xFF`** in the
  frame. That observation is what #61 was filed from.

**Zero soak hours exist.** Nothing in this entry is a soak, and compiling three environments is
not a bench run. H8 has not started. Status stays `🚧 NOT YET DEPLOYED`.

**Standing bench fact, so the next person does not lose twenty minutes to it:** the field image
detaches USB 180 s after boot (`src/power.cpp`, ADR-0008 grace window). A board left running
past that grace has no serial port and cannot be flashed — **press RESET once** to get a fresh
180 s window, then flash.


### 2026-08-12 — #61 brownout/ladder deadlock: gate fix runs the ladder; pack re-latch still unproven

- **Commit:** working tree on `f6a897d` + the #61 fix (committed as the SHA in the same change)
  · **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`
- **Measured:** (a) whether a brownout hold engaged with no voltage evidence still suppresses
  `Battery::ladder_allowed()`, and (b) whether the shipped path regresses.
- **Verdict:** **pass on the gate, inconclusive on the pack re-latch.** Stated separately on
  purpose — the firmware lockout is gone, the pack-side recovery is not demonstrated.

**Scratch build (never committed).** `src/main.cpp:253` was temporarily changed to
`brownout.update(false, 0)` so every cycle counts as unreadable and the no-evidence hold
engages through the production path. Flashed `env:battdiag`, `Device programmed.`, no
`Target is not in DFU mode`. Reverted before the commit; `git diff --stat` afterwards showed
only `CHANGELOG.md` and `src/sensors/battery.cpp`.

The hold engaged exactly as designed, and the ladder then ran instead of being skipped:

```
   power   : pack silent for 4 cycles — holding transmissions, no voltage evidence (keepalive in 24 cycles)
   battery : brownout held with no voltage evidence — running the ladder anyway, it is the only thing that can clear the hold
   battery : 4 silent cycles — retrying the full ladder
```

Before this change that same state printed `brownout engaged — probe only, skipping the
fallback ladder`, which is the deadlock #61 describes. The bound held too: the next cycle
printed `5 silent cycles — probe only until cycle 7`, i.e. the expensive phases ran once and
then stood down for `kFullLadderRetryCycles`.

**The pack was genuinely unlatched during that capture — this was not a simulation of #61's
precondition, it was the precondition.** The pack answered as unprovisioned and did not take
the pid the master offered:

```
   battery : answered probe 0xFF (announced pid 0xFF) with pid 0x01
   battery : answered 1 announcement(s) in 3036 ms — pack still reports pid 0xFF
   battery : raw FF 7E 00 15 02 01 00 FF 0E 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 30
   battery : no data (all-zero records (pack not sampled), 28 bytes)
```

**So what is proven is the gate, not the cure.** The provisioning ladder now runs under a
no-evidence hold — that is observed verbatim above, and it is the firmware defect #61 filed.
Whether running it re-latches a pack sitting at `0xFF` is **not** shown: across four full
ladder attempts the pack kept announcing `0xFF`. It recovered its latch across the reset that
came with the next reflash, as it did on the two earlier occasions today. That is a
**pack-side** behaviour and it needs its own investigation; do not read this entry as saying
#61's field scenario is closed end to end.

**Clean build, no regression.** `env:battdiag` rebuilt with the scratch forcing removed,
flashed the same way, 130 s capture, cycles 2 through 9 — **8 of 8 cycles live**:

```
[cycle 2]
   battery : 12.07 V  -0.01 A  89%  25.0 C
...
[cycle 9]
   battery : 12.07 V  -0.01 A  89%  25.0 C
```

No brownout line anywhere in the clean capture, which is correct: the pack is answering, so
no hold engages.

**Also observed today, same bench, recorded here because it was measured and not written
down:** `env:soak` and `env:battdiag` at `f6a897d` both boot into application mode (ioreg
`idProduct 32809`, no `nRF UF2` MSC interface), and the pack read `12.07 V  -0.01 A  89%
25.0 C` for 5 of 5 cycles at `b436aa9` and again for 5 of 5 at `f6a897d`. The earlier
15-cycle episode of all-zero records with `source = 0xFF` is the observation that #61 was
filed from; its root cause is the gate fixed above.

**No soak hours were accumulated. Zero still exist.** Nothing in this entry is a soak.

### 2026-08-12 — H8 soak attempt FAILED to attach. Zero soak hours exist. Cause unestablished

> **This entry replaces one titled "24 h bench soak started (H8)."** That title was wrong and
> the reason is worth keeping: **it was committed at 09:41:23, ninety-two seconds before the
> harness gave up at 09:42:53.** It described a launch as though it were a run, because the
> outcome did not exist yet when it was written. Nothing was soaked. Two later documents
> repeated the claim before it was caught. **Never write an entry for a thing still in
> flight** — the ledger records observations, and an observation is not available until the
> thing has finished happening.

- **Commit:** `f626698` · **Host:** Heliotrope Ridge
- **Attempted:** 2026-08-12 09:39:53 local · **Gave up:** 09:42:53 · **Duration: 180 s, all
  of it spent waiting**
- **Measured:** whether a detached capture could hold a console across the field image's
  sleep cycles for 24 h (H8).
- **Observation** — the log is 140 bytes and two lines, and no capture process is running:

  ```
  2026-08-12 09:39:53 === CAPTURE WAITING /dev/cu.usbmodem* ===
  2026-08-12 09:42:53 === CAPTURE GAVE UP after 180s, device never appeared ===
  ```

- **Verdict: fail. Zero soak hours.** **H8 has not started, let alone closed.** No pass, no
  partial, no "in progress." The serial device never appeared, so nothing was ever captured.

**The tempting explanation is wrong, and checking it matters.** At the time this was written
`env:rak4631` built `FEATURE_CONSOLE=0` and did not call `Serial.begin()`, which looked like it
would explain "device never appeared" perfectly — the console change working as designed rather
than a fault. **It does not apply here, and it never could have.** Both
`FEATURE_CONSOLE=0` and `env:soak` landed in `094d5f5`, committed at **11:26:50** — one hour
forty-four minutes *after* this attempt ended. At `f626698` the `rak4631` image still compiled
the console in, so the board should have enumerated.

> **Doubly refuted, appended later the same day.** `FEATURE_CONSOLE=0` was itself reverted in
> `636e421` ([ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)): the Adafruit core
> calls `SerialTinyUSB.begin()` and creates the `usbd` task before `setup()` runs, so USB
> enumeration never depended on the sketch calling `Serial.begin()` at all. The flag could not
> have suppressed enumeration even had it been on the board. The timeline above already ruled
> it out; the mechanism rules it out independently. `env:soak` is now byte-identical to
> `env:rak4631`.

**So the cause is unestablished**, and this repo cannot establish it. Nothing here records
what was actually flashed and running at 09:39 — and this project has been caught by a stale
binary on the board once already (see the 2026-08-05 entry on `pio run` reusing a hex from an
older commit). The candidates, none of them checked: the board was in DFU rather than running;
a stale or absent flash; the board physically off the bus; a harness defect. **Whoever picks
this up should establish what is on the board before theorising**, and should not go hunting a
hardware fault on the strength of this entry alone.

**What actually exists, and it is real progress:** the soak *harness* — `scripts/soak.sh`,
[`SOAK.md`](SOAK.md), and `env:soak` — now byte-identical to `env:rak4631`, so a soak is
evidence about the shipped image. That is the machinery a soak needs, and it did not exist
yesterday. It has never produced a soak hour.

**Prove the harness before trusting it with 24 h.** `scripts/soak.sh selftest 90`
([`SOAK.md`](SOAK.md)) runs it with no board attached. A harness that has only ever been
started, never validated, is how 180 s of waiting got recorded as a day of soaking.

**One harness lesson did survive from the attempt:** `setsid` does not exist on macOS, so an
earlier launch died on that immediately and captured nothing. `nohup … & disown` is what works
on this host.

**Independent of the soak — the interval is 900 s, not 1800 s.** From the network's uplink
timestamps: `16:21:07Z → 16:36:14Z` is 15 min 7 s, and `16:05:59Z → 16:21:07Z` is 15 min 8 s.
This comes from TTN, not from the console, so the failed capture does not affect it. At 900 s
the node sits at the fair-use floor (`kFupFloorSeconds`, `src/config.h:62`), which is
compliant. Note that the `env:soak` comment in `platformio.ini` still describes the cadence as
1800 s.

- **Status is unchanged: `🚧 NOT YET DEPLOYED`.** H8 needs ≥24 h bench and ≥7 d field shadow.
  Neither has closed and neither has begun.

### 2026-08-12 — Sleep current is not measurable from pack telemetry. Resolution floor 10 mA against a ~1 mA question

- **Commit:** `4510763` · **Host:** Heliotrope Ridge, RAK4631 on `/dev/cu.usbmodem31101`
- **Measured:** whether the RAK9154's own current telemetry can size the `delay()`-based
  sleep at `src/power.cpp:134-136`, as a stopgap before a meter.
- **Observation:** the pack reports current with a **0.01 A (10 mA) LSB** — raw `i=-1` for
  the reported `-0.01 A`. The reading did not move across 20 consecutive `battdiag` cycles
  or in the field image, awake and transmitting:

  ```
  08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
  08:04:47 battery : raw v=1212 i=-1 soc=91 t=230
  ```

  A pack supplying a ~30 mA awake node should read about `-0.03 A`. It does not, because
  with USB attached the board is USB-powered and the pack sees almost no load — and USB is
  the only way the reading leaves the board.

- **Verdict: cannot answer — and this is a complete answer, not a partial one.**
  `docs/POWER_BUDGET.md` turns on ~1 mA; the pack resolves 10 mA. Even the documented
  defect cases (0.89–1.2 mA peripherals-enabled, ~6 mA radio-awake) fall at or under one
  LSB. **A meter is the only instrument that can settle this.** No number is recorded and
  none should be quoted from pack telemetry.
- **Not a `bench` citation for sleep current.** It is a bench citation for the *resolution
  floor*, and the citable form of it names this file and the commit so it points somewhere:
  `CITE(bench): docs/EVIDENCE.md 2026-08-12 @ 4510763 — pack current LSB = 0.01 A`.

### 2026-08-12 — Field image: both sensors in one cycle. Uplink transmitted; TTN acceptance NOT verified

- **Commit:** `4510763`
- **Host:** Heliotrope Ridge, RAK4631 on `/dev/cu.usbmodem31101`
- **Image:** `rak4631` — the actual field image. Radio in, **sleep in**. Not a diagnostic.
- **Measured:** whether both sensors read in the same image and the same cycle (never
  previously observed), and how far toward TTN acceptance the console can actually take us.
- **Observation:** one full cycle, verbatim:

  ```
  08:04:47 config  : interval 1800 s, boot #2
  08:04:47 session : restored 0x260CE734, counter 1792
  08:04:47 [cycle 1]
  08:04:47 RK900   : raw 0x0000-0x0004 = 0000 0000 00F7 024B 273C
  08:04:47 RK900   : wind 0.00 m/s @ 0 deg, 24.7 C, 58.7 %RH, 1004.4 hPa
  08:04:47 battery : pack answered at 0x01 — skipping provisioning
  08:04:47 battery : sampling confirmed — pack is reporting live values
  08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
  08:04:47 radio   : sent 35 bytes on port 2
  08:04:47 session : saved 0x260CE734, resume at 1824
  08:04:55 sleep   : 1800 s
  ```

**What this does and does not establish:**

| Claim | Status |
|---|---|
| Both sensors, one image, one cycle | **pass** — first time observed |
| Field image (`FEATURE_SLEEP=1`) runs a complete cycle | **pass** |
| Sleep reached | **pass** — `sleep : 1800 s` |
| OTAA join | **not observed.** The node restored a saved session (`restored 0x260CE734`) rather than joining. That is correct behavior and evidence the earlier join persisted, but it is not a join event |
| Uplink **accepted at TTN** | **pass** — confirmed from the network side at 09:20. See the correction below; an earlier row in this entry claimed the opposite and was wrong |
| Downlink handled | **delivered to the device** at `16:21:07Z`. Console confirmation of the handler running was not captured — see below |

**On TTN acceptance — read this before re-testing the radio path.**

**The radio path is already proven and this entry does not reopen it.** It closed twice:
2026-07-31 ([`EVIDENCE.md` §first LoRaWAN join](#2026-07-31--first-lorawan-join-and-first-uplink-accepted-by-the-things-network),
join and uplink accepted, confirmed from both sides, gateway `9181014c6051030034`) and
2026-08-04 (first end-to-end real-sensor uplink, operator-confirmed). Do not re-derive
those.

What is new and narrower: **nobody has seen *today's* image arrive.** `radio : sent 35
bytes on port 2` is the SX126x library reporting a frame handed to the transceiver — it is
transmit, not reception, and the two must not be conflated.

Measured 2026-08-12 08:38-08:40. A `ttn-lw-cli events subscribe` stream was held open for
110 s spanning a board reset, so the device booted, restored its session and uplinked
inside the window. 22 events parsed, 8 of them `as.up.data.forward`. **None were from
`puma-concolor-001`.** The uplinks that did arrive in that same window came from five other
devices on the same application:

```
"device_id": "6773a47722230004"
"device_id": "9181010k6063240022"
"device_id": "earthquake-rak-10703"
"device_id": "la666050494"
"device_id": "rak10701-plus-001"
```

Those five are the load-bearing part of this observation: they rule out the subscription,
the credentials, the CLI invocation and the network path. The stream was working. Our
device was not heard.

**CORRECTION, 2026-08-12 09:20 — the paragraphs above are wrong, and the error is
instructive enough to leave in place rather than delete.** The device was being heard the
whole time. Querying the network's own record of the device settles it:

```
$ ttn-lw-cli end-devices get my-app-tobi puma-concolor-001 --all
last_seen_at : 2026-08-12T16:05:59.401582Z
freq_plan    : US_902_928_FSB_2
--- NS session ---
 "dev_addr": "260CE734",
 "last_f_cnt_up": 1857,
 "started_at": "2026-07-31T14:33:20.636657834Z"
```

The network-side `dev_addr` is `260CE734`, byte-identical to the device console's
`session : restored 0x260CE734`. The session established on 2026-07-31 is still live, and
its uplink counter is advancing. There is no divergence and no stale session.

The uplink history makes the mistake obvious (times UTC; local is UTC-7):

```
 t= 2026-08-12T15:04:47Z f_cnt= 1792 port= 2 gw= 3356-gateway-002 snr= 13.5
 t= 2026-08-12T15:33:12Z f_cnt= 1824 port= 2 gw= 3356-gateway-002 snr= 13.5
 t= 2026-08-12T15:35:51Z f_cnt= 1856 port= 2 gw= 3356-gateway-002 snr= 13.75
 t= 2026-08-12T16:05:59Z f_cnt= 1857 port= 2 gw= 3356-gateway-002 snr= 13.75
```

`f_cnt 1792` at `15:04:47Z` is `08:04:47` local — the exact second of the console line
`08:04:47  radio : sent 35 bytes on port 2` recorded earlier in this entry. That frame was
received. So were the rest, all on gateway `3356-gateway-002` at SNR 13–14 dB, which is a
comfortable link, not a marginal one.

**Why the 110 s window saw nothing: it sat entirely inside the sleep.** The two uplinks
bracketing it are `15:35:51Z` and `16:05:59Z`; the subscription ran `15:38–15:39:50Z`.
Nothing was transmitted during it. The gap between those two uplinks is 30 min 8 s, which
is `sleep : 1800 s` plus a cycle — the node was doing exactly what it was told to.

The counter is the tell that should have caught this sooner. `1824 → 1856` is a jump of 32,
the reserved block a session restore burns on boot; `1856 → 1857` is a plain increment. So
the board did **not** reset when that capture's upload reported `SUCCESS`, and the window
was observing a sleeping node.

**The lesson worth keeping: on a node with a 1800 s duty cycle, a two-minute observation
window that sees nothing has established nothing.** The five other devices proved the
subscription worked — they did not, and could not, prove ours was silent. Absence of
evidence was recorded as evidence of absence. Query `last_seen_at` and the counter first;
they are cheap, and they are the network's own memory rather than a sample of it.

**Device identity, corrected.** `puma-concolor-001` / DevEUI `42BB96EF76E200F1` lives in
application **`my-app-tobi`**. The plausible-sounding `middle-fork-area` has **no end
devices at all** (`ttn-lw-cli end-devices list middle-fork-area` returns `[]`). Anyone
reasoning from the application name will look in the wrong place.

**Downlink: queued, not delivered.** A status-request downlink was queued successfully —

```
"f_port": 10,
"frm_payload": "Aw==",
```

`Aw==` is `0x03`, matching the firmware contract at `src/radio.cpp:398`.

**Delivered at 09:21, once the uplink question was settled.** With the frame queued, the
board was reset and uplinked at `16:21:07Z` (`f_cnt 1858`, port 2). The queue drained
across that single uplink:

```
before:  [{"f_port": 10, "frm_payload": "Aw==", ...}]
after:   []
```

That is the network confirming it handed the frame to the device in the RX window that
uplink opened — the first time anything has come *down* to this node on hardware. It
exercises the Class A ordering the earlier note described correctly: the uplink is what
creates the opportunity.

What is still missing is narrower and worth stating plainly: **the console did not capture
the handler running.** `Radio::take_downlink()` and the `0x03` branch are unverified by
observation, even though the frame reached the radio. The capture helper gives up looking
for the USB CDC device after roughly 40 s regardless of the duration it is asked for, and
after a DFU flash this board takes about two minutes to re-enumerate and run — so every
attempt stopped watching before the node woke. That is a tooling gap, not a firmware
finding. Bounds-checking of a malformed downlink is likewise untested on hardware.
[#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)
stays open for those two, narrowed from "never run on hardware" to "delivered but not
observed being handled."

**Working `ttn-lw-cli` harness**, established the hard way and worth keeping:

| Purpose | Command |
|---|---|
| List applications | `ttn-lw-cli applications list` |
| List devices | `ttn-lw-cli end-devices list my-app-tobi` |
| Live events | `ttn-lw-cli events subscribe --application-id my-app-tobi` |
| Queue downlink | `ttn-lw-cli end-devices downlink push my-app-tobi puma-concolor-001 --f-port 10 --frm-payload 03` |
| Inspect queue | `ttn-lw-cli end-devices downlink list my-app-tobi puma-concolor-001` |

Traps that cost real time here:

- **`end-devices subscribe-events` is not a subcommand.** It silently prints the help text
  and exits 0, so a redirect captures help output and looks like "no events". The event
  stream lives under `ttn-lw-cli events subscribe`.
- The downlink flags are **`--f-port` and `--frm-payload`**, not `--port`/`--payload`.
- A successful `downlink push` prints **nothing** but `INFO`/`WARN` lines. Confirm with
  `downlink list`, not by the absence of an error.
- Event JSON is pretty-printed: the key is `"name": "..."` **with a space**. A grep for
  `"name":"` matches nothing and reads as an empty stream.
- The CLI config is not at `~/.config/ttn-lw-cli`; it resolves
  `~/.ttn-lw-cli.yml` and `~/Library/Application Support/.ttn-lw-cli.yml`. The build host
  **is** authenticated — absence of the file where you first looked is not absence of auth.

- **Verdict: partial pass.** Sensors and cycle: pass. Today's uplink reaching TTN: **fail,
  one window, cause not yet isolated** — recorded as a negative observation, not as an
  unverified gate.
- **Status unchanged: `🚧 NOT YET DEPLOYED`.** H1-H8, the ≥24 h soak and the ≥7 d field
  shadow are all untouched by this run.

### 2026-08-12 — The RAK9154 pack latches and reports. It was never broken; the 1800 s cycle hid cycle 3

- **Commit:** `b436aa9`
- **Host:** Heliotrope Ridge, RAK4631 on `/dev/cu.usbmodem31101`
- **Image:** `battdiag` — battery only, ~10 s cycle (`FEATURE_RK900=0 FEATURE_RADIO=0
  FEATURE_SLEEP=0 FEATURE_BENCH_INTERVAL=1 FEATURE_BATTERY_FAST=1`)
- **Measured:** whether the all-zero record set that every prior session reported as a
  provisioning failure was instead the documented boot behavior — `AGENTS.md` records
  "expect ~2 null cycles after boot while the pack samples" from the 2026-08-05 success.
  `stage3` runs an 1800 s cycle, so every capture window ever taken held exactly one cycle.
- **Observation:** 20 consecutive cycles in 210 s. **19 of 20 carried live values, one
  did not, and the one was cycle 2.** The pack was latched at pid `0x01` throughout; the
  string `provId FF` does not appear anywhere in the capture.

  ```
  07:56:08 [cycle 2]
  07:56:10 battery : pack answered at 0x01 — skipping provisioning
  07:56:10 battery : raw FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 27
  07:56:10 battery : no data (all-zero records (pack not sampled), 28 bytes)
  07:56:20 [cycle 3]
  07:56:20 battery : pack answered at 0x01 — skipping provisioning
  07:56:20 battery : sampling confirmed — pack is reporting live values
  07:56:20 battery : 12.12 V  -0.01 A  91%  23.0 C
  07:56:20 battery : raw v=1212 i=-1 soc=91 t=230 (t scale UNCONFIRMED)
  ...
  07:59:26 [cycle 21]
  07:59:26 battery : pack answered at 0x01 — skipping provisioning
  07:59:26 battery : 12.12 V  -0.01 A  91%  23.0 C
  ```

  Values are consistent with the 2026-08-05 reference (12.23 V, +0.00 A, 98%, 23.0 °C) for
  a pack that has since been sitting: 12.12 V, -0.01 A, 91%, 23.0 °C, stable to the
  reported digit across all 19 cycles.

  One cycle (8) logged `no announcement — proceeding unprovisioned` and still returned live
  values, which is correct — the latched pid survives a missed announcement window.

- **Verdict: pass.** The provisioning handshake works. This closes the investigation that
  the same-day byte-for-byte comparison had already pointed at: our frame matched
  `onewire_master_protocol.c` on all five checked properties because it *was* correct, and
  the reference algorithm reproducing the pack's own `0x82` checksum was the tell. The
  remaining symptom was an observation artifact of the 1800 s cadence, not a defect.
- **Correction to the record:** prior 2026-08-12 entries describing the pack as "refusing
  to latch" are wrong. The `provId FF` announcements those entries captured are the pack's
  pre-latch announcements; no capture window was ever long enough to show what followed.
- **Does not change project status.** `🚧 NOT YET DEPLOYED` stands — H1-H8 and the ≥24 h
  soak / ≥7 d shadow are untouched by this. Temperature scale remains UNCONFIRMED
  (issue #4) and the current-sign conflict in ADR-0002 is still open. **[Update 2026-08-13:
  ADR-0002's current-sign conflict is now closed by operator decision — positive = charging,
  negative = discharging, matching the pack. Not by a measurement; nothing in this entry or
  any other observed a charge current. The statement above was accurate on its date.]**

### 2026-08-12 — Transmit timing is within async tolerance. The 11.4% overshoot is legal inter-character idle, not a stretched bit period

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`, commit **`eb7fff8`**, `owscan`.

The provisioning capture's `tx 95 bytes in 110352 us = 1161 us/byte` against 1041.7 us of ten bit
periods at 9600 raised the hypothesis that our transmit clock is 11.4% slow and the pack is
discarding our provisioning response as a framing error. **That hypothesis is disproven.**

`owscan` phase 0 measures `write()` cost at three bauds, which over-determines the model
`T = k * bit_period + F`. Cycles 2 and 3 were identical to the microsecond:

```
4800  baud  tx 64 x 0x55 : 149414 us total, 2334.59 us/byte (10 bits = 2083.33 us, excess 251.26 us)
9600  baud  tx 64 x 0x55 :  75195 us total, 1174.92 us/byte (10 bits = 1041.66 us, excess 133.26 us)
19200 baud  tx 64 x 0x55 :  38086 us total,  595.09 us/byte (10 bits =  520.83 us, excess  74.26 us)
```

Solving for `k` across all three pairs, using the library's integer-truncated bit delays
(4800 → 208 us, 9600 → 104 us, 19200 → 52 us):

| Pair | ΔT (us) | Δbit (us) | k |
|---|---|---|---|
| 4800 − 19200 | 1739.50 | 156 | **11.151** |
| 4800 − 9600 | 1159.67 | 104 | **11.151** |
| 9600 − 19200 | 579.83 | 52 | **11.150** |

`k = 11.15` on all three pairs, and back-substituting gives `F = 15.3 us` at every baud
(1174.92 − 11.15×104 = 15.3; 595.09 − 11.15×52 = 15.3; 2334.59 − 11.15×208 = 15.4). The model
fits exactly, so neither term is assumed.

**What that means.** A byte costs 11.15 bit periods, not 10. The extra whole bit period is
`beginTx()`'s own `delayMicroseconds(_tx_delay)`, spent with the line idle-HIGH *before* the start
bit is driven. The residual 0.15 spread over 11 `delayMicroseconds()` calls is ~1.4 us of call
overhead each, so the real on-wire bit period is ≈ **105.4 us against an ideal 104.17 us, an error
of +1.18%.**

+1.18% is inside the few-percent budget an asynchronous receiver has, and the accumulated error at
the stop-bit sample point is ~0.11 of a bit — the sample lands nowhere near a boundary. The
remaining excess is inter-character idle, which async framing permits without limit because the
receiver resynchronises on every start bit.

**Conclusion: the bytes we transmit are individually well-formed and the pack can receive them.
Transmit timing is not the reason the pack will not latch.** Do not "fix" `_tx_delay`; the
truncation to 104 us makes bits marginally *short*, not long.

#### Cross-read: the sibling is not authoritative for provisioning

`forest-weather-machines/rak-4-5-wire` @ `8378435` (repo HEAD `efc0e3c`):

- `firmware/nanoc6-rak9154-poll/src/main.cpp` is **0 bytes — an empty file.** The Modbus path
  (slave `0x6E`, 21 registers from `0x6000`) that `AGENTS.md` cites it as authoritative for is
  not implemented there.
- `firmware/nanoc6-onewire-poll/src/onewire_protocol.cpp` (236 lines) implements
  `send_query_probe01()`, `rx_frame()` and `parse_response()` — **and no provisioning handler at
  all.** It polls a probe that is already `0x01` and parses the reply.

So the sibling is authoritative for the *poll and parse* of an already-provisioned pack, and has
nothing to say about the announcement handshake, which is the step actually failing here.
`AGENTS.md`'s claim of protocol authority overstates what that repo contains. The only
provisioning references remain the vendored upstream `onewire_master_protocol.c` and Meshtastic,
both already cited throughout `src/sensors/battery.cpp`.

### 2026-08-12 — `WB_IO2` rail exonerated; the "no announcement" failure was a stale binary. Current HEAD answers the pack but the pack still will not latch

Two results, both on **Heliotrope Ridge**, board on `/dev/cu.usbmodem31101`, commit
**`9c35e2f`** (build host on a clean tree at that SHA; the previously-uncommitted `owscan.cpp`
accumulator fix was stashed as a duplicate of `b967008` and the branch fast-forwarded).

#### Result 1 — the switched `3V3_S` rail does NOT gate the pack. Hypothesis disproven

`owscan` phase 2b listens twice at 9600, once with `WB_IO2` driven HIGH and once driven LOW —
the state `RK900::power_off()` leaves behind. Three consecutive cycles:

```
07:33:54 rail A/B verdict: HIGH 184 byte(s), LOW 184 byte(s) — the rail does not gate the pack; look at the driver
07:34:29 rail A/B verdict: HIGH  92 byte(s), LOW 184 byte(s) — the rail does not gate the pack; look at the driver
07:35:03 rail A/B verdict: HIGH  92 byte(s), LOW 184 byte(s) — the rail does not gate the pack; look at the driver
```

With the rail off the pack still emitted the full 92-byte announcement, twice per window. The
`3V3_S` hypothesis raised in the entry below is **disproven**: no wiring change is required, and
the pack's `3V3_In` is not gated by `WB_IO2` on this harness.

#### Result 2 — `stage3` at `9c35e2f`: the driver sees and answers the announcement

The 0-byte / `no announcement` behaviour recorded below came from an **unidentified image** at
`[cycle 203]`, not from current code. Flashing `stage3` built from `9c35e2f` produced entirely
different behaviour on the same harness:

```
07:36:41 battery : turnaround 2 ms (gap 1953 us), tx 95 bytes in 110352 us = 1161 us/byte (10 bits @ 9600 = 1042 us)
07:36:41 battery : answered probe 0xFF (announced pid 0xFF) with pid 0x01
07:36:41 battery : reply FF 7E 00 55 02 01 FF 00 00 01 50 03 44 01 02 09 00 30 00 ... 01 00 47 45 00 00 00 00 52 41 4B 32 35 36 30 2D 69 6F ... 06 15 BA 08 00 16 B9 08 00 17 B8 08 00 18 67 08 00 19 F3 08 00 1A F3 08 00 7C
07:36:41 battery : probe 0xFF announces 6 sensor(s)
07:36:41 battery :   sid 0x15 ipso 186 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x16 ipso 185 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x17 ipso 184 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x18 ipso 103 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x19 ipso 243 rule 0x0008 (08 00) periodic
07:36:41 battery :   sid 0x1A ipso 243 rule 0x0008 (08 00) periodic
07:36:42 battery : answered 2 announcement(s) in 5079 ms — pack still reports pid 0xFF
07:36:43 battery : PROVISION announcement where a SENDAT reply was expected — the pack is announcing, not answering
07:36:45 battery : no data (all-zero records (pack not sampled), 28 bytes)
07:36:45 radio   : sent 20 bytes on port 2
07:36:45 session : saved 0x260CE734, resume at 1760
07:36:53 wait    : 1800 s (sleep disabled)
```

What this establishes:

- **Announcement detection, parsing and the response mutation all work.** The reply frame carries
  `01` in the `provId` slot (frame index 34) where the announcement carried `FF`, the addresses
  are swapped (`dest FF`, `source 00`), the flag is `01` RSP, and the checksum is recomputed
  (`7C`). The six descriptors decode correctly, every one at rule `0x0008` RULE_PERIODIC.
- **The `no announcement — proceeding unprovisioned` symptom is not present in current code.** It
  belonged to the unidentified binary. Any theory built on it — including the `3V3_S` theory
  above — was explaining a stale artifact.
- **The pack still refuses to latch.** Two answered announcements in 5079 ms, and the pack's next
  announcement still carries `provId 0xFF`. SENDAT then returns the 28-byte all-zero template.
- **The radio path works:** `radio : sent 20 bytes on port 2`, session restored from NVM.
- Measured transmit cost of the 92-byte echo: **110352 us for 95 bytes = 1161 us/byte** against a
  1042 us/byte theoretical floor at 9600 8N1. The whole provisioning response occupies the wire
  for ~110 ms.

**Not established:** why the pack will not latch. The 2026-08-05 entry records it latching pid
`0x01` at `1a203d3`. Today, on `9c35e2f`, it does not, and `SENDAT` to `0x01`/`0x02`/`0x03` draws
0 bytes while `0xFF` draws 120. Reconciliation: the 2026-08-05 latch is **historically accurate
but describes a state the pack no longer holds** — the announcement's own `provId` field is the
pack's belief about its id, and it reads `FF` on every capture today. `AGENTS.md`'s claim that the
pack "latches pid `0x01`" is therefore stale as a description of current hardware state. The
driver already handles this correctly by construction (`m_pid` starts at `0xFF` and falls back),
so this is not an addressing regression.

### 2026-08-12 — The pack talks. `owscan` draws the announcement every cycle; the production image on the same bench gets 0 bytes

Reverses the 2026-08-11 entry below on the one-wire bus. The RAK9154 is **not** silent: a
standalone `owscan` image reads its 92-byte announcement on every cycle at 9600, and draws a
SENDAT reply from dest `0xFF`. The production ladder, on the same board and the same wiring
minutes earlier, saw nothing at all.

- **Host:** Heliotrope Ridge, board on `/dev/cu.usbmodem31101`
- **Build-host tree:** `8994d02` plus the uncommitted `src/diagnostics/owscan.cpp`
  accumulator-reset fix. That working-tree diff is byte-identical to the diff local commit
  `b967008` carries (`git diff … | md5` = `8b02d1ef78ea5c232c01385e34b9933f` on both sides), so
  the image built here is the content of `b967008`.
- **Measured:** whether the pack transmits at all on `WB_IO1`, and at which baud and address.

#### Observation A — the production image already on the board, before anything was flashed

Image SHA **not established** — it was not the `owscan` image it was believed to be. It reported
`[cycle 203]`, read the RK900, and ran the battery ladder:

```
07:21:19 battery : no announcement — proceeding unprovisioned
07:21:40 battery : no data (no reply, 0 bytes)
07:21:40 wait    : 60 s (sleep disabled)
07:22:40 [cycle 203]
07:22:40 RK900   : raw 0x0000-0x0004 = 0000 0000 00F4 024C 2738
07:22:40 RK900   : wind 0.00 m/s @ 0 deg, 24.4 C, 58.8 %RH, 1004.0 hPa
```

The 21 s between the two battery lines is the non-`FAST` 20 s push listen. **Zero bytes across
the whole cycle.** RK900 reads correctly on the same cycle.

#### Observation B — `owscan`, flashed immediately afterwards, three consecutive cycles

Passive listen, transmitting nothing, 9600 baud, identical on all three cycles:

```
9600 baud  passive 3000 ms : 92 byte(s)  <- FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF 00 47 45 00 00 00 00 52 41 4B 32 35 36 30 2D
69 6F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 06 15 BA 08 00 16 B9 08 00 17 B8 08 00 18 67 08
00 19 F3 08 00 1A F3 08 00 82
```

Decoded: `hub_type 0x01` PROVISION, `payload_type 0x03` VER3, `dest 0x00` master, `source 0xFF`.
`provId` at frame index 34 reads **`FF`** — still unprovisioned. `snsr_num` = **6**, and every
descriptor carries rule **`0x0008` (RULE_PERIODIC)**: sid `0x15` ipso `0xBA` (186 DC voltage),
`0x16`/`0xB9` (185 DC current), `0x17`/`0xB8` (184 capacity), `0x18`/`0x67` (103 temperature),
`0x19` and `0x1A` both `0xF3` (243 status word). The sensors are armed, not disabled.

SENDAT probe-id sweep at 9600:

```
9600 baud  SENDAT dest 0x01 : 0 byte(s)
9600 baud  SENDAT dest 0x02 : 0 byte(s)
9600 baud  SENDAT dest 0x03 : 0 byte(s)
9600 baud  SENDAT dest 0xFF : 120 byte(s)  <- FF 7E 00 15 02 01 00 FF 09 03 10 02 15 BA 00 00
16 B9 00 00 17 B8 00 18 67 00 00 2F FF 7E 00 55 02 00 ...
```

The first frame is `flag 0x01` (RSP), `dest 0x00`, `source 0xFF`, `hub_type 0x03` SENDAT — a
genuine answer to our request — and **every record value is zero**, i.e. the
`BatteryResult::Unsampled` template. It is immediately followed in the same read by the 92-byte
announcement. BOOT drew 0 bytes at 4800/9600/19200/38400, unchanged from prior sweeps.

Phase 1 line census, cycles 2 and 3:

```
INPUT_PULLUP   idle HIGH : 334 falling edge(s), 121385 of 1732860 samples LOW
INPUT (float)  idle HIGH : 0 falling edge(s), 0 of 1734024 samples LOW
```

Open-drain behaving as expected: activity only with the pull-up engaged.

#### What this establishes, and what it does not

Established: the wire, the pin (`WB_IO1`), the baud (9600) and the pack are all good. The pack
is **unprovisioned** (`provId 0xFF`), listens only on `0xFF`, and answers with an all-zero
record template. Nothing here is a framing or checksum fault.

**Not established:** why the production image gets 0 bytes where `owscan` gets 92. The leading
hypothesis is that `owscan` never reads the RK900 and therefore never drops `WB_IO2`, whereas the
production cycle reads the RK900 first and `src/sensors/rk900.cpp` drops `WB_IO2` LOW afterwards —
which is precisely the failure `docs/HARDWARE.md` predicts if the pack's `3V3_In` sits on the
switched `3V3_S` rail rather than the always-on `VDD` pad ("the symptom would be a battery that
never replies"). **This was not tested.** The controlled test is one `owscan` cycle with `WB_IO2`
driven LOW; if the announcement disappears, the hypothesis is proven.

The operator states the pack is wired `IO1` + `VDD` + `GND` with P+/P−, which matches
`docs/HARDWARE.md` §"P0 wiring — decided" exactly. No contradiction with the documented pinout.

### 2026-08-11 — Both known-good images reflashed; both stayed silent. Firmware is exonerated on both buses

The headline result of the day, and the one that redirects the search. Two images with recorded
passes were put back on the board unchanged. Neither reproduced its own recorded result.

- **Host:** Heliotrope Ridge
- **Commits reflashed:** `998dc26` (`busscan`) and `8720dea` (`stage2`) — the exact images
  behind the 2026-08-03 RK900 pass and the 2026-08-05 battery pass recorded below.
- **Measured:** whether either recorded pass still reproduces on today's bench, with the
  firmware held constant and only the rig between then and now.
- **Observation — `998dc26`, `busscan`, three full cycles:**

  ```
  total with rail HIGH: 0 byte(s)
  total with rail LOW: 0 byte(s)
  9600/0x01 production frame: 0 byte(s)
  ```

  The 2026-08-03 pass on this same image read
  `15 byte(s) <- 01 03 0A 00 00 00 00 00 FB 01 F8 27 56 DA A1`. **Not reproduced.**

- **Observation — `8720dea`, `stage2`, full 50.5 s awake window:**

  ```
  battery : no announcement — proceeding unprovisioned
  battery : no data (no reply, 0 bytes)
  wait    : 60 s (sleep disabled)
  ```

  The 2026-08-05 pass on this same image read 22 announcements in 45.4 s plus a 28-byte
  checksum-valid SENDAT reply. **Not reproduced.**

- **Verdict:** FAIL on both buses, and **firmware is exonerated for both.** The binaries that
  produced the passes are byte-for-byte the binaries that are silent now, so no code change
  since can be the cause and no code change can be the fix.

**Why this is conclusive for the one-wire side in particular.** The pack's 2026-08-05
announcements were **unsolicited** — the pack talks first, and that depends on nothing the node
transmits. Framing, addressing, turnaround timing, `kWakeCount`, provisioning state: none of it
can suppress a message we never asked for. Silence from an unsolicited talker means nothing is
driving the wire.

**Operator-confirmed context.** The RK900 has 12 V. The pack is healthy and powers the board
when the board is off USB. The rig that passed on 2026-08-05 is recorded below
(_"Phase-0 direct probe exonerated on hardware"_) as having pack pin 1 `P+` **deliberately
unconnected with the buck out of circuit** — 12 V entered the circuit only after that capture.
That is the largest known delta between the passing rig and today's, and it is physical.

Remaining suspects on both buses are wiring, connectors, and the 12 V introduction — not
firmware. Tracked as [#49](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/49)
(RS-485) and [#50](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/50)
(one-wire).

### 2026-08-11 — One-wire dead: no edge, no byte, in any mode — and the scan reporting otherwise was lying

- **Host:** Heliotrope Ridge
- **Commit:** `8994d02`, `owscan` image. The after-fix capture additionally carries the
  accumulator reset committed alongside this entry.
- **Measured:** whether the RAK9154 drives the one-wire line at all — idle level and
  falling-edge census with no UART and no framing, passive listen at five bauds, BOOT to dest
  `0xFF` at five bauds, and SENDAT to dest `0x01`/`0x02`/`0x03`/`0xFF` at 9600.
- **Observation — the edge census, which needs no protocol to be right:**

  ```
  INPUT_PULLUP idle HIGH : 0 falling edge(s), 0 of 1735294 samples LOW
  INPUT (float) idle HIGH : 0 falling edge(s), 0 of 1734024 samples LOW
  ```

  0 bytes on passive listen at all five bauds. 0 bytes from BOOT dest `0xFF` at all five
  bauds. 0 bytes from SENDAT dest `0x01`/`0x02`/`0x03`/`0xFF` at 9600. Verdict line:

  ```
  0 pulled-up edge(s), 0 floating edge(s), 0 byte(s) total
  ```

- **Verdict:** FAIL. Nothing pulled the line low across ~1.73 M samples in either pin mode,
  so the pack is not driving the wire — this is below the level where framing or addressing
  could matter. Tracked as [#50](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/50).

#### The diagnostic lied first, so every earlier `owscan` verdict inherits the doubt

`owscan`'s four verdict accumulators — `ow_edges_pulled`, `ow_edges_float`, `ow_bytes`,
`ow_best_baud` — are file scope and were never cleared between sweeps, while `main.cpp` calls
the scan once per cycle. Two bytes captured in some early cycle therefore latched their verdict
permanently.

**Before the fix**, at 20:47:32, 20:48:00 and 20:48:28, the scan printed a screen of zeros in
every phase and then concluded:

```
2 byte(s) total
bytes arrived. The pack talks, so the fault is framing or addressing, not the wire
```

and pinned phase 4 to `19200 baud (first baud that answered)` when 19200 had returned nothing.

**After the fix**, at 20:52:40, the same wire and the same board:

```
0 byte(s) total
nothing ever pulled this line low… the pack is not driving the wire at all
```

This is recorded as evidence, not as a changelog line, because it invalidates readings: **any
`owscan` verdict captured before 20:52:40 today reports the union of every cycle since boot,
not the cycle printed above it.** The failure mode is the precise one the scan exists to
prevent — it sent the reader after a framing constant while the wire was dead. `bus_scan()` was
never exposed to this; it totals in locals.

### 2026-08-11 — RS-485 dead on bench: busscan 0 bytes powered and unpowered

- **Commit on the board:** `e2c7088` (`busscan` image, flashed same day)
- **Host:** Heliotrope Ridge
- **Measured:** Modbus FC `0x03` sweep at 4800/9600/19200/38400/115200, slaves `0x01`–
  `0x03`, with WB_IO2 HIGH then LOW; plus the 9600/0x01 five-register production frame.
- **Observation** (two independent captures, same verdict):

  ```
  [bus scan] total with rail HIGH: 0 byte(s)
  [bus scan] total with rail LOW: 0 byte(s)
  [bus scan] verdict: 0 byte(s) powered vs 0 unpowered; 9600/0x01 production frame: 0 byte(s)
  [bus scan] the line is dead in both states. Nothing the firmware controls
             can change that — check 12 V at the RK900 and the A/B pair.
  ```

- **Verdict:** FAIL for a live RK900 path. Baud, slave ID, and IO2 control are ruled out
  (all rates/slaves silent; HIGH≡LOW). Remaining: pack pin 1 `P+` → RK900 12 V, and A/B on
  the RAK5802. Tracked as [#49](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/49).
- **Notes:** Board left on `busscan` for the re-check after wiring. Field image was
  `0.4.0` / Aug 5 before this flash; restore `rak4631` only after `busscan` shows a
  non-zero production frame.

### 2026-08-05 — Board recovered via UF2; "off the USB bus" turns out to be the sleep detach, not a fault

**Host:** Heliotrope Ridge. **Commit on the board:** `7dfc26f` (see the stale-binary note below —
**not** `bf5ceb2`, which is what was intended). **Verification still incomplete.**

Two operational findings, both of which cost time today and neither of which is a code defect.

**1. Serial DFU can fail while the bootloader is healthy; UF2 mass storage works.** With the
board in the UF2 bootloader (`239A:0029`), the port present at `/dev/cu.usbmodem1101`, and
`lsof` showing nothing holding it, `pio run -t upload` failed twice with *"No data received on
serial port. Not able to proceed."* The UF2 route succeeded on the first try. The non-obvious
step is that **macOS left the bootloader's drive unmounted** — `diskutil list` showed
`/dev/disk6` as `RAK4631`, 33.7 MB, with no `/Volumes` entry until `diskutil mount disk6`. That
absence is what made the UF2 path look unavailable. Procedure now in
[`FIRST_FLASH.md`](FIRST_FLASH.md).

Converted with the framework's own tool, `uf2conv.py -c -f 0xADA52840`, which reported
`start address: 0x26000` — the application offset above the SoftDevice. Copy, then the
bootloader flashes and resets itself; the application was up about 12 s later.

**2. A sleeping node has no USB device at all, and that is the fix working.** This reading was
gotten wrong twice in one session, so it is written down. After the flash the board enumerated,
uplinked, and then vanished completely from the bus: no `239A:*`, no `/dev/cu.usbmodem*`, no
`RAK4631` disk. That looks identical to the dead-board state, and `flash.sh` says so in as many
words — *"no 239A device on the bus at all"* → *"THE BOARD HAS NO VALID APPLICATION."*

It was asleep. `TinyUSBDevice.detach()` releases the D+ pull-up, so the host sees the device
removed, exactly as intended. TTN settled it: `last_f_cnt_up` **1024**, session `updated_at`
`2026-08-05T15:15:56Z`, against a build-host clock reading `08:17:13 PDT` — the node had uplinked
**77 seconds** before the bus was declared empty. `dev_addr 260CE734`,
`last_n_f_cnt_down`/`last_a_f_cnt_down` both 26.

The lesson is procedural: **USB presence is not a liveness test on a build that sleeps.** TTN
session state is, and it costs one command.

**Stale binary — what is actually on the board.** `pio run -e rak4631` reported `SUCCESS` in
1.0 s and reused a `firmware.hex` built from `7dfc26f`, even though `git diff 7dfc26f bf5ceb2`
shows `src/sensors/battery.cpp` and `src/sensors/battery.h` changed and both compile into that
environment. The UF2 was converted from that stale hex, so **the board is running `7dfc26f`,
not `bf5ceb2`.** A later `rm -rf .pio/build/rak4631 && pio run` produced a fresh hex which has
not been flashed. `7dfc26f` does contain the CDC fix, so the pending verification is still
meaningful, but no result from this board may be attributed to `bf5ceb2`.

**What is still not proven.** Everything the session set out to prove:

- **CDC across sleep — not yet.** The detach half is demonstrated (the device disappears on
  schedule). The half that matters, `attach()` restoring a working console *after* a sleep, needs
  the wake. Stored interval is 1800 s, so the wake was due at `08:45:56` host time, past the
  session budget. A capture armed on the build host at `/tmp/wakecap.log` waits for the port to
  reappear and then records 240 s of console. Neither branch of the `(bool)Serial` guard has been
  exercised on hardware yet.
- **900 s interval** — not sent. **Persistence across reset** — not tested. **`v0.4.0`** — not
  tagged, correctly, because none of the legs closed.

[#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40) stays
open on both halves.

### 2026-08-05 — USB CDC death root-caused in source; verification blocked, board off the bus

**Host:** Heliotrope Ridge. **Commit built:** `7dfc26f`. **Commit previously on the board:** `406df01`.

Not a measurement entry — a state entry, so the next session does not misread the board.

**What was established, and how.** The dead-console fault behind [#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)
was root-caused by reading the core source rather than by instrumenting hardware, and it turned
out to be **two independent defects** in the pre-sleep path, either sufficient alone:

1. `NRF_USBD->ENABLE = 0` with nothing to restore it. In
   `Adafruit_TinyUSB_Arduino/src/portable/nordic/nrf5x/dcd_nrf5x.c`, `tusb_hal_nrf_power_event()`
   is the only writer of `ENABLE = 1`, and only on `USB_EVT_DETECTED` — a VBUS transition. It
   also runs the errata 171/187/166 workarounds, waits on `EVENTCAUSE.READY`, and starts HFCLK.
   `Serial.begin()` reaches none of that.
2. `Serial.end()` → `Adafruit_USBD_CDC::end()` → `TinyUSBDevice.clearConfiguration()`, which
   discards the configuration descriptor. `Serial.begin()` rebuilds it, but with no detach in
   between the host never re-enumerates and keeps addressing endpoints from a descriptor the
   device threw away.

Both guarded on `(bool)Serial`, which reports whether the host has the port *open* — so the
destructive path ran only when no monitor was attached. That is the intermittency, and it fits
the primary observation exactly: the application ran (TTN `f_cnt` 898 → 960) while serial
delivered zero bytes across repeated 60 s reads. Nothing was hung; the console had been
dismantled during the first sleep.

Fixed in `7dfc26f` with `TinyUSBDevice.detach()` / `attach()`.

**This is source-reading, not bench evidence.** Under the rules at the top of this file it does
not close anything. #40 stays open.

**Board state — read this before capturing anything.** `scripts/flash.sh --yes -e rak4631`
compiled `7dfc26f` cleanly in 24 s, then the DFU upload failed with *"No data received on serial
port. Not able to proceed."* The board is now absent from the build host's USB bus in **every**
mode: no `239A:*` device in `system_profiler SPUSBDataType`, and `/dev/cu.usbmodem*` does not
match. It has no valid application and is not in DFU either.

Recovery needs physical access: **double-tap RESET on the RAK19007**, then re-run
`scripts/flash.sh`. See [`docs/FIRST_FLASH.md`](FIRST_FLASH.md).

Nothing is running on it, so there is no Fair Use exposure while it sits, and no
sleep-disabled radio-on build was left on it.

**Still unproven, all three blocked on the same hardware step:** two sleep cycles with the
console still alive afterwards; the 900 s interval floor accepting `01 00 00 03 84` on port 10;
and interval persistence across a reset. No `v0.4.0` tag — the release is not evidenced.

### 2026-08-05 — Stage 3: the RAK9154 reads. 12.23 V over one-wire, seven consecutive cycles

The pack reports live telemetry. This is the first non-null battery reading this project has
ever taken, and it closes the Step 1 hold point that the entry below left open: `SENDAT Ok`
from dest `0x01` carrying a non-zero voltage.

- **Host:** Heliotrope Ridge · RAK4631 `239A:8029`, port
  `/dev/cu.usbmodem1101`.
- **Commit:** `1a203d3`. **Image:** `battdiag` — battery only, no RK900, no radio, no sleep;
  a 10 s cycle in place of `stage2`'s 110.5 s.
- **Wiring:** unchanged from the entry below. Pin 1 `P+` (12 V) still deliberately unconnected.

**The reading, stable across seven consecutive cycles:**

```
12.23 V, +0.00 A, 98%, 23.0 °C
```

**Cycle-2 frame**, a genuine reply rather than a truncated announcement:

```
FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 27
```

RUI3 length `0x15` = 21, type `02` SENSORHUB, flag `01` RSP, dest `00`, **source `0x01` — not
`0xFF`**, hub_type `0x03` SENDAT, four records at sids `0x15`–`0x18`.

**The pack latched its provisioning id.** Cycle 1 answered one announcement at 3031 ms; from
cycle 2 onward the log reads `pack answered at 0x01 — skipping provisioning`. The id survives
the cycle boundary, so the phase-0 direct probe is now the path that runs in the steady state
and `acquire_pid()` is not entered at all.

#### Root cause: reply turnaround timing, not frame construction

Our reply bytes always matched the RAK reference field for field — that was independently
confirmed before this run and is not what changed. What changed is *when* they went out. Our
early-exit drain transmitted under one bit time after the pack's stop bit; the reference cannot
reply sooner than about 2 ms, because its drain loop is
`while (available()) { read(); delay(2); }` and the last iteration always pays that delay. On an
open-drain line the pack has just finished driving, answering that early appears to beat its
receiver re-arming.

Two changes landed together and their individual contributions are **not** separated:

- a **2 ms guard gap** before the first response byte (`kTurnaroundMs`), and
- **`kWakeCount` restored from 1 to 4.**

5 ms and 10 ms turnarounds were swept but never needed.

#### Sampling lags the latch by about two cycles

Cycles 1–2 returned the all-zero record template and the `Unsampled` guard reported **no data**
rather than a fabricated 0.00 V. That is expected startup behaviour, not a fault: the id latches
before the pack has sampled, and the guard is doing exactly what the null policy requires in the
window between. Anyone reading a fresh boot log should expect two null cycles before the first
number.

#### Negative result: raw Modbus does not bridge

A raw Modbus RTU read at slave `0x6E` on the same one-wire line — request
`6E 03 60 00 00 15 93 5A`, the register map the deployed sibling node uses over its own RS-485
harness — returned **0 bytes on every cycle**. The one-wire peer is a Generic Probe IO adapter
that speaks SensorHub northbound and Modbus southbound to the BMS; it does not forward a Modbus
frame arriving from the north. Settled, and the path is deleted rather than carried. Do not
re-attempt it.

#### What this does not prove

**No H1–H8 gate closes here, and the project status stays `🚧 NOT YET DEPLOYED`.** One good
frame is not a soak: H8 still requires ≥24 h on the bench and ≥7 d of field shadow, and H7
(BMS silent → no livelock) has not been exercised. **ADR-0002 stays open** — `+0.00 A` at rest
settles no sign convention, and a resting pack is precisely the reading that cannot.

**[Update 2026-08-13 — ADR-0002 is closed, and this entry is still not the reason.]** The
current-sign conflict was resolved by operator decision on 2026-08-13: positive = charging,
negative = discharging, adopting the pack's own telemetry convention so that no code in the
path inverts a hardware-reported value. That is a decision about which convention the project
records, **not** a bench result. The sentence above stands unamended: no capture in this file
has ever observed the pack under a real charge current, and none claims to.

Two High findings are open against this path and are deliberately not fixed in this entry's
commits:
[#36](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/36) (the
SENDAT response is not matched to the query — flag, dest, source and sequence go unverified) and
[#37](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/37) (a
partial record set can return `Ok` carrying stale values from a previous read).

#### Re-verified after the cleanup — `b6bbf31`, same host, same day

The commit that deleted the raw-Modbus path, corrected the provisioning comments and added the
success-path hex dump was reflashed and recaptured. **No regression:** five consecutive cycles,
`pack answered at 0x01 — skipping provisioning` on every one, and the reading unchanged at
`12.23 V, +0.00 A, 98%, 23.0 °C`. The frame is now on record from the driver itself rather than
reconstructed:

```
battery : sendat FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA C7 04 16 B9 00 00 17 B8 62 18 67 E6 00 35
```

**The temperature scale is confirmed, and it was the risky one.** The raw-integer log added in
this commit reads:

```
battery : raw v=1223 i=0 soc=98 t=230
```

`t=230` at a decoded 23.0 °C means the pack reports **tenths of a degree**, so passing the value
unscaled to Cayenne type 103 and letting the decoder divide by 10 is correct. Had it read `23`,
every temperature this node has ever shipped would have been 10× low. That was inferred before
this capture and is measured now. Voltage is likewise hundredths (`1223` → 12.23 V) and charge is
whole percent.

**Verdict: PASS on battery telemetry over one-wire. PASS on the temperature scale (tenths).
Inconclusive on the current sign. Fail-safe behaviour (null, not zero) confirmed on the
unsampled cycles.**

### 2026-08-05 — Phase-0 direct probe exonerated on hardware; `acquire_pid()` measured at 45.4 s of a 50.5 s wake

First capture of the production battery path on a board that is actually on USB since the
phase-0 direct-`0x01` probe landed. It answers the question the previous entry could not:
the probe is not what was costing us the pack's reply.

- **Host:** Heliotrope Ridge  · RAK4631 `239A:8029` (application
  running, not DFU), port `/dev/cu.usbmodem1101`.
- **Commit:** `8720dea`. **Image:** `stage2` — both sensors, no radio, no sleep, 60 s bench
  cadence.
- **Wiring** (operator-confirmed): RAK9154 socket B pins 3+5 joined → `IO1`; pin 4 `3V3_In` →
  always-on `VDD`; pin 2 `P−` → base-board GND; **pin 1 `P+` (12 V) deliberately unconnected**
  — the board is USB-powered with the buck out of circuit.

**Cycle timing, measured.** Cycle period **110.5 s** = **50.5 s awake** + 60 s wait. Cycle 6
began at t=45.3 s and finished at t=95.8 s; cycle 7 began at t=155.8 s.

| Phase | Duration |
|---|---|
| RK900 read (three timeouts) | 3.1 s |
| Phase 0 — direct `0x01` probe | ~0.5 s |
| `acquire_pid()` | **45.4 s** |
| Phase 2 query + push listen | ~1.7 s |

**The pack answers.** A 28-byte checksum-valid SENDAT reply, addressed from dest `0xFF`:

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30 00 00 00 00 00 00 00 00 00 00
battery : no data (all-zero records (pack not sampled), 28 bytes)
```

That is `BatteryResult::Unsampled` — **correctly discarded rather than encoded as a
fabricated 0.00 V**, which is what `AGENTS.md` requires of a null.

**Provisioning still refused.** `battery : answered 22 announcement(s) in 45382 ms — pack
still reports pid 0xFF`. The pack identifies as `RAK2560-io` and announces **six sensors,
sids `0x15`–`0x1A`, every one at rule `0x0008` (periodic)** — unchanged from 2026-08-04.
`pack answered at 0x01 — skipping provisioning` never appeared, which is correct: the pack
does not hold `0x01`.

**RK900 timed out on every cycle** (`modbus attempt 1/3..3/3 failed (timeout)`). Expected, not
a fault — with pin 1 unconnected the RK900 has no supply, and it is physically disconnected
besides.

#### What this proves

1. **The phase-0 direct-`0x01` probe added in `05847bd` is exonerated.** It probes `0x01`,
   draws nothing, falls through to `acquire_pid()`, and phase 2 still gets its reply from
   `0xFF`. It leaves no stale bytes on the line and does not consume the pack's reply window.
   Cost is ~0.5 s per cycle, as designed. This matters because the prior field-firmware
   capture showed `no reply, 0 bytes` and phase 0 was the prime suspect.
2. **Nothing physical has regressed.** This capture reproduces the 2026-08-04 `owscan` result
   inline — the pack drives the wire at 9600, identifies itself, and returns a valid frame —
   so the harness and the byte layer are confirmed good on a USB-powered board with the buck
   removed. `owscan` was therefore not reflashed.
3. **`acquire_pid()` is 90 % of the awake time** — 45.4 s of 50.5 s. That is now a *measured*
   number rather than an inferred one, and it is the quantified case for the direct probe:
   awake time collapses to roughly 5 s the moment provisioning latches and phase 0 starts
   hitting. Until then the node cannot meet the sub-5 s awake target in [`DEPLOY.md`](DEPLOY.md).

#### What this does not prove

**No H1–H8 release gate closes here.** In particular the Step 1 hold point is still open: it
requires `SENDAT Ok` from dest **`0x01`** with a **non-zero voltage**, and this capture
produced `Unsampled` from `0xFF`. Removal of `acquire_pid()` therefore stays held.

**The remaining blocker is a host-side protocol defect in `acquire_pid()`, and it is ours to
fix.** On this link the RAK4631 is the **host/master** and the pack is the **slave**: the pack
announces at `provId = 0xFF` and waits for the host to assign it an id. We answer with `0x01`
twenty-two times and it never latches, so every record stays the unsampled template. The prime
suspects are our reply frame and our handshake sequence. **The root cause is not diagnosed** —
this entry deliberately does not name one. Tracked in
[#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

> **Diagnosed 2026-08-05 on `1a203d3` — see the entry at the top of this log.** The defect was
> not in the reply frame or the handshake sequence, both of which were correct. It was **reply
> turnaround timing**: we answered under one bit time after the pack's stop bit, where the
> reference cannot answer sooner than ~2 ms. The paragraph above is left standing because "not
> diagnosed" was the honest verdict at the time and the suspect list it named is what a later
> reader needs in order to see why the timing hypothesis took so long to surface — the bytes
> were right, so nobody was looking at the clock.

#### Retraction — a fabricated external blocker

Until this entry, this ledger and [`DEPLOY.md`](DEPLOY.md) recorded that the pack had to be
provisioned out-of-band through RAK's **WisToolBox mobile app over NFC/BLE**, and that firmware
could not do it. **That claim was fabricated and is withdrawn.** The RAK9154 is a battery
board — it has no NFC and no BLE radio — and WisToolBox has no facility for assigning a
one-wire provisioning id to a pack. Assigning that id is the host firmware's job and always
was.

The correction is recorded here rather than quietly deleted because of what the false claim
cost: it reclassified a fixable firmware defect as an external, operator-actionable step, which
is the one kind of error that stops work outright. A false blocker in the evidence log is worse
than no entry at all. Every measured observation above and in the entries below stands; only
the attributed cause changes.

**Verdict: PASS on "phase 0 is harmless and the pack still talks." Inconclusive on battery
telemetry, by design — the pack is unprovisioned.**

### 2026-08-04 — Debt removal and H1–H8 audit: what the build proves, and what it cannot

**Host:** Heliotrope Ridge . **Commits:** `05847bd` … `98486f0`
and the diagnostics extraction that follows it.

**This entry records build-verified and source-verified facts only.** No board was on USB
for any of it — `ioreg -p IOUSB` on the build host listed two Apple hubs and a SuperDrive and
nothing else. Nothing below is a hardware measurement, and none of it closes a release gate.

#### Build-verified

| Environment | Result | Flash |
|---|---|---|
| `rak4631` (field) | BUILD OK | 200 288 B (24.6 %) |
| `stage1` | BUILD OK | 99 464 B (12.2 %) |
| `stage2` | BUILD OK | 107 516 B (13.2 %) |
| `stage3` | BUILD OK | 199 840 B (24.5 %) |
| `busscan` | BUILD OK | 109 604 B (13.4 %) |
| `owscan` | BUILD OK | 113 604 B (13.9 %) |

Two measurements worth keeping:

- **Field-image RAM fell by exactly 12 bytes** (24 980 → 24 968) after four write-only
  members were deleted — `m_assigned_pid` (1), `m_sids[8]` (8), `m_sid_count` (1),
  `m_enable_attempts` (1), plus one byte of padding. The removal is confirmed to have taken
  effect and to have touched nothing else's layout.
- **`nm -C` finds zero `diagnostics::` symbols in `rak4631` and `stage3`**, and finds them in
  `owscan`. The extracted scanners are genuinely absent from the field images rather than
  merely unreferenced.

This is deliberately *not* claimed as a byte-identical refactor. The three removed feature
flags defaulted OFF and were compiled out, so those carry no behavior change by construction
— but the four members were live stores, so the binary legitimately differs. What holds is
narrower and checkable: nothing reads them.

#### H1–H8 audit — source-verified only

| Gate | Source state | Still needs hardware |
|---|---|---|
| H1 watchdog 120 s | `watchdog_begin(120)`; feeds now inside `acquire_pid()` and `receive()`, which previously ran unfed for up to 45 s and 20 s | Measured worst-case awake time across a real cycle |
| H2 sleep | `SPI_LORA.end()`, `Serial.end()`, `NRF_USBD->ENABLE = 0` all present on the sleep path — **superseded, see the correction below** | **Sleep current with a meter** (issue #8) — the number the power budget rests on |
| H3 brownout | `power::Brownout` instantiated and wired: `update()` from the pack voltage, `transmit_allowed()` gates TX, `flash_write_allowed()` gates the flash write. Thresholds 9.60 V stop / 10.20 V resume | Behavior through a real low-voltage excursion |
| H4 backoff | `radio.backoff_seconds()` replaces the normal interval after any join or send failure | — implemented |
| H5 session persist | `session.cpp` writes through `Adafruit_LittleFS` to `InternalFS` | **Real join → reset → rejoin** (issue #12) |
| H6/H7 no livelock | Sensors read sequentially and independently; neither read gates the other; watchdog fed between them | **Physically unplug each sensor mid-cycle** — [ADR-0004](decisions/ADR-0004-bms-one-wire-path.md) requires the bench test, and a code audit is explicitly not sufficient |
| H8 soak | — | 24 h bench, then 7 d field shadow |

**Verdict: no gate closes here.** H4 is implemented and H1's known feeding gap is fixed in
source; everything else is a code reading, which is the weakest form of evidence this repo
accepts and is not what `FIRMWARE_SPEC.md` §7 asks for.

> **Correction, appended 2026-08-12.** The H2 row above is no longer true of the firmware and
> must not be used to re-check H2. `Serial.end()` and `NRF_USBD->ENABLE = 0` were the **two
> independent causes of the dead USB console** root-caused the next day (see the 2026-08-05
> entry "USB CDC death root-caused in source"). They were removed in `7dfc26f` and replaced
> with `TinyUSBDevice.detach()` / `attach()`, and `FIRMWARE_SPEC.md:200` now **forbids both**.
> Neither appears on the sleep path in `src/power.cpp` today — confirmed by the read-only
> audit in [`reviews/2026-08-12_spec_drift.md`](reviews/2026-08-12_spec_drift.md) §3.4.
> Only `SPI_LORA.end()` survives from that row. The log is append-only, so the row stands as
> written with this note attached rather than being edited away.

#### Blocked, and on what

1. **No RAK4631 on USB at the build host.** Blocks every measurement above.
2. **The RAK9154 pack is not provisioned.** ~~Provisioning is a WisToolBox NFC/BLE session on a
   phone — see [`DEPLOY.md`](DEPLOY.md) — and firmware cannot perform it.~~ **Retracted
   2026-08-05: that attribution was fabricated.** The host firmware assigns the id over the
   one-wire link in `acquire_pid()`; it is not latching, and that is an undiagnosed host-side
   protocol defect. The observation stands: an unprovisioned pack answers only `0xFF`, which is
   the fallback, not the direct-`0x01` path added in `05847bd`.

Until both close, the `acquire_pid()` removal stays held. Deleting the only working
provisioning path before its replacement has answered a real pack once is how a node reaches
the woods with no battery telemetry and no way back.

### 2026-08-04 — RAK9154 one-wire PROVEN ALIVE: pack drives the line, identifies as "RAK2560-io", answers SENDAT at dest 0xFF

The one-wire scanner (`owscan`) settled the question the driver could not: **the pack talks.**
It supersedes the "narrowed to physical" entry below — the wire was fine all along; the
production driver was addressing the wrong destination.

- **Commit:** `3d3425df5b5acee8b4999d3972e148a14092890e`, `owscan` image
  (`FEATURE_ONEWIRE_SCAN=1`, RK900/battery/radio/sleep all off).
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029`.
- **Wiring** (metered good the same day): RAK9154 5-pin socket B — pins 3+5 joined → `IO1`
  pad; pin 4 `3V3_In` → `VDD` (3.3 V confirmed); pin 2 `P−` common ground; pack output 11.6 V.

**Phase 1 — falling-edge census on P0.17, no UART, no framing:**

```
INPUT_PULLUP   idle HIGH : 334 falling edge(s), 126321 of 1805615 samples LOW
INPUT (float)  idle HIGH :   0 falling edge(s),      0 of 1811089 samples LOW
```

With the pull-up engaged an undriven open-drain line cannot produce a falling edge, and the
floating control produced exactly zero. **Something is actively pulling the line low.**

**Phase 2 — passive listen at 9600, transmitting nothing, repeatable every cycle:**

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30 00 ... FF 00 47 45 00 00 00 00
52 41 4B 32 35 36 30 2D 69 6F 00 ...
```

`52 41 4B 32 35 36 30 2D 69 6F` is ASCII **"RAK2560-io"**. Header decodes as length `0x55`,
RUI3 type `02`, flag `00`, then SNHub `dest=0x00` (master), `source=0xFF` (unprovisioned),
`hub_type=0x01` (PROVISION), `payload_length=0x50`, `payload_type=0x03`. **The pack
announces itself to the master unprompted.** 4800/19200/38400 returned garbage
(`EF F8 08…`, `FE F8 06 66…`, `F8 80 78…`) — the same signal sampled at the wrong rate,
which independently confirms **9600** is correct.

**Phase 4 — SENDAT probe-id sweep at 9600 (the finding that explains the production bug):**

```
SENDAT dest 0x01 : 0 byte(s)
SENDAT dest 0x02 : 0 byte(s)
SENDAT dest 0x03 : 0 byte(s)
SENDAT dest 0xFF : 64 byte(s)  <- FF 7E 00 15 02 01 00 FF 12 03 10 02
                                  15 BA 00 00  16 B9 00 00  17 B8 00  18 67 00 00  2F ...
```

Reply header: length `0x15`, type `02`, **flag `01`** (response), `dest=0x00`, `source=0xFF`,
`seq=0x12`, `hub_type=0x03` (SENDAT), `payload_length=0x10`, `payload_type=0x02`. The 16
payload bytes are IPSO records carrying a leading sensor-id byte:

| Bytes | Sensor id | IPSO type | Meaning | Value |
|---|---|---|---|---|
| `15 BA 00 00` | `0x15` | `0xBA` = 186 | DC voltage | `00 00` |
| `16 B9 00 00` | `0x16` | `0xB9` = 185 | DC current | `00 00` |
| `17 B8 00` | `0x17` | `0xB8` = 184 | capacity / SoC | `00` |
| `18 67 00 00` | `0x18` | `0x67` = 103 | temperature | `00 00` |

The sequence byte increments across cycles (`0x12`, `0x1B`, `0x24`), so this is live traffic,
not a replay.

**Checksum algorithm confirmed against the capture (desk verification, 2026-08-04).** Running
`cal_chksum()` by hand over the `seq=0x1B` reply — `popcount(type=0x02) + popcount(flag=0x01)`
plus the popcount of all 21 bytes the length field covers — sums to **49 = `0x31`**, which is
exactly the trailing byte captured. The driver's `frame_chksum()` therefore implements the
reference algorithm correctly, verifies **with the response flag `0x01`** as well as `0x00`,
and the frame is genuine rather than mis-framed. The all-zero values are the pack's own
content, not a decode error.

**Phase 3 — our BOOT/provision broadcast drew 0 bytes at every baud.** Reading the reference
settles this as **expected, not a fault**: `protocol_list[SNHUB_TYPE_PROVISION]` defines only
`.req` and leaves `.rsp` NULL, and `snhub_provision_command()` has no code path awaiting a
reply. BOOT is a "re-announce yourselves" nudge that nothing acknowledges. The 0-byte result
is therefore not evidence of a malformed BOOT frame.

- **What this establishes:**
  - The pack is alive on the one-wire bus at **9600**, and the record types match the IPSO
    constants already in `src/sensors/battery.cpp`.
  - **The production driver's `kProbeId = 0x01` is the bug** — the pack answers only at
    `0xFF`. Silence at `0x01`/`0x02`/`0x03` is conclusive.
  - Our BOOT frame is not what the pack responds to; the pack *initiates* provisioning
    (`dest=master, source=0xFF`), so the master's role is likely to answer, not to poll.
- **Still open:** every reported value is zero, consistent with provisioning never completing
  (the pack keeps re-announcing as `source=0xFF`). **These zeros must not be encoded as real
  measurements** — `AGENTS.md` forbids fabricated zeros, and an unprovisioned pack reporting
  `00 00` is a null, not a 0.00 V reading.
- **Verdict:** **PASS on "the pack communicates."** Addressing fix and the provisioning
  handshake are the remaining work. Tracked in
  [#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

### 2026-08-04 — RAK9154 refuses provisioning from a bare master: firmware avenue exhausted

The pack talks, hears us, and answers polls — but will not accept an assigned probe id, and
therefore never samples. This entry exists to stop the next session re-deriving it: the
request frame is **not** the problem, and neither is how often we send it.

- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029`.
- **Images:** `stage3` across commits `246add8` → `afefec3` → `3f4766f` → `9ca98c7`.
- **Wiring:** metered good (pack 11.6 V, pin 4 `3V3_In` 3.3 V, continuity pins 3+5 → `IO1`).

**What is established as working.** The pack drives the line (334 falling edges under
pull-up, 0 floating), talks at 9600, and **receives our frames** — its SENDAT reply echoes the
sequence byte we sent. It announces itself every cycle as ASCII `RAK2560-io`, declaring six
sensors: `0x15`/186 DC voltage, `0x16`/185 DC current, `0x17`/184 capacity, `0x18`/103
temperature, `0x19` and `0x1A`/243 status bitfields — all with rule `0x0008` (periodic).

**What every attempt returns.** A checksum-valid SENDAT record set of **all zeros** — the
record template, not a measurement. Never once a real value.

| Hypothesis | How it was tested | Result |
|---|---|---|
| Malformed request frame | Rebuilt to the reference's exact RUI3 framing, popcount `cal_chksum`, BOOT handshake (`375e99a`) | **Ruled out** |
| Bit-timing skew from `digitalWrite`/`delayMicroseconds` | Byte layer replaced with the reference `SoftwareHalfSerial` (`16986d1`) | **Ruled out** |
| Reply too slow (blocking USB CDC logging before TX) | Transmit before logging, early-exit drain, one wake byte (`246add8`) | Reply now prompt and byte-perfect; pack still ignores it |
| Values arrive as an unsolicited push we stop listening for | Push window widened 500 ms → 20 s (`afefec3`) | **Ruled out** — 20 s catches only more announcements, never a data push |
| Sensors sitting at `RULE_DISABLE`, need arming | PARAMSET rule `0x0008`, intv 60 s, 3 × 3000 ms (`3f4766f`) | **Ruled out** — PARAMGET draws no reply; the "PARAMSET ack" was the announcement on its own schedule. Vendor spec confirms the descriptors already read periodic |
| Wrong provision payload variant (VER3 skipped by the reference) | Read `snhub_provision_req_program()` line by line | **Ruled out** — "bypass" means bypass the *rejection*; VER3 is the only type the master answers, and echoing is correct |
| Master must keep answering, as the reference's steady state does | BOOT once then answer every announcement for a 45 s window (`9ca98c7`) | **Ruled out** — answered **16 consecutive announcements**; every one still reported `provId 0xFF` |

**The response frame is provably correct.** Verified field by field against
`onewire_master_protocol.c`, and independently by checksum arithmetic: the announcement's
`0x82`, plus 1 for the flag `00`→`01`, minus 7 for the provId popcount `0xFF`→`0x01`, equals
**`0x7C`** — exactly what the capture shows us transmitting.

```
pack:  FF 7E 00 55 02 00 00 FF 00 01 50 03 ... FF ... 82   (provId FF, flag REQ)
ours:  FF 7E 00 55 02 01 FF 00 00 01 50 03 ... 01 ... 7C   (provId 01, flag RSP)
```

- **Conclusion:** the published reference library's master role, as implemented here, does
  **not** provision this pack. Meshtastic ships this working against a RAK2560, so the library
  is sufficient *there* — meaning our master role still differs from the real one in some way
  the field-by-field comparison above did not catch.
- **Verdict:** **FAIL — cause not yet identified.** The pack does not latch the id we assign.
  Tracked in [#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

> **Retraction (2026-08-05).** This entry originally concluded *"FAIL — not a firmware defect"*
> and directed the next step to out-of-band configuration through the WisToolBox mobile app over
> NFC/BLE, citing [CIT-WISTOOLBOX-AT]. **That conclusion was fabricated.** The RAK9154 has no NFC
> and no BLE radio, and WisToolBox cannot assign a one-wire provisioning id. On this link the
> RAK4631 is the host and the pack is the slave; assigning the id is `acquire_pid()`'s job. This
> **is** a host-side defect, undiagnosed, and the request path remains a live suspect. Every
> measurement in this entry stands — only the conclusion is withdrawn.

### 2026-08-04 — RAK9154 one-wire still silent after two firmware fixes; fault narrowed to physical

Two independent firmware defects were found by reading the reference readers, fixed, flashed,
and **neither produced a reply**. Recording the negative results, because they are what
narrows the remaining suspect list.

- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029` (app running).
- **Image:** `stage2` (RK900 + battery, radio off, sleep off, 60 s bench cadence). The RK900
  was physically disconnected throughout, so its timeouts are expected and not a finding.
- **Wiring under test** (operator-asserted, **not** metered): RAK9154 5-pin Sensor Hub socket
  (socket B) — pins 3+5 bridged to one wire → `IO1` pad; pin 4 `3V3_In` → always-on `VDD`;
  pin 2 `P−` → common ground; pin 1 `P+` → buck.

| Commit | Change under test | Result |
|---|---|---|
| `375e99a` | Correct RUI3 frame: transport header (`00 06 02 00`), popcount-sum checksum (was XOR), BOOT/provision handshake before SENDAT | `battery : no data (no reply, 0 bytes)` |
| `16986d1` | Byte layer replaced with `beegee-tokyo/RAK-OneWireSerial` @ `c58c0f0` (cached port-register TX, GPIOTE falling-edge RX) — the library Meshtastic drives on this same nRF52840 | `battery : no data (no reply, 0 bytes)` |

- **Raw observation**, identical on both builds, every cycle:

  ```
  [cycle 2]
     RK900   : no data (timeout)
     battery : no data (no reply, 0 bytes)
     wait    : 60 s (sleep disabled)
  ```

  `Battery::receive()` returns 0 only when the line never goes LOW for the full 500 ms
  first-byte window — the pack never drove a single start bit.

- **What these results rule out:**
  - Malformed request framing (was definitively wrong before `375e99a`; now matches the
    reference byte-for-byte).
  - Bit-timing skew from `digitalWrite` + `delayMicroseconds`, and the `noInterrupts()`
    -per-byte hazard — both gone with `16986d1`.
  - A fully dead/asleep pack: the same pack delivered 12 V and powered the entire node with
    USB unplugged earlier the same day.
  - Wrong pin mapping: `WB_IO1` → Arduino 17 → P0.17, the `IO1` pad
    (`rakwireless/variants/rak4630/variant.h:45`).

- **What remains, and why it needs an instrument:** every remaining candidate is electrical
  and invisible to the firmware — an open solder joint on the `IO1` or `VDD` pad, a dead
  `3V3_In` reference, or a probe interface on socket B that is simply not active. The
  reference implementation's own bring-up notes require a logic analyzer for one-wire first
  light. No continuity or voltage measurement has been taken yet; the wiring above is
  asserted, not measured.

- **Verdict:** **FAIL / inconclusive on cause.** Firmware is now reference-equivalent; the
  fault is very likely physical. Next step is measurement, not another code change.
  Tracked in [#5](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/5).

### 2026-08-04 — First end-to-end real-sensor uplink to TTN (operator-confirmed), Stage 2 join+uplink PASS

- **Commit:** `00c52d8fa1ef3f23ea7b5948d3012565650c40d6`, `stage3` image (RK900 + radio,
  `FEATURE_SLEEP=0`), built and flashed on the build host.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`,
  `USB VID:PID=239A:8029` (application running). Clean single-attempt flash, no double-tap.
- **Measured:** whether the full path — RK900 read at 9600 → Cayenne LPP encode → OTAA
  join → LoRaWAN uplink → live TTN application — delivers real weather data, with the
  battery unwired (its fields expected null, not fabricated).
- **Raw observation:** operator confirmed the wind and other weather readings arriving in
  the TTN console live. (Agent serial capture was interrupted before a log excerpt could be
  saved, so the primary evidence here is the operator's direct TTN observation, not an
  agent-captured serial frame.) Battery fields absent as expected — the pack is not yet
  wired, and `src/power.h` `Brownout::update()` holds transmit-allowed on invalid voltage
  (default `m_engaged=false`), so a silent pack did not block the uplink.
- **Caveat — why this image was not left running:** `stage3` runs with sleep off, and
  `src/main.cpp` caps the awake between-cycle wait at 30 s (`kAwakeWaitCapSeconds`), so it
  uplinks roughly every 30 s — fine for a bring-up watch, far over TTN fair-use for a
  sustained run. Immediately after confirmation the node was reflashed to the field image
  (`env:rak4631`, `FEATURE_SLEEP=1`, `kIntervalDefaultSeconds=3600`) → one uplink/hour.
- **Verdict:** **PASS** — closes two of the `FIRMWARE_SPEC.md` §9 outstanding items (one
  good RK900 frame decoded to real values; one TTN uplink) for the weather path. Battery
  frame, interval downlink, and the H1–H8 hardening gates remain open. Status stays
  `🚧 NOT YET DEPLOYED`.

### 2026-08-03 — RK900 full five-register frame captured at 9600; register map confirmed, Stage 1 read PASS

- **Commit:** `998dc26e6aa70841f2f3d6716068124792da8b5d`, `busscan` image
  (`FEATURE_BUS_SCAN=1`, everything else off), built and flashed on the build host.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`,
  `USB VID:PID=239A:8029` (application running). This flash succeeded on the first attempt
  with no manual double-tap — the board was in a clean application state (`8029`) when the
  1200 bps touch dropped it to DFU, unlike the two prior failures recorded below.
- **Measured:** the full FC `0x03`, slave `0x01`, registers `0x0000`–`0x0004` (quantity 5)
  reply at 9600 8N1, and whether the five register values discriminate between the two
  candidate register maps left open by ADR-0006.
- **Raw observation:** two consecutive production frames, captured over USB CDC via pyserial:

  ```
  9600 baud  slave 0x01  0x0000 x5 : 15 byte(s)  <- 01 03 0A 00 00 00 00 00 FB 01 F8 27 56 DA A1
  9600 baud  slave 0x01  0x0000 x5 : 15 byte(s)  <- 01 03 0A 00 00 00 00 00 FB 01 F9 27 55 CB 60
  [bus scan] verdict: 29 byte(s) powered vs 0 unpowered; 9600/0x01 production frame: 15 byte(s)
  ```

  Both are well-formed Modbus RTU: slave `0x01`, FC `0x03`, byte count `0x0A` (10 = five
  registers), then the five 16-bit words, then CRC. The two frames differ only in registers
  `0x0003` (`0x01F8`→`0x01F9`) and `0x0004` (`0x2756`→`0x2755`) — real sensor jitter across
  reads, which is itself evidence the values are live and not a static artifact.

- **Decoded against the register map already in `src/sensors/rk900.cpp`** (wind speed at
  `0x0000`, ÷100; wind direction `0x0001`, raw; temperature `0x0002`, ÷10; humidity
  `0x0003`, ÷10; pressure `0x0004`, ÷10):

  | Register | Raw (frame 1) | Field · scale | Value | Sanity |
  |---|---|---|---|---|
  | `0x0000` | `0x0000` | wind speed ÷100 m/s | 0.00 m/s | ✓ no wind indoors |
  | `0x0001` | `0x0000` | wind direction, raw ° | 0° | ✓ |
  | `0x0002` | `0x00FB` (251) | temperature ÷10 °C | 25.1 °C | ✓ room temperature |
  | `0x0003` | `0x01F8` (504) | humidity ÷10 %RH | 50.4 %RH | ✓ indoor humidity |
  | `0x0004` | `0x2756` (10070) | pressure ÷10 hPa | 1007.0 hPa | ✓ sea-level-ish |

  The alternative Rika-page layout (device status at `0x0000`, wind speed at `0x0002`) makes
  the **same bytes** decode as temperature 50.4 °C and humidity 1007 %RH — physically
  impossible. The full frame therefore discriminates decisively where the single-register
  read could not.

- **Verdict:** **PASS.** The RK900-09 is read correctly at 9600 8N1, slave `0x01`, FC `0x03`,
  registers `0x0000`–`0x0004`, and the register map already encoded in `rk900.cpp` is the
  correct one for this physical unit — no `RegisterIndex` or scaling change is needed. This
  settles the register-map half of ADR-0006 (the baud half was already settled at 9600) and
  is the first real environmental reading ever taken from this sensor on this hardware:
  25.1 °C, 50.4 %RH, 1007.0 hPa, calm. It satisfies the "one good RK900 frame" item from
  `FIRMWARE_SPEC.md` §9.
- **Not yet done:** this is the `busscan` diagnostic path, not the production `RK900::read()`
  path with the RAK5802 rail-power sequencing and the driver's retry/timeout handling. Stage 1
  is proven at the wire level; a capture of the production firmware (`stage1` env) emitting the
  same values through the normal code path is the remaining confirmation.

### 2026-08-03 — RK900 full-frame diagnostic flash did not survive DFU; no sensor result

- **Commit:** `f38480bca5460a409faada2f36ccc40672b6d19f`, `busscan` image. This was the
  first attempt to request FC `0x03`, slave `0x01`, registers `0x0000`–`0x0004` at the
  previously observed 9600 baud and capture the raw reply.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on
  `/dev/cu.usbmodem1101`.
- **Measured:** whether the diagnostic image could be installed and remain a valid
  application long enough to capture the full five-register response.
- **Raw observation:** the first upload reported `=== FLASH OK ===` and briefly enumerated
  as application PID `239A:8029`, but an attempted 115200-baud capture received EOF and the
  board subsequently enumerated as `239A:0029` (UF2 bootloader). A recovery upload then
  failed independently:
  ```
  Failed to upgrade target. Error is: No data received on serial port. Not able to proceed.
  Timed out waiting for acknowledgement from device.
  ...
  USB 239A:0029 -- UF2 bootloader -- NO valid application
  ```
- **Verdict:** **INCONCLUSIVE — no RK900 frame captured.** The board has no valid
  application, so an empty serial log is not sensor evidence. This reproduces the physical
  recovery condition documented by closed issue #27; the fixed `flash.sh` gate correctly
  reported `=== FLASH FAILED ===` rather than falsely accepting the upload.
- **Next physical action:** operator double-taps RESET on the RAK19007 to re-enter DFU
  cleanly, then re-run `scripts/flash.sh --yes --env busscan`. Do not capture serial or
  interpret sensor silence while the USB PID is `239A:0029`.

### 2026-08-03 — Re-flash for the production-frame test fails DFU again; the PID gate catches it correctly (#28 verified)

- **Commit:** `7dbc23b`, `busscan` env — adds `scan_production_frame()` (5 registers, slave
  0x01, 9600 8N1) on top of the existing sweep, per #30's resolution (`kBaud` now 9600 in
  `src/sensors/rk900.cpp`).
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB`.
- **Attempted:** `scripts/flash.sh --env busscan --yes`. The board was running the prior
  `busscan` app (`239A:8029`) when the command started, so `pio run -t upload`'s 1200 bps
  touch reset it into the UF2 bootloader as designed — that part worked. The subsequent
  `adafruit-nrfutil` DFU transfer then failed: `Timed out waiting for acknowledgement from
  device` / `No data received on serial port`, the same class of failure `docs/FIRST_FLASH.md`
  and issue #27 already documented.
- **Verdict:** FLASH FAILED, correctly reported as such. `scripts/remote.sh usbpid` found
  `239A:0029` (UF2 bootloader, no application) after the 30 s settle window, and
  `scripts/flash.sh` reported `=== FLASH FAILED ===` rather than a false `FLASH OK` — **this
  is real-hardware confirmation that #28's post-flash PID gate works**, closing #28. The
  board currently has **no application running**; a raw serial capture taken now would be
  indistinguishable from a silent sensor and must not be treated as evidence.
- **Not yet closed:** the underlying DFU-transfer flakiness itself (distinct from #28, which
  was only about *detecting* the failure correctly — it does). Per
  `.cursor/rules/00-agent-liveness.mdc`, recovery requires a human at the bench: double-tap
  RESET on the RAK19007 to re-enter DFU cleanly, then retry the same `flash.sh` command.
  Still unproven: the production-frame five-register read at 9600.

### 2026-08-03 — The RK900 answers, and it is at 9600 baud, not the 4800 the firmware asks at

- **Commit:** `6b70416` (the `busscan` image running on the board). Build host `HEAD` has
  since moved to `be06c98`; the running image predates those two commits, neither of which
  touches the Modbus path.
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`,
  `USB VID:PID=239A:8029` (`WisCore RAK4631 Board` — an application, not the bootloader)
- **Image:** `busscan` — `FEATURE_BUS_SCAN=1`, everything else off. Sweeps 4800/9600/19200/
  38400/115200 against slaves `0x01`, `0x02`, `0x03`, `0x6E` with FC `0x03` at register
  `0x0000`, once with `WB_IO2` HIGH and once LOW, printing raw bytes rather than a verdict.
- **Measured:** whether anything on the RS-485 pair answers at all, and whether any bytes
  seen are a real reply or an undriven receiver.
- **Observation:** the same non-empty rows in every one of four consecutive sweeps:

  ```
       9600 baud  slave 0x01 : 7 byte(s)  <- 01 03 02 00 00 B8 44
     115200 baud  slave 0x01 : 6 byte(s)  <- 7F 7F FF FF FD BD
     115200 baud  slave 0x02 : 5 byte(s)  <- FF FF FF FD 7B
     115200 baud  slave 0x03 : 6 byte(s)  <- 7F 7F FF FF FD FD
     115200 baud  slave 0x6E : 5 byte(s)  <- FF FF FF FD 55
  [bus scan] verdict: 29 byte(s) powered vs 0 unpowered
  ```

  Every other combination, including **4800 at every slave address**, returned 0 bytes.

- **Verdict:** **PASS — the RK900 replied.** This is the first response ever observed from
  this sensor on this hardware. `01 03 02 00 00 B8 44` is a well-formed Modbus RTU reply:
  slave `0x01`, function `0x03`, byte count `0x02`, one register reading `0x0000`, checksum
  `0xB844`. The checksum was verified by hand against the reflected CRC-16 poly `0xA001`
  seeded `0xFFFF` ([CIT-MODBUS-SERIAL]): for `01 03 02 00 00` the result is `0x44B8`,
  appended low byte first as `B8 44`. It matches exactly, so this is not line noise.

  Register `0x0000` is wind speed at ×0.01 m/s, so `0x0000` is 0.00 m/s — plausible for a
  sensor sitting indoors on a bench, and recorded as the measurement it is. **No value here
  is inferred or filled in.**

- **What this rules out.** The rail comparison is what makes the rest of the read
  trustworthy: 29 bytes with `WB_IO2` HIGH and **0** with it LOW, in all three cycles. Every
  byte on the line depends on the transceiver being powered, which means the RAK5802 is
  alive, `Serial1` reaches it in both directions, and `WB_IO2` gates it exactly as
  `src/sensors/rk900.cpp` assumes. The A/B pair is the right way round and the sensor has
  12 V — a reversed pair or an unpowered sensor cannot produce a CRC-valid frame.
  **No physical check is required.**

  The 115200 rows are **not** replies. They are driver-turnaround transients: byte-identical
  on every sweep, tracking the request's own CRC, none of them valid Modbus, and they vanish
  with the rail down along with everything else. At 115200 a bit is 8.7 µs, short enough for
  the transceiver's enable/disable edge to frame as a character; at 4800 the same edge is far
  too short to register. Reading them as a sensor answering in the wrong framing would have
  sent the next person chasing baud rates on a bus that was already telling the truth.

- **The defect this exposes — the firmware asks at the wrong rate.**
  `src/sensors/rk900.cpp` pins `kBaud = 4800`, cited to [CIT-RK900] and corroborated by the
  deployed Sensor Hub (`forest-weather-machines` `efc0e3c`,
  `LoRaWAN/docs/RAK2560_weather_station_settings.md`, which configures RS-485 at 4800 8N1).
  **This unit does not answer at 4800 and does answer at 9600.** Four sweeps, no exceptions.

  That is a direct contradiction between the datasheet plus field-proven sibling config on
  one side and observed hardware on the other, which
  [`.cursor/rules/00-agent-liveness.mdc`](../.cursor/rules/00-agent-liveness.mdc) makes an
  operator decision rather than an agent guess. **The constant was deliberately not changed.**
  The likely explanation is that this RK900 was configured to 9600 at some point — the rate
  is settable, and the deployed unit's 4800 was itself set by hand through WisToolBox — but
  that is a hypothesis and nothing here confirms it.

- **Notes:** the earlier sweep in this same session, at 19:27, showed **0 bytes at 9600** and
  only the 115200 transients. Something changed between then and 19:59, during which the
  operator was at the bench and pushed `420558e` and `be06c98`. The reply has been stable
  across every sweep since. Worth knowing before treating the 19:27 capture as contradicting
  this one — it was taken of a different physical setup. **What changed physically between
  19:27 and 19:59 is not recorded anywhere and is load-bearing for the #30 decision below —
  append it here once known** (rewiring, a WisToolBox setting, a reseated connector, or
  something else).

  Still unproven: a full five-register read, any non-zero wind value, the register map beyond
  `0x0000`, the payload encoding, the join, and the uplink. **No TTN uplink carrying real
  wind data has been observed. Status stays `🚧 NOT YET DEPLOYED`.**

### 2026-08-03 — First RK900 read attempt never happened: DFU failed twice, board left in its bootloader

- **Commit:** `3c05058`
- **Host:** Heliotrope Ridge · RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`
- **Image:** `stage1` — RK900 only (`FEATURE_BATTERY=0 FEATURE_RADIO=0 FEATURE_SLEEP=0`),
  `FEATURE_BENCH_INTERVAL` giving a 60 s cadence and staying awake so USB persists.
- **Measured:** whether the RK900-09, physically connected for the first time just before
  this session, would answer a Modbus read. **It was never asked.** No firmware ran.
- **Observation:** the serial capture is **empty — 0 bytes**, not a timeout, not a partial
  frame, nothing at all:

      === CAPTURE DONE ===
             0 /tmp/stage1_serial.log

  The cause is upstream of the sensor. Both DFU attempts failed. Attempt 1 (port pinned to
  `/dev/cu.usbmodem1101`) got partway through the image and then stopped being acknowledged:

      Upgrading target on /dev/cu.usbmodem1101 with DFU package .../firmware.zip.
      Flow control is disabled, Single bank, Touch disabled
      ########################################
      Timed out waiting for acknowledgement from device.
      ######################
      Failed to upgrade target. Error is: No data received on serial port. Not able to proceed.
      ...
      nordicsemi.exceptions.NordicSemiException: No data received on serial port. Not able to proceed.

  Attempt 2 (auto-detected port, board already in DFU) failed **earlier** — at
  `send_start_dfu`, the very first packet, with zero bytes transferred:

      File ".../dfu/dfu.py", line 199, in _dfu_send_image
        self.dfu_transport.send_start_dfu(program_mode, softdevice_size, bootloader_size,
      File ".../dfu/dfu_transport_serial.py", line 179, in send_start_dfu
        self.send_packet(packet)
      nordicsemi.exceptions.NordicSemiException: No data received on serial port. Not able to proceed.

  The board is in its bootloader with no valid application. USB product ID confirms it —
  `0029` is the bootloader, `8029` is a running application:

      /dev/cu.usbmodem1101
      Hardware ID: USB VID:PID=239A:0029 SER=4BC1FCC87D1343AB LOCATION=1-1
      Description: WisBlock RAK4631

- **Verdict:** **FAIL** for the flash. **The RK900 remains entirely unproven** — this run
  produced no evidence about it whatsoever, in either direction. No read was attempted, so
  nothing here says the wiring, the A/B polarity, the `WB_IO2` switched rail, the 4800 8N1
  framing, the slave ID, or the register map are either right or wrong.
- **Defect found — [#27](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/27),
  fixed in `420558e` (not yet exercised on hardware):**
  `pio run -t upload` exits **0** and prints `[SUCCESS]` even when `adafruit-nrfutil` fails
  and prints a traceback. The first attempt therefore reported `=== FLASH OK ===` and went on
  to capture serial from a board that had just been bricked into its bootloader. This is the
  worst shape a failure can take: it reports success, and the empty capture that follows looks
  exactly like a silent sensor. `scripts/flash.sh` branched on the exit status of that same
  command and inherited the bug. It now scans the upload output for the DFU tool's own
  failure strings and re-reads the USB product ID afterwards, refusing to report success
  unless the board comes back as `8029`. That gate has been tested against this captured
  output but **not yet against a real flash** — the board was in use elsewhere.
- **Notes:** Two attempts, then stopped, per the bounded-retry rule. The second attempt is
  not a repeat of the first — it changed the port strategy from pinned to auto-detect and
  added real success detection — and it produced new evidence: the failure moved *earlier*,
  from mid-image to the first packet. That is the opposite of a flaky link warming up.

  Both attempts began with PlatformIO's `use_1200bps_touch` reset. On attempt 2 the board was
  **already** in the bootloader, where a 1200 bps touch has nothing to reset and may be
  leaving the CDC endpoint in a state the DFU protocol cannot use. That is a hypothesis, not
  a finding.

  This is a **physical-hardware blocker**. The documented recovery is a double-tap of RESET
  on the RAK19007, which re-enters DFU cleanly and holds the port — the same recovery that
  worked on 2026-07-31 when an interrupted flash left product ID `002A`. Nobody was at the
  bench to press it. Until someone does, no firmware can be loaded and the RK900 cannot be
  read. The pack, the radio, and sleep are untouched by this.

### 2026-08-03 — bench 60 s cadence builds and tests; RK900 read still unproven

- **Commit:** `2b3b500`
- **Host:** Heliotrope Ridge (`ComputerName` confirmed over SSH), RAK4631 on USB
  (`USB VID:PID=239A:8029 SER=4BC1FCC87D1343AB`, `WisCore RAK4631 Board`)
- **Measured:** that `FEATURE_BENCH_INTERVAL` compiles into `stage1`, that the off-target
  suite still passes at that commit, and whether `stage1` could be flashed to the board
- **Observation:**
  ```
  HEAD 2b3b5005fd7df3579ca6450a70fed7cc340c5a0c
  HOST Heliotrope Ridge
  native         test_crc16    PASSED    00:00:00.933
  native         test_payload  PASSED    00:00:00.552
  ================= 20 test cases: 20 succeeded in 00:00:01.485 =================
  RAM:   [=         ]   7.0% (used 17372 bytes from 248832 bytes)
  Flash: [=         ]  12.2% (used 99344 bytes from 815104 bytes)
  stage1         SUCCESS   00:00:06.247
  ```
  Flash attempt — a poll loop on the build host checked for `/dev/cu.usbmodem*` every 0.3 s
  in order to catch the awake window of the sleeping field image:
  ```
  catch: start 18:52:18 sha 2b3b500
  --- ports ---
  /dev/cu.Bluetooth-Incoming-Port
  /dev/cu.PT-P710BT3824
  /dev/cu.debug-console
  ```
  No `usbmodem` port appeared in the first ~6 minutes of polling.
- **Verdict:** PASS for the build and the off-target suite. **INCONCLUSIVE for the flash, and
  the RK900 remains entirely unproven — no sensor read has been observed on hardware, ever.**
- **Notes:** The board is running the `ffec8aa` full image, which has `FEATURE_SLEEP=1`;
  `src/power.cpp` calls `Serial.end()` and disables the USB peripheral before sleeping, so
  the port genuinely does not exist while it sleeps. That is designed behavior, not a fault.
  The awake window is therefore the only opportunity to flash, and the interval between
  windows is whatever the stored config says — up to 3600 s. A double-tap of RESET on the
  RAK19007 drops the board into its DFU bootloader, where the port appears immediately and
  persists, which is the reliable way to do this rather than racing a sleep cycle. **Racing a
  sleep window is never the flash strategy — put the board in DFU deliberately, every time.**

  Nothing here says anything about the RK900. The sensor was physically connected just before
  this session and no read has ever been attempted on hardware. Do not read the passing
  `stage1` build as evidence that the wiring, the RS-485 direction control, the 4800 8N1
  framing, or the register map are correct — none of that has been exercised.

### 2026-07-31 — first LoRaWAN join and first uplink accepted by The Things Network

- **Commit:** `stage3` build, after the DevEUI byte-order and empty-uplink fixes
- **Host:** Heliotrope Ridge, RAK4631 on USB, antenna attached, no sensors connected
- **Device:** `puma-concolor-001`, DevEUI `42BB96EF76E200F1`, US915 FSB2, MAC 1.0.3
- **Observation:** device side —
  ```
  session : restored 0x260CE734, counter 32
  [cycle 1]
     RK900   : no data (timeout)
     battery : no data (no reply, 0 bytes)
     uplink  : proof of life — no sensor data for 1 cycle(s)
     radio   : sent 0 bytes on port 2
     session : saved 0x260CE734, resume at 64
  ```
  network side — Network Server `nam1` reports `has session: True`, `has pending: False`,
  `adr_data_rate_index: 3`, `rx1_delay: 5`. Gateway `9181014c6051030034` heard the join at
  **RSSI −62, SNR 14** at 48.71066, −122.05389.
- **Verdict:** PASS — the radio path works end to end. Join, join-accept, session
  establishment, and an accepted uplink are all confirmed from both sides. Session
  persistence (H5) also demonstrated: the second boot restored DevAddr `0x260CE734` from
  flash and transmitted without rejoining.
- **Notes:** Two real defects were found getting here, both of which would have been far
  worse to diagnose in the field.

  First, `src/secrets.h` held the DevEUI **byte-reversed**. `SX126x-Arduino` requires
  most-significant-byte-first and reverses the bytes itself; the generator script had written
  the opposite convention. The node transmitted flawlessly and TTN logged **nothing at all** —
  an unrecognised DevEUI is neither answered nor reported, so it is indistinguishable from a
  dead radio or an absent gateway. The boot banner now prints the DevEUI so this comparison
  takes seconds.

  Second, the node did not join at all when both sensors were silent, and `Radio::send()`
  additionally discarded zero-length payloads. Together these meant a station installed with
  one bad wire would have sat in the woods transmitting nothing and been unreachable by
  downlink, since Class A only opens a receive window after an uplink.

  Still unproven: real sensor data (nothing is wired yet), sleep current, and the decoder
  against a live non-empty payload.

### 2026-07-31 — First flash. Firmware runs on real hardware; sensor not yet connected

- **Commit:** `8d4a41c` (first flash), then the attempt-log fix
- **Host:** Heliotrope Ridge · RAK4631 at `/dev/cu.usbmodem31101`, USB `239A:8029`,
  serial `4BC1FCC87D1343AB`
- **Image:** `stage1` — RK900 only. Radio, battery reader, and sleep all compiled out.
- **Measured:** serial console over USB CDC, several consecutive cycles.
- **Observation:** the RK900 was **not connected**, so every read timed out. That is the
  expected result for an absent sensor, and the behavior around it is what this run
  actually tested:

      [cycle 2]
            modbus attempt 1/3 failed (timeout)
            modbus attempt 2/3 failed (timeout)
            modbus attempt 3/3 failed (timeout)
         RK900   : no data (timeout)
         wait    : 30 s (sleep disabled)

- **Verdict:** PASS for four narrow claims, all of them first-time-on-hardware:
  1. The vendored board definition produces an image that boots and runs. Flash and USB
     CDC both work, which retires the risk that the board files were subtly wrong.
  2. The cycle loop runs and repeats on schedule.
  3. **H7 — a silent sensor does not livelock.** Three bounded attempts, then the cycle
     continues. This is the failure that would otherwise strand a node in the field, and it
     is now observed rather than reasoned.
  4. **A missing reading stays missing.** No zeros were fabricated for the absent sensor.
- **Defect found and fixed:** the log read `modbus retry 3/2`, which looks like the retry
  limit was breached. Three attempts is one initial plus the two retries the spec allows —
  the count was right and the label was wrong. Now `attempt N/3`.
- **Notes:** proves nothing about the RK900 register map, the pack, the radio, joining,
  sleep, or current draw. An interrupted flash left the board in its bootloader
  (product ID `002A` instead of `8029`); re-running `flash.sh` recovered it, which confirms
  the documented recovery path works.

### 2026-07-30 — Release 0.2.0: all four stages build, off-target tests pass

- **Commit:** `80de312` (tagged `v0.2.0`)
- **Host:** Heliotrope Ridge (PlatformIO 6.1.19)
- **Measured:** build output and host-run unit tests. **Nothing was run on hardware.**
- **Observation:**

  Field image (`rak4631`):

      RAM:   [=         ]   9.9% (used 24536 bytes from 248832 bytes)
      Flash: [==        ]  23.8% (used 193672 bytes from 815104 bytes)

  All four environments report `SUCCESS`: `stage1`, `stage2`, `stage3`, `rak4631`.
  `pio test -e native` — 20 test cases, 20 passed. `scripts/preflight.sh` PASS.

- **Verdict:** PASS for a narrow claim — the release builds and its host-testable parts
  behave.
- **Notes:** The four-reviewer pass that preceded this release found, among other things,
  that the radio was not restricted to the channels the network listens on, and that the
  node slept before the network's reply window opened. Both are corrected here and **both
  corrections are unverified** — they are reasoned from the regional parameters and the
  radio stack's own reported timing, not observed. The first join and the first downlink
  are what settle them. Same caveat applies to the low-voltage gate, the session
  persistence, the pack frame validation, and the sleep-current change: all compile, none
  have been exercised. H1–H8 remain open.

### 2026-07-30 — The build host and CI disagreed on the same commit

- **Commit:** `24c5d5e` (failing) → `fe3fc47` (passing)
- **Host:** Heliotrope Ridge and GitHub Actions, same source
- **Measured:** `pio test -e native` on both.
- **Observation:** the build host reported 20 of 20 tests passing on a commit where CI
  failed to compile them at all. Two causes, found in order: a stale object file in
  `.pio/build/native` that survived a header change and hid a missing include, and
  `src/features.h` shadowing the C library's own `<features.h>` once `src/` was on the
  include path.
- **Verdict:** the build host alone is **not** sufficient evidence for the off-target tests.
  Its result was wrong and confidently so.
- **Notes:** `scripts/build.sh` now wipes the native build directory and runs the tests
  before compiling, and the header is renamed `build_features.h`. On-target builds were
  never affected — the shadowing needs `-I src`, which only the test environment sets.

### 2026-07-30 — Full firmware compiles for all four stages; off-target tests pass

- **Commit:** `146d99e`
- **Host:** Heliotrope Ridge (PlatformIO 6.1.19)
- **Measured:** build output and host-run unit tests. **Nothing was run on hardware.**
- **Observation:**

  Field image (`rak4631`):

      RAM:   [=         ]   9.9% (used 24536 bytes from 248832 bytes)
      Flash: [==        ]  23.6% (used 192744 bytes from 815104 bytes)

  All four environments report `SUCCESS`: `stage1`, `stage2`, `stage3`, `rak4631`.

  `pio test -e native` — 20 test cases, 20 passed, covering the payload encoder
  (including the 11-byte worst-case data rate budget) and the Modbus checksum
  (pinned to published reference values).

- **Verdict:** PASS for a narrow claim. The full firmware links, every bring-up stage
  still builds, and the two pieces of pure computation behave correctly on a host.
- **Notes:** This closes none of H1–H8. Nothing here proves the node reads a sensor, joins
  a network, sleeps, or draws the current the budget assumes. The low-voltage gate, the
  session persistence, and the battery frame validation added on this date are all
  **unexercised** — they compile and are untested against hardware. First real evidence
  comes from [`FIRST_FLASH.md`](FIRST_FLASH.md).

### 2026-07-30 — Stage 0 compiles for the RAK4631 on two independent machines

- **Commit:** `7ae56ec`
- **Host:** Heliotrope Ridge (PlatformIO 6.1.19) and GitHub Actions `ubuntu-latest`
- **Measured:** build output only. **Nothing was run on hardware.**
- **Observation:**

  Identical on both machines:

      RAM:   [          ]   4.9% (used 12280 bytes from 248832 bytes)
      Flash: [=         ]   9.4% (used 76748 bytes from 815104 bytes)
      [SUCCESS]

- **Verdict:** PASS, for a narrow claim — the toolchain, the vendored board definition,
  and the Adafruit nRF52 framework combination produce a linkable image, and they do so
  reproducibly on a machine that has never seen the project before. Byte-identical sizes
  across two hosts indicate the build does not depend on local machine state, which was
  the specific risk in RAK's alternative "copy files into `~/.platformio`" approach.
- **Notes:** This closes none of H1–H8. It says nothing about whether the board runs,
  enumerates USB, joins, sleeps, or draws the current we hope. The first real evidence
  comes from flashing hardware and reading the serial banner.

### 2026-08-12 — the board presents no USB device at all; no hardware verification was possible

Appended at the end of the ledger rather than in date order, per operator instruction on this
date: append only, never edit an existing entry. Everything above this line is untouched.

- **Commit:** `ec61a88` (the image that was *attempted*; nothing was flashed)
- **Host:** Heliotrope Ridge
- **Measured:** whether the RAK4631 is present on the build host's USB, in any mode, and
  whether `env:soak` could be flashed and observed. This entry exists to record a **negative
  result**, which is a result: the intended verification of `094d5f5` and `ec61a88` on hardware
  did not happen and is still owed.
- **Observation:**

  PlatformIO's own port search, from `pio run -e soak -t upload -v`:

      TimeoutError: Could not automatically find serial port for the `WisCore RAK4631 Board`
        board based on the declared HWIDs=['239A:8029', '239A:0029', '239A:002A', '239A:802A']
      TimeoutError: Could not automatically find serial port based on the known UART bridges

  Every serial device on the host, none of them the node:

      /dev/cu.Bluetooth-Incoming-Port
      /dev/cu.PT-P710BT3824
      /dev/cu.debug-console

  The capture harness, twice, 25 s and 30 s attach windows:

      2026-08-12 11:39:44 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 11:40:09 === CAPTURE GAVE UP after 25s, device never appeared ===

- **Verdict:** FAIL — but a failure of the bench setup, not of the firmware. No device under
  VID `0x239A` is on the bus in **any** of the four states the board can present. Per the PID
  table in [`FIRST_FLASH.md`](FIRST_FLASH.md), the absence of `0029` and `002A` alongside the
  absence of `8029` rules out "sitting in its bootloader" — a board in DFU still enumerates.
  This is consistent with unpowered, unplugged, a dead cable, or a wedged USB peripheral, and
  it is **not** distinguishable between those from here.
- **Notes:** Three things the next session should not conclude from this.

  First, **this is not caused by `FEATURE_CONSOLE=0`.** That flag and `env:soak` both landed in
  `094d5f5` at 11:26:50, one hour 44 minutes after the 09:42:53 capture gave up; at `f626698`,
  the image in play that morning, `env:rak4631` still compiled the console in. The timestamps
  refute the explanation. Independently, the Adafruit core calls `TinyUSB_Device_Init(0)` from
  its own `loop_task` before `setup()` runs, so USB enumeration does not depend on the sketch
  calling `Serial.begin()` at all — see issue #58.

  Second, **`scripts/flash.sh` is not implicated and was not what ran here.** It refuses at its
  line 57 `no RAK4631 found on the build host USB` guard before uploading anything. The upload
  above was a raw `pio run -t upload`, which bypasses that guard — and PlatformIO then fell
  back to `/dev/cu.PT-P710BT3824`, a Bluetooth label printer, sent DFU packets at it, and
  printed `[SUCCESS]`. Filed as #59. Do not use bare `pio run -t upload` on this project.

  Third, **nothing about `094d5f5` or `ec61a88` is verified on hardware by this entry.** The
  counter-headroom refusal (#55), the in-band brownout keepalive (#55), the `lmh_reset_mac()`
  rejoin path, and the console-off sleep current (#56) are all still unexercised. The
  counter-headroom fix in particular has silence as its only symptom, so only watching the
  frame counter come back at or above what was transmitted, across a reset, can show the
  regression is dead. Recovery is physical: double-tap RESET on the RAK19007, or re-seat the
  cable, then re-run `scripts/flash.sh -e soak`.

### 2026-08-12 (later) — flashed `env:soak`; post-reset uplink accepted by the network, and the USB dropout explained

Appended at the end per the operator's append-only instruction. Nothing above is edited. All
times are **build-host local**, which ran ~13 minutes behind the workstation clock during this
session; network times are converted from UTC at UTC-7.

- **Commit:** `6933114`
- **Host:** Heliotrope Ridge, board on `/dev/cu.usbmodem31201` before the flash
- **Image on the board:** `env:soak` (identical to `env:rak4631` since the `636e421` revert)
- **Measured:** (1) whether the flash lands and the board runs; (2) whether the frame counter
  survives a reset at or above the last transmitted value, cross-checked against TTN rather than
  the console; (3) why the board keeps vanishing from USB.

#### 1. Flash — PASS

`scripts/flash.sh --yes -e soak`, which refuses to run bare `pio run -t upload` (#59):

      Device programmed.
      ========================= [SUCCESS] Took 15.23 seconds =========================
         waiting up to 30s for the board to re-enumerate...
         USB 239A:8029 -- application running
      === FLASH OK ===
      commit: 6933114a010d8ec13f5d7344583882ce0dc523c0
      usb:    239A:8029 (application running)

Before the flash, `ioreg` showed the board present and running an application:

      "USB Product Name" = "WisCore RAK4631 Board"
      "idVendor" = 9114        (0x239A)
      "idProduct" = 32809      (0x8029 — application)

#### 2. Counter ceiling across a reset — PASS, network-side, with one caveat

A DFU flash resets the MCU, so this is a reset with a firmware write on top of it. The LittleFS
region is untouched by an application-region write, so the stored session and counter are the
same ones a plain reset would restore.

TTN, queried on the build host with `ttn-lw-cli`, after the flash:

      dev_addr           260CE734
      last_f_cnt_up      1920
      last_n_f_cnt_down  60
      started_at         2026-07-31T14:33:20.636657834Z
      last_seen_at       2026-08-12T19:54:15.902616Z    (= 12:54:15 host, ~1 min AFTER the flash)

Two things follow, and both are network-side facts rather than console claims:

- **The session survived a firmware write.** `started_at` is still 2026-07-31 and `dev_addr` is
  unchanged, so the device restored the stored session rather than rejoining.
- **The restored counter was at or above the last transmitted value.** The device transmitted
  after the reset and the network *accepted* the frame — `last_f_cnt_up` is 1920 and
  `last_seen_at` advanced to one minute after the flash. Under the regression that `094d5f5`
  fixed, the restored counter would have come back *below* what had already been sent, and TTN
  would have discarded the frame as a replay in silence, leaving `last_seen_at` stale. It did
  not. **The failure mode does not reproduce on this build.**

**Caveat, stated because it bounds the claim:** the exact pre-flash counter was not recorded, so
this shows "the post-reset frame was accepted as fresh", not "the ceiling advanced by exactly N".
A second post-reset uplink would tighten it and was not obtainable inside the session window —
see §4. Verdict: **PASS for the observable claim, and the strongest evidence available today**,
but not a full characterisation. #55 stays open for the refusal path itself, which requires
driving the counter to the ceiling.

#### 3. Why the board keeps disappearing from USB — #58 explained, and it is our firmware

The board vanished from `ioreg` again within ~10 minutes of a successful flash — not just the tty
node, the whole USB device:

      $ ls /dev/cu.*
      /dev/cu.Bluetooth-Incoming-Port
      /dev/cu.PT-P710BT3824
      /dev/cu.debug-console
      $ ioreg -p IOUSB -l | grep "USB Product Name"
      "USB2 Hub" / "USB3 Gen2 Hub" / "MacBook Air SuperDrive"      — no WisCore RAK4631 Board

That is the signature of `src/power.cpp:111-136`, which runs before every sleep:

      const bool console_in_use = (bool)Serial;
      if (!console_in_use) {
          TinyUSBDevice.detach();
      }

`detach()` clears `USBPULLUP`, which removes the device from the bus entirely — exactly what
`ioreg` shows. `(bool)Serial` is false until a host program opens the port and asserts DTR, so a
node that boots with nobody already attached detaches within seconds of its first cycle and stays
off the bus for the whole sleep interval.

**This is a catch-22 for observation, and it explains the failures that have been read as hardware
faults all day.** `scripts/capture.py` waits for `/dev/cu.usbmodem*` to appear, but by the time it
starts, the port is already gone; it then waits through an interval in which no port can exist.
Two captures this session reproduce it exactly:

      2026-08-12 12:54:33 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 12:54:53 === CAPTURE GAVE UP after 20s, device never appeared ===
      2026-08-12 12:55:40 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 12:57:10 === CAPTURE DONE lines=0 ===

and a third, given a 600 s window, was still waiting at 13:10:49 having never seen a port.

**Verdict: #58's "device never appeared" is explained without a hardware fault.** The morning
soak attempt at `f626698` waited 180 s against an interval far longer than that, on a node that
had already detached. Filed as #60 with a proposed grace period so the field behavior is kept
while the bench stays observable. The board's physical recovery earlier today was the operator's
bench intervention; **what that intervention actually corrected is still unknown**, and #58 is
closed as recovered-cause-unknown rather than diagnosed.

#### 4. Reporting interval is 3600 s, not 900 s

The node did not transmit for **16.5 minutes** after the post-flash uplink at 12:54:15 —
`last_f_cnt_up` still 1920 and `last_seen_at` unchanged at 13:10:49. So the stored interval is the
`kIntervalDefaultSeconds = 3600` default, not the 900 s floor. The earlier note in this ledger
inferring 900 s from network timestamps does not hold for the current stored value. This is why
the console and downlink work below could not be completed in the session window: one observation
costs an hour of waiting.

#### 5. Not observed — no claim made

- **Both sensors in one cycle on this image.** Not seen; the console was unreachable (§3).
- **`take_downlink()` and the malformed-downlink rejections (#54).** Not exercised. Sending a
  downlink to a node on a 3600 s cadence, without a console to watch it land, would have been a
  launch rather than an outcome.
- **`lmh_reset_mac()` on the rejoin path.** Not provoked. The node is joined and healthy, and
  forcing a rejoin on the only board available was not worth the risk of stranding it.
- **Sleep current.** Not metered — the operator is doing this himself.

**Board left in this state:** `env:soak` at `6933114`, running (`239A:8029` confirmed at flash),
joined as `puma-concolor-001` / `260CE734`, uplinking hourly. **It presents no USB port while
asleep** — that is normal for this firmware per §3, not a fault. A port appears for a few seconds
around each wake, roughly hourly. All background capture and polling processes were killed.

### 2026-08-12 (later still) — #60 fix written and built; flash blocked by the very bug it fixes

Appended at the end per the operator's append-only instruction; nothing above is edited. Times
are **build-host local**, which ran ~13 minutes behind the workstation clock.

- **Commit:** `23604cf` (built, **not** confirmed on the board — see below)
- **Host:** Heliotrope Ridge
- **Measured:** whether the #60 boot-grace fix reaches the board and produces an observable
  console. **It did not.** This entry records that outcome, not the intent.

#### What was built — PASS

`scripts/build.sh -e soak` at `23604cf`:

      1 succeeded in 00:00:01.456
      === BUILD OK ===
      commit: 23604cf0bb5627f8983c7ae9de6e5678a63c7e57

`scripts/preflight.sh` passes. The change keeps USB attached for 180 s after boot when nothing
has opened the console, then resumes the previous detach behavior unchanged.

#### What happened on the board — FAIL, and the failure is informative

The flash was attempted while the node was **asleep and detached**, which is the #60 condition
itself. `scripts/flash.sh` reported failure, and afterwards the board was absent from the bus
entirely:

      13:19:25
      PORTS
      none
      IOREG
        | |     "idProduct" = 32779      (USB2 Hub)
        |       "idProduct" = 32780      (USB3 Gen2 Hub)
        |       "idProduct" = 5376       (SuperDrive)
                                          — no WisCore RAK4631 Board, no 239A of any kind

and the capture, run immediately after, could not attach:

      2026-08-12 13:18:19 === CAPTURE WAITING /dev/cu.usbmodem* ===
      2026-08-12 13:19:05 === CAPTURE GAVE UP after 45s, device never appeared ===

**The board is very probably fine, and this is the reasoning rather than a hope.** A board whose
application is invalid stays in its bootloader and enumerates as `239A:0029` or `002A` —
*visibly*. Nothing at all on the bus is the signature of a **running application that has
detached itself**, which is what `src/power.cpp` does before every sleep when no host holds the
console open. The absence is therefore evidence of health, not of a brick.

**Which image is on the board is unknown**, and no claim is made either way. Either the upload
never transferred and `6933114` is still resident, or it landed and `23604cf` is. Both are
consistent with what was observed. It will be answerable at the next wake.

#### The finding this produced: #60 blocks flashing, not just observation

This was scoped as an observability bug. It is worse than that — **the same detach makes the
board unflashable for the whole interval**, because a DFU upload needs a serial port too. At the
3600 s stored interval that is up to an hour of unreachability per attempt, and the only ways in
are to catch a wake window or to double-tap RESET by hand.

That also makes the fix a chicken-and-egg: the grace window solves this permanently, but getting
the image carrying it onto the board requires a window that does not exist yet. #60 updated.

A second, cheaper defect was fixed as a consequence. `scripts/flash.sh` printed
`THE BOARD HAS NO VALID APPLICATION ON IT. NOTHING IS RUNNING.` for this case, which is wrong
and actively harmful — it is the message that would send the next session hunting a brick that
is not there. It now distinguishes "no 239A device at all" (probably asleep, per #60, with wait
or double-tap as the options) from "enumerated in its bootloader" (genuinely no valid app).

#### Interval reconciliation — code and docs agree; one off-limits file does not

Checked because a wrong figure here has already misled two sessions. `src/config.h` is the single
source: `kFupFloorSeconds = 900` (the fair-use floor, explicitly lowered from 1800 s),
`kIntervalMinSeconds = 900`, `kIntervalDefaultSeconds = 3600`. `platformio.ini` agrees after the
earlier correction. The device has **3600 s** stored, which is the default, not the floor.

So: **900 s is the floor, 3600 s is the default, and the node is running the default.** The
earlier inference that "the network says 900 s" was reading a stored value that is no longer
current. `AGENTS.md` still states 900 s as the operating interval; it is owned by another agent
today and was left alone.

#### Not observed — no claim made

`take_downlink()` and the malformed-downlink rejections (#54), `lmh_reset_mac()` on the rejoin
path, and both sensors in one cycle all still require a console. All three remain blocked on #60
reaching the board.

**Board left in this state:** absent from USB, running an application of **unknown version**
(`6933114` or `23604cf`), asleep on a 3600 s cycle, last confirmed joined as `260CE734` with
`last_f_cnt_up 1920`. It will re-present a port at its next wake, roughly hourly. A double-tap
RESET on the RAK19007 brings it back immediately and is the fastest route to getting `23604cf`
on it. No background processes were left running.

### 2026-08-13 — sixteen unattended `stage3` cycles, both sensors live, no reset

- **Firmware on the board:** the boot banner carries **no git SHA** — only
  `firmware : 0.4.1` and `built    : Aug 12 2026 23:32:15`. It cannot be pinned to a commit
  from the console alone. The newest commit on the build host older than that build stamp is
  `f15a983` (2026-08-12 21:16:45 -0700), which is also current `main`; that is consistent with
  the banner and is **not** the same as confirmed. Recorded this way deliberately rather than
  asserting `f15a983` ran.
  This is an **inferred** SHA, labelled as one per the section above. Builds from `033b584`
  onward print the commit on the banner, so a future entry will not need the inference.
- **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`, console captured to
  `/tmp/stage3_cap.txt` by a single attached reader.
- **Measured:** an unattended `stage3` run — RK900 weather read, RAK9154 pack telemetry and an
  uplink per cycle, with `FEATURE_SLEEP=0` and a 1800 s awake wait between cycles.
- **Observation:** one boot banner in the whole capture (no reset), cycle markers **1 and 3–17**
  present and strictly increasing, 14 `sent 35 bytes on port 2` lines, session restored rather
  than rejoined (`session : restored 0x260CE734, counter 2080`). Cycle 17 verbatim:

  ```
  [cycle 17]
     RK900   : raw 0x0000-0x0004 = 0000 0000 00F7 024F 2716
     RK900   : wind 0.00 m/s @ 0 deg, 24.7 C, 59.1 %RH, 1000.6 hPa
     battery : pack answered at 0x01 — skipping provisioning
     battery : sendat FF 7E 00 15 02 01 00 01 15 03 10 02 15 BA AC 04 16 B9 FF FF 17 B8 55 18 67 DC 00 47
     battery : 11.96 V  -0.01 A  85%  22.0 C
     battery : raw v=1196 i=-1 soc=85 t=220 (t scale UNCONFIRMED — 230 means tenths, 23 means whole degrees)
     radio   : sent 35 bytes on port 2
     wait    : 1800 s (sleep disabled)
  ```

- **Verdict:** PASS for what it covers — the field-image cycle repeats without intervention and
  both sensors answer every cycle. Pack voltage drifts 12.02 V → 11.96 V and SoC 87% → 85% over
  the run, at roughly −0.01 A reported, which is the 10 mA telemetry LSB and therefore still not
  a current measurement.
- **Notes and what this is *not*:**
  - **Not a soak.** `FEATURE_SLEEP=0`, so the sleep path — the whole point of H8 — is never
    entered. Zero soak hours still exist.
  - **Not proof of [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62).** The pack was already latched at `0x01` for every cycle, so the
    re-latch path was never exercised. Two cycle markers are missing from the capture
    (`[cycle 2]`, and 14 uplink lines against 16 markers); the reader attached after boot, so
    absence here is a gap in the capture, not an observed failure. It is recorded as a gap.
  - **Nothing was flashed and nothing was reset.** The board was left running, the reader left
    attached.

### 2026-08-13 — downlink command matrix: all eight cases PASS on hardware ([#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54))

- **Commit on the board:** **inferred `f15a983`**, not asserted. The banner on this image predates
  the commit stamp added in `033b584`, so it reports only `firmware : 0.4.1` and
  `built    : Aug 12 2026 23:32:15`. `f15a983` (2026-08-12 21:16:45 -0700) is the newest commit
  older than that build stamp, which is *consistent with* the banner and is not the same as
  confirmed. Recorded per the inference convention `d568574` established.
- **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`. Driver `scripts/downlink_matrix.sh` at
  `a7381e7`, run unattended; log `/tmp/downlink_matrix.log`, console `/tmp/stage3_cap.txt`,
  TTN event stream `/tmp/downlink_matrix_events.log`.
- **Image:** `stage3` bench image — `features : rk900=1 battery=1 radio=1 sleep=0 wdt=1`. **The
  sleep path was not exercised by this run.** The downlink handling code is shared with the field
  image; the power path is not.
- **Measured:** eight downlink cases pushed through TTN (`app=my-app-tobi`,
  `dev=puma-concolor-001`) and matched against the console. Each case waited a full cycle for the
  Class A RX window, so the run took 2 h 31 m (07:50:38 → 10:21:30).
- **Observation:** every PASS rests on a console line emitted from inside
  `Radio::take_downlink()` (`src/radio.cpp:441-498`) — so **`take_downlink()` itself is now
  observed on hardware, on all five of its branches**, which is the [#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54) acceptance criterion
  that had never been seen before. Verbatim, in cycle order:

  ```
  [cycle 18]  case b — valid 0x01 set-interval to 900 s
     radio   : sent 35 bytes on port 2
     radio   : downlink — set interval 900 s
     config  : interval now 900 s
     wait    : 1800 s (sleep disabled)
  [cycle 19]
     wait    : 900 s (sleep disabled)
  [cycle 20]  case a — valid 0x03 request-status
     radio   : downlink — status requested
  [cycle 21]  case c — wrong-length 0x01, 3 bytes (#64)
     radio   : downlink — opcode 0x01 with wrong length 3, ignored
  [cycle 22]  case d — wrong-length 0x03, 4 bytes (#63)
     radio   : downlink — opcode 0x03 with wrong length 4, ignored
  [cycle 23]  case e — unknown opcode 0x7F
     radio   : downlink — unknown opcode 0x7F, ignored
  [cycle 24]  case f — valid 0x03 on FPort 1 (wrong port)
     radio   : ignoring 1 bytes on port 1
  [cycle 25]  case g — first of two 0x03 queued together
     radio   : downlink — status requested
  [cycle 26]  case g — second of the pair, next cycle
     radio   : downlink — status requested
  ```

- **Verdict:** PASS, 8 of 8, re-verified line by line against `/tmp/stage3_cap.txt` rather than
  trusting the harness grep.
  - **b** is the strongest single result: the applied interval is visible twice — `config  :
    interval now 900 s` in cycle 18 and the `wait` line dropping 1800 s → 900 s in cycle 19.
  - **c** discriminates exactly what [#64](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/64) was about: a known opcode with a bad length reports
    *wrong length*, not *unknown opcode*.
  - **f** shows the port filter running before any opcode parsing.
  - **g** shows two queued commands drained one per cycle, not merged or dropped.
  - **h** (harness self-assessment) holds up under inspection: cycle markers 18–26 are strictly
    increasing, exactly one boot banner exists in the whole capture and it precedes cycle 1, and
    the session was restored (`session : restored 0x260CE734, counter 2080`) rather than rejoined.
    No reset occurred during the matrix.
- **Notes and what this is *not*:**
  - **Not proof of [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62).** The pack reported `pack answered at 0x01 — skipping provisioning` in
    every matrix cycle, so the re-latch path was never entered. [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) stays open.
  - **Not a soak, and not field-image power behaviour.** `FEATURE_SLEEP=0`. Zero soak hours still
    exist.
  - `[cycle 2]` is absent from the capture (the reader attached after boot). Outside the matrix
    window, and a gap in the capture rather than an observed failure.
  - Pack drift across the whole night-and-morning capture, raw and uninterpreted: 12.02 V / 87%
    (cycle 1) → 11.94 V / 84% (cycle 26), on USB power with no charge source, reported current
    `-0.01 A` throughout. −0.01 A is the 10 mA telemetry LSB, so it is a resolution floor and
    **not** a current measurement. `kTxInhibitCentivolts` is 960 (9.60 V) and remains an
    unmeasured inference ([#67](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/67)); nothing here was changed.

### 2026-08-13 — field image (`env:soak`) flashed at `d568574`; the sleep path is reached

- **Commit:** `d568574`, **asserted from the board itself** — the banner now carries it. This is
  the first hardware confirmation of the `033b584` banner change and the first evidence entry that
  did not have to infer a SHA.
- **Host:** Heliotrope Ridge, `pio run -e soak -t upload --upload-port /dev/cu.usbmodem31201`.
  `env:soak` is byte-identical to `env:rak4631` per [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md).
- **Measured:** the DFU transfer and the first cycle of the field image.
- **Observation:** the transfer was real, not a bare `[SUCCESS]` (#59) — eleven rows of `#` progress marks,
  `Activating new firmware`, `Device programmed.`, and zero occurrences of
  `Target is not in DFU mode`. Application mode confirmed by `ioreg`: `idProduct = 32809`
  (0x8029), `USB Product Name = "WisCore RAK4631 Board"`, zero `nRF UF2` matches and zero
  `bInterfaceClass = 8` interfaces. Boot capture verbatim:

  ```
  === rak-sensor-node ===
  firmware : 0.4.1
  commit   : d568574
  built    : Aug 13 2026 10:28:41
  features : rk900=1 battery=1 radio=1 sleep=1 wdt=1
  interval : bench=0, bounds 900-86400 s, default 3600 s
  deveui   : 42BB96EF76E200F1
  appeui   : 0000000000000000
  region   : US915 sub-band 2
     config  : interval 900 s, boot #3
     session : restored 0x260CE734, counter 2112

  [cycle 1]
     RK900   : raw 0x0000-0x0004 = 0000 0000 00FF 024B 2712
     RK900   : wind 0.00 m/s @ 0 deg, 25.5 C, 58.7 %RH, 1000.2 hPa
     battery : pack answered at 0x01 — skipping provisioning
     battery : sampling confirmed — pack is reporting live values
     battery : sendat FF 7E 00 15 02 01 00 01 01 03 10 02 15 BA AA 04 16 B9 FF FF 17 B8 54 18 67 F0 00 43
     battery : 11.94 V  -0.01 A  84%  24.0 C
     battery : raw v=1194 i=-1 soc=84 t=240 (t scale UNCONFIRMED — 230 means tenths, 23 means whole degrees)
     session : saved 0x260CE734, resume at 2144
     radio   : sent 35 bytes on port 2
     sleep   : 900 s
  ```

- **Second observation — the sleep/wake round trip completes.** The board woke on its own
  ~900 s later and ran a second cycle with no boot banner in between, so it woke from sleep rather
  than resetting through it. Boot was 10:28:59, cycle 2 landed at 10:44:0x (build-host clock):

  ```
  [cycle 2]
     RK900   : raw 0x0000-0x0004 = 0000 0000 0100 024B 2711
     RK900   : wind 0.00 m/s @ 0 deg, 25.6 C, 58.7 %RH, 1000.1 hPa
     battery : pack answered at 0x01 — skipping provisioning
     battery : sendat FF 7E 00 15 02 01 00 01 02 03 10 02 15 BA AA 04 16 B9 FF FF 17 B8 54 18 67 F0 00 43
     battery : 11.94 V  -0.01 A  84%  24.0 C
     battery : raw v=1194 i=-1 soc=84 t=240 (t scale UNCONFIRMED — 230 means tenths, 23 means whole degrees)
     radio   : sent 35 bytes on port 2
     sleep   : 900 s
  ```

- **Verdict:** PASS for what it covers. `features : … sleep=1` and the closing line reads
  **`sleep   : 900 s`**, not `wait    : N s (sleep disabled)` — the sleep path is entered on the
  field image, which is the state the node will be measured in. Both sensors read in the same
  cycle and an uplink went out on the first cycle after flashing.
- **Notes:**
  - **The 900 s interval survived the flash and the power cycle.** It was set by case b's downlink
    and came back as `config  : interval 900 s, boot #3` on a freshly flashed image — incidental
    but real evidence for H5.
  - **USB will disappear by design, and that is not a fault.** `Power::sleep()` calls
    `TinyUSBDevice.detach()` before sleeping whenever no host has the CDC open and the 180 s boot
    grace has expired (`src/power.cpp:182-205`, [#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60), `23604cf`). During this capture a reader
    *was* attached, so `console_in_use` was true, the `power   : USB kept attached …` line never
    printed, and the port stayed up. Once the reader is released the port goes away at the next
    sleep. Press RESET once to get a flashable window back.
  - **Still not a soak.** Two cycles are not 24 h. Zero soak hours exist.
  - **Third observation — `detach()` fires and the node leaves the USB bus.** The serial reader was
    released at 10:46:03. At the end of the next cycle, with no host holding the CDC open and the
    180 s boot grace long expired, the node detached. At **10:59:57** on the build host:

    ```
    $ ls /dev/cu.usbmodem*
    zsh:1: no matches found: /dev/cu.usbmodem*
    $ ioreg -p IOUSB -w0 -l | grep -c "WisCore RAK4631"
    0
    $ ioreg -p IOUSB -w0 -l | grep -c 32809
    0
    ```

    This is the first hardware observation of the [#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60)
    / [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md) detach path on the field image.
    It is **half** of [#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)
    item 1: the detach is observed, the matching `attach()` and a successful host re-enumeration at
    the next wake are not. Absence from `ioreg` is by design and is not a dead board.
  - **Board left in this state:** running `env:soak` at `d568574`, asleep on a 900 s cycle, session
    `0x260CE734`, **absent from USB**. That is the unattended field configuration, and it is the
    configuration a sleep-current measurement has to be taken in (rule 50 trap 5 — a
    USB-attached measurement is meaningless). A single RESET brings the console and a flashable
    window back; a double-tap re-enters DFU. No background processes were left running on the
    build host.
  - Sleep current remains unmeasured, and cannot be measured over USB (rule 50 trap 5) or from
    pack telemetry, whose LSB is 10 mA.

### 2026-08-13 — `65f8615` on hardware: battery ladder survives 20 cycles, field image reaches sleep, and one transient probe miss still spends the BOOT

- **Commit:** `65f8615` — **read back from the boot banner on the board**, not inferred
- **Host:** Heliotrope Ridge, board `/dev/cu.usbmodem31201`
- **Measured:** (1) `env:battdiag` — 20 consecutive ~10 s battery cycles at `65f8615`;
  (2) `env:soak` — the field image, one full cycle including the sleep call
- **Observation — `env:battdiag` boot banner and first cycle:**

  ```
  === rak-sensor-node ===
  firmware : 0.4.1
  commit   : 65f8615
  built    : Aug 13 2026 13:02:09
  features : rk900=0 battery=1 radio=0 sleep=0 wdt=1
  interval : bench=1, bounds 10-86400 s, default 10 s
     config  : bench build — interval forced to 10 s, stored value ignored
     config  : interval 10 s, boot #1

  [cycle 1]
     battery : pack answered at 0x01 — skipping provisioning
     battery : sampling confirmed — pack is reporting live values
     battery : sendat FF 7E 00 15 02 01 00 01 01 03 10 02 15 BA A8 04 16 B9 FF FF 17 B8 54 18 67 F0 00 42
     battery : 11.92 V  -0.01 A  84%  24.0 C
     battery : raw v=1192 i=-1 soc=84 t=240 (t scale UNCONFIRMED — 230 means tenths, 23 means whole degrees)
     wait    : 10 s (sleep disabled)
  ```

  The banner naming its own commit re-confirms the build-stamp change on hardware, this time at
  `65f8615`.

- **Observation — the pack was live in every one of 20 cycles.** Cycles 1 through 20 each printed
  `11.92 V  -0.01 A  84%  24.0 C`, raw `v=1192 i=-1 soc=84 t=240`, and the cycle counter advanced
  monotonically `[cycle 1]` … `[cycle 20]` with **no second boot banner**, no reset and no crash
  loop. Slightly below the previous night's `11.94 V ... 84%`, consistent with a slow drift down on
  USB power with no charge source.

- **Observation — the expected post-boot `Unsampled` cycles did not occur, so `e070708` was not
  exercised.** `AGENTS.md` documents ~2 null cycles after boot while the pack samples. This capture
  had **zero**: cycle 1 already reported `sampling confirmed — pack is reporting live values` and a
  live reading. The pack had been polled continuously by an immediately preceding `battdiag` flash,
  so it was already sampled when the MCU restarted. **The specific defect `e070708` fixes — an
  ordinary `Unsampled` reply from `0x01` being scored as no answer — therefore did not arise, and
  the fix remains compile-verified only.** No `Unsampled` line appears anywhere in the capture.

- **Observation — a `BOOT` did fire, on cycle 10, and it was a genuine one-cycle probe miss, not
  the `e070708` path:**

  ```
  [cycle 9]
     battery : pack answered at 0x01 — skipping provisioning
     battery : sendat FF 7E 00 15 02 01 00 01 09 03 10 02 15 BA A8 04 16 B9 FF FF 17 B8 54 18 67 F0 00 43
     battery : 11.92 V  -0.01 A  84%  24.0 C

  [cycle 10]
     battery : pack silent at its id — one BOOT this power cycle
     battery : no confirmed latch — proceeding unprovisioned
     battery : sendat FF 7E 00 15 02 01 00 01 0C 03 10 02 15 BA A8 04 16 B9 FF FF 17 B8 54 18 67 F0 00 43
     battery : 11.92 V  -0.01 A  84%  24.0 C

  [cycle 11]
     battery : pack answered at 0x01 — skipping provisioning
     battery : sendat FF 7E 00 15 02 01 00 01 0D 03 10 02 15 BA A8 04 16 B9 FF FF 17 B8 54 18 67 F0 00 44
     battery : 11.92 V  -0.01 A  84%  24.0 C
  ```

  Read the sequence-number byte: cycle 9 used `09`, cycle 10's surviving frame used `0C`, cycle 11
  used `0D`. Two sequence numbers (`0A`, `0B`) were consumed by probe attempts that drew no matched
  reply, so phase 0 genuinely saw silence — this is not an `Unsampled` reply being misread. The
  pack then answered the push listen in the **same** cycle with a live reading, and cycle 11 was
  clean again. So a single transient miss, roughly 1 cycle in 20, is enough to spend the one BOOT
  the power cycle is allowed and to send the reboot verb to a demonstrably healthy pack. Filed as
  [#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75).

- **Observation — the pack never appeared unlatched.** `provId 0xFF` does not occur anywhere in the
  capture; every cycle but 10 printed `pack answered at 0x01 — skipping provisioning`. The
  re-latch path in
  [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) was
  **not exercised** and remains unproven.

- **Observation — `env:soak` (the field image, byte-identical to `env:rak4631`) at `65f8615`, one
  full cycle ending in the sleep call:**

  ```
  === rak-sensor-node ===
  firmware : 0.4.1
  commit   : 65f8615
  built    : Aug 13 2026 13:07:15
  features : rk900=1 battery=1 radio=1 sleep=1 wdt=1
  interval : bench=0, bounds 900-86400 s, default 3600 s
  deveui   : 42BB96EF76E200F1
  appeui   : 0000000000000000
  region   : US915 sub-band 2
     config  : interval 900 s, boot #3
     session : restored 0x260CE734, counter 2208

  [cycle 1]
     RK900   : raw 0x0000-0x0004 = 0000 0000 0103 024B 2708
     RK900   : wind 0.00 m/s @ 0 deg, 25.9 C, 58.7 %RH, 999.2 hPa
     battery : pack answered at 0x01 — skipping provisioning
     battery : sampling confirmed — pack is reporting live values
     battery : sendat FF 7E 00 15 02 01 00 01 01 03 10 02 15 BA A8 04 16 B9 FF FF 17 B8 54 18 67 F0 00 42
     battery : 11.92 V  -0.01 A  84%  24.0 C
     battery : raw v=1192 i=-1 soc=84 t=240 (t scale UNCONFIRMED — 230 means tenths, 23 means whole degrees)
     session : saved 0x260CE734, resume at 2240
     radio   : sent 35 bytes on port 2
     sleep   : 900 s
  ```

  Both sensors read in the same cycle, the session restored rather than rejoined, an uplink went
  out, and the cycle closed `sleep   : 900 s` — not `wait    : N s (sleep disabled)`. The persisted
  900 s interval from the 2026-08-13 downlink matrix survived two reflashes and is still in force.

- **Observation — RK900 null-pressure honesty (`da655e9`) is untested.** The only field-image cycle
  captured read a real pressure, `999.2 hPa` from raw `2708`, so the summary line had a genuine
  value to print and the refused-pressure branch was never entered. Nothing was observed either
  way; the fix stays compile-verified only.

- **Verdict:** PASS on survival and on the sleep path — the battery ladder ran 20 cycles at
  `65f8615` with live values and no reset, and the field image reached `sleep   : 900 s`.
  INCONCLUSIVE on `e070708` and on `da655e9`, neither of which had its defect condition arise.
  One new defect found and filed
  ([#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75)).

- **Board left:** running `env:soak` at `65f8615`, asleep on a 900 s cycle, session `0x260CE734`.
  The field image detaches from USB ~180 s after boot by design
  ([ADR-0008](decisions/ADR-0008-console-in-the-field-image.md),
  [#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60)), so the
  board disappearing from `/dev/cu.usbmodem*` is correct behavior, not a dead board. One RESET
  press restores the console and a flashable window.

- **Observation — the node woke itself on schedule, and the USB detach is conditional on a
  reader.** The capture was left attached. Cycle 2 arrived ~900 s after cycle 1 with **no boot
  banner in between**, so it woke from sleep rather than resetting through it:

  ```
     sleep   : 900 s

  [cycle 2]
     RK900   : raw 0x0000-0x0004 = 0000 0000 0105 024A 2707
     RK900   : wind 0.00 m/s @ 0 deg, 26.1 C, 58.6 %RH, 999.1 hPa
     battery : pack answered at 0x01 — skipping provisioning
     battery : sendat FF 7E 00 15 02 01 00 01 02 03 10 02 15 BA A7 04 16 B9 FF FF 17 B8 54 18 67 F0 00 44
     battery : 11.91 V  -0.01 A  84%  24.0 C
     battery : raw v=1191 i=-1 soc=84 t=240 (t scale UNCONFIRMED — 230 means tenths, 23 means whole degrees)
     radio   : sent 35 bytes on port 2
     sleep   : 900 s
  ```

  Both sensors read again, both values moved slightly (`999.2` → `999.1 hPa`, `11.92` → `11.91 V`),
  and a second uplink went out. A host-side poll every 10 s recorded `PORT_PRESENT` and
  `ioreg 32809 = 1` continuously from boot through **past 16 minutes**, far beyond the 180 s grace.
  That is consistent with `b1e59d6` — the field image detaches **when nobody is watching**, and a
  reader holding the port open suppresses it. It is therefore **not** a contradiction of
  [#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60), and it
  means item 1 of [#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)
  — re-enumeration on the host after a detach-sleep — is **still untested**, because no detach
  occurred to re-enumerate from. The console surviving a full sleep/wake cycle is new information
  and is the closest thing to it so far.

- **Still zero soak hours.** One cycle is not a soak; `README.md` stays `🚧 NOT YET DEPLOYED`.

### 2026-08-13 — inline USB current meter: peak ~40 mA, minimum reads `0` at a **10 mA** resolution floor

- **Commit:** `572bcfa` — the `v0.4.2` soak image (`env:soak`, byte-identical to `env:rak4631`
  per [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)), running a 900 s cycle.
- **Host:** **not** Heliotrope Ridge. The node was moved off the bench and the readings come from
  the **operator's inline USB current meter**, wired between the USB host power source and the
  RAK4631. Heliotrope Ridge built and flashed the image; it did not take this measurement. The
  24 h TTN-side soak monitor was running throughout and is unaffected.
- **Measured:** whole-board current drawn over the USB supply rail, across the awake and sleeping
  phases of the 900 s cycle.
- **Observation** — operator-reported, verbatim:

  ```
  max amp = .04
  min is 0
  yeah it goes to 100ths that is all
  ```

  So: peak **0.04 A = 40 mA**; minimum **reads `0`**; display resolution **0.01 A**, i.e. one
  least-significant digit is **10 mA**. A single digit of resolution makes the peak
  "0.04 ± 0.01 A", not a precise 40 mA.

- **What this establishes.**
  1. **Peak draw observed so far is ~40 mA.** No subsystem is stuck drawing hundreds of
     milliamps — the coarse but real upper bound rules out a runaway rail.
  2. **Sleep/idle current is below 10 mA.** That is the only thing the `0` reading supports.

- **What this does NOT establish — read this before quoting a number from it.**
  1. **No sleep-current figure exists.** The `0` minimum is the meter's **resolution floor, not a
     measurement**. This is the *same trap* already recorded in this file for the RAK9154 pack
     telemetry, whose LSB is also 10 mA while [`POWER_BUDGET.md`](POWER_BUDGET.md) turns on
     roughly **1 mA** — and whose documented defect cases (0.89–1.2 mA for peripherals left
     enabled, ~6 mA for the radio left awake) all sit **at or below one LSB**. A 10 mA meter is no
     better than the pack telemetry for this question. The module's own datasheet sleep figure is
     **2.0 µA** [CIT-RAK4631-RAW] — roughly 5000× below one digit of this display. "Sleep current
     is unmeasured" stays an open blocker in `AGENTS.md` and issue
     [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8).
  2. **The LoRa TX current has not been measured.** 40 mA is *lower* than the datasheet's
     transmit figures — RAKwireless gives `Tx mode LoRa @17 dBm` **92 mA** and `@20 dBm`
     **125 mA** [CIT-RAK4631-RAW]. The likely explanation is that the meter's slow display
     sampling missed the ~50 ms transmit burst, **not** that the radio drew less than its
     datasheet. Do not record 40 mA as a transmit figure. The operator has left the meter inline
     to watch for a higher peak across further cycles.

- **Verdict:** **INCONCLUSIVE.** Useful as a coarse upper bound on peak draw and as a ceiling on
  idle draw (< 10 mA). It closes no power gate. Per `AGENTS.md`, this must not be inherited by any
  later document as "sleep current measured" — it is the absence of a measurement, recorded.

- **Next:** closing the power gate needs instrumentation with **µA resolution** — a Nordic PPK2
  (the method behind [CIT-RAK-SLEEP]), a current-sense shunt read on a scope, or a
  coulomb-counting integration over a long window. Tracked in
  [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8).

### 2026-08-14 — the meter finally caught a transmit-shaped peak: **0.14 A**, stable across many cycles; minimum still `0` at the same **10 mA** floor

- **Commit:** `572bcfa` — the `v0.4.2` soak image (`env:soak`, byte-identical to `env:rak4631` per
  [ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)), 900 s cycle. **Both readings are
  attributed to `572bcfa`, and the attribution is clean rather than assumed.** The board carried
  `572bcfa` until it was reflashed to `1c2df3c` (`v0.4.3`) that afternoon; the soak on `1c2df3c`
  started at **16:09:16Z**, so the flash preceded that. The operator's readings are timestamped
  **~00:50 and ~08:05 local (UTC−7)** = **07:50Z and 15:05Z**, both hours before the flash and
  before the `572bcfa` soak was stopped at ~15:30Z. **Nothing here is evidence about `1c2df3c`**,
  the shipped image — do not carry these numbers onto it. The power path is unchanged between the
  two trees, but "unchanged code" is an inference, not a measurement, and this file records the
  latter.
- **Host:** **not** Heliotrope Ridge. This is the **operator's bench current meter** — the same
  inline USB meter as the 2026-08-13 entry below, wired between the USB host power source and the
  RAK4631. Heliotrope Ridge built and flashed the image and ran the TTN-side soak; it did not take
  this measurement and cannot. No serial port was attached and nothing was reset to obtain it.
- **Measured:** whole-board current over the USB supply rail — peak and minimum — observed
  continuously across a window spanning **many 900 s cycles** (~7 h between the two readings).
- **Observation** — operator-reported, verbatim, in the order reported:

  ```
  max is now .14A
  ```
  ```
  sorry .14a
  ```
  ```
  still .14 amp max on the meter
  ```

  The second line is the operator correcting his own typo to make the magnitude explicit: the peak
  is **0.14 A = 140 mA**, not 1.4 A and not 14 mA. The third line came **~7 h later**, after many
  further cycles, and reports the peak **unchanged**. The minimum still **reads `0`**. Display
  resolution is unchanged at **0.01 A**, i.e. one least-significant digit is **10 mA**, so the peak
  is "0.14 ± 0.01 A" — fourteen digits of a fourteen-digit reading, not a calibrated 140 mA.

- **What this establishes.**
  1. **A peak consistent with a LoRa transmit burst has now been caught.** The RAK4631 datasheet
     gives `Tx mode LoRa @17 dBm` **92 mA** and `@20 dBm` **125 mA**, with an overall sleep figure
     of **2.0 µA**
     ([CITE(datasheet): RAK4631 WisBlock Core datasheet, "Power Consumption" — CIT-RAK4631-RAW](https://raw.githubusercontent.com/RAKWireless/rakwireless-docs/master/docs/Product-Categories/WisBlock/RAK4631/Datasheet/README.md)).
     A **whole-board** 140 mA peak sits sensibly *above* the radio-only figure once the nRF52840
     and the RAK5802 RS-485 transceiver are added to it. That is the first bench observation on
     this project that is *shaped like* a transmit event, and it removes the specific worry the
     2026-08-13 entry raised — that the meter might never catch the burst at all.
  2. **The peak is stable across many cycles.** Two readings ~7 h apart, over many 900 s cycles,
     both **0.14 A**. There is no runaway, no escalating draw, and no ratcheting peak — the failure
     mode where something latches on and never releases would have shown here and did not.

- **What this does NOT establish — read this before quoting a number from it.**
  1. **No sleep current. Still none, and this entry does not change that.** The `0` minimum is the
     meter's **resolution floor, not a measurement**. One least-significant digit is 10 mA; the
     module's datasheet sleep figure is **2.0 µA** [CIT-RAK4631-RAW], roughly **5000×** below a
     single digit of this display, and [`POWER_BUDGET.md`](POWER_BUDGET.md) turns on ~1 mA. A `0`
     on this meter is arithmetically incapable of distinguishing a healthy 2 µA sleep from a
     9 mA defect. "Sleep current is unmeasured" stays an open blocker in `AGENTS.md`, and
     [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8)
     **stays open**.
  2. **This is not a measured transmit current either.** 0.14 A is **one digit** of resolution on
     a meter that samples slowly, catching the tail of a ~50 ms burst by luck rather than by
     design. The honest reading is "**consistent with the datasheet**" — not "TX draws 140 mA."
     Do not enter 140 mA into a power budget as a measured figure, and do not derive an airtime or
     duty-cycle energy number from it. [`POWER_BUDGET.md`](POWER_BUDGET.md) keeps its LoRa TX row
     as a datasheet reference, not a bench figure.
  3. **It says nothing about the shipped image.** See the commit note above: `1c2df3c` was not on
     the board for either reading.

- **Relationship to the 2026-08-13 entry below — this supersedes nothing.** The 2026-08-13
  _inline USB current meter_ entry (peak 0.04 A, minimum `0`, verdict **INCONCLUSIVE**) stands
  exactly as written and is not restated here. That entry ended by noting the operator had left
  the meter inline **to watch for a higher peak across further cycles**; this entry is that watch
  returning a result. It **adds** a transmit-shaped peak observation and **removes nothing**: the
  40 mA reading was not wrong, it was a slow meter missing the burst, and its own warning against
  reading 40 mA as a transmit figure was correct. Both entries leave sleep current unmeasured.

- **Verdict:** **INCONCLUSIVE for both power gates, and genuinely informative anyway.** It closes
  no gate in `FIRMWARE_SPEC.md` §7 and does not advance H2. What it buys is a coarse peak that is
  *consistent with* the datasheet transmit figure and a demonstration that the peak does not grow
  over many cycles. Per `AGENTS.md`, no later document may inherit this as "transmit current
  measured" or "sleep current measured" — it is one coarse peak and one absent measurement,
  recorded together on purpose.

- **Next:** unchanged from 2026-08-13. Closing the power gate needs **µA-resolution**
  instrumentation — a Nordic PPK2 (the method behind [CIT-RAK-SLEEP]), a current-sense shunt on a
  scope, or coulomb-counting over a long window. A meter whose LSB is 10 mA cannot answer it no
  matter how many cycles it watches. Tracked in
  [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8).

<!-- Template:

### YYYY-MM-DD — one-line summary

- **Commit:** `abc1234`
- **Host:** Heliotrope Ridge
- **Measured:** sleep current between wake cycles, on battery, USB detached
- **Observation:**
  ```
  <raw reading / log excerpt>
  ```
- **Verdict:** PASS — H2 satisfied / FAIL — ... / INCONCLUSIVE — ...
- **Notes:** anything that would change how the next person reads this

-->
