# Evidence ledger

Measured results, and nothing else. **If it is not written down here, it did not happen** — and
the project status stays `🚧 NOT YET DEPLOYED` (`[AGENTS.md](../AGENTS.md)`).

Newest first. Add new entries at the top of [the log](#log).

## Reading and writing entries

Every entry names **date · host · commit SHA · what was measured · the raw observation · a
verdict**. "Inconclusive" is a legitimate verdict and beats an optimistic pass.

This file holds numbers and what they were read off. Everything else has a better home:
reasoning goes in `[decisions/](decisions/)`, open work goes in the tracker, behaviour goes in the
code, and procedure goes in `[FIRST_FLASH.md](FIRST_FLASH.md)`, `[SOAK.md](SOAK.md)` or
`[DEPLOY.md](DEPLOY.md)`. An entry that argues is an entry nobody finishes reading.

**Record outcomes, never launches.** On 2026-08-12 a "24 h soak started" entry was committed 92
seconds before the harness gave up, having never attached to the board; three other documents
inherited the claim before it was caught. Wait for the outcome, then write it.

### Commit SHA provenance

The boot banner prints the SHA since `033b584`, injected by `scripts/pio_git_rev.py`. Read the
`commit` line and record it verbatim.

| Banner says | Means | Record it as |
|---|---|---|
| `a7381e7` | built from that commit, clean tree | the SHA, **asserted** |
| `a7381e7-dirty` | that commit **plus uncommitted work** | the SHA and `-dirty`; the tree is unrecoverable, so the build is unreproducible |
| `unknown` | no git history at build time | `unknown`, plus whatever else identifies the build |
| nothing captured | the field image sleeps and detaches USB before a capture can attach | **NOT OBSERVED** — asserted from the build and DFU inputs, not from the device |

Banners before `033b584` print only `firmware :` and `built :`. A version plus a timestamp does
not identify a commit — two builds of one version read identically. Do not resolve it by picking
the newest commit older than the build stamp; that is an inference, and it is wrong whenever the
build came from a dirty tree, a branch, or a checkout that was not `main`. Mark such a SHA
**inferred**, name what the inference rests on, and **never assert a SHA the board did not
print.** A quietly-asserted inference poisons every document that copies it.

Three commits have named themselves off the board: `d568574`, `65f8615` and `1c2df3c`.

### Traps that invalidate an entry

- **A 10 mA least-significant digit cannot answer a ~1 mA question.** Both the RAK9154's current
  telemetry and the operator's inline USB meter resolve 0.01 A;
  `[POWER_BUDGET.md](POWER_BUDGET.md)` turns on ~1 mA and the module's datasheet sleep figure is
  2.0 µA. A `0` reading from either is a resolution floor, not a measurement.
- **Sleep current over USB is meaningless.** Measure on battery, USB detached
  (`[50-power-management.mdc](../.cursor/rules/50-power-management.mdc)`).
- **`239A:8029` proves *an* application is running, not *which*.** A previously resident image
  enumerates identically. Only a banner identifies the image.
- **A short window on a long duty cycle establishes nothing.** A 110 s subscription on an 1800 s
  cycle saw no uplink and was written up as a radio fault; the node had been heard the whole time.
  Query the device record, not the air — see `[DEPLOY.md](DEPLOY.md)`.
- **USB absence is not a liveness test** on a build that sleeps and detaches. TTN session state
  is, and it costs one command.
- A bench run with a debugger attached is not a field run.
- A decoded TTN payload proves the formatter matches *that* build, not the schema. The parity gate
  proves the schema (`[60-decoder-parity.mdc](../.cursor/rules/60-decoder-parity.mdc)`).

## Release gates awaiting evidence

From `[FIRMWARE_SPEC.md](FIRMWARE_SPEC.md)` §7. **None of these can be closed by inspection.**

"In source" means the mechanism exists in `src/` and a human read it — a precondition, not a pass,
and the weakest evidence this repo accepts. "Status" is the gate, which only a measurement closes.
A gate can be fully implemented and still `⬜ none`; H4 is exactly that.

| ID | Requirement | In source (2026-08-12 audit) | Evidence needed | Status |
|---|---|---|---|---|
| H1 | Hardware WDT resets a hung Modbus/BMS read | ✅ `NRF_WDT` armed at 120 s, fed on the long sensor paths; paused across sleep by design | Induced hang → observed reset | ⬜ none |
| H2 | Deep sleep between cycles; radio sleeps | 🟡 radio, SPI and USB-detach sleep are real; the "deep" half is a `delay()` loop and the code says so | Sleep current on battery, with a µA-resolution meter | ⬜ none |
| H3 | Brownout: no flash thrash, no TX when low | ✅ thresholds (9.60 V stop / 10.20 V resume), TX gate and flash gate all wired; the ungated session writer found by the audit closed in `378384e` | Sag the supply → observed skip | ⬜ none |
| H4 | Bounded backoff; survives multi-day no-gateway | ✅ doubling, clamped | Gateway off ≥48 h → observed backoff | ⬜ none |
| H5 | Interval + keys survive power loss | ✅ interval and session over `InternalFS`; keys are compiled in, so "keys survive" is trivially true | Set interval, cut power, confirm retained | 🟡 partial — session restore 2026-07-31, re-confirmed on node 002 at `9e2fcb4`; the 900 s interval survived two reflashes and a power cycle 2026-08-13. Interval-across-power-loss not isolated |
| H6 | RK900 absent → no livelock | ✅ bounded 1000 ms reply timeout; caller tolerates failure | Unplug sensor → cycle continues | 🟡 partial — silent sensor bounded 2026-07-31 and at `9e2fcb4`; still needs connected-then-removed injection |
| H7 | BMS silent → no livelock | ✅ bounded first-byte, inter-byte and provisioning windows — but **long**: `acquire_pid()` measured at 45.4 s of a 50.5 s wake, inside a 120 s WDT window with less margin than it sounds | Unplug BMS data → cycle continues | 🟡 partial — the no-latch path completed and slept at `9e2fcb4`; 13 pack-absent cycles ran the full ladder correctly 2026-08-30. Still needs connected-then-removed injection |
| H8 | Bench soak ≥24 h, field shadow ≥7 d | n/a — a process gate, no code implements it | Soak log + TTN ingest history | 🟨 **open on both halves.** See below |

**H8 in detail, because it is the most misread row in this file.** Real hours exist; 24
uninterrupted bench hours do not.

| Run | Image | Result | Why it does not close the bench half |
|---|---|---|---|
| 2026-08-13/14 | `572bcfa` (`v0.4.2`) | 19.03 h, 76 uplinks, 0 anomalies | Stopped deliberately at 15:30Z to ship the #75 fix. 19.03 h < 24 h, and a partial run on one image cannot be topped up by another |
| 2026-08-14/15 | `1c2df3c` (`v0.4.3`) | Ran its full 86405 s of 86400 s and exited normally | A **deliberate RESET inside the window** at 17:50Z (`resets=1`) — the operator's press to make the board print its banner. Not uninterrupted. It was also in the field with no console, so 7 of the 10 bench criteria in `[SOAK.md](SOAK.md)` could not be evaluated at all |
| 2026-08-12 | `f626698` | 180 s of waiting, 140 bytes, never attached | Failed attempt, cause unestablished |

The **field-shadow half has effectively not started.** 2026-08-14 produced the project's first
>24 h of continuous field runtime — 27.37 h of TTN-recorded uplinks, `f_cnt` 2391 → 2600 — and
then the operator picked the node up and packed it. Day one is not seven, and a shadow does not
accrue while the node is in a bag.

The `[FIRMWARE_SPEC.md](FIRMWARE_SPEC.md)` §9 first-light list is **closed**, and closing it
changes nothing above. First light is not hardening.

| §9 item | Closed by |
|---|---|
| One good RK900 frame | 2026-08-03, `998dc26` — full five-register read at 9600 |
| One good BMS frame | 2026-08-05, `1a203d3` / `b6bbf31`; re-confirmed 2026-08-12 at `b436aa9` |
| One TTN uplink | 2026-07-31 join + accepted uplink; session confirmed still advancing 2026-08-12 at `f4075c0` |
| One downlink applied | 2026-08-13 — all eight cases of the downlink matrix, `take_downlink()` observed on all five branches |

Power projections live in `[POWER_BUDGET.md](POWER_BUDGET.md)`. A projection is a hypothesis; only
a measurement recorded here closes it.

## Log

### 2026-08-30 — the count is NINE pads, and all nine died under agent-flashed firmware nobody authorized

**Source:** operator-stated, correcting the count for the second time. No instrument — the operator
counting their own dead pads is the measurement. Earlier entries below say four or seven; they are
the record of what was known at the time. **Nine is the number.**

Every one of the nine was the pad carrying the RAK9154 data line, and every one was running
**diagnostic firmware the operator never authorized, never requested and never invoked.** Agents
wrote it; agents flashed it over SSH, repeatedly, across sessions, including after pads had
started dying. **Zero pads have been destroyed by the production image** — node 001 has run it in
the field for weeks on the same parts order.

The attribution is established. The electrical *mechanism* is not, and must not be quoted as
settled. Tracked in
[#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102); rule
in `[05-never-instruct-an-unmeasured-connection.mdc](../.cursor/rules/05-never-instruct-an-unmeasured-connection.mdc)`.

### 2026-08-30 — the field image is flashed onto a silent core over SWD, and the core runs it

**Host:** Heliotrope Ridge. **Probe:** CMSIS-DAP-MuseLab `07000001006900394e00000956304750a5a5a5a597969908`,
USB `0D28:0204`, OpenOCD 0.12.0-01004 (xPack) at 1000 kHz. **Commit:** `b7e6e92`, `env:rak4631`,
application only — no bootloader or SoftDevice written. **Verdict: pass.**

**Core: NOT IDENTIFIED.** No die ID was captured, so this cannot be matched to a physical part and
does **not** assert which core it was. Nothing on the sensor side was inspected, so this makes no
claim about the RK900, the pack, the harness or the antenna — not even that they were absent.

Flash 211,972 bytes (26.0 % of 815,104), RAM 25,052 (10.1 %). One session, no reset between
connect and program:

```
Info : SWD DPIDR 0x2ba01477
Info : [nrf52.cpu] Cortex-M4 r0p1 processor detected
xPSR: 0x21000000 pc: 0x000fb702 msp: 0x2003ffd0     <- BEFORE: bootloader region
Info : nRF52840-xxAA(build code: D0) 1024kB Flash, 256kB RAM
Warn : no working area available, falling back to slow memory writes
** Verified OK **
```

Then `reset run`, 500 ms, `halt` → `pc (/32): 0x00050a42`. **The program counter moved from
`0x000FB702` (bootloader, ≥ `0xF4000`) to `0x00050A42` (application, `0x26000..0x59C04`).** A
read-only session seven minutes later re-verified 211,972 bytes in 4.458219 s and found the core
**already halted at `0x00050A42` on attach, before any reset** — so the application is still
executing minutes later, not merely reached once out of reset.

`no working area available` is the documented procedure working: `configure -work-area-size 0`
denies OpenOCD the RAM scratch area so it writes through the debug port instead of running a
loader on the target.

Flash contents, every address read over SWD rather than assumed:

| Address | Value | Reading |
|---|---|---|
| `0x10001014` | `0x000F4000` | UICR `NRFFW[0]` — bootloader pointer, correct |
| `0x10001018` | `0x000FE000` | UICR `NRFFW[1]` — MBR params, correct |
| `0x10001208` | `0xFFFFFFFF` | UICR `APPROTECT` unprogrammed — hardware protection not armed |
| `0x00000000` | `0x20000400`, `0x00000A81` | MBR vector table present and plausible |
| `0x00003000` | `0x2C` (low byte) | SoftDevice info struct size = 44 bytes |
| `0x00003008` | `0x00026000` | `SD_SIZE` — SoftDevice extent, and the application start it implies |
| `0x0000300C` | `0x00B6` (low half) | **`SD_FWID` = S140 6.1.1** |
| `0x00026000` | `0x20040000`, `0x00050B2D` | application vector table, freshly written |
| `0x000F4000` | `0x20040000`, `0x000FB26D` | bootloader vector table intact |

`SD_FWID 0x00B6` exactly matches `build.softdevice.sd_fwid` in `rakwireless/boards/rak4630.json`.

**What this retires:** "a core that answers SWD but not USB is a dead chip", for whichever core
this was. The part accepts a write, verifies it and executes it; bootloader, MBR, SoftDevice and
UICR pointers are all present, so it is not software-bricked either.

**The identity gap.** `FICR.DEVICEID` at `0x10000060` was read twice immediately afterwards and
both failed at `Error connecting DP: cannot read IDR`, the second attempt with
`connect_assert_srst` added. Retries stopped at two. The probe initialised fully both times
(firmware 2.1.0, `SWCLK/TCK = 1 SWDIO/TMS = 1 nRESET = 1`), so the failure is at the target end of
the wires — the marginal-contact signature already on this bench, not a part that died in the
interval. The successful program-and-verify came *first*. **Any session that gets a stable
connection should read `mdw 0x10000060 2` before anything else**, so results on this part stop
being unattributable.

**Not established:** no board-asserted SHA. No `239A` device on the bus before or after the flash
— but USB silence on this core is now *not* explained by a missing or invalid application, because
the application demonstrably runs. No sensor, radio, join, uplink or sleep claim.

CITE(prior-art): [CIT-OPENOCD-NRF5] — the `work-area-size 0` fallback to direct NVMC writes
(`nrf5.c:1143-1145`) behind that warning.
CITE(datasheet): [CIT-NRF-APPROTECT] — why `UICR.APPROTECT = 0xFFFFFFFF` is the open state, and
why no reset was issued between connect and program.
CITE(datasheet): [CIT-RAK-BOOTLOADER] — the RAK4631 ships bootloader V0.4.4 paired with S140
6.1.1, which is what `SD_FWID 0x00B6` decodes to.
CITE(bench): transcript at `~/rak-swd-logs/20260830_b7e6e92_swd_flash.log` on the build host.
Commit `b7e6e929550b8d085915aff21eaf5bb07624edb5`.

### 2026-08-30 — the pack's announcement decoded off the wire: it broadcasts unprompted at 9600 and identifies as "RAK2560-io"

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, capture 15,
`Async Serial` analyzer at 9600 8N1. **Core: none fitted, nothing transmitting** — pin 4 energised
from a bench supply at 3.3 V, data wire to the analyzer only, no MCU in the loop.

**828 bytes in 17.6 s. Nine frames, 92 bytes each, all nine byte-identical.** Inter-frame period
min 1.208 s, max 2.755 s, mean **2.186 s**.

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 33 09 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF 00 47 45 00 00
00 00 52 41 4B 32 35 36 30 2D 69 6F 00 00 00 00 00 00 00 00
00 00 00 00 00 00 06 15 BA 08 00 16 B9 08 00 17 B8 08 00 18
67 08 00 19 F3 08 00 1A F3 08 00 86
```

| Offset | Bytes | Reading |
|---|---|---|
| 0 | `FF` | wake byte — matches the four-wake-byte lead the driver implements |
| 1 | `7E` | frame delimiter, HDLC-style |
| 42–51 | `52 41 4B 32 35 36 30 2D 69 6F` | **ASCII `"RAK2560-io"`** — the device names itself |
| 66 | `06` | six entries follow |
| 67–90 | `15 BA0800 · 16 B90800 · 17 B80800 · 18 670800 · 19 F30800 · 1A F30800` | six ID/3-byte-value triplets, IDs `0x15`–`0x1A`; values LE = 2234, 2233, 2232, 2151, 2291, 2291 |
| 91 | `86` | trailing byte, presumably the `cal_chksum()` popcount |

**Established:** the pack transmits with no master on the bus and no MCU connected at all — the
protocol was previously believed strictly request/response. 9600 8N1 confirmed by decode on the
pack side, independent of [ADR-0006](decisions/ADR-0006-rk900-baud-and-register-map.md) (which
established 9600 for the *RK900* on a different bus). The device identity is on the wire in plain
ASCII, confirming [CIT-RAK-ONEWIRESERIAL] / [CIT-MESHTASTIC-9154] are the right references.

**Not established:** all nine frames are identical, so this is an announcement, not telemetry —
no field varied across 17.6 s. `FF` at offsets 7 and 34 is consistent with an unprovisioned id
awaiting assignment, so **listen-only cannot be assumed to yield battery readings**; the pack very
likely has to be answered at least once. Six consecutive IDs carrying six similar values is
relevant to [#7](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/7)
(cell count) but concludes nothing — the unit is unknown and six IDs need not mean six cells.

CITE(prior-art): [CIT-ONEWIRE-SERIAL] `RAK-OneWireSerial` @ `c58c0f0` — the wake-byte lead and
`cal_chksum()` this frame's first and last bytes are read against.
CITE(prior-art): [CIT-RAK-ONEWIRESERIAL] — RAK's own library, which names the RAK2560 Sensor Hub
probe protocol; the ASCII identity confirms it applies.
CITE(bench): decoded bytes at `/tmp/rak-owprobe/decode/pack9600_c15.csv`, capture preserved at
`~/rak-captures/20260830_pack_announcement_9600.sal` on the build host.

### 2026-08-30 — the pack's data line MEASURED at last: 3.31 V idle, 0.087 V driven low, never out of pad spec

**The oldest unsourced electrical assumption in the project is now a measurement.** The RAK9154's
data-line logic level had never been established from a datasheet or a bench reading on any node.

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, capture 13,
three analog channels at 781.25 kS/s, 2,347,642 samples over 60.100 s. **Core: none fitted, base
board not in the loop.** Pack pin 4 (`3V3_In`) energised from a bench supply at 3.3 V, 50 mA
limit, supply negative bonded to pack pin 2 with the analyzer ground.

Pin 4 was stable above 3.0 V for **57.11 s** (median +3.291 V) before the supply was switched off;
all figures are from that window.

| Channel | Pack pin | min | median | max |
|---|---|---|---|---|
| 0 | 1 (`P+`) | +10.521 V | +10.521 V | +10.521 V (analyzer clipped, ≥ 10.52 V) |
| 1 | 4 (`3V3_In`) | +3.004 V | **+3.291 V** | +3.302 V |
| 2 | joined 3+5 (data) | **+0.014 V** | **+3.313 V** | **+3.318 V** |

| The data line | Measured |
|---|---|
| Idle / HIGH | **+3.3118 V** mean, 96.56 % of samples (2,154,191) |
| Driven LOW | **+0.0867 V** mean, 3.44 % of samples (76,644) |
| Absolute max / min seen | **+3.318 V** / **+0.014 V** |
| Midpoint (1.65 V) crossings | **9,520** in 57.11 s |

1. **Overvoltage from the pack is RULED OUT.** Peak +3.318 V against the nRF52840's
   `VDD + 0.3 V` = 3.600 V absolute maximum — **282 mV of headroom**. Not 5 V, not 12 V.
2. **The pack's driver is referenced to pin 4, not `P+`.** Data HIGH +3.3118 V against pin 4 at
   +3.291 V — 21 mV apart, the same rail within the analyzer's 4.88 mV LSB. Since the build feeds
   pin 4 from the node's own `VDD`, the pack **structurally cannot** drive the line above the rail
   the nRF52840's I/O runs on.
3. **No negative excursion** across 9,520 edges: floor +0.014 V against the pad's −0.3 V limit.
   This test has no node ground return in it, so it constrains the pack, not the assembled system.
4. **The pack is an ACTIVE low-side driver, not a passive pull-down** — it pulls to +0.0867 V mean.
   Our end idles as a push-pull output driven HIGH, so an overlap is two active drivers in
   opposition with nothing between them but their on-resistances. **Contention is confirmed as
   physically available**
   ([#99](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/99)).
5. **A mated harness on a dark core is 11× over its absolute maximum.** The line idles at
   +3.31 V; an *unpowered* nRF52840 pad's maximum is **0.3 V**. Previously asserted from the rail
   label, now measured
   ([#101](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/101)).

**Not concluded:** a pack transmitting for 57 s at a safe level does not explain nine dead pads —
it removes candidates. Still open: H0 on the **base-board header** (`BAT` sits on the same 2.54 mm
row as the pads that die, and the board was not in this test), the contention path in conclusion 4,
and the unpowered-core exposure in conclusion 5.

CITE(datasheet): [CIT-NRF-GPIO] — the `VDD + 0.3 V` pad maximum the 282 mV headroom is measured
against.
CITE(prior-art): [CIT-NRF-BACKPOWER] — the 0.3 V unpowered maximum behind conclusion 5.
CITE(datasheet): [CIT-SALEAE-LOGICPRO8] — ±10 V analog range (why channel 0 clips) and 4.88 mV per
LSB (why 21 mV reads as "same rail").
CITE(bench): capture at `/tmp/rak-owprobe/energised/analog.csv` on the build host.

### 2026-08-30 — the pack alone is harmless, and the harness has no 12 V bridge to the data line

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, capture 12,
three analog channels at 781.25 kS/s, 1,172,363 samples over 30.012 s. **Core: none fitted, base
board not in the loop.** Only the pack and the analyzer, GND on pack pin 2.

| Channel | Pack pin | min | median | max | stdev | distinct ADC codes |
|---|---|---|---|---|---|---|
| 0 | 1 (`P+`) | +10.521 V | +10.521 V | +10.521 V | **0.000 mV** | **1** |
| 1 | 4 (`3V3_In`) | −0.046 V | −0.020 V | +0.011 V | 2.99 mV | 12 |
| 2 | joined 3+5 (data) | −0.017 V | +0.003 V | +0.045 V | 9.04 mV | 13 |

Zero samples on any channel sat more than 0.5 V from that channel's median. The pack transmitted
nothing for the full 30 s.

1. **The pack was live.** Channel 0 held +10.521 V on all 1,172,363 samples with one ADC code and
   zero variance — the analyzer clipping at the top of its ±10 V range, not a measurement. `P+` is
   **≥ 10.52 V**. This is the control the earlier 0 V capture lacked: that reading was not a dead
   pack.
2. **The pack does not drive pin 4** — −0.020 V with 2.99 mV of noise, far below the 20 mV floating
   threshold, so genuinely connected and genuinely at ground. `3V3_In` is a passive input exactly
   as [CIT-RAK9154-DS] describes. **Rejected: that the pack presents a rail on pin 4 which could
   back-feed the board's 3.3 V.**
3. **No `P+`-to-data-line bridge inside the harness.** `P+` was live at ≥ 10.52 V while the data
   wire read +0.003 V. Its only loads were the analyzer's 2 MΩ and the pack's measured 15 kΩ
   pull-down, so a resistive bridge would appear as a divider; 3 mV across 15 kΩ bounds any such
   path at **> 50 MΩ**, i.e. absent. **This clears the harness of H0.** H0 on the *base-board
   header* is untouched — the board was not in this test.
4. **With pin 4 unpowered the pack is mute and everything but `P+` is at ground.** A mated harness
   on an unpowered node is not sitting there with a lethal level on the data wire.

CITE(datasheet): [CIT-SALEAE-LOGICPRO8] — the ±10 V range that makes channel 0's single-code
reading a clip, and the 2 MΩ input that makes the divider argument in conclusion 3 valid.
CITE(datasheet): [CIT-RAK9154-DS] — SP11 pinout; `3V3_In` on pin 4, corroborated here by
measurement rather than by the label.
CITE(bench): capture at `/tmp/rak-owprobe/pins/analog.csv` on the build host.

### 2026-08-30 — the measurement gating #102 is void: it reads 0 V by construction

**Host:** Heliotrope Ridge. **Instrument:** Saleae Logic Pro 8 `AF11F852CEC20A9`, Logic 2.4.46.
**Core: none** — no RAK4631 involved and none at risk.

[#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102) names
one discriminating measurement and blocks fitting another core until it passes: *"pack live,
harness unplugged from the node, meter the joined data wire against pack pin 2. Anything above
3.3 V is the pin killer."* It was run. **It cannot answer the question, and would have returned a
false all-clear.**

| Capture | Setup | Samples | Span | min | max | median | stdev |
|---|---|---|---|---|---|---|---|
| 9 | nothing connected (instrument baseline) | 1,639 | 2.097 s | −0.192 V | +0.220 V | −0.009 V | **78.43 mV** |
| 11 | pack powered, harness unplugged from the node, ch0 on joined data wire, GND on pack pin 2 | 786,429 | 20.133 s | −0.025 V | +0.001 V | −0.009 V | **0.74 mV** |

Capture 11 is **0.000 V, flat, for 20 s** — 775,463 of 786,429 samples (98.6 %) on a single ADC
code, six distinct codes in the whole file. It is **105× quieter** than the baseline, and a 2 MΩ
input with nothing on it wanders across 77 codes while a low-impedance tie to ground does not — so
the probe was genuinely on the wire and the wire was genuinely at ground.

**Why it proves nothing:** the pack's data-line reference is its own pin 4, and pin 4 is fed from
the node. With the harness unplugged, pin 4 has no supply, the driver has no rail, and the line
rests at 0 V through the pack's 15 kΩ pull-down. That is the only possible outcome — identical
whether the harness is lethal or benign. The obvious reading ("≤ 3.3 V, therefore cleared") would
have licensed fitting another core to an unqualified harness. **No harness has been cleared. #102
stays open.**

Changed as a result: `scripts/owprobe.py` now distinguishes floating clip (> 20 mV stdev),
de-energised driver (< 20 mV stdev) and a genuine driven low, exiting 1 on the first two;
`[HARDWARE.md](HARDWARE.md)` § "Qualifying the pack harness" specifies the test that works —
energise pin 4 from the base board's `VDD` **with no Core fitted**, since the 3.3 V regulator is on
the base board, not the Core.

CITE(datasheet): [CIT-SALEAE-LOGICPRO8] — the ±25 V absolute maximum and 2 MΩ input that make this
measurable without a core; the ±10 V analog range that would have saturated on a 12 V line.
CITE(prior-art): [CIT-RAK19007] — the 3.3 V regulator sits on the base board.
CITE(bench): captures at `/tmp/rak-owprobe/{baseline,live}/analog.csv` on the build host.

### 2026-08-30 — SDA/P0.13 is a dead one-wire pad, and a 1 kΩ series resistor did not prevent it

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Core:** node 002's third core. **Image:**
`env:owscan_sda` at `afdaf48`. Measured by the firmware, so it is repeatable.

Three readings, each with one more thing taken away:

| Pack connector | 2.2 kΩ pull-up | `INPUT_PULLUP` result |
|---|---|---|
| connected | fitted | `idle LOW : 0 falling edge(s), 1735755 of 1735755 samples LOW` |
| connected | lifted at one end | `idle LOW : 0 falling edge(s), 1746448 of 1746448 samples LOW` |
| **unplugged** | **lifted** | `idle LOW : 0 falling edge(s), 1749426 of 1749426 samples LOW` |

With nothing on the net and the chip's own ~13 kΩ internal pull-up enabled the pin still reads LOW
on every sample. **P0.13 is shorted to ground.** The Saleae agrees independently: 18.8 M analog
samples over 12.024 s, median 0.121 V, range 0.106–0.205 V, **zero edges even while the firmware
was bit-banging 64 bytes of `0x55` at three bauds.**

**The meter comparison, on the same core with the same meter, powered down:** `SDA`/P0.13 reads
**5.6 kΩ** to ground against **240 kΩ** on the never-connected `SCL` control pin — **43× apart.**
And the 0.121 V seen on the *powered* pin implies **~495 Ω** at 3.3 V, which is not the 5.6 kΩ the
meter reads unpowered — so it is a **non-linear damaged junction**, a punched-through ESD clamp on
the **ground** side. That is the direction in which our pin sinks the pack's active high.

**The contention arithmetic behind [#99](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/99),
stated with the fact/inference line drawn.** **Fact:** the production battery read drives roughly
**14 bytes** per cycle at a 900 s cadence, while `owscan` phase 0 drove **192 bytes** per cycle —
64 bytes of `0x55` at three baud rates — and cycled in seconds. `0x55` toggles every bit, the worst
possible pattern for the contention path. **Inference, not measured:** that two live drivers in
opposition pass ~**3 mA** through the pad. That figure is Ohm's law across the 1 kΩ series
resistor, not an observation — nobody has ever watched the node's end of that wire while it
transmits. The arithmetic does resolve one real error, though: earlier sessions compared it to the
nRF52840's **15 mA absolute maximum**, found it under, and cleared contention — when the pin's
standard-drive *continuous* rating is **1–2 mA** [CIT-NRF-GPIO-TOTAL]. Wrong limit compared
against. That makes chronic degradation plausible; it does not make it established.

**The measurement that would settle it, and has not been done:** an analyzer channel on each side
of the 1 kΩ series resistor while the node transmits. The difference divided by 1 kΩ is the
contention current, directly. Under 1 mA kills the hypothesis; ~3 mA supports it. It needs a
flashable core ([#95](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/95)).

Two hypotheses refuted here: a pull-up miswired to ground (lifting the resistor changed nothing)
and a powered pack clamping the bus low (unplugging the pack changed nothing either).

**A 1 kΩ series resistor was inline when SDA died.** That is the mitigation `HARDWARE.md` required
and this ledger recommended after the A1 failure. **Series resistance is refuted as sufficient
protection.**

**No firmware change since 001's image can explain it — established by diff, not argument.** Node
001 has been in the woods since 2026-08-14 on `1c2df3c` with no pad damage. Diffing the one-wire
path `1c2df3c` → `afdaf48`:

| File | Change |
|---|---|
| `src/sensors/battery.cpp` | **none** |
| `src/sensors/battery_frame.{h,cpp}` | **none** |
| `src/diagnostics/owscan.cpp` | 8 lines, every one a comment or a log string |
| `src/build_features.h` | the two pin-select flags and the version bump |
| `src/main.cpp` | `kBatteryPin` selection, the reset banner, a session-recovery callback |

So the driver where the pads died is functionally identical to the one running without incident on
001 — same push-pull `setTX()`, same four wake bytes, same ladder, same `0x55` sweeps. **This
refutes bus contention as the explanation for the difference between the two nodes**, not as
physics. Any theory has to account for two cores failing on one harness while a third runs the same
code safely on another.

The variables never swapped are **node 002's harness and its pack.** Note also that back-powering
through an ESD diode should short a pin to VDD, not to ground; Nordic's own thread raises that
objection and leaves it unresolved. Every dead pad here measures short to *ground*. **The cause is
unestablished. Only the correlation is solid.**

### 2026-08-30 — node 002 reads weather and transmits on SDA; opening the console is what resets the board

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Core:** node 002's third core, one-wire on
`SDA` (P0.13), 1 kΩ inline. **Image:** field image at `7ad5daa`, then `env:battdiag_sda`.
**Pack physically disconnected throughout** — operator-confirmed.

```
2026-08-30 12:37:38    session : restored 0x260C1AF4, counter 1088
2026-08-30 12:37:38 [cycle 1]
2026-08-30 12:37:38    RK900   : raw 0x0000-0x0004 = 0000 0000 0107 01B5 275E
2026-08-30 12:37:38    RK900   : wind 0.00 m/s @ 0 deg, 26.3 C, 43.7 %RH, 1007.8 hPa
2026-08-30 12:38:05    battery : no data (no reply, 0 bytes)
2026-08-30 12:38:05    radio   : sent 20 bytes on port 2
2026-08-30 12:38:13    sleep   : 3600 s
```

**`battery : no data` is the correct result with no pack attached, not a fault** — three sessions
were spent treating it as one. The 20-byte uplink is wind-only with no battery block. DevAddr
`0x260C1AF4`, not the `0x260CE734` carried all month, so this core performed a fresh OTAA join
earlier that day rather than restoring the old session.

**The interval downlink was rejected, so cadence never changed.** The node was asked for 300 s
repeatedly through the afternoon and refused every time; the floor is 900 s. Every "why is it
still on the slow cadence" question that day has this answer.

```
radio   : downlink — set interval 300 s
config  : rejected interval 300 s (allowed 900-86400)
```

**Inference (strong, 3/3 correlation): the console attach is what reset the board.** Every
`[cycle 1]` in the day's captures falls within one second of `scripts/capture.py` opening the port
— 12:35:47, 12:36:11, 12:37:38. The `f_cnt` steps read as fault evidence all afternoon
(`864 → 896 → 928 → 960 → 992 → 1024`, uniform +32) are the `kCounterMargin` signature of one
reset each, aligning with capture attempts rather than transmits. **A counter jump observed while a
capture is being started is not evidence of a fault.** This is timing correlation, not a measured
reset cause — `power.cpp` read `RESETREAS` and discarded everything but the watchdog bit. **Fixed
in `d25f823`**: the boot banner now names the cause, making the next occurrence a measurement.

**The failure ladder behaves as specified with the pack absent.** 13 consecutive `battdiag_sda`
cycles: one BOOT per failure episode and not more (`BOOT already spent this failure episode`),
degradation to `probe only`, full-ladder retry every second cycle, and the no-evidence hold arming
its bounded escape — `power : pack silent for 4 cycles — holding transmissions, no voltage
evidence (keepalive in 24 cycles)`. First time the #45 / #71 hardening has been observed running
on hardware.

### 2026-08-30 — node 002's silent core is ALIVE: SWD answered, chip unlocked

**Host:** Heliotrope Ridge, CMSIS-DAP probe `07000001006900394e...`, OpenOCD 0.12.0. **Core:** node
002's third core, the one that stopped enumerating on USB.

```
Info : Connecting under reset
Info : SWD DPIDR 0x2ba01477
Info : [nrf52.cpu] Cortex-M4 r0p1 processor detected
Info : [nrf52.cpu] target has 6 breakpoints, 4 watchpoints
0x00000001          <- CTRL-AP APPROTECTSTATUS
```

**`APPROTECTSTATUS = 0x00000001` means access port protection is NOT active** — the debug port is
open, so no mass erase is needed. A dead chip cannot return a valid DPIDR, let alone report its own
core revision and breakpoint counts. Combined with RAK's discriminator — a missing SoftDevice kills
USB while leaving SWD working [CIT-RAK-USBDEAD] — **this core is software-bricked, not damaged.**
The connection required `connect_assert_srst`; plain `init` failed at 100, 400 and 1000 kHz, which
indicates firmware executing in there that interferes with the debug port when allowed to run.

**Not reproducible on demand.** After that single success, **120 consecutive attempts failed** with
`Error connecting DP: cannot read IDR` across three clock speeds, with and without
`connect_assert_srst`. Nothing changed on the software side. The probe initialises correctly every
time, so the failure is at the target end of the wires — **the signature of marginal probe contact,
not of a chip that died in between.**

**Not established:** what is in flash. The UICR bootloader pointer, the vector table at `0x0`, the
application at `0x26000` and the bootloader at `0xF4000` were all queued and **none were read** —
every attempt after the first failed to connect. (They were read successfully later the same day;
see the SWD flash entry above.)

### 2026-08-30 — buck output measured at 4.98 V; the buck is cleared

**Host:** Heliotrope Ridge bench. **Core:** node 002's third core. **Meter:** operator's DMM.

**Buck output 4.98 V**, pack near full charge (~12.4–12.5 V at the buck input, from the same pack
that reported 12.41 V over the air at 18:13:20Z the same day). That is inside the RAK19007's
`VBUS` absolute maximum of 5.5 V [CIT-RAK19007-DS] and the nRF52840's own 5.8 V.
**Overvoltage from the buck is refuted as the cause of the whole-core USB failure.** This closes
the check `[FIRST_FLASH.md](FIRST_FLASH.md)`:25 has asked for since the project began.

**Retracted the same day, recorded so it is not re-derived:** the hypothesis that the buck's real
hazard was *input sag* — a growing sawtooth overshooting setpoint as the pack discharged toward the
module's enable threshold. It does not apply. [CIT-MP1584-SCOPE] was taken on a module configured
for **3.3 V** out, whose sawtooth appeared near an ~11.8 V input threshold specific to that board;
an MP1584 configured for **5 V** out needs roughly 6 V in, the pack's own protection cuts off far
above that, and this firmware stops transmitting at a 9.60 V inhibit. [CIT-TI-BUCK-OVERSHOOT]
describes overshoot on recovery from an *output short*, a different event. Both citations stay in
the registry because they correctly describe what they measured; neither supports a claim about
this node.

**Therefore still unexplained:** the whole-core USB failure has no established cause. The
back-powering mechanism in
[#96](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/96) explains a
single dead pad and predicts the rest of the chip surviving; it does not predict a core that
vanishes from the USB tree.

**Unrelated finding the same day: RAK's own reference path does not put this data line on a bare
core GPIO.** `beegee-tokyo/RAK-OneWireSerial`, the only published code naming the RAK9154, requires
a **RAK13002 WisBlock IO module** — "the Sensor Probe is connected with 3.3 V, GND and RXD1 only
from the RAK13002 module" — prefers `RX1`/P0.15, says other GPIOs "should work… but it is not
tested", and is marked BETA [CIT-RAK-ONEWIRESERIAL]. This build's topology is outside anything RAK
documents, and the module skipped is the one that sits between the pack and the core. The RAK9154
datasheet confirms the SP11 pinout in use but is **silent on every electrical property** of the
data lines [CIT-RAK9154-DS] — no logic level, no open-drain-versus-push-pull, no internal pull
resistance, no statement of what `3V3_In` is for. The 15 kΩ measured on the pack's data line is
close to the nRF52840's own internal pull range of **11–16 kΩ** [CIT-NRF-GPIO-TOTAL], but nothing
in RAK's documentation says what the pack presents.

### 2026-08-30 — RETRACTED: back-powering an unpowered core named as the cause of the dead pads

> **Retracted the same day it was written.** The entry claimed the cause was established. It was
> not, and it was wrong on two counts. **Direction:** back-powering conducts through the pad's
> *upper* ESD diode and shorts a pin to **VDD**; every measured pad here reads short to
> **ground**, and Nordic's engineers hit this same signature and say the remaining possibilities
> are a negative voltage on the pin or a lifted ground [CIT-NRF-GNDLIFT]. **Count:** it said four
> pads across four cores; the count is nine. The mechanism that does match the signature is loss
> of the ground return, which makes the data pin carry the core's entire supply current
> [CIT-NRF-GNDLOSS] — also a candidate, not a proven cause. It is left in place rather than
> deleted because deleting a wrong claim hides that it was made. Tracking:
> [#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102).

The **observations** in it stand:

- A dead pad reads permanently LOW as an input and measures **~3.9 Ω to ground**. The rest of each
  chip kept working. In every case the dead pad was the one carrying the RAK9154 link; pads on the
  same cores never connected to the pack (`A0`, `SCL`) remained healthy.
- Node 002 stopped presenting on USB entirely — no `/dev/cu.usbmodem*`, no UF2 volume, and
  **nothing in `ioreg -p IOUSB`** but the Mac's own hubs and a SuperDrive. Not even a
  failed-enumeration entry.
- **The header-geography reading is incidental.** Every dead pad sat on the `BAT IO2 IO1 A1 IN1`
  row while every healthy pad sat elsewhere — real, but those pads were on that row because that
  is where the one-wire pad happened to be. It has no mechanism and cannot explain `SDA` dying
  later on a different row.
- **Why 002 and not 001, as an exposure argument rather than a mechanism:** the buck powers the
  core *through the USB-C connector*, so the buck and a host cable are mutually exclusive, and
  every swap contains a window with the core dark and the harness mated. Node 002 went through
  that window roughly a dozen times in one day of bring-up; node 001 was wired once and deployed.
  RAK's own recommendation is that a regulated external supply goes to the **P2 "Green Power"
  connector, not USB-C** [CIT-RAK-GREENPOWER], which would close that window.

Connector sequencing is now a documented rule in `[HARDWARE.md](HARDWARE.md)`: the pack connector
is mated **last** and unmated **first**, always.

### 2026-08-30 — node 002 moves to SDA/P0.13 after A1 dies; the pack reads and the uplink lands

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`. **Commit:** `7ad5daa` (`env:rak4631_sda`,
`FEATURE_BATTERY_PIN_SDA=1`), asserted from build and DFU inputs — `banner_commit=NOT OBSERVED`.
**Core identity not captured**; pinned to the node (`puma-concolor-002`, DevEUI
`42BB96EF76E200F2`), not to a physical part.

**What failed first.** `A1` (P0.31) carried a complete pack reading at 15:08:48Z (12.54 V, entry
below) and was held low by 16:45Z. Measured with `env:owscan_a1`:

| Pin | Reading | Conditions |
|---|---|---|
| `A1` P0.31 | idle LOW, 1,823,490 of 1,823,490 samples | original harness, a **second** harness, and harness **fully removed** |
| `IO1` P0.17 | idle LOW, 1,822,564 of 1,822,564 | never wired; and again with the RAK5802 removed, 1,814,723 of 1,814,723 |
| `A0` P0.5 | idle HIGH, 0 of 1,852,145 low | never wired — validates the instrument |
| `SDA` P0.13 | idle HIGH, 0 of 1,848,823 low | never wired |

`A0` and `SDA` reading HIGH on the same core is what makes the LOW readings real rather than an
artefact. **Pulling the RAK5802 did not lift IO1**, refuting the hypothesis that a slot module was
holding it (`WB_IO1` is a `SLOT_A`/`SLOT_B` signal per `variant.h`:45).

**The pack is not an overvoltage source — first time metered on this project.** Operator DMM,
joined 3+5 wire unplugged from the board, pack live: **20 mV DC** to pack minus, and **15 kΩ** to
pack minus. A pull-down, not a driver clamping the line. **Consequence:** 15 kΩ against the
nRF52840's ~13 kΩ internal pull-up puts the idle line near 1.7 V, inside the undefined band
between V_IL and V_IH — **this bus has been marginal by construction.** An external 2.2 kΩ–4.7 kΩ
pull-up to 3V3 puts idle near 2.9 V.

**SDA baseline before the pack was connected** — the first "before" measurement ever taken on a
one-wire pad on this project: `env:owscan_sda` after full assembly with the pack disconnected,
`idle HIGH`, 0 of 1,847,336 samples low.

**The pack reads on SDA and the pad survived the connect.** `env:battdiag_sda` at `7ad5daa`, 10 s
cycles, two sessions:

```
[cycle 9]  battery : sampling confirmed — pack is reporting live values
           battery : 12.44 V  +0.00 A  100%  24.0 C
           battery : sendat FF 7E 00 15 02 01 00 01 16 03 10 02 15 BA DC 04 16 B9 00 00
                     17 B8 64 18 67 F0 00 36
```

Cycles 9–12 clean; after the pack was unmated and remated, cycles 15–19 clean at **12.43 V,
−0.01 A, 100 %, 24.0 °C**. Latched at `0x01` both times. Cycles 1–8 returned `all-zero records
(pack not sampled)` — the documented warm-up, longer here than the usual ~2 cycles.

**Field image and TTN.** `env:rak4631_sda` at `7ad5daa`, console showed `sleep : 3600 s`. TTN then
reported `updated_at 2026-08-30T17:56:24Z` (the boot second) with `last_f_cnt_up 832`, `dev_addr
260C1AF4`, session `started_at 2026-08-30T04:00:02Z` — **session restored, not rejoined.**

**Why the pads failed: still unknown, and six hypotheses are dead.** Recorded so none is
re-derived.

| Hypothesis | Status |
|---|---|
| Driver contention (push-pull TX, no current limiting) | **Real defect, not sufficient.** `owscan` transmitted into node 001's IO1 on 2026-08-04 and 2026-08-12; 001's IO1 works. Tracked as #99 |
| Ground-reference float on host USB | **Refuted.** The buck is non-isolated, and `P−` lands directly on the base-board `GND` pad |
| `VDD` at 5 V overdriving the pack reference | **Refuted.** RAK19007 J12 `VDD` is 3.3 V [CIT-RAK19007] |
| Miswired replacement harness | **Refuted.** Operator confirmed pin by pin |
| Slot module holding IO1 low | **Refuted.** RAK5802 removed, IO1 still 100 % low |
| Electrostatic discharge during handling | **Weak.** ~66 % ambient RH, no carpet, no pets |

**The gap that matters: no core's IO1 has ever been measured before installation.** Every reading
is post-hoc, so "arrived shorted" cannot be told from "shorted here." Incoming test on every new
core — `IO1` to `GND` on the core connector, expect megohms — is the cheapest thing that would
close it.

**Not established:** a decoded payload for this cycle, any wind reading on this image, a
board-asserted SHA, sleep current. **Node 002 is not cleared for the field.**

### 2026-08-30 — node 002 reads the RAK9154 over A1 and the uplink lands at TTN

**Host:** Heliotrope Ridge. **Commit:** `a48e996eac974670d6f722c06c0d150962f5a744`
(`env:rak4631_a1`, `FEATURE_BATTERY_PIN_A1=1`), asserted by the build and DFU inputs and by
`flash.sh`'s post-upload PID check — not read back from a banner. **Core identity not captured**;
pinned to the node, not the part. Pack wired to `WB_A1` (P0.31).

```
puma-concolor-002  dev_addr 260C1AF4  f_port 2  f_cnt 448
2026-08-30T15:08:48Z  gateway 3356-gateway-002  RSSI -101  SNR 8  SF7  905.1 MHz
frm_payload  17b86415ba04e616b9fffe186700dc   (15 bytes)
```

Decoded **12.54 V, −0.02 A, 100 %, 22.0 °C** — channels 21 `0xba` voltage, 22 `0xb9` current, 23
`0xb8` capacity, 24 `0x67` temperature. First RAK9154 reading over **A1** rather than IO1, and it
went end to end: pack → one-wire on P0.31 → encoder → radio → TTN → decoder.

**The RK900 answers once the harness is assembled.** Later the same morning, `env:stage1` at
`dd36e63d5f198195213530104d8b379afa9362f4`:

```
RK900   : raw 0x0000-0x0004 = 0000 0000 0100 01BA 275F
RK900   : wind 0.00 m/s @ 0 deg, 25.6 C, 44.2 %RH, 1007.9 hPa
```

Full five-register read at 9600 8N1, slave `0x01`; the raw words corroborate the decode
(`0x0100` → 25.6 °C, `0x01BA` → 44.2 %RH, `0x275F` → 1007.9 hPa). **Wind 0.00 m/s at 0° is a real
reading from a still anemometer, not a fabricated zero** — registers `0x0000`/`0x0001` are
genuinely zero and the driver reported a successful read. So **both sensors answer on this node.**

**A paired example of the null policy, worth keeping.** Earlier the same day, before assembly, the
uplink was **15 bytes carrying 4 fields** where a full one is 35 bytes carrying 9 — no wind channel
present at all, because a failed RK900 read is omitted rather than encoded as zero. The cause was
simply that the harness was not assembled. Same firmware, same day: field omitted when the sensor
could not answer, genuine `0.00 m/s` encoded when it could.

**Not established:** whether this core's IO1 is intact — A1 was chosen by decision, not by
measurement. Also no wind reading on the field image, no sleep current, no board-asserted SHA.

### 2026-08-30 — a RAK4631-R (RUI3) core is converted to the Arduino bootloader over SWD and runs v0.4.4

**Host:** Heliotrope Ridge. **Commit:** `31805bad9f6470765a6ea4f6179c878ad3f70bbb` (`v0.4.4`),
asserted from build and DFU inputs and `flash.sh`'s PID check, not from a banner. RAK4631-R module
on its base board, USB-C to the build host, RAKDAP1 on SWD. **No sensors attached.**

> **This core was later discarded** — it kept dropping off USB and SWD and the operator replaced
> it; it is **not** the core in node 002. No die ID was captured, so nothing here can be matched to
> a surviving part. The **procedure** is core-independent and remains valid
> ([#97](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/97)).

**What was established: a board bought as RAK4631-R can be converted to this project's bootloader
in place, and the converted core runs the field image.**

The core enumerated as **`1915:521F`** (RUI3), which neither `adafruit-nrfutil` nor Nordic
`nrfutil dfu` could program. The Arduino S140 bootloader was written over SWD in 197 s at
1000 kHz, `** Verified OK **`. Two mechanisms had to be understood first, both recorded with
citations in `[FIRST_FLASH.md](FIRST_FLASH.md)`:

- OpenOCD's nRF5 driver executes its flash loader **on the target**, which a mass-erased part
  cannot do — it double-faults into lockup at `pc 0xfffffffe`. `configure -work-area-size 0`
  forces direct NVMC writes ([CIT-OPENOCD-NRF5], `nrf5.c:1143-1145`).
- The software half of APPROTECT re-arms on **every** reset unless running firmware writes
  `SwDisable`, so any reset between erase and program re-locks the part mid-recovery
  ([CIT-NRF-APPROTECT]).

UICR read back correct (`NRFFW[0] = 0x000F4000`, `NRFFW[1] = 0x000FE000`). Post-conversion the
board enumerated **`239A:0029`** — UF2 bootloader, no application — confirming the conversion
rather than inferring it. Then `=== FLASH OK ===` at `usb: 239A:8029 (application running)`.

**A debug-power failure that reads as a dead chip.** Between conversion and flash, every SWD
attempt failed **after** `SWD DPIDR 0x2ba01477` read cleanly:

```
Debug: DAP: wait CDBGPWRUPACK
Debug: DAP: poll 4 timeout
Debug: Command 'dap init' failed with error code -5
```

pyOCD failed identically at the same point. `DPIDR` answering while `CDBGPWRUPACK` never arrives
means the debug port's always-on logic is alive but the die has no core power — and it looks
exactly like a destroyed chip. **A power cycle of the base board restored it**: on the 13th retry
the same command returned `Cortex-M4 r0p1 processor detected` and the `239A` device reappeared on
the same attempt. No damage. Recorded because the misreading cost an hour and the correct test —
power-cycle, then retry — takes ten seconds.

### 2026-08-28 — node 002 runs v0.4.4 without sensors: bounded failures, session checkpoint/restore, TTN delivery, sleep

**Host:** Heliotrope Ridge. **Commit:** `9e2fcb4a14a6db21165ee2ce23502c544673d99d` (`v0.4.4`),
asserted from build/DFU inputs and an immediate pinned-port capture; `capture.py` attached just
after the banner and correctly reported `banner_commit=NOT OBSERVED`. `puma-concolor-002` RAK4631
and antenna only — no RK900, no RAK9154, no field wiring. **Verdict: PASS for the software-only
node 002 baseline.**

Build: `36 test cases: 36 succeeded`, `rak4631 SUCCESS`, then `USB 239A:8029 -- application
running`. Before the flash the offline checker compared the build host's ignored `src/secrets.h`
against the DevEUI and JoinEUI read from TTN and returned `IDENTITY_OK`. No AppKey was printed.

First sensor-absent cycle, and again after an identical DFU reset — byte-for-byte the same shape
both times except the counter:

```
config  : interval 3600 s, boot #1
session : restored 0x260CE002, counter 96        (second run: 128)
[cycle 1]
   modbus attempt 1/3 failed (timeout)   ... 2/3 ... 3/3
RK900   : no data (timeout)
battery : silent at its id 1 of 3 consecutive cycles — no BOOT yet
battery : no confirmed latch — proceeding unprovisioned
battery : no data (no reply, 0 bytes)
uplink  : proof of life — no sensor data for 1 cycle(s)
session : saved 0x260CE002, resume at 128        (second run: 160)
radio   : sent 0 bytes on port 2
sleep   : 3600 s
```

TTN stayed at `last_f_cnt_up=96` after the first cycle — that frame reused the counter held by the
pre-v0.4.4 session state and was not delivered, though the new image still wrote the future ceiling
128 before transmitting. After the reset TTN reported `last_f_cnt_up=128`, **proving the restored
reserve produced a network-accepted post-reset uplink.**

Strengthens H5/H6/H7: checkpoint/restore and TTN acceptance across reset, both absent-sensor paths
bounded, sleep reached twice. Does **not** exercise measured-low brownout, intermittent one-wire
validity, failed LittleFS operations, connected sensors, sleep current, or H8.

### 2026-08-27 — a second node exists: `puma-concolor-002` registered, flashed and OTAA-joined; board-side behaviour NOT observed

**Host:** Heliotrope Ridge — build, USB upload and `ttn-lw-cli` all on the build host.
**Commit:** `1bb18d7`, asserted from the build host's checked-out HEAD at upload time, **not read
back from the banner** — the serial capture returned 0 bytes.

This establishes that a *second* physical node has its own network identity and can join. It
establishes nothing about sensors, payload, sleep or power on that node.

| Claim | Verdict |
|---|---|
| `puma-concolor-002` registered on TTN, distinct identity, `001` unaffected | **PASS** |
| Uplink payload formatter present on `002` and byte-identical to the live formatter | **PASS** — set 22:01:57Z, `717afceb…` |
| Payload parity by gate, against the live formatter | **PASS** — 9 fields, plus 21 golden-vector values |
| Field image uploaded; board running an application (`239A:8029`) | **PASS** |
| OTAA join by `puma-concolor-002` | **PASS** |
| Which commit is executing, from the board | **NOT OBSERVED** |
| Sensors, payload, uplink contents, sleep, power | **NOT OBSERVED** — no sensors attached |
| A decoded uplink from `002` | **NOT OBSERVED** — none sent |

**Registration** mirrors `puma-concolor-001` exactly — same JoinEUI, frequency plan
(`US_902_928_FSB_2 MAC_V1_0_3 PHY_V1_0_3_REV_A`), `nam1` servers and RX2 (DR8, 923.3 MHz). Only
the DevEUI (`42BB96EF76E200F2`) and AppKey differ. Root keys were generated by TTN and written
directly into the gitignored `src/secrets.h` on the build host; the AppKey was never printed to a
console, a chat, or a tracked file, and `git status --porcelain` was empty afterwards. New hardware
serial on record: `DD1823E10F746C44`.

**The join is the strongest observation here.** A populated `session` and `mac_state` timestamped
~50 s after the upload completed means the node joined on its own with no console and no operator
action. Keys, radio and the running image all have to be correct simultaneously, so this is what
makes "an application is running" specifically *this* image — by inference from the network side.
`dev_addr 260CE002`, `started_at 2026-08-27T21:47:19.090255124Z`, `last_f_cnt_up: None`. Link
margin at join **RSSI −109 dBm, SNR −0.5 dB** to `3356-gateway-002` at SF7 — joined fine, far
weaker than `001`'s field readings of −59 dBm / 14.5 dB.

**Why `002` has not uplinked — designed behaviour, not a fault.** The one stored record is the
join, identifiable because its inner `received_at` is byte-identical to the session `started_at`;
it carries no `f_port`, `f_cnt` or `frm_payload`. With no pack reading, `power::Brownout` engages
without evidence after `kInvalidReadsBeforeInhibit = 4` cycles and the uplink is held until
`kNoEvidenceKeepaliveCycles = 24` have passed, at which point one *empty* keepalive goes out. A
fresh board also has no stored interval, so it runs `kIntervalDefaultSeconds = 3600 s`. **A
decodable uplink from this node requires the pack to be attached.**

**Two real defects found, both in the payload contract.** First, the parity gate **failed** and
`flash.sh` refused to build; the upload was run directly with `pio` at the operator's explicit
instruction, which skipped the gate. The finding was real — the formatter in
`forest-weather-machines` had moved (`717afceb…` against the pinned `9c58c2b9…`) and
`check_decoder_parity.py` could not parse its new shape, because `3cfd281` replaced the flat
`CHANNEL_NAMES` string map with a `CHANNELS` map of objects.

Second, and worse: **the device had NO uplink payload formatter at all.** Formatters on this
application are set **per device** (`WIND_DIR_OFFSET` differs per install), so there was no
inheritance to fall back on — every uplink from `002` would have landed with `frm_payload` and no
`decoded_payload`, from a node that looked perfectly healthy. `001` had one, but a **stale**
revision predating the unified formatter.

Both were fixed rather than pinned around, and the gate is no longer bypassed: the checker now
reads both map shapes and reports an unparseable map as its own named failure; all **9** emitted
fields were re-verified against the live map with **no firmware change needed**;
`check_golden_vectors.py` PASSED **21 decoded values across 5 vectors** with `source: live` — real
encoder bytes through this exact formatter; only then was the pin moved to `058bd69` /
`717afceb…`, and `preflight.sh` reaches `=== PREFLIGHT OK ===`. The formatter was read back **out
of** TTN and compared byte-for-byte rather than trusted: `BYTE IDENTICAL: True`.

**The build host had no `forest-weather-machines` clone at all**, which is why its golden-vector
run reported `source: pinned` and could never have detected upstream drift. Cloned via `gh`, not
copied between machines. Closes
[#83](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/83).

`WIND_DIR_OFFSET = 230` is **001's site correction, not a statement about 002's orientation.** It
affects only `wind_direction`, and 002 has no RK900 attached — but it must be set before 002 is
installed or the heading arrives rotated and plausible
([#84](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/84)).

### 2026-08-15 — first >24 h of continuous field runtime, ended by the operator picking the node up; watcher log deleted and raw transcripts permanently lost

**Host:** Heliotrope Ridge, **network side only** — `ttn-lw-cli` against `my-app-tobi` /
`puma-concolor-001`. Nothing was attached to the node: it was not flashed, reset or read over
serial for this entry. **Commit:** `1c2df3c` (`v0.4.3`), asserted from the board's own banner
2026-08-14T17:50:12Z and unchanged since.

Three separate things, which must not be collapsed into one another.

**1. The 24 h watcher run completed. It did not die.** The process was absent because it
**finished**, having written its summary.

```
2026-08-14T16:09:16Z === SOAK TTN START === label=rc-v0.4.3-1c2df3c duration=86400s
2026-08-15T16:09:21Z === SOAK TTN DONE === elapsed=86405s uplinks=89 resets=3 anomalies=1 query_failures=2 f_cnt=2464->2596
```

Every non-routine line in the 374-line log — this is the complete set:

```
2026-08-14T17:50:21Z SOAK NOTE  counter-step +26 in 971s within the 32-frame reset reserve
2026-08-14T20:58:47Z SOAK WARN ttn query failed (1 so far) -- no statement about the node
2026-08-14T21:01:17Z SOAK WARN ttn query failed (2 so far) -- no statement about the node
2026-08-14T22:10:01Z SOAK ANOMALY silence 2788s with no counter advance (limit 2700s)
2026-08-14T22:56:29Z SOAK NOTE  counter-step +18 in 2789s within the 32-frame reset reserve
2026-08-15T15:24:53Z SOAK NOTE  counter-step +2 in 2005s within the 32-frame reset reserve
```

The `+26` at 17:50:21Z is the operator's deliberate RESET, the same press that produced the banner.
**The `+18` at 22:56:29Z is not explained by anything anybody did** and, unlike the later silence,
is not accounted for by handling — it predates the packing by nearly 18 h. It follows a 5539 s
silence, and the frame that ended it (`f_cnt 2528`) was heard at RSSI −59 dBm, SNR 14.5 dB, so the
gap is not a coverage story. **The step size is the stored reserve, not 18 transmissions:** the
node restored at 2496, its first uplink wrote the ceiling at 2496 + `kCounterMargin` = 2528, it
transmitted through 2510, went quiet, and reappeared at **exactly 2528**. TTN holds exactly one
message at 2528. So what is unexplained is the reset itself and why the node took 5539 s — about
six 908 s cycles — to reappear. Carried forward as
[#82](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/82).

**2. Independent TTN record: 27.37 h, and the silence is operator handling.** Pulled from the
storage integration, which is a separate record from the watcher's polling and supersedes it for
per-uplink history:

```
107 stored uplinks
first  2026-08-14T13:41:45Z  f_cnt 2391
last   2026-08-15T17:04:14Z  f_cnt 2600
span   98549 s = 27.37 h
```

Discontinuities across the whole span, again the complete set:

```
gap=  96s  2026-08-14T15:57:56 f=2400 -> 15:59:32 f=2432  delta=32
gap= 197s  2026-08-14T15:59:32 f=2432 -> 16:02:49 f=2464  delta=32
gap=  89s  2026-08-14T17:48:44 f=2471 -> 17:50:13 f=2496  delta=25
gap=5539s  2026-08-14T21:22:13 f=2510 -> 22:54:32 f=2528  delta=18
```

The first three are bench-era reflash/reset turnarounds before the node was carried out. The fourth
is the §1 field event. **Session restored, never rejoined**, for the entire period: `dev_addr
260CE734` with `started_at 2026-07-31T14:33:20.636657834Z` and the counter at 2600.

The newest uplink TTN holds is **`f_cnt 2600` at 2026-08-15T17:04:14Z**; queried at 21:34:45Z,
`last_f_cnt_up` was still 2600 — 4 h 30 m of silence. Cadence immediately before was metronomic at
~908 s wake-to-wake (2591 … 2600, 14:47:58Z → 17:04:14Z), so the stop looks like an event rather
than a drift. **Cause: operator handling.** The node was packed into a bag and moved at
approximately 17:04:14Z, ending its RF path. Operator-confirmed. It was never at a field site
during this period; it was in a vehicle. **No firmware defect is implicated**, and the brownout,
watchdog-loop, backoff and counter-ceiling hypotheses are all withdrawn.

Two network-side facts are consistent with handling and inconsistent with the node protecting
itself or failing:

- **The pack was healthy and charging when it went quiet.** `f_cnt 2600` decodes to
  `batt_voltage 11.77 V`, `batt_capacity 78 %`, `batt_current +0.02 A`, against a
  `kTxInhibitCentivolts` of 9.60 V — **2.17 V above the inhibit and rising.** The four preceding
  frames read 11.75, 11.75, 11.76, 11.76, 11.77 V: flat, not sagging. `power::Brownout` cannot have
  engaged.
- **The next flash write was not due.** `session.cpp:278` stores `uplink_counter + kCounterMargin`
  (32) and only writes when the live counter reaches that ceiling, so writes landed at 2528, 2560
  and 2592 and the next was due at **2624**. The node stopped at 2600 — **24 frames short of any
  flash write** — so the permanent-mute paths in
  [#74](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/74) and
  [#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68) were
  unreachable.

The link was strong right to the stop: `f_cnt 2600` heard by `3356-gateway-002` at **RSSI −76 dBm,
SNR 13.75 dB, SF7BW125**, matching the preceding frames (−75 to −77 dBm, 9.75–14 dB). No
degradation before the silence.

**3. What was lost — a monitoring failure recorded as an outcome.** `$HOME/soak-runs` was deleted
by the operator during a tidy-up, deliberately and reasonably: project data had no business living
in a home folder. It was found in `~/.Trash` and copied out intact, then lost anyway — the Trash
was emptied at ~14:35Z and an agent removed the recovered copies at 14:36Z before confirming the
copy into the repo, which had failed because its source was already gone. `tmutil` holds only
`com.apple.os.update-*` snapshots and `listbackups` returns `Failed to mount destination`. **The
raw `events.log` files are permanently gone.**

| Artifact | State |
|---|---|
| `rc-v0.4.3-1c2df3c` summary, header, closing line, all 6 non-routine lines | **Preserved verbatim above** |
| Its ~360 routine `SOAK UPLINK` / `SOAK HEARTBEAT` lines | **Lost**, superseded by the §2 TTN series, which is independent and finer-grained |
| `rc-v0.4.2` (`572bcfa`) 19.03 h run — 308-line `events.log` | **Lost in full.** Its measured result survives in the 2026-08-14 entry below; the transcript does not |
| TTN-side history | **Never at risk** — held by TTN, re-pullable |

The lesson is in the code now, not in a caveat: `scripts/soak_ttn.sh` writes to `<repo>/soak-runs/`
as of this commit. A prior worker documented the `$HOME` path as a gotcha instead of fixing it.

**Verdict.** First >24 h of continuous runtime this project has had, on banner-verified `1c2df3c`,
**ended by being picked up, not by failing.** No recency FAIL, because F6 and F1 are not assessable
across an interval when the node was in a vehicle. **The ≥24 h bench half of H8 is NOT met and this
run does not move it** — the runtime was in the field with nothing attached, so of the ten bench
criteria in `[SOAK.md](SOAK.md)` only B1 and part of B5 can be evaluated at all; B2, B3, B4, B6, B7
and B9 all need the console and there was no console. **The 7 d field shadow has not started.**

CITE(spec): `[FIRMWARE_SPEC.md](FIRMWARE_SPEC.md)` §7 H8 — ≥24 h bench **and** ≥7 d field shadow;
this entry is measured against that bar and does not close it.
CITE(policy): CIT-TTN-FUP, `[CITATIONS.md](CITATIONS.md)` — the 900 s cadence observed here is what
keeps the node inside the 30 s/24 h airtime budget.
CITE(prior-art): `src/session.h:41` `kCounterMargin = 32` — the reserve that makes a counter step of
+18 or +26 one uplink after a reset rather than 18 or 26 transmissions.
CITE(bench): `docs/EVIDENCE.md` 2026-08-15, this entry — `dev_addr 260CE734`, `last_f_cnt_up`
2464 → 2600 observed at TTN, and `f_cnt 2600` at 2026-08-15T17:04:14Z as the newest uplink.

### 2026-08-14 — `1c2df3c` read back from the board's own banner, and six unattended 900 s cycles

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`. **Commit, asserted from the device:**
`1c2df3c`. **Environment:** `env:soak` (byte-identical to `env:rak4631`,
[ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)). Raw capture `/tmp/banner.log`, 81
lines. **Verdict: pass — image identity, cadence and both sensors confirmed.**

The operator pressed RESET **once** at 17:50:12Z while a capture was held open, specifically to
make the board print its banner:

```
firmware : 0.4.3
commit   : 1c2df3c
built    : Aug 14 2026 09:00:34
features : rk900=1 battery=1 radio=1 sleep=1 wdt=1
interval : bench=0, bounds 900-86400 s, default 3600 s
region   : US915 sub-band 2
   config  : interval 900 s, boot #3
   session : restored 0x260CE734, counter 2496
```

`commit : 1c2df3c` **matches the SHA that was flashed**, with no `-dirty` and not `unknown`, so the
SHA is **asserted**. Third distinct commit ever to name itself off the board, after `d568574` and
`65f8615`.

**Six consecutive unattended cycles on the 900 s cadence.** Cycles 3–8 ran untouched from
16:33:04Z to 17:48:51Z, about **1 h 15 m of continuous correct cycling**. Wake-to-wake spacing from
the `[cycle N]` stamps: 908, 908, 907, 908, 908 s against a 900 s target — the ~8 s excess is the
cycle's own awake time, which `sleep : 900 s` confirms is scheduled *after* the work.

```
2026-08-14 09:33:04 [cycle 3]
   RK900   : raw 0x0000-0x0004 = 0000 0000 00FD 0254 2727
   RK900   : wind 0.00 m/s @ 0 deg, 25.3 C, 59.6 %RH, 1002.3 hPa
   battery : pack answered at 0x01 — skipping provisioning
   battery : 11.76 V  -0.01 A  78%  23.0 C
   radio   : sent 35 bytes on port 2
   sleep   : 900 s
```

**All eight cycles in the capture closed `sleep : 900 s`** — the field sleep path, never `wait : N s
(sleep disabled)` — and every one sent 35 bytes on port 2. The sensors track a real afternoon
rather than repeating a cached value: RK900 25.3 → 25.4 → 25.3 → 24.3 → 23.1 → 23.5 °C, 59.6 →
64.9 %RH, pressure steady at 1002.3–1002.4 hPa, wind calm. Pack 11.76 → 11.75 V, −0.01 A, 78 %,
23.0 °C, latched at `0x01` every cycle.

**The RESET is recorded as an interruption, not as uptime.** The 24 h soak on this image was
already running when the button was pressed, so **that run is not 24 h of uninterrupted runtime and
must never be read as such.** The soak caught the same event independently:

```
2026-08-14T17:50:21Z SOAK UPLINK f_cnt=2496 delta=26 gap=971s total=7
2026-08-14T17:50:21Z SOAK NOTE  counter-step +26 in 971s within the 32-frame reset reserve --
2026-08-14T17:50:21Z     one uplink after a reset, not 26 transmissions (session.cpp:278); resets=1
```

Two new results fall out. **Serial and TTN agree on the counter across a reset** — the network-side
`f_cnt=2496` is the same number the banner printed as `counter 2496` nine seconds earlier, from two
independent observers. And **the frame-counter-step fix `7b03d3a` is now observed on hardware**: it
classified the +26 as one uplink after a reset and held `anomalies=0` rather than reporting 26
phantom transmissions. That fix was previously "believed correct, unobserved."

**Post-reset cycle 1 spent no BOOT on a healthy pack**, the behaviour `ec9725a` (the #75 fix) was
written to produce — `pack answered at 0x01`, then `sampling confirmed` on the very **first** cycle.
Note there is **no null cycle after this boot**, because the pack was already sampling from the
preceding run. The "expect ~2 null cycles after boot" note describes a pack that has just been
powered, not one that has merely been reset.

**The honest limit of it.** A grep of the whole 81-line capture for `brownout`, `provId`,
`BOOT this`, `no confirmed latch`, `Unsampled`, `rejoin`, `keepalive` and `silent at` returns
**nothing**. So the **#75 defect condition never arose** (not one transient probe miss in eight
cycles — the run shows the healthy path and the BOOT correctly unspent, and does not exercise the
consecutive-miss counter the fix adds); the **#62 re-latch path was never entered** (no `provId
0xFF` anywhere, and the pack kept its `0x01` latch straight through the RESET); the **brownout path
was never entered** (11.75–11.76 V all afternoon); and the **rejoin and keepalive paths were never
entered**.

**Process note.** The capture opens with the harness refusing rather than producing garbage:
`=== CAPTURE REFUSED /dev/cu.usbmodem31201 held by ...Python(31754) ===`. Port contention has cost
this project a great deal of time, usually by a second reader silently producing a truncated log
that then gets believed. Naming the holding pid and declining is correct behaviour.

### 2026-08-14 — `1c2df3c` flashed as `env:soak`; both sensors and `sleep : 900 s` observed, banner SHA not yet read back

**Host:** Heliotrope Ridge. **Commit built and flashed:** `1c2df3c`, `env:soak`. **Verdict: flash
confirmed, image identity NOT confirmed from the device** (settled by the entry above, later the
same day). **This is the image that goes to the woods** — the operator chose to ship the #75
BOOT-allowance fix (`ec9725a`) rather than the longer-soaked `572bcfa`, accepting fewer soak hours
on the shipping image.

`=== FLASH OK ===` at 15:59:31Z, `usb: 239A:8029 (application running)`, re-checked independently
~10 min later still `idProduct = 32809`.

**A capture must span >900 s to catch a cycle.** Three attempts returned 0 bytes and the diagnosis
is worth keeping: `pio device monitor` cannot be used non-interactively (it dies in `miniterm` with
`termios.error: (25, 'Inappropriate ioctl for device')` when stdout is redirected — use a raw
`cat`); two raw `cat` captures of 100 s and 200 s both fell inside the 900 s sleep; and a
backgrounded capture launched over SSH with `nohup ... &` was **dead** when checked, having not
survived the session closing — run it in the foreground or under `screen`.

`scripts/capture.py` reattaches across the sleep-time USB drop, and run for 1500 s it caught a
full cycle:

```
2026-08-14 09:17:56 [cycle 2]
   RK900   : wind 0.00 m/s @ 0 deg, 25.1 C, 59.7 %RH, 1002.2 hPa
   battery : pack answered at 0x01 — skipping provisioning
   battery : 11.76 V  -0.01 A  79%  23.0 C
   radio   : sent 35 bytes on port 2
   sleep   : 900 s
```

Both sensors in one cycle, and the cycle closes on the field sleep path. It also
**cross-confirms the network record**: the serial cycle at 16:17:56Z is the same event the soak
logged from TTN as `f_cnt=2465` at 16:19:24Z — two independent observers, one cycle.

**It still does not identify the image.** `[cycle 2]` is a *wake*, not a boot, and the banner prints
only at boot, so everything above would look identical on a resident older build.

**A second soak was started by mistake and stopped.** Two workers each launched `soak_ttn.sh`
against the same device, at 16:09:16Z (pid 28695) and 16:23:32Z (pid 31838). Two pollers reading one
device is not additive evidence — they double the TTN query rate and each sees the other's uplinks
— so the later run was killed and the earlier, longer run survived. The abandoned directory is kept
as `ABANDONED_duplicate_20260814T162332Z` rather than deleted. Check for a running soak before
starting work; a `pgrep` taken minutes earlier is stale.

**Two process defects found the expensive way**, both fixed in this range. A flash window was lost
to a poller whose "port appeared" line was misread as empty. A second was lost to the new
`flash.sh` soak guard **firing with no soak running** — it grepped `remote.sh`'s own echoed command
line, which contains the pattern string. Fixed in `1c2df3c` by anchoring on a leading pid. A false
refusal on a deadline is worse than no guard, because the override becomes reflexive.

### 2026-08-14 — 19.03 h of clean soak on `572bcfa`, stopped deliberately at 76 uplinks and 0 anomalies

**Host:** Heliotrope Ridge. **Tree soaked:** `572bcfa` (`v0.4.2`), banner commit `dcd6807`
(`572bcfa` is `dcd6807` plus documentation only). **Verdict: partial pass, superseded on purpose.**
**These are the first soak hours this project ever accumulated.**

Read from `events.log` (33745 bytes) at the moment of the stop, not inferred from the process
existing:

```
2026-08-13T20:27:35Z === SOAK TTN START === label=rc-v0.4.2 duration=86400s device=puma-concolor-001
2026-08-13T20:27:35Z     image      : firmware=0.4.2 banner_commit=dcd6807
2026-08-13T20:27:36Z     baseline   : last_f_cnt_up=2272
2026-08-14T15:29:28Z SOAK UPLINK f_cnt=2398 delta=1 gap=969s total=76
2026-08-14T15:29:28Z === SOAK HEARTBEAT 228 === elapsed=68511s of 86400s uplinks=76 f_cnt=2398 anomalies=0 query_failures=1
```

**Final: elapsed 68511 s (19.03 h) of 86400 s, 76 uplinks, `f_cnt` 2398 from a baseline of 2272, 0
anomalies, 1 query failure.** Zero anomalies across 19 h means no gap ever exceeded the 2700 s
silence threshold; the query failure was a TTN API read, not a missed uplink.

Stopped by `kill 76549` at ~15:30Z with its directory intact, because the operator was taking the
node to the field and chose to ship the #75 fix — an explicit trade of soak hours for a
battery-path fix, made with the alternative on the table.

**Does not establish:** H8, which needs ≥24 h. It says nothing about the battery path in the
shipping build, since the #75 fix is not in `572bcfa`. It reads TTN ingest, so it proves uplinks
*arrived* and asserts nothing about which SHA was running beyond the `dcd6807` banner captured at
the earlier flash.

**Unrelated but worth recording:** the build host dropped off the network entirely for ~50 min
(≈14:40Z–15:29Z, SSH and ICMP both; `uptime` afterwards showed 47 days, so it never rebooted). The
soak was unaffected — it kept logging and `query_failures` stayed at 1 — so the outage was between
the workstation and the host, not the host and TTN.

### 2026-08-14 — the meter caught a transmit-shaped peak: 0.14 A, stable across many cycles; minimum still reads `0`

**Commit:** `572bcfa` (`v0.4.2`, `env:soak`), 900 s cycle. **The attribution is clean rather than
assumed:** the board carried `572bcfa` until it was reflashed to `1c2df3c` that afternoon and the
soak on `1c2df3c` started at 16:09:16Z, while the operator's readings are timestamped ~07:50Z and
~15:05Z — both hours before the flash. **Nothing here is evidence about `1c2df3c`.** The power path
is unchanged between the two trees, but "unchanged code" is an inference and this file records
measurements.

**Host: NOT Heliotrope Ridge.** This is the operator's inline USB current meter, wired between the
USB host power source and the RAK4631. The build host built and flashed the image and ran the
TTN-side soak; it did not take this measurement and cannot. No serial port was attached.

Operator-reported, verbatim, across a window spanning many 900 s cycles (~7 h between readings):

```
max is now .14A
sorry .14a
still .14 amp max on the meter
```

Peak **0.14 A = 140 mA**, minimum still **`0`**, display resolution 0.01 A. The third line came
~7 h later, after many further cycles, and reports the peak **unchanged**.

**Establishes** two things. A peak consistent with a LoRa transmit burst has now been caught: the
datasheet gives `Tx mode LoRa @17 dBm` **92 mA** and `@20 dBm` **125 mA**, and a *whole-board*
140 mA peak sits sensibly above the radio-only figure once the nRF52840 and the RAK5802 are added.
That is the first bench observation on this project *shaped like* a transmit event, and it removes
the 2026-08-13 worry that the meter might never catch the burst. And the peak is **stable across
many cycles** — two readings 7 h apart, both 0.14 A, so there is no runaway and no ratcheting peak.

**Does NOT establish** either power figure. **No sleep current**: the `0` is the meter's resolution
floor, arithmetically incapable of distinguishing a healthy 2 µA sleep from a 9 mA defect.
**Not a measured transmit current either**: 0.14 A is one digit of resolution on a slow-sampling
meter catching the tail of a ~50 ms burst by luck. The honest reading is "consistent with the
datasheet", not "TX draws 140 mA" — do not enter it in a power budget or derive an airtime energy
number from it. **Verdict: INCONCLUSIVE for both power gates, and genuinely informative anyway.**

Closing the power gate needs **µA-resolution** instrumentation — a Nordic PPK2 (the method behind
[CIT-RAK-SLEEP]), a current-sense shunt on a scope, or coulomb-counting over a long window. A meter
whose LSB is 10 mA cannot answer it no matter how many cycles it watches
([#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8)).

CITE(datasheet): [CIT-RAK4631-RAW] RAK4631 WisBlock Core datasheet, "Power Consumption" — `Tx mode
LoRa @17 dBm` 92 mA, `@20 dBm` 125 mA, sleep 2.0 µA.

### 2026-08-13 — inline USB current meter: peak ~40 mA, minimum reads `0` at a 10 mA resolution floor

**Commit:** `572bcfa` (`v0.4.2`, `env:soak`), 900 s cycle. **Host: NOT Heliotrope Ridge** — the
operator's inline USB meter, as above. The 24 h TTN-side soak was running throughout and is
unaffected.

```
max amp = .04
min is 0
yeah it goes to 100ths that is all
```

Peak **0.04 A = 40 mA**, minimum `0`, resolution 0.01 A. **Establishes** that peak draw is ~40 mA,
so no subsystem is stuck drawing hundreds of milliamps, and that sleep/idle current is **below
10 mA** — the only thing the `0` supports.

**Does not establish a sleep-current figure**, for the same resolution reason as the entry above.
**And 40 mA is not a transmit figure** — it is *lower* than the datasheet's 92/125 mA, so the
likely explanation is that the meter's slow display sampling missed the ~50 ms burst, not that the
radio drew less than its datasheet. **Verdict: INCONCLUSIVE.** The operator left the meter inline
to watch for a higher peak; the 2026-08-14 entry above is that watch returning a result, and it
**removes nothing** from this one — the 40 mA reading was not wrong, it was a slow meter missing
the burst.

### 2026-08-13 — downlink command matrix: all eight cases PASS on hardware (#54)

**Commit on the board: inferred `f15a983`**, not asserted — the banner on this image predates
`033b584` and reports only `firmware : 0.4.1` / `built : Aug 12 2026 23:32:15`. `f15a983`
(2026-08-12 21:16:45 -0700) is the newest commit older than that stamp, which is *consistent with*
the banner and not the same as confirmed.

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`. Driver `scripts/downlink_matrix.sh` at
`a7381e7`, run unattended. **Image:** `stage3` bench image, `features : rk900=1 battery=1 radio=1
sleep=0 wdt=1` — **the sleep path was not exercised.** The downlink handling code is shared with
the field image; the power path is not. Eight cases pushed through TTN and matched against the
console, each waiting a full cycle for the Class A RX window, so the run took 2 h 31 m
(07:50:38 → 10:21:30).

**Every PASS rests on a console line emitted from inside `Radio::take_downlink()`
(`src/radio.cpp:441-498`) — so that function is now observed on hardware, on all five of its
branches**, which is the [#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)
acceptance criterion that had never been seen before.

```
[cycle 18]  case b — valid 0x01 set-interval to 900 s
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

**Verdict: PASS, 8 of 8**, re-verified line by line against the capture rather than trusting the
harness grep.

- **b** is the strongest single result: the applied interval is visible twice — `config : interval
  now 900 s` in cycle 18 and the `wait` line dropping 1800 s → 900 s in cycle 19.
- **c** discriminates exactly what #64 was about: a known opcode with a bad length reports *wrong
  length*, not *unknown opcode*.
- **f** shows the port filter running before any opcode parsing.
- **g** shows two queued commands drained one per cycle, not merged or dropped.
- Cycle markers 18–26 are strictly increasing, exactly one boot banner exists in the whole capture
  and it precedes cycle 1, and the session was restored (`counter 2080`) rather than rejoined. **No
  reset occurred during the matrix.**

**Not proof of [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62)** —
the pack reported `pack answered at 0x01` in every matrix cycle, so the re-latch path was never
entered. **Not a soak**, and not field-image power behaviour. Pack drift across the whole
night-and-morning capture, raw and uninterpreted: 12.02 V / 87 % (cycle 1) → 11.94 V / 84 % (cycle
26) on USB power with no charge source, reported current `-0.01 A` throughout — the 10 mA LSB, so a
resolution floor and **not** a current measurement.

### 2026-08-13 — sixteen unattended `stage3` cycles, both sensors live, no reset

**Commit: inferred `f15a983`** — same banner, same reasoning as the entry above; recorded as
inferred rather than asserted. **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31201`, single attached
reader. `FEATURE_SLEEP=0` with an 1800 s awake wait between cycles.

One boot banner in the whole capture (no reset), cycle markers 1 and 3–17 present and strictly
increasing, 14 `sent 35 bytes on port 2` lines, session restored rather than rejoined
(`counter 2080`). Cycle 17:

```
[cycle 17]
   RK900   : raw 0x0000-0x0004 = 0000 0000 00F7 024F 2716
   RK900   : wind 0.00 m/s @ 0 deg, 24.7 C, 59.1 %RH, 1000.6 hPa
   battery : pack answered at 0x01 — skipping provisioning
   battery : 11.96 V  -0.01 A  85%  22.0 C
   battery : raw v=1196 i=-1 soc=85 t=220
   radio   : sent 35 bytes on port 2
   wait    : 1800 s (sleep disabled)
```

**Verdict: PASS for what it covers** — the cycle repeats without intervention and both sensors
answer every cycle. Pack voltage drifts 12.02 → 11.96 V and SoC 87 → 85 % over the run at a
reported −0.01 A, which is the 10 mA LSB and therefore still not a current measurement.

**Not a soak** (`FEATURE_SLEEP=0`, so the sleep path is never entered) and **not proof of #62** (the
pack was already latched at `0x01` every cycle). Two cycle markers are missing from the capture;
the reader attached after boot, so that is a gap in the capture, not an observed failure.

### 2026-08-13 — `65f8615` on hardware: battery ladder survives 20 cycles, field image reaches sleep, and one transient probe miss spends the BOOT

**Commit:** `65f8615`, **read back from the boot banner** — not inferred. **Host:** Heliotrope
Ridge, `/dev/cu.usbmodem31201`. Two images: `env:battdiag` (20 consecutive ~10 s cycles) and
`env:soak` (the field image, byte-identical to `env:rak4631`).

**The pack was live in every one of 20 `battdiag` cycles.** Each printed `11.92 V  -0.01 A  84%
24.0 C`, raw `v=1192 i=-1 soc=84 t=240`, with the counter advancing monotonically `[cycle 1]` …
`[cycle 20]`, **no second boot banner**, no reset, no crash loop. Slightly below the previous
night's 11.94 V, consistent with slow drift on USB power with no charge source.

**The expected post-boot `Unsampled` cycles did not occur, so `e070708` was not exercised.** Cycle
1 already reported `sampling confirmed` and a live reading — zero null cycles, because the pack had
been polled continuously by an immediately preceding flash and was already sampled when the MCU
restarted. **The specific defect `e070708` fixes — an ordinary `Unsampled` reply from `0x01` scored
as no answer — did not arise, and the fix stays compile-verified only.** No `Unsampled` line appears
anywhere in the capture.

**A BOOT did fire, on cycle 10, and it was a genuine one-cycle probe miss, not the `e070708`
path:**

```
[cycle 9]   battery : pack answered at 0x01 — skipping provisioning
            battery : sendat ... 01 09 03 10 02 ...           <- seq 09
[cycle 10]  battery : pack silent at its id — one BOOT this power cycle
            battery : no confirmed latch — proceeding unprovisioned
            battery : sendat ... 01 0C 03 10 02 ...           <- seq 0C
            battery : 11.92 V  -0.01 A  84%  24.0 C
[cycle 11]  battery : pack answered at 0x01 — skipping provisioning
            battery : sendat ... 01 0D 03 10 02 ...           <- seq 0D
```

Read the sequence byte: cycle 9 used `09`, cycle 10's surviving frame used `0C`, cycle 11 used `0D`.
**Two sequence numbers (`0A`, `0B`) were consumed by probe attempts that drew no matched reply**, so
phase 0 genuinely saw silence — this is not an `Unsampled` reply being misread. The pack then
answered the push listen in the **same** cycle with a live reading. So a single transient miss,
roughly 1 cycle in 20, is enough to spend the one BOOT the power cycle is allowed and send the
reboot verb to a demonstrably healthy pack. Filed as
[#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75).

**The pack never appeared unlatched** — `provId 0xFF` does not occur anywhere in the capture, so
the #62 re-latch path was **not exercised** and remains unproven.

**`env:soak` at `65f8615`, one full cycle:** banner asserted the commit again,
`features : ... sleep=1`, `session : restored 0x260CE734, counter 2208`, both sensors read in the
same cycle (RK900 25.9 °C / 58.7 %RH / 999.2 hPa; pack 11.92 V, −0.01 A, 84 %, 24.0 °C),
`radio : sent 35 bytes on port 2`, and the cycle closed **`sleep : 900 s`** — not `wait : N s (sleep
disabled)`. The persisted 900 s interval from the downlink matrix survived two reflashes.

**The node woke itself on schedule, and the USB detach is conditional on a reader.** Cycle 2
arrived ~900 s later with **no boot banner in between**, so it woke from sleep rather than resetting
through it; both sensors read again and both values moved slightly (999.2 → 999.1 hPa, 11.92 →
11.91 V). A host-side poll every 10 s recorded `PORT_PRESENT` continuously from boot through past
16 minutes, far beyond the 180 s grace — consistent with `b1e59d6`: the field image detaches **when
nobody is watching**, and a reader holding the port open suppresses it. So this is **not** a
contradiction of [#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60),
and item 1 of [#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)
— host re-enumeration after a detach-sleep — is **still untested**, because no detach occurred to
re-enumerate from.

**`da655e9` (RK900 null-pressure honesty) is untested** — the only field-image cycle captured read a
real pressure (999.2 hPa from raw `2708`), so the refused-pressure branch was never entered.

**Verdict: PASS on survival and on the sleep path. INCONCLUSIVE on `e070708` and `da655e9`, neither
of which had its defect condition arise. One new defect found and filed (#75).**

### 2026-08-13 — field image (`env:soak`) flashed at `d568574`; the sleep path is reached

**Commit:** `d568574`, **asserted from the board itself** — the first hardware confirmation of the
`033b584` banner change and the first evidence entry that did not have to infer a SHA. **Host:**
Heliotrope Ridge.

The transfer was real, not a bare `[SUCCESS]` (#59): eleven rows of `#` progress marks,
`Activating new firmware`, `Device programmed.`, zero occurrences of `Target is not in DFU mode`.
Application mode confirmed by `ioreg`: `idProduct = 32809`, zero `nRF UF2` matches, zero
`bInterfaceClass = 8` interfaces.

```
=== rak-sensor-node ===
firmware : 0.4.1
commit   : d568574
built    : Aug 13 2026 10:28:41
features : rk900=1 battery=1 radio=1 sleep=1 wdt=1
deveui   : 42BB96EF76E200F1
region   : US915 sub-band 2
   config  : interval 900 s, boot #3
   session : restored 0x260CE734, counter 2112

[cycle 1]
   RK900   : wind 0.00 m/s @ 0 deg, 25.5 C, 58.7 %RH, 1000.2 hPa
   battery : sampling confirmed — pack is reporting live values
   battery : 11.94 V  -0.01 A  84%  24.0 C
   battery : raw v=1194 i=-1 soc=84 t=240
   session : saved 0x260CE734, resume at 2144
   radio   : sent 35 bytes on port 2
   sleep   : 900 s
```

**The sleep/wake round trip completes.** The board woke ~900 s later and ran cycle 2 with no boot
banner in between (25.6 °C, 58.7 %RH, 1000.1 hPa; pack unchanged at 11.94 V), so it woke from sleep
rather than resetting through it.

**Verdict: PASS for what it covers.** `sleep=1` and the closing `sleep : 900 s` mean the sleep path
is entered on the field image, which is the state the node will be measured in.

**The 900 s interval survived the flash and the power cycle** — set by the downlink matrix's case b,
and back as `config : interval 900 s, boot #3` on a freshly flashed image. Incidental but real
evidence for H5.

**`detach()` fires and the node leaves the USB bus** — the first hardware observation of the #60 /
ADR-0008 detach path on the field image. The reader was released at 10:46:03; at **10:59:57**,
`/dev/cu.usbmodem*` matched nothing and `ioreg` counted zero `WisCore RAK4631` and zero `32809`.
That is **half** of #40 item 1: the detach is observed; the matching `attach()` and a successful
host re-enumeration at the next wake are not. **Absence from `ioreg` is by design and is not a dead
board.**

**Board left** running `env:soak` at `d568574`, asleep on a 900 s cycle, absent from USB — the
unattended field configuration, and the configuration a sleep-current measurement has to be taken
in. One RESET restores the console and a flashable window; a double-tap re-enters DFU.

### 2026-08-12 (night) — ten fixes landed. COMPILE-VERIFIED ONLY. No hardware ran any of them

**Host:** Heliotrope Ridge. **Range:** everything after `2e89c93` through the `v0.4.1` release
commit. **Board state during this work:** asleep, USB detached, unreachable — no serial port was
opened and no image was flashed.

**What is claimed: these commits compile.** `pio run -e rak4631`, `-e soak` and `-e battdiag` each
returned `SUCCESS` in a throwaway worktree. That is the whole claim.

**What is NOT claimed:** that anything here has been observed running. Ten behavioural fixes — the
no-evidence brownout hold that disabled its own exit (#61), keepalive frame-counter starvation, the
ungated boot-counter flash write, sub-band re-selection after the rejoin escape, downlink length
checking (#63, #64), a set-interval downlink applied during a brownout hold (#65), the backoff first
step raised to the fair-use floor, the pack no longer rebooted on every re-latch attempt (#62 root
cause), empty pack records no longer counted as silence, and an all-zero RK900 span refused instead
of encoded as weather, plus a bounded Modbus drain that feeds the watchdog — are **reasoned from
code, not measured.** Several touch the sleep and brownout paths, which are exactly the paths that
cannot be proven by compiling. The honest status of every one is *believed correct, unobserved*.

**Standing bench fact:** the field image detaches USB 180 s after boot. A board left running past
that grace has no serial port and cannot be flashed — **press RESET once** for a fresh 180 s window,
then flash.

### 2026-08-12 — #61 brownout/ladder deadlock: the gate fix runs the ladder; the pack re-latch is still unproven

**Commit:** working tree on `f6a897d` plus the #61 fix. **Host:** Heliotrope Ridge,
`/dev/cu.usbmodem31101`. **Verdict: pass on the gate, inconclusive on the pack re-latch** — stated
separately on purpose: the firmware lockout is gone, the pack-side recovery is not demonstrated.

A scratch build (never committed) forced `brownout.update(false, 0)` so every cycle counts as
unreadable and the no-evidence hold engages through the production path. Reverted before the commit.

```
power   : pack silent for 4 cycles — holding transmissions, no voltage evidence (keepalive in 24 cycles)
battery : brownout held with no voltage evidence — running the ladder anyway, it is the only thing that can clear the hold
battery : 4 silent cycles — retrying the full ladder
```

Before the fix that same state printed `brownout engaged — probe only, skipping the fallback
ladder`, which is the deadlock #61 describes. The bound held too: the next cycle printed `5 silent
cycles — probe only until cycle 7`, so the expensive phases ran once and then stood down.

**The pack was genuinely unlatched during that capture** — this was the precondition, not a
simulation of it:

```
battery : answered probe 0xFF (announced pid 0xFF) with pid 0x01
battery : answered 1 announcement(s) in 3036 ms — pack still reports pid 0xFF
battery : raw FF 7E 00 15 02 01 00 FF 0E 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 30
battery : no data (all-zero records (pack not sampled), 28 bytes)
```

**So what is proven is the gate, not the cure.** Whether running the ladder re-latches a pack
sitting at `0xFF` is **not** shown — across four full ladder attempts the pack kept announcing
`0xFF`. It recovered its latch across the reset that came with the next reflash. That is a
**pack-side** behaviour needing its own investigation.

**Clean build, no regression.** Rebuilt with the scratch forcing removed, 130 s capture, cycles 2
through 9 — **8 of 8 live at `12.07 V  -0.01 A  89%  25.0 C`**, and no brownout line anywhere,
which is correct because the pack is answering.

Also measured the same day and recorded because it was not written down elsewhere: `env:soak` and
`env:battdiag` at `f6a897d` both boot into **application mode** (`idProduct 32809`, no `nRF UF2` MSC
interface), and the pack read `12.07 V  -0.01 A  89%  25.0 C` for **5 of 5 cycles at `b436aa9`** and
again **5 of 5 at `f6a897d`**. Earlier the same day, a **15-cycle episode of all-zero records with
`source = 0xFF`** — the observation #61 was filed from.

### 2026-08-12 — H8 soak attempt FAILED to attach. Zero soak hours. Cause unestablished

> **This entry replaces one titled "24 h bench soak started (H8)."** That title was wrong and the
> reason is worth keeping: **it was committed at 09:41:23, ninety-two seconds before the harness
> gave up at 09:42:53.** It described a launch as though it were a run, because the outcome did not
> exist yet when it was written. Two later documents repeated the claim before it was caught.

**Commit:** `f626698`. **Host:** Heliotrope Ridge. **Attempted** 09:39:53 local, **gave up**
09:42:53 — **180 s, all of it spent waiting.** The log is 140 bytes and two lines, and no capture
process is running:

```
2026-08-12 09:39:53 === CAPTURE WAITING /dev/cu.usbmodem* ===
2026-08-12 09:42:53 === CAPTURE GAVE UP after 180s, device never appeared ===
```

**Verdict: fail. Zero soak hours. H8 has not started, let alone closed.**

**The tempting explanation is wrong, twice over, and checking it matters.** `FEATURE_CONSOLE=0`
looked like it would explain "device never appeared" perfectly. It cannot: that flag and `env:soak`
both landed in `094d5f5` at **11:26:50**, one hour forty-four minutes *after* this attempt ended, so
at `f626698` the image still compiled the console in. Independently, `FEATURE_CONSOLE=0` was itself
reverted in `636e421` ([ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)) because the
Adafruit core calls `SerialTinyUSB.begin()` and creates the `usbd` task before `setup()` runs — USB
enumeration never depended on the sketch calling `Serial.begin()` at all. The timeline rules it out;
the mechanism rules it out independently.

**So the cause is unestablished**, and this repo cannot establish it: nothing recorded what was
actually flashed and running at 09:39, and this project has been caught by a stale binary on the
board once already. Candidates, none checked: the board in DFU rather than running; a stale or
absent flash; the board physically off the bus; a harness defect. **Establish what is on the board
before theorising.**

**What actually exists, and it is real progress:** the soak *harness* — `scripts/soak.sh`,
`[SOAK.md](SOAK.md)`, and `env:soak` now byte-identical to `env:rak4631`, so a soak is evidence
about the shipped image. It has never produced a soak hour. **Prove the harness before trusting it
with 24 h:** `scripts/soak.sh selftest 90` runs it with no board attached. A harness that has only
ever been started, never validated, is how 180 s of waiting got recorded as a day of soaking. One
harness lesson did survive: `setsid` does not exist on macOS, so an earlier launch died on it
immediately; `nohup … & disown` is what works on this host.

**Independent of the soak — the interval is 900 s, not 1800 s.** From the network's own uplink
timestamps: `16:21:07Z → 16:36:14Z` is 15 min 7 s and `16:05:59Z → 16:21:07Z` is 15 min 8 s. This
comes from TTN, so the failed capture does not affect it. At 900 s the node sits at the fair-use
floor (`kFupFloorSeconds`), which is compliant. *(Superseded later the same day: the stored value
was 3600 s — see the interval reconciliation below.)*

### 2026-08-12 — sleep current is not measurable from pack telemetry: a 10 mA floor against a ~1 mA question

**Commit:** `4510763`. **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Measured:** whether
the RAK9154's own current telemetry can size the `delay()`-based sleep as a stopgap before a meter.

The pack reports current with a **0.01 A (10 mA) LSB** — raw `i=-1` for the reported `-0.01 A`. The
reading did not move across 20 consecutive `battdiag` cycles, nor in the field image, awake and
transmitting:

```
08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
08:04:47 battery : raw v=1212 i=-1 soc=91 t=230
```

A pack supplying a ~30 mA awake node should read about `-0.03 A`. It does not, because with USB
attached the board is USB-powered and the pack sees almost no load — and USB is the only way the
reading leaves the board.

**Verdict: cannot answer, and this is a complete answer, not a partial one.**
`[POWER_BUDGET.md](POWER_BUDGET.md)` turns on ~1 mA; the pack resolves 10 mA. Even the documented
defect cases (0.89–1.2 mA peripherals-enabled, ~6 mA radio-awake) fall at or under one LSB. **A
meter is the only instrument that can settle this.** No number is recorded and none should be quoted
from pack telemetry.

This is **not** a `bench` citation for sleep current. It is one for the *resolution floor*:
`CITE(bench): docs/EVIDENCE.md 2026-08-12 @ 4510763 — pack current LSB = 0.01 A`.

### 2026-08-12 — field image: both sensors in one cycle, and the uplink was accepted at TTN

**Commit:** `4510763`. **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Image:** `rak4631` —
the actual field image, radio in, **sleep in**. Not a diagnostic.

```
08:04:47 config  : interval 1800 s, boot #2
08:04:47 session : restored 0x260CE734, counter 1792
08:04:47 [cycle 1]
08:04:47 RK900   : raw 0x0000-0x0004 = 0000 0000 00F7 024B 273C
08:04:47 RK900   : wind 0.00 m/s @ 0 deg, 24.7 C, 58.7 %RH, 1004.4 hPa
08:04:47 battery : pack answered at 0x01 — skipping provisioning
08:04:47 battery : 12.12 V  -0.01 A  91%  23.0 C
08:04:47 radio   : sent 35 bytes on port 2
08:04:55 sleep   : 1800 s
```

| Claim | Status |
|---|---|
| Both sensors, one image, one cycle | **pass** — first time observed |
| Field image (`FEATURE_SLEEP=1`) runs a complete cycle; sleep reached | **pass** |
| OTAA join | **not observed** — the node restored a saved session, which is correct behaviour and evidence the earlier join persisted, but is not a join event |
| Uplink **accepted at TTN** | **pass**, confirmed from the network side at 09:20 |
| Downlink handled | **delivered to the device** at 16:21:07Z; console confirmation of the handler was not captured |

**A correction worth leaving in place, because the error is instructive.** An earlier row in this
entry recorded the uplink as *not* reaching TTN. A 110 s `ttn-lw-cli events subscribe` stream held
open across a board reset parsed 22 events, 8 of them `as.up.data.forward`, and **none from
`puma-concolor-001`** — while five other devices on the same application did arrive, which ruled out
the subscription, the credentials, the CLI invocation and the network path. The stream was working.
So the conclusion drawn was that our device was not heard.

**The device was being heard the whole time.** Querying the network's own record settles it:

```
last_seen_at : 2026-08-12T16:05:59.401582Z
 "dev_addr": "260CE734",  "last_f_cnt_up": 1857,  "started_at": "2026-07-31T14:33:20.636657834Z"

 t= 2026-08-12T15:04:47Z f_cnt= 1792 port= 2 gw= 3356-gateway-002 snr= 13.5
 t= 2026-08-12T15:33:12Z f_cnt= 1824 port= 2 gw= 3356-gateway-002 snr= 13.5
 t= 2026-08-12T15:35:51Z f_cnt= 1856 port= 2 gw= 3356-gateway-002 snr= 13.75
 t= 2026-08-12T16:05:59Z f_cnt= 1857 port= 2 gw= 3356-gateway-002 snr= 13.75
```

`f_cnt 1792` at 15:04:47Z is `08:04:47` local — the exact second of the console line `radio : sent
35 bytes on port 2` above. That frame was received, and so were the rest, all at SNR 13–14 dB. The
network-side `dev_addr 260CE734` is byte-identical to the console's `session : restored`, and the
session established 2026-07-31 is still live.

**Why the 110 s window saw nothing: it sat entirely inside the sleep.** The uplinks bracketing it
are 15:35:51Z and 16:05:59Z; the subscription ran 15:38–15:39:50Z. The counter is the tell —
`1824 → 1856` is the 32-frame reserve a restore burns on boot, `1856 → 1857` a plain increment, so
the board did not reset when that capture's upload reported `SUCCESS`. **The lesson: on a node with
an 1800 s duty cycle, a two-minute observation window that sees nothing has established nothing.**
The five other devices proved the subscription worked; they could not prove ours was silent.
Absence of evidence was recorded as evidence of absence.

**Downlink: queued, then delivered.** A status request was queued on `f_port 10` with
`frm_payload "Aw=="` — `0x03`, matching the firmware contract. With the frame queued the board was
reset and uplinked at 16:21:07Z (`f_cnt 1858`, port 2), and the queue drained across that single
uplink (`[{...}]` → `[]`). **First time anything has come *down* to this node on hardware**, and it
exercises the Class A ordering correctly: the uplink is what creates the opportunity.

**Still missing, narrowly:** the console did not capture the handler running.
`Radio::take_downlink()` and the `0x03` branch are unverified by observation even though the frame
reached the radio, because the capture helper gave up looking for the CDC device after ~40 s while
the board takes about two minutes to re-enumerate after a DFU flash. That is a tooling gap, not a
firmware finding. #54 stays open, narrowed from "never run on hardware" to "delivered but not
observed being handled."

**Verdict: pass on sensors, cycle and network acceptance. Status unchanged.**

### 2026-08-12 — the RAK9154 pack latches and reports. It was never broken; the 1800 s cycle hid cycle 3

**Commit:** `b436aa9`. **Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Image:** `battdiag` —
battery only, ~10 s cycle. **Verdict: pass. The provisioning handshake works.**

**Measured:** whether the all-zero record set that every prior session reported as a provisioning
failure was instead the documented boot behaviour. `stage3` runs an 1800 s cycle, so **every capture
window ever taken held exactly one cycle.**

**20 consecutive cycles in 210 s. 19 of 20 carried live values, one did not, and the one was cycle
2.** The pack was latched at pid `0x01` throughout, and the string `provId FF` does not appear
anywhere in the capture.

```
07:56:08 [cycle 2]
07:56:10 battery : pack answered at 0x01 — skipping provisioning
07:56:10 battery : raw FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 27
07:56:10 battery : no data (all-zero records (pack not sampled), 28 bytes)
07:56:20 [cycle 3]
07:56:20 battery : sampling confirmed — pack is reporting live values
07:56:20 battery : 12.12 V  -0.01 A  91%  23.0 C
07:56:20 battery : raw v=1212 i=-1 soc=91 t=230
```

Stable to the reported digit across all 19 live cycles at **12.12 V, −0.01 A, 91 %, 23.0 °C** —
consistent with the 2026-08-05 reference (12.23 V, +0.00 A, 98 %) for a pack that has since been
sitting. One cycle (8) logged `no announcement — proceeding unprovisioned` and still returned live
values, which is correct: the latched pid survives a missed announcement window.

**Correction to the record:** prior 2026-08-12 entries describing the pack as "refusing to latch"
are wrong. The `provId FF` announcements they captured are the pack's pre-latch announcements; no
capture window was ever long enough to show what followed. Our frame matched
`onewire_master_protocol.c` on all five checked properties because it *was* correct, and the
reference algorithm reproducing the pack's own `0x82` checksum was the tell. **The remaining symptom
was an observation artefact of the 1800 s cadence, not a defect.**

Temperature scale remained UNCONFIRMED at this point (settled 2026-08-05, re-read below). The
ADR-0002 current-sign conflict was closed 2026-08-13 by operator decision — positive = charging,
negative = discharging, matching the pack — **not by a measurement**; nothing in this entry or any
other observed a charge current.

### 2026-08-12 — transmit timing is within async tolerance: the 11.4 % overshoot is legal inter-character idle, not a stretched bit period

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Commit:** `eb7fff8`, `owscan`.

The provisioning capture's `tx 95 bytes in 110352 us = 1161 us/byte` against 1041.7 µs of ten bit
periods at 9600 raised the hypothesis that our transmit clock is 11.4 % slow and the pack is
discarding our reply as a framing error. **That hypothesis is disproven.**

`owscan` phase 0 measures `write()` cost at three bauds, which over-determines the model
`T = k × bit_period + F`. Cycles 2 and 3 were identical to the microsecond:

```
4800  baud  tx 64 x 0x55 : 149414 us total, 2334.59 us/byte (10 bits = 2083.33 us)
9600  baud  tx 64 x 0x55 :  75195 us total, 1174.92 us/byte (10 bits = 1041.66 us)
19200 baud  tx 64 x 0x55 :  38086 us total,  595.09 us/byte (10 bits =  520.83 us)
```

Solving across all three pairs with the library's integer-truncated bit delays (208, 104, 52 µs):

| Pair | ΔT (µs) | Δbit (µs) | k |
|---|---|---|---|
| 4800 − 19200 | 1739.50 | 156 | **11.151** |
| 4800 − 9600 | 1159.67 | 104 | **11.151** |
| 9600 − 19200 | 579.83 | 52 | **11.150** |

`k = 11.15` on all three pairs, and back-substituting gives `F = 15.3 µs` at every baud. The model
fits exactly, so neither term is assumed.

**A byte costs 11.15 bit periods, not 10.** The extra whole bit period is `beginTx()`'s own
`delayMicroseconds(_tx_delay)`, spent with the line idle-HIGH *before* the start bit. The residual
0.15 spread over 11 `delayMicroseconds()` calls is ~1.4 µs of call overhead each, so the real
on-wire bit period is ≈ **105.4 µs against an ideal 104.17 µs — an error of +1.18 %.** That is
inside the few-percent budget an asynchronous receiver has; accumulated error at the stop-bit sample
point is ~0.11 of a bit, nowhere near a boundary. The remaining excess is inter-character idle,
which async framing permits without limit because the receiver resynchronises on every start bit.

**Conclusion: the bytes we transmit are individually well-formed and the pack can receive them.
Transmit timing is not the reason the pack will not latch. Do not "fix" `_tx_delay`** — the
truncation to 104 µs makes bits marginally *short*, not long.

**Cross-read: the sibling is not authoritative for provisioning.**
`forest-weather-machines/rak-4-5-wire` @ `8378435` (repo HEAD `efc0e3c`) —
`firmware/nanoc6-rak9154-poll/src/main.cpp` is **0 bytes**, so the Modbus path (slave `0x6E`, 21
registers from `0x6000`) that `AGENTS.md` cited it for is not implemented there;
`firmware/nanoc6-onewire-poll/src/onewire_protocol.cpp` (236 lines) implements query, frame RX and
parse — **and no provisioning handler at all.** So the sibling is authoritative for the poll and
parse of an already-provisioned pack and has nothing to say about the announcement handshake, which
is the step that was failing. The only provisioning references remain the vendored upstream
`onewire_master_protocol.c` and Meshtastic.

### 2026-08-12 — `WB_IO2` rail exonerated; the "no announcement" failure was a stale binary

**Host:** Heliotrope Ridge, `/dev/cu.usbmodem31101`. **Commit:** `9c35e2f` (clean tree; the
previously-uncommitted `owscan.cpp` accumulator fix was a duplicate of `b967008`).

**Result 1 — the switched `3V3_S` rail does NOT gate the pack.** `owscan` phase 2b listens twice at
9600, once with `WB_IO2` HIGH and once LOW — the state `RK900::power_off()` leaves behind. Three
consecutive cycles: `HIGH 184 / LOW 184`, `HIGH 92 / LOW 184`, `HIGH 92 / LOW 184` bytes. With the
rail off the pack still emitted the full 92-byte announcement. **Hypothesis disproven — no wiring
change is required.**

**Result 2 — `stage3` at `9c35e2f`: the driver sees and answers the announcement.** The 0-byte /
`no announcement` behaviour recorded in the entry below came from an **unidentified image** at
`[cycle 203]`, not from current code.

```
battery : turnaround 2 ms (gap 1953 us), tx 95 bytes in 110352 us = 1161 us/byte
battery : answered probe 0xFF (announced pid 0xFF) with pid 0x01
battery : reply FF 7E 00 55 02 01 FF 00 00 01 50 03 44 ... 52 41 4B 32 35 36 30 2D 69 6F ... 7C
battery : probe 0xFF announces 6 sensor(s)
battery :   sid 0x15 ipso 186 rule 0x0008 periodic   ... through sid 0x1A ipso 243
battery : answered 2 announcement(s) in 5079 ms — pack still reports pid 0xFF
battery : PROVISION announcement where a SENDAT reply was expected
battery : no data (all-zero records (pack not sampled), 28 bytes)
radio   : sent 20 bytes on port 2
```

**Announcement detection, parsing and the response mutation all work.** The reply carries `01` in
the `provId` slot (frame index 34) where the announcement carried `FF`, the addresses are swapped
(`dest FF`, `source 00`), the flag is `01` RSP, and the checksum is recomputed (`7C`). All six
descriptors decode at rule `0x0008` RULE_PERIODIC. **The `no announcement` symptom is not present in
current code** — it belonged to the stale binary, so any theory built on it, including the `3V3_S`
theory above, was explaining a stale artefact. The radio path works.

**The pack still refuses to latch** — two answered announcements in 5079 ms and the next announcement
still carries `provId 0xFF`; SENDAT then returns the 28-byte all-zero template. Measured transmit
cost of the 92-byte echo: **110352 µs for 95 bytes = 1161 µs/byte** against a 1042 µs/byte
theoretical floor, so the whole provisioning response occupies the wire for ~110 ms.

**Reconciliation with 2026-08-05, which recorded a successful latch at `1a203d3`:** that latch is
historically accurate but describes a state the pack no longer holds — the announcement's own
`provId` field is the pack's belief about its id, and it read `FF` on every capture that day. The
driver handles this correctly by construction (`m_pid` starts at `0xFF` and falls back), so it is
not an addressing regression. *(Settled later the same day: the pack was latched all along and the
1800 s cadence was hiding it — see the `b436aa9` entry above.)*

### 2026-08-12 — the pack talks: `owscan` draws the announcement every cycle; the production image on the same bench gets 0 bytes

Reverses the 2026-08-11 entry below on the one-wire bus. **Host:** Heliotrope Ridge,
`/dev/cu.usbmodem31101`. **Build-host tree:** `8994d02` plus the uncommitted `owscan.cpp`
accumulator-reset fix, byte-identical to the diff `b967008` carries (`md5
8b02d1ef78ea5c232c01385e34b9933f` on both sides), so the image built here is the content of
`b967008`.

**Observation A — the production image already on the board, image SHA not established.** It
reported `[cycle 203]`, read the RK900 correctly, and drew **zero bytes across the whole cycle**:

```
07:21:19 battery : no announcement — proceeding unprovisioned
07:21:40 battery : no data (no reply, 0 bytes)
07:22:40 [cycle 203]
07:22:40 RK900   : wind 0.00 m/s @ 0 deg, 24.4 C, 58.8 %RH, 1004.0 hPa
```

**Observation B — `owscan`, flashed immediately afterwards.** Passive listen, transmitting nothing,
9600 baud, identical on all three cycles:

```
9600 baud  passive 3000 ms : 92 byte(s)  <- FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30
... FF 00 47 45 00 00 00 00 52 41 4B 32 35 36 30 2D 69 6F 00 ... 06 15 BA 08 00 16 B9 08 00
17 B8 08 00 18 67 08 00 19 F3 08 00 1A F3 08 00 82
```

Decoded: `hub_type 0x01` PROVISION, `payload_type 0x03` VER3, `dest 0x00` master, `source 0xFF`,
`provId` at frame index 34 reading **`FF`** — still unprovisioned. `snsr_num` = **6**, every
descriptor at rule **`0x0008` (RULE_PERIODIC)**: sid `0x15` ipso 186 DC voltage, `0x16`/185 DC
current, `0x17`/184 capacity, `0x18`/103 temperature, `0x19` and `0x1A` both 243 status word. **The
sensors are armed, not disabled.**

SENDAT probe-id sweep at 9600: dest `0x01`, `0x02`, `0x03` all **0 bytes**; dest `0xFF` **120
bytes**, the first frame being `flag 0x01` (RSP), `hub_type 0x03` SENDAT — a genuine answer — with
**every record value zero**, i.e. the `Unsampled` template, immediately followed in the same read by
the 92-byte announcement. BOOT drew 0 bytes at 4800/9600/19200/38400.

Phase 1 line census, cycles 2 and 3 — open-drain behaving as expected, activity only with the
pull-up engaged:

```
INPUT_PULLUP   idle HIGH : 334 falling edge(s), 121385 of 1732860 samples LOW
INPUT (float)  idle HIGH : 0 falling edge(s), 0 of 1734024 samples LOW
```

**Established:** the wire, the pin (`WB_IO1`), the baud (9600) and the pack are all good. The pack
is unprovisioned, listens only on `0xFF`, and answers with an all-zero template. Nothing here is a
framing or checksum fault. **Not established:** why the production image gets 0 bytes where `owscan`
gets 92. The leading hypothesis was that `owscan` never reads the RK900 and therefore never drops
`WB_IO2`, whereas the production cycle drops it LOW afterwards — *tested and disproven later the
same day, in the `9c35e2f` entry above.*

### 2026-08-11 — both known-good images reflashed; both stayed silent. Firmware is exonerated on both buses

The headline result of the day, and the one that redirects the search. **Host:** Heliotrope Ridge.
**Commits reflashed:** `998dc26` (`busscan`) and `8720dea` (`stage2`) — the exact images behind the
2026-08-03 RK900 pass and the 2026-08-05 battery pass. **Verdict: FAIL on both buses, and firmware
is exonerated for both.**

`998dc26`, `busscan`, three full cycles: `total with rail HIGH: 0 byte(s)`, `total with rail LOW: 0
byte(s)`, `9600/0x01 production frame: 0 byte(s)`. The 2026-08-03 pass on this same image read
`15 byte(s) <- 01 03 0A 00 00 00 00 00 FB 01 F8 27 56 DA A1`. **Not reproduced.**

`8720dea`, `stage2`, full 50.5 s awake window: `no announcement — proceeding unprovisioned`,
`no data (no reply, 0 bytes)`. The 2026-08-05 pass on this same image read 22 announcements in
45.4 s plus a 28-byte checksum-valid SENDAT reply. **Not reproduced.**

The binaries that produced the passes are byte-for-byte the binaries that are silent now, so **no
code change since can be the cause and no code change can be the fix.**

**Why this is conclusive for the one-wire side in particular:** the pack's 2026-08-05 announcements
were **unsolicited** — the pack talks first, which depends on nothing the node transmits. Framing,
addressing, turnaround timing, `kWakeCount`, provisioning state: none of it can suppress a message
we never asked for. Silence from an unsolicited talker means nothing is driving the wire.

**Operator-confirmed context.** The RK900 has 12 V. The pack is healthy and powers the board off
USB. The rig that passed on 2026-08-05 had pack pin 1 `P+` **deliberately unconnected with the buck
out of circuit**; 12 V entered the circuit only after that capture. **That is the largest known
delta between the passing rig and today's, and it is physical.** Remaining suspects on both buses
are wiring, connectors and the 12 V introduction — not firmware
([#49](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/49),
[#50](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/50)).

### 2026-08-11 — one-wire dead: no edge, no byte, in any mode — and the scan reporting otherwise was lying

**Host:** Heliotrope Ridge. **Commit:** `8994d02`, `owscan`; the after-fix capture additionally
carries the accumulator reset committed alongside this entry.

The edge census, which needs no protocol to be right:

```
INPUT_PULLUP  idle HIGH : 0 falling edge(s), 0 of 1735294 samples LOW
INPUT (float) idle HIGH : 0 falling edge(s), 0 of 1734024 samples LOW
```

0 bytes on passive listen at all five bauds, 0 from BOOT dest `0xFF` at all five, 0 from SENDAT dest
`0x01`/`0x02`/`0x03`/`0xFF` at 9600. **Verdict: FAIL.** Nothing pulled the line low across ~1.73 M
samples in either pin mode, so the pack is not driving the wire — below the level where framing or
addressing could matter (#50).

**The diagnostic lied first, so every earlier `owscan` verdict inherits the doubt.** Its four
verdict accumulators (`ow_edges_pulled`, `ow_edges_float`, `ow_bytes`, `ow_best_baud`) are file
scope and were never cleared between sweeps, while `main.cpp` calls the scan once per cycle — so two
bytes captured in some early cycle latched their verdict permanently. **Before the fix**, at
20:47:32, 20:48:00 and 20:48:28, the scan printed a screen of zeros in every phase and then
concluded `2 byte(s) total / bytes arrived. The pack talks, so the fault is framing or addressing,
not the wire`, pinning phase 4 to `19200 baud (first baud that answered)` when 19200 had returned
nothing. **After the fix**, at 20:52:40, same wire and same board: `0 byte(s) total / nothing ever
pulled this line low… the pack is not driving the wire at all`.

Recorded as evidence rather than a changelog line because it invalidates readings: **any `owscan`
verdict captured before 20:52:40 that day reports the union of every cycle since boot, not the cycle
printed above it.** The failure mode is the precise one the scan exists to prevent — it sent the
reader after a framing constant while the wire was dead. `bus_scan()` was never exposed to this; it
totals in locals.

### 2026-08-11 — RS-485 dead on bench: `busscan` 0 bytes powered and unpowered

**Commit on the board:** `e2c7088` (`busscan`, flashed same day). **Host:** Heliotrope Ridge.
**Measured:** Modbus FC `0x03` sweep at 4800/9600/19200/38400/115200, slaves `0x01`–`0x03`, with
`WB_IO2` HIGH then LOW, plus the 9600/0x01 five-register production frame. Two independent captures,
same verdict:

```
[bus scan] total with rail HIGH: 0 byte(s)
[bus scan] total with rail LOW: 0 byte(s)
[bus scan] verdict: 0 byte(s) powered vs 0 unpowered; 9600/0x01 production frame: 0 byte(s)
```

**Verdict: FAIL for a live RK900 path.** Baud, slave ID and IO2 control are ruled out — all
rates/slaves silent, HIGH ≡ LOW. Remaining: pack pin 1 `P+` → RK900 12 V, and A/B on the RAK5802
(#49).

### 2026-08-05 — board recovered via UF2; "off the USB bus" turns out to be the sleep detach, not a fault

**Host:** Heliotrope Ridge. **Commit on the board:** `7dfc26f` — **not** `bf5ceb2`, which is what
was intended; see the stale-binary note below.

**1. Serial DFU can fail while the bootloader is healthy; UF2 mass storage works.** With the board
in the UF2 bootloader (`239A:0029`), the port present, and `lsof` showing nothing holding it,
`pio run -t upload` failed twice with *"No data received on serial port."* The UF2 route succeeded
first try. The non-obvious step: **macOS left the bootloader's drive unmounted** — `diskutil list`
showed `/dev/disk6` as `RAK4631`, 33.7 MB, with no `/Volumes` entry until `diskutil mount disk6`.
That absence is what made the UF2 path look unavailable. Converted with the framework's own
`uf2conv.py -c -f 0xADA52840`, which reported `start address: 0x26000` — the application offset
above the SoftDevice. Procedure now in `[FIRST_FLASH.md](FIRST_FLASH.md)`.

**2. A sleeping node has no USB device at all, and that is the fix working.** After the flash the
board enumerated, uplinked, then vanished completely: no `239A:*`, no `/dev/cu.usbmodem*`, no
`RAK4631` disk. That looks identical to the dead-board state, and `flash.sh` said so in as many
words. **It was asleep** — `TinyUSBDevice.detach()` releases the D+ pull-up, so the host sees the
device removed. TTN settled it: `last_f_cnt_up` **1024**, session `updated_at
2026-08-05T15:15:56Z`, against a build-host clock reading `08:17:13 PDT` — the node had uplinked
**77 seconds** before the bus was declared empty. `dev_addr 260CE734`, `last_n_f_cnt_down` and
`last_a_f_cnt_down` both 26.

**Stale binary — what is actually on the board.** `pio run -e rak4631` reported `SUCCESS` in 1.0 s
and reused a `firmware.hex` built from `7dfc26f`, even though `git diff 7dfc26f bf5ceb2` shows
`src/sensors/battery.{cpp,h}` changed and both compile into that environment. The UF2 was converted
from that stale hex, so **the board is running `7dfc26f`, not `bf5ceb2`.** `7dfc26f` does contain
the CDC fix, so the pending verification is still meaningful, but **no result from this board may be
attributed to `bf5ceb2`.**

**Still not proven:** CDC across sleep. The detach half is demonstrated; the half that matters —
`attach()` restoring a working console *after* a sleep — needs the wake, and neither branch of the
`(bool)Serial` guard has been exercised on hardware. The 900 s interval was not sent, persistence
across reset was not tested, and `v0.4.0` was correctly not tagged
([#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)).

### 2026-08-05 — USB CDC death root-caused in source; verification blocked, board off the bus

**Host:** Heliotrope Ridge. **Commit built:** `7dfc26f`. **Commit previously on the board:**
`406df01`. Not a measurement entry — a state entry, so the next session does not misread the board.

The dead-console fault behind #40 was root-caused by **reading the core source**, and it turned out
to be **two independent defects** in the pre-sleep path, either sufficient alone:

1. `NRF_USBD->ENABLE = 0` with nothing to restore it. In `dcd_nrf5x.c`,
   `tusb_hal_nrf_power_event()` is the only writer of `ENABLE = 1`, and only on `USB_EVT_DETECTED` —
   a VBUS transition. It also runs the errata 171/187/166 workarounds, waits on `EVENTCAUSE.READY`
   and starts HFCLK. `Serial.begin()` reaches none of that.
2. `Serial.end()` → `clearConfiguration()`, which discards the configuration descriptor.
   `Serial.begin()` rebuilds it, but with no detach in between the host never re-enumerates and
   keeps addressing endpoints from a descriptor the device threw away.

Both were guarded on `(bool)Serial`, which reports whether the host has the port *open* — so the
destructive path ran only when no monitor was attached. **That is the intermittency**, and it fits
the primary observation exactly: the application ran (TTN `f_cnt` 898 → 960) while serial delivered
zero bytes across repeated 60 s reads. Nothing was hung; the console had been dismantled during the
first sleep. Fixed in `7dfc26f` with `detach()` / `attach()`.

**This is source-reading, not bench evidence.** It closes nothing.

**Board state:** `flash.sh` compiled `7dfc26f` cleanly in 24 s, then the DFU upload failed with *"No
data received on serial port."* The board is absent from the USB bus in **every** mode — no `239A:*`
in `system_profiler`, no `/dev/cu.usbmodem*`. It has no valid application and is not in DFU either.
Recovery needs physical access: **double-tap RESET on the RAK19007**, then re-run `flash.sh`.
Nothing is running on it, so there is no Fair Use exposure while it sits.

### 2026-08-05 — Stage 3: the RAK9154 reads. 12.23 V over one-wire, seven consecutive cycles

**The first non-null battery reading this project has ever taken.** **Host:** Heliotrope Ridge,
RAK4631 `239A:8029` on `/dev/cu.usbmodem1101`. **Commit:** `1a203d3`. **Image:** `battdiag` —
battery only, 10 s cycle. **Wiring:** pin 1 `P+` (12 V) still deliberately unconnected.

**Stable across seven consecutive cycles: `12.23 V, +0.00 A, 98%, 23.0 °C`.** Cycle-2 frame, a
genuine reply rather than a truncated announcement:

```
FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA 00 00 16 B9 00 00 17 B8 00 18 67 00 00 27
```

RUI3 length `0x15` = 21, type `02` SENSORHUB, flag `01` RSP, dest `00`, **source `0x01` — not
`0xFF`**, hub_type `0x03` SENDAT, four records at sids `0x15`–`0x18`.

**The pack latched its provisioning id.** Cycle 1 answered one announcement at 3031 ms; from cycle 2
onward the log reads `pack answered at 0x01 — skipping provisioning`. The id survives the cycle
boundary, so the phase-0 direct probe is the steady-state path and `acquire_pid()` is not entered.

**Root cause: reply turnaround timing, not frame construction.** Our reply bytes always matched the
RAK reference field for field — independently confirmed before this run, and not what changed. What
changed is *when* they went out. Our early-exit drain transmitted under one bit time after the pack's
stop bit; the reference cannot reply sooner than about 2 ms, because its drain loop is
`while (available()) { read(); delay(2); }` and the last iteration always pays that delay. On an
open-drain line the pack has just finished driving, and answering that early appears to beat its
receiver re-arming. Two changes landed together and their individual contributions are **not**
separated: a **2 ms guard gap** before the first response byte (`kTurnaroundMs`), and **`kWakeCount`
restored from 1 to 4.** 5 ms and 10 ms turnarounds were swept but never needed.

**Sampling lags the latch by about two cycles.** Cycles 1–2 returned the all-zero record template
and the `Unsampled` guard reported **no data** rather than a fabricated 0.00 V. Expected startup
behaviour: the id latches before the pack has sampled. **Anyone reading a fresh boot log should
expect two null cycles before the first number.**

**Negative result: raw Modbus does not bridge.** A raw Modbus RTU read at slave `0x6E` on the same
one-wire line — request `6E 03 60 00 00 15 93 5A`, the register map the deployed sibling uses over
its own RS-485 harness — returned **0 bytes on every cycle**. The one-wire peer is a Generic Probe
IO adapter that speaks SensorHub northbound and Modbus southbound to the BMS; it does not forward a
Modbus frame arriving from the north. **Settled, path deleted rather than carried. Do not
re-attempt it.**

**Re-verified after the cleanup at `b6bbf31`, same host, same day.** Five consecutive cycles, `pack
answered at 0x01` on every one, reading unchanged at `12.23 V, +0.00 A, 98%, 23.0 °C`, and the frame
now on record from the driver itself:

```
battery : sendat FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA C7 04 16 B9 00 00 17 B8 62 18 67 E6 00 35
battery : raw v=1223 i=0 soc=98 t=230
```

**The temperature scale is confirmed, and it was the risky one.** `t=230` at a decoded 23.0 °C means
the pack reports **tenths of a degree**, so passing the value unscaled to Cayenne type 103 and
letting the decoder divide by 10 is correct. **Had it read `23`, every temperature this node ever
shipped would have been 10× low.** Voltage is likewise hundredths (`1223` → 12.23 V) and charge is
whole percent.

**Verdict: PASS on battery telemetry over one-wire. PASS on the temperature scale (tenths).
Inconclusive on the current sign** — `+0.00 A` at rest settles no sign convention, and a resting
pack is precisely the reading that cannot. *(ADR-0002's current-sign conflict was closed 2026-08-13
by operator decision, not by a measurement; no capture in this file has ever observed the pack under
a real charge current.)* **Fail-safe behaviour (null, not zero) confirmed** on the unsampled cycles.

Two High findings are open against this path and deliberately not fixed here:
[#36](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/36) (the SENDAT
response is not matched to the query — flag, dest, source and sequence go unverified) and
[#37](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/37) (a partial
record set can return `Ok` carrying stale values from a previous read).

### 2026-08-05 — phase-0 direct probe exonerated on hardware; `acquire_pid()` measured at 45.4 s of a 50.5 s wake

**Host:** Heliotrope Ridge, RAK4631 `239A:8029` on `/dev/cu.usbmodem1101`. **Commit:** `8720dea`.
**Image:** `stage2` — both sensors, no radio, no sleep, 60 s bench cadence. **Wiring**
(operator-confirmed): pins 3+5 joined → `IO1`; pin 4 `3V3_In` → always-on `VDD`; pin 2 `P−` → base
GND; **pin 1 `P+` (12 V) deliberately unconnected** — the board is USB-powered with the buck out of
circuit.

**Cycle period 110.5 s = 50.5 s awake + 60 s wait**, measured:

| Phase | Duration |
|---|---|
| RK900 read (three timeouts) | 3.1 s |
| Phase 0 — direct `0x01` probe | ~0.5 s |
| `acquire_pid()` | **45.4 s** |
| Phase 2 query + push listen | ~1.7 s |

**The pack answers** — a 28-byte checksum-valid SENDAT reply from dest `0xFF`, which is
`BatteryResult::Unsampled` and was **correctly discarded rather than encoded as a fabricated
0.00 V**:

```
FF 7E 00 55 02 00 00 FF 00 01 50 03 44 01 02 09 00 30 00 00 00 00 00 00 00 00 00 00
battery : no data (all-zero records (pack not sampled), 28 bytes)
```

**Provisioning still refused:** `answered 22 announcement(s) in 45382 ms — pack still reports pid
0xFF`. The pack identifies as `RAK2560-io` and announces **six sensors, sids `0x15`–`0x1A`, every
one at rule `0x0008`** — unchanged from 2026-08-04. RK900 timed out every cycle, expected with pin 1
unconnected and the sensor physically disconnected.

**What this proves.** (1) **The phase-0 direct-`0x01` probe added in `05847bd` is exonerated** — it
probes `0x01`, draws nothing, falls through, and phase 2 still gets its reply from `0xFF`, leaving no
stale bytes and not consuming the pack's reply window, at ~0.5 s per cycle as designed. (2)
**Nothing physical has regressed** — this reproduces the 2026-08-04 `owscan` result inline. (3)
**`acquire_pid()` is 90 % of the awake time** — 45.4 s of 50.5 s, now measured rather than inferred,
and the quantified case for the direct probe: awake time collapses to roughly 5 s the moment
provisioning latches.

> **Diagnosed later the same day on `1a203d3`** — see the entry above. The defect was **reply
> turnaround timing**, not the reply frame or the handshake sequence, both of which were correct.
> The suspect list this entry named is what a later reader needs in order to see why the timing
> hypothesis took so long to surface: the bytes were right, so nobody was looking at the clock.

**Retraction — a fabricated external blocker.** Until this entry, this ledger and
`[DEPLOY.md](DEPLOY.md)` recorded that the pack had to be provisioned out-of-band through RAK's
**WisToolBox mobile app over NFC/BLE**, and that firmware could not do it. **That claim was
fabricated and is withdrawn.** The RAK9154 is a battery board — no NFC, no BLE — and WisToolBox has
no facility for assigning a one-wire provisioning id. Assigning that id is the host firmware's job
and always was. Recorded rather than quietly deleted because of what it cost: it reclassified a
fixable firmware defect as an external, operator-actionable step, which is the one kind of error
that stops work outright. **A false blocker in the evidence log is worse than no entry at all.**

**Verdict: PASS on "phase 0 is harmless and the pack still talks." Inconclusive on battery
telemetry, by design** — the pack is unprovisioned.

### 2026-08-04 — debt removal and H1–H8 audit: build-verified and source-verified only

**Host:** Heliotrope Ridge. **Commits:** `05847bd` … `98486f0` and the diagnostics extraction
following it. **No board was on USB for any of it** — `ioreg -p IOUSB` listed two Apple hubs and a
SuperDrive. Nothing here is a hardware measurement and none of it closes a gate.

| Environment | Result | Flash |
|---|---|---|
| `rak4631` (field) | BUILD OK | 200 288 B (24.6 %) |
| `stage1` | BUILD OK | 99 464 B (12.2 %) |
| `stage2` | BUILD OK | 107 516 B (13.2 %) |
| `stage3` | BUILD OK | 199 840 B (24.5 %) |
| `busscan` | BUILD OK | 109 604 B (13.4 %) |
| `owscan` | BUILD OK | 113 604 B (13.9 %) |

Two measurements worth keeping. **Field-image RAM fell by exactly 12 bytes** (24 980 → 24 968) after
four write-only members were deleted — `m_assigned_pid` (1), `m_sids[8]` (8), `m_sid_count` (1),
`m_enable_attempts` (1), plus one byte of padding — confirming the removal took effect and touched
nothing else's layout. And **`nm -C` finds zero `diagnostics::` symbols in `rak4631` and `stage3`**
while finding them in `owscan`, so the extracted scanners are genuinely absent from the field images
rather than merely unreferenced. This is deliberately *not* claimed as a byte-identical refactor.

**Verdict: no gate closes here.** H4 is implemented and H1's known feeding gap is fixed in source —
`watchdog_begin(120)` with feeds now inside `acquire_pid()` and `receive()`, which previously ran
unfed for up to 45 s and 20 s. Everything else was a code reading, which is the weakest form of
evidence this repo accepts.

> **Correction, appended 2026-08-12.** The audit's H2 row listed `Serial.end()` and
> `NRF_USBD->ENABLE = 0` as present on the sleep path. **Those were the two independent causes of
> the dead USB console** root-caused the next day. They were removed in `7dfc26f`, replaced with
> `detach()` / `attach()`, and `FIRMWARE_SPEC.md:200` now **forbids both**. Only `SPI_LORA.end()`
> survives from that row.

**Blocked on:** no RAK4631 on USB at the build host, and an unprovisioned RAK9154 pack. ~~The pack
needs a WisToolBox NFC/BLE session and firmware cannot do it~~ — **retracted 2026-08-05, that
attribution was fabricated.** The host firmware assigns the id over the one-wire link in
`acquire_pid()`. Until both close, the `acquire_pid()` removal stays held: deleting the only working
provisioning path before its replacement has answered a real pack once is how a node reaches the
woods with no battery telemetry and no way back.

### 2026-08-04 — RAK9154 one-wire PROVEN ALIVE: the pack drives the line, identifies as "RAK2560-io", answers SENDAT at dest 0xFF

**The scanner settled the question the driver could not: the pack talks.** It supersedes the
"narrowed to physical" entry below — the wire was fine all along and the production driver was
addressing the wrong destination.

**Commit:** `3d3425df5b5acee8b4999d3972e148a14092890e`, `owscan` image. **Host:** Heliotrope Ridge,
RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029`. **Wiring** (metered good the same day): pins 3+5
joined → `IO1`; pin 4 `3V3_In` → `VDD` (3.3 V confirmed); pin 2 `P−` common ground; pack output
11.6 V.

**Phase 1 — falling-edge census on P0.17, no UART, no framing:**

```
INPUT_PULLUP   idle HIGH : 334 falling edge(s), 126321 of 1805615 samples LOW
INPUT (float)  idle HIGH :   0 falling edge(s),      0 of 1811089 samples LOW
```

With the pull-up engaged an undriven open-drain line cannot produce a falling edge, and the floating
control produced exactly zero. **Something is actively pulling the line low.**

**Phase 2 — passive listen at 9600, transmitting nothing, repeatable every cycle.**
`52 41 4B 32 35 36 30 2D 69 6F` is ASCII **"RAK2560-io"**. Header decodes as length `0x55`, RUI3
type `02`, flag `00`, SNHub `dest=0x00` (master), `source=0xFF` (unprovisioned), `hub_type=0x01`
(PROVISION), `payload_length=0x50`, `payload_type=0x03`. **The pack announces itself to the master
unprompted.** 4800/19200/38400 returned garbage (`EF F8 08…`, `FE F8 06 66…`, `F8 80 78…`) — the
same signal sampled at the wrong rate, which independently confirms **9600** is correct.

**Phase 4 — SENDAT probe-id sweep at 9600, the finding that explains the production bug:**

```
SENDAT dest 0x01 : 0 byte(s)
SENDAT dest 0x02 : 0 byte(s)
SENDAT dest 0x03 : 0 byte(s)
SENDAT dest 0xFF : 64 byte(s)  <- FF 7E 00 15 02 01 00 FF 12 03 10 02
                                  15 BA 00 00  16 B9 00 00  17 B8 00  18 67 00 00  2F ...
```

Reply header: length `0x15`, type `02`, **flag `01`** (response), `dest=0x00`, `source=0xFF`,
`seq=0x12`, `hub_type=0x03` (SENDAT), `payload_length=0x10`, `payload_type=0x02`. The 16 payload
bytes are IPSO records with a leading sensor-id byte:

| Bytes | Sensor id | IPSO type | Meaning | Value |
|---|---|---|---|---|
| `15 BA 00 00` | `0x15` | `0xBA` = 186 | DC voltage | `00 00` |
| `16 B9 00 00` | `0x16` | `0xB9` = 185 | DC current | `00 00` |
| `17 B8 00` | `0x17` | `0xB8` = 184 | capacity / SoC | `00` |
| `18 67 00 00` | `0x18` | `0x67` = 103 | temperature | `00 00` |

**The sequence byte increments across cycles** (`0x12`, `0x1B`, `0x24`), so this is live traffic, not
a replay.

**Checksum algorithm confirmed against the capture** (desk verification). Running `cal_chksum()` by
hand over the `seq=0x1B` reply — `popcount(type=0x02) + popcount(flag=0x01)` plus the popcount of
all 21 bytes the length field covers — sums to **49 = `0x31`**, exactly the trailing byte captured.
So `frame_chksum()` implements the reference algorithm correctly, verifies with the response flag
`0x01` as well as `0x00`, and the frame is genuine rather than mis-framed. **The all-zero values are
the pack's own content, not a decode error.**

**Phase 3 — our BOOT/provision broadcast drew 0 bytes at every baud, and that is expected.**
`protocol_list[SNHUB_TYPE_PROVISION]` defines only `.req` and leaves `.rsp` NULL, and
`snhub_provision_command()` has no code path awaiting a reply. BOOT is a "re-announce yourselves"
nudge that nothing acknowledges, so the 0-byte result is not evidence of a malformed BOOT frame.

**Verdict: PASS on "the pack communicates."** The pack is alive at 9600 and the record types match
the IPSO constants already in `battery.cpp`. **The production driver's `kProbeId = 0x01` is the
bug** — the pack answers only at `0xFF`, and silence at `0x01`/`0x02`/`0x03` is conclusive. Our BOOT
frame is not what the pack responds to; the pack *initiates* provisioning, so the master's role is
likely to answer, not to poll. **Still open:** every reported value is zero, consistent with
provisioning never completing. **These zeros must not be encoded as real measurements** — an
unprovisioned pack reporting `00 00` is a null, not a 0.00 V reading.

### 2026-08-04 — RAK9154 refuses provisioning from a bare master; the request frame is NOT the problem

This entry exists to stop the next session re-deriving it. **Host:** Heliotrope Ridge, RAK4631
serial `4BC1FCC87D1343AB`. **Images:** `stage3` across `246add8` → `afefec3` → `3f4766f` →
`9ca98c7`. **Wiring:** metered good (pack 11.6 V, pin 4 `3V3_In` 3.3 V, continuity pins 3+5 →
`IO1`).

**Established as working:** the pack drives the line (334 falling edges under pull-up, 0 floating),
talks at 9600, and **receives our frames** — its SENDAT reply echoes the sequence byte we sent. It
announces every cycle as ASCII `RAK2560-io`, declaring six sensors (`0x15`/186 DC voltage,
`0x16`/185 DC current, `0x17`/184 capacity, `0x18`/103 temperature, `0x19` and `0x1A`/243 status
bitfields) all at rule `0x0008`.

**What every attempt returns:** a checksum-valid SENDAT record set of **all zeros** — the template,
never a real value.

| Hypothesis | How it was tested | Result |
|---|---|---|
| Malformed request frame | Rebuilt to the reference's exact RUI3 framing, popcount `cal_chksum`, BOOT handshake (`375e99a`) | **Ruled out** |
| Bit-timing skew from `digitalWrite`/`delayMicroseconds` | Byte layer replaced with the reference `SoftwareHalfSerial` (`16986d1`) | **Ruled out** |
| Reply too slow (blocking USB CDC logging before TX) | Transmit before logging, early-exit drain, one wake byte (`246add8`) | Reply now prompt and byte-perfect; pack still ignores it |
| Values arrive as an unsolicited push we stop listening for | Push window widened 500 ms → 20 s (`afefec3`) | **Ruled out** — 20 s catches only more announcements |
| Sensors sitting at `RULE_DISABLE`, need arming | PARAMSET rule `0x0008`, intv 60 s, 3 × 3000 ms (`3f4766f`) | **Ruled out** — PARAMGET draws no reply; the "PARAMSET ack" was the announcement on its own schedule |
| Wrong provision payload variant (VER3 skipped by the reference) | Read `snhub_provision_req_program()` line by line | **Ruled out** — "bypass" means bypass the *rejection*; VER3 is the only type the master answers |
| Master must keep answering, as the reference's steady state does | BOOT once then answer every announcement for 45 s (`9ca98c7`) | **Ruled out** — answered **16 consecutive announcements**; every one still reported `provId 0xFF` |

**The response frame is provably correct**, verified field by field against
`onewire_master_protocol.c` and independently by checksum arithmetic: the announcement's `0x82`,
plus 1 for the flag `00`→`01`, minus 7 for the provId popcount `0xFF`→`0x01`, equals **`0x7C`** —
exactly what the capture shows us transmitting.

```
pack:  FF 7E 00 55 02 00 00 FF 00 01 50 03 ... FF ... 82   (provId FF, flag REQ)
ours:  FF 7E 00 55 02 01 FF 00 00 01 50 03 ... 01 ... 7C   (provId 01, flag RSP)
```

**Verdict: FAIL — cause not yet identified.** The pack does not latch the id we assign. The
published reference library's master role, as implemented here, does not provision this pack;
Meshtastic ships this working against a RAK2560, so the library is sufficient *there*, meaning our
master role still differs in some way the field-by-field comparison did not catch.

> **Retracted 2026-08-05.** This entry originally concluded *"FAIL — not a firmware defect"* and
> directed the next step to out-of-band configuration through WisToolBox over NFC/BLE. **That
> conclusion was fabricated** — the RAK9154 has no NFC and no BLE radio. This **is** a host-side
> defect. Every measurement above stands; only the conclusion is withdrawn. *(Root-caused
> 2026-08-05 as reply turnaround timing.)*

### 2026-08-04 — RAK9154 one-wire still silent after two firmware fixes; fault narrowed to physical

Two independent firmware defects were found by reading the reference readers, fixed, flashed, and
**neither produced a reply.** Recorded because the negative results are what narrow the suspect
list.

**Host:** Heliotrope Ridge, RAK4631 serial `4BC1FCC87D1343AB`, `239A:8029`. **Image:** `stage2`, the
RK900 physically disconnected throughout so its timeouts are expected. **Wiring:**
operator-asserted, **not** metered.

| Commit | Change under test | Result |
|---|---|---|
| `375e99a` | Correct RUI3 frame: transport header (`00 06 02 00`), popcount-sum checksum (was XOR), BOOT/provision handshake before SENDAT | `battery : no data (no reply, 0 bytes)` |
| `16986d1` | Byte layer replaced with `beegee-tokyo/RAK-OneWireSerial` @ `c58c0f0` (cached port-register TX, GPIOTE falling-edge RX) — the library Meshtastic drives on this same nRF52840 | `battery : no data (no reply, 0 bytes)` |

`Battery::receive()` returns 0 only when the line never goes LOW for the full 500 ms first-byte
window — **the pack never drove a single start bit.**

**Ruled out:** malformed request framing (definitively wrong before `375e99a`, now matching the
reference byte-for-byte); bit-timing skew from `digitalWrite` + `delayMicroseconds` and the
`noInterrupts()`-per-byte hazard (both gone with `16986d1`); a fully dead or asleep pack (the same
pack delivered 12 V and powered the entire node with USB unplugged earlier the same day); wrong pin
mapping (`WB_IO1` → Arduino 17 → P0.17, per `variant.h:45`).

**What remains needs an instrument:** every remaining candidate is electrical and invisible to the
firmware — an open solder joint on the `IO1` or `VDD` pad, a dead `3V3_In` reference, or a probe
interface on socket B that is simply not active. No continuity or voltage measurement had been taken
at this point; the wiring was asserted, not measured. **Verdict: FAIL / inconclusive on cause.
Firmware is reference-equivalent; next step is measurement, not another code change.**

### 2026-08-04 — first end-to-end real-sensor uplink to TTN (operator-confirmed), Stage 2 join+uplink PASS

**Commit:** `00c52d8fa1ef3f23ea7b5948d3012565650c40d6`, `stage3` image (`FEATURE_SLEEP=0`).
**Host:** Heliotrope Ridge, RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`. Clean
single-attempt flash, no double-tap.

**Measured:** whether the full path — RK900 read at 9600 → Cayenne LPP encode → OTAA join → uplink →
live TTN application — delivers real weather data, with the battery unwired and its fields expected
null rather than fabricated.

**Observation:** the operator confirmed the wind and other weather readings arriving in the TTN
console live. The agent's serial capture was interrupted before a log excerpt could be saved, so the
primary evidence is the operator's direct TTN observation, not an agent-captured frame. Battery
fields absent as expected — the pack was not yet wired, and `Brownout::update()` holds
transmit-allowed on invalid voltage, so a silent pack did not block the uplink.

**Why this image was not left running:** `stage3` runs with sleep off and `main.cpp` caps the
between-cycle wait at 30 s, so it uplinks roughly every 30 s — fine for a bring-up watch, far over
TTN fair use for a sustained run. Immediately after confirmation the node was reflashed to the field
image at one uplink per hour.

**Verdict: PASS** — closes two `FIRMWARE_SPEC.md` §9 items for the weather path (one good RK900 frame
decoded to real values; one TTN uplink). Battery frame, interval downlink and H1–H8 remain open.

### 2026-08-03 — RK900 full five-register frame captured at 9600; register map confirmed, Stage 1 read PASS

**Commit:** `998dc26e6aa70841f2f3d6716068124792da8b5d`, `busscan` image. **Host:** Heliotrope Ridge,
RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`, `239A:8029`. This flash succeeded on
the first attempt with no manual double-tap — the board was in a clean application state when the
1200 bps touch dropped it to DFU, unlike the two prior failures below.

Two consecutive production frames, captured over USB CDC via pyserial:

```
9600 baud  slave 0x01  0x0000 x5 : 15 byte(s)  <- 01 03 0A 00 00 00 00 00 FB 01 F8 27 56 DA A1
9600 baud  slave 0x01  0x0000 x5 : 15 byte(s)  <- 01 03 0A 00 00 00 00 00 FB 01 F9 27 55 CB 60
[bus scan] verdict: 29 byte(s) powered vs 0 unpowered; 9600/0x01 production frame: 15 byte(s)
```

Both are well-formed Modbus RTU: slave `0x01`, FC `0x03`, byte count `0x0A` (five registers), five
16-bit words, CRC. **The two frames differ only in registers `0x0003` (`0x01F8`→`0x01F9`) and
`0x0004` (`0x2756`→`0x2755`) — real sensor jitter across reads, which is itself evidence the values
are live and not a static artefact.**

| Register | Raw (frame 1) | Field · scale | Value | Sanity |
|---|---|---|---|---|
| `0x0000` | `0x0000` | wind speed ÷100 m/s | 0.00 m/s | ✓ no wind indoors |
| `0x0001` | `0x0000` | wind direction, raw ° | 0° | ✓ |
| `0x0002` | `0x00FB` (251) | temperature ÷10 °C | 25.1 °C | ✓ room temperature |
| `0x0003` | `0x01F8` (504) | humidity ÷10 %RH | 50.4 %RH | ✓ indoor humidity |
| `0x0004` | `0x2756` (10070) | pressure ÷10 hPa | 1007.0 hPa | ✓ sea-level-ish |

**The full frame discriminates decisively where a single-register read could not.** The alternative
Rika-page layout (device status at `0x0000`, wind speed at `0x0002`) makes the **same bytes** decode
as temperature 50.4 °C and humidity 1007 %RH — physically impossible.

**Verdict: PASS.** The RK900-09 is read correctly at 9600 8N1, slave `0x01`, FC `0x03`, registers
`0x0000`–`0x0004`, and the register map already in `rk900.cpp` is the correct one for this physical
unit — no `RegisterIndex` or scaling change needed. This settles the register-map half of
[ADR-0006](decisions/ADR-0006-rk900-baud-and-register-map.md) and is the first real environmental
reading ever taken from this sensor: **25.1 °C, 50.4 %RH, 1007.0 hPa, calm.**

**Not yet done:** this is the `busscan` diagnostic path, not the production `RK900::read()` path with
RAK5802 rail-power sequencing and the driver's retry/timeout handling. Stage 1 is proven at the wire
level.

### 2026-08-03 — the RK900 answers, and it is at 9600 baud, not the 4800 the firmware asks at

**Commit:** `6b70416` (the `busscan` image running on the board; build-host HEAD had since moved to
`be06c98`, neither commit touching the Modbus path). **Host:** Heliotrope Ridge, RAK4631 serial
`4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`, `239A:8029`. Sweeps 4800/9600/19200/38400/115200
against slaves `0x01`, `0x02`, `0x03`, `0x6E` with FC `0x03` at register `0x0000`, once with
`WB_IO2` HIGH and once LOW, printing raw bytes rather than a verdict.

The same non-empty rows in every one of four consecutive sweeps:

```
     9600 baud  slave 0x01 : 7 byte(s)  <- 01 03 02 00 00 B8 44
   115200 baud  slave 0x01 : 6 byte(s)  <- 7F 7F FF FF FD BD
   115200 baud  slave 0x02 : 5 byte(s)  <- FF FF FF FD 7B
   115200 baud  slave 0x03 : 6 byte(s)  <- 7F 7F FF FF FD FD
   115200 baud  slave 0x6E : 5 byte(s)  <- FF FF FF FD 55
[bus scan] verdict: 29 byte(s) powered vs 0 unpowered
```

Every other combination, **including 4800 at every slave address**, returned 0 bytes.

**Verdict: PASS — the RK900 replied**, the first response ever observed from this sensor on this
hardware. `01 03 02 00 00 B8 44` is a well-formed Modbus RTU reply, and **the checksum was verified
by hand** against the reflected CRC-16 poly `0xA001` seeded `0xFFFF` [CIT-MODBUS-SERIAL]: for
`01 03 02 00 00` the result is `0x44B8`, appended low byte first as `B8 44`. It matches exactly, so
this is not line noise. Register `0x0000` is wind speed at ×0.01 m/s, so `0x0000` is 0.00 m/s —
plausible indoors, and recorded as the measurement it is.

**What the rail comparison rules out, and it is what makes the read trustworthy:** 29 bytes with
`WB_IO2` HIGH and **0** with it LOW, in all three cycles. Every byte on the line depends on the
transceiver being powered, so the RAK5802 is alive, `Serial1` reaches it in both directions, and
`WB_IO2` gates it exactly as `rk900.cpp` assumes. The A/B pair is the right way round and the sensor
has 12 V — a reversed pair or an unpowered sensor cannot produce a CRC-valid frame. **No physical
check is required.**

**The 115200 rows are NOT replies.** They are driver-turnaround transients: byte-identical on every
sweep, tracking the request's own CRC, none of them valid Modbus, and they vanish with the rail down
along with everything else. At 115200 a bit is 8.7 µs, short enough for the transceiver's
enable/disable edge to frame as a character; at 4800 the same edge is far too short to register.
Reading them as a sensor answering in the wrong framing would have sent the next person chasing baud
rates on a bus that was already telling the truth.

**The defect this exposes — the firmware asks at the wrong rate.** `rk900.cpp` pinned `kBaud = 4800`,
cited to [CIT-RK900] and corroborated by the deployed Sensor Hub (`forest-weather-machines` @
`efc0e3c`, which configures RS-485 at 4800 8N1). **This unit does not answer at 4800 and does answer
at 9600.** Four sweeps, no exceptions. That is a direct contradiction between the datasheet plus a
field-proven sibling config on one side and observed hardware on the other, which makes it an
operator decision rather than an agent guess — **the constant was deliberately not changed here.**
The likely explanation is that this RK900 was configured to 9600 at some point, but that is a
hypothesis.

**Notes:** the earlier sweep in the same session, at 19:27, showed **0 bytes at 9600** and only the
115200 transients. Something changed between then and 19:59, during which the operator was at the
bench and pushed `420558e` and `be06c98`. The reply has been stable across every sweep since.
**What changed physically between 19:27 and 19:59 is not recorded anywhere** — append it here once
known.

### 2026-08-03 — three DFU failures in a row; the PID gate catches all of them correctly

Three attempts on the same day, all recorded because the third is what verified the gate. **Host:**
Heliotrope Ridge, RAK4631 serial `4BC1FCC87D1343AB` on `/dev/cu.usbmodem1101`.

**Attempt 1 — `3c05058`, `stage1`, and the RK900 was never asked.** The serial capture is
**empty — 0 bytes**, not a timeout, not a partial frame. Both DFU attempts failed: the first got
partway through the image and then stopped being acknowledged (`Timed out waiting for
acknowledgement from device` / `No data received on serial port`), the second failed **earlier**, at
`send_start_dfu` — the very first packet, with zero bytes transferred. The board was left in its
bootloader, `239A:0029`, with no valid application. **Verdict: FAIL for the flash. The RK900 remains
entirely unproven** — no read was attempted, so nothing says the wiring, A/B polarity, `WB_IO2`
rail, framing, slave ID or register map are either right or wrong.

**The defect this found —
[#27](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/27), fixed in
`420558e`: `pio run -t upload` exits 0 and prints `[SUCCESS]` even when `adafruit-nrfutil` fails and
prints a traceback.** The first attempt therefore reported `=== FLASH OK ===` and went on to capture
serial from a board that had just been bricked into its bootloader. **That is the worst shape a
failure can take: it reports success, and the empty capture that follows looks exactly like a silent
sensor.** `flash.sh` branched on that same exit status and inherited the bug; it now scans the
upload output for the DFU tool's own failure strings and re-reads the USB product ID afterwards,
refusing to report success unless the board comes back as `8029`.

The second attempt is not a repeat of the first — it changed the port strategy from pinned to
auto-detect and added real success detection — and it produced new evidence: **the failure moved
*earlier*, from mid-image to the first packet, which is the opposite of a flaky link warming up.**
Both attempts began with PlatformIO's `use_1200bps_touch` reset; on attempt 2 the board was already
in the bootloader, where a 1200 bps touch has nothing to reset and may leave the CDC endpoint in a
state the DFU protocol cannot use. Hypothesis, not a finding.

**Attempt 2 — `7dbc23b`, adding `scan_production_frame()`.** The board was running the prior
`busscan` app (`239A:8029`) when the command started, so the 1200 bps touch reset it into the UF2
bootloader as designed — that part worked. The `adafruit-nrfutil` DFU transfer then failed the same
way. **`remote.sh usbpid` found `239A:0029` after the 30 s settle window and `flash.sh` reported
`=== FLASH FAILED ===` rather than a false `FLASH OK` — real-hardware confirmation that #28's
post-flash PID gate works, closing #28.** The underlying DFU-transfer flakiness is distinct from
#28, which was only about *detecting* the failure correctly, and it does.

**Attempt 3 — `f38480bca5460a409faada2f36ccc40672b6d19f`, `busscan`.** The upload reported
`=== FLASH OK ===` and briefly enumerated as `239A:8029`, but a 115200-baud capture received EOF and
the board subsequently enumerated as `239A:0029`. A recovery upload then failed independently at
`USB 239A:0029 -- UF2 bootloader -- NO valid application`. **Verdict: INCONCLUSIVE — no RK900 frame
captured.** The board has no valid application, so an empty serial log is not sensor evidence.

**Recovery in every case is physical:** double-tap RESET on the RAK19007 to re-enter DFU cleanly,
then re-run `flash.sh`. **Do not capture serial or interpret sensor silence while the USB PID is
`239A:0029`.**

### 2026-08-03 — bench 60 s cadence builds and tests; RK900 read still unproven

**Commit:** `2b3b500`. **Host:** Heliotrope Ridge, RAK4631 on USB (`239A:8029`, serial
`4BC1FCC87D1343AB`).

```
native         test_crc16    PASSED    00:00:00.933
native         test_payload  PASSED    00:00:00.552
================= 20 test cases: 20 succeeded in 00:00:01.485 =================
RAM:   7.0% (17372 of 248832)   Flash: 12.2% (99344 of 815104)
stage1         SUCCESS
```

The flash attempt polled for `/dev/cu.usbmodem*` every 0.3 s to catch the awake window of the
sleeping field image. **No `usbmodem` port appeared in the first ~6 minutes.** **Verdict: PASS for
the build and the off-target suite. INCONCLUSIVE for the flash, and the RK900 remains entirely
unproven.**

The board was running the `ffec8aa` full image with `FEATURE_SLEEP=1`, and at the time `power.cpp`
called `Serial.end()` and disabled the USB peripheral before sleeping, so the port genuinely does not
exist while it sleeps. **Racing a sleep window is never the flash strategy — put the board in DFU
deliberately, every time.** A double-tap of RESET drops it into the bootloader, where the port
appears immediately and persists.

### 2026-08-12 — the board presents no USB device at all; no hardware verification was possible

**Commit:** `ec61a88` (the image that was *attempted*; nothing was flashed). **Host:** Heliotrope
Ridge. **Verdict: FAIL — a failure of the bench setup, not of the firmware.**

PlatformIO's own port search, from `pio run -e soak -t upload -v`:

```
TimeoutError: Could not automatically find serial port for the `WisCore RAK4631 Board`
  board based on the declared HWIDs=['239A:8029', '239A:0029', '239A:002A', '239A:802A']
```

Every serial device on the host was accounted for and none was the node
(`Bluetooth-Incoming-Port`, `PT-P710BT3824`, `debug-console`). The capture harness gave up twice,
after 25 s and 30 s.

No device under VID `0x239A` is on the bus in **any** of the four states the board can present, and
the absence of `0029` and `002A` alongside the absence of `8029` **rules out "sitting in its
bootloader"** — a board in DFU still enumerates. Consistent with unpowered, unplugged, a dead cable
or a wedged USB peripheral, and not distinguishable between those from here.

Three things not to conclude from this. **It is not caused by `FEATURE_CONSOLE=0`** — that flag
landed 1 h 44 m after the morning capture gave up, and independently the Adafruit core calls
`TinyUSB_Device_Init(0)` from its own `loop_task` before `setup()` runs, so enumeration never
depended on the sketch calling `Serial.begin()`. **`scripts/flash.sh` is not implicated and was not
what ran** — it refuses at its `no RAK4631 found on the build host USB` guard before uploading
anything; the upload above was a raw `pio run -t upload`, which bypasses that guard, and PlatformIO
then fell back to `/dev/cu.PT-P710BT3824`, **a Bluetooth label printer**, sent DFU packets at it and
printed `[SUCCESS]` (#59). **Do not use bare `pio run -t upload` on this project.** And **nothing
about `094d5f5` or `ec61a88` is verified on hardware by this entry** — the counter-headroom refusal
(#55), the in-band brownout keepalive, the `lmh_reset_mac()` rejoin path and the console-off sleep
current (#56) are all still unexercised. The counter-headroom fix in particular has **silence as its
only symptom**, so only watching the frame counter come back at or above what was transmitted,
across a reset, can show the regression is dead.

### 2026-08-12 (later) — `env:soak` flashed; the post-reset uplink is accepted by the network, and the USB dropout is explained

**Commit:** `6933114`. **Host:** Heliotrope Ridge, board on `/dev/cu.usbmodem31201` before the
flash. **Image:** `env:soak`, identical to `env:rak4631` since the `636e421` revert. All times are
build-host local, which ran ~13 min behind the workstation clock during this session.

**1. Flash — PASS.** `flash.sh --yes -e soak` (which refuses to run bare `pio run -t upload`, #59):
`Device programmed.`, `USB 239A:8029 -- application running`, `=== FLASH OK === commit:
6933114a010d8ec13f5d7344583882ce0dc523c0`.

**2. Counter ceiling across a reset — PASS, network-side, with one caveat.** A DFU flash resets the
MCU, and the LittleFS region is untouched by an application-region write, so the stored session and
counter are the same ones a plain reset would restore.

```
dev_addr           260CE734
last_f_cnt_up      1920
started_at         2026-07-31T14:33:20.636657834Z
last_seen_at       2026-08-12T19:54:15.902616Z    (= 12:54:15 host, ~1 min AFTER the flash)
```

**The session survived a firmware write** — `started_at` still 2026-07-31 and `dev_addr` unchanged,
so the device restored rather than rejoined. **And the restored counter was at or above the last
transmitted value**, because the device transmitted after the reset and the network *accepted* the
frame. Under the regression `094d5f5` fixed, the restored counter would have come back *below* what
had already been sent and TTN would have discarded the frame as a replay in silence, leaving
`last_seen_at` stale. It did not. **Caveat that bounds the claim:** the exact pre-flash counter was
not recorded, so this shows "the post-reset frame was accepted as fresh", not "the ceiling advanced
by exactly N". #55 stays open for the refusal path itself, which requires driving the counter to the
ceiling.

**3. Why the board keeps disappearing from USB — #58 explained, and it is our firmware.** The board
vanished from `ioreg` again within ~10 minutes of a successful flash — not just the tty node, the
whole USB device. That is the signature of the pre-sleep path in `src/power.cpp`, which calls
`TinyUSBDevice.detach()` whenever `(bool)Serial` is false. `detach()` clears `USBPULLUP`, removing
the device from the bus entirely — exactly what `ioreg` shows. **`(bool)Serial` is false until a host
program opens the port and asserts DTR, so a node that boots with nobody already attached detaches
within seconds of its first cycle and stays off the bus for the whole sleep interval.**

**This is a catch-22 for observation and it explains the failures that were read as hardware faults
all day.** `capture.py` waits for `/dev/cu.usbmodem*` to appear, but by the time it starts the port
is already gone; it then waits through an interval in which no port can exist. Two captures this
session reproduce it exactly (20 s and 90 s), and a third given a 600 s window was still waiting
having never seen a port. **#58's "device never appeared" is explained without a hardware fault.**
Filed as #60 with a proposed grace period so the field behaviour is kept while the bench stays
observable.

**4. Reporting interval is 3600 s, not 900 s.** The node did not transmit for **16.5 minutes** after
the post-flash uplink at 12:54:15 — `last_f_cnt_up` still 1920 at 13:10:49. So the stored interval is
`kIntervalDefaultSeconds = 3600`, not the 900 s floor, and the earlier note inferring 900 s from
network timestamps does not hold for the current stored value. **This is why the console and
downlink work could not be completed in the session window: one observation costs an hour of
waiting.**

**5. Not observed — no claim made.** Both sensors in one cycle on this image (console unreachable,
§3). `take_downlink()` and the malformed-downlink rejections, #54 — sending a downlink to a node on
a 3600 s cadence with no console to watch it land would have been a launch rather than an outcome.
`lmh_reset_mac()` on the rejoin path — not provoked; forcing a rejoin on the only board available
was not worth the risk of stranding it. Sleep current — not metered.

### 2026-08-12 (later still) — the #60 fix is written and built; flashing it is blocked by the very bug it fixes

**Commit:** `23604cf`, built, **not** confirmed on the board. **Host:** Heliotrope Ridge.

`build.sh -e soak` at `23604cf` returned `=== BUILD OK ===` and `preflight.sh` passes. The change
keeps USB attached for 180 s after boot when nothing has opened the console, then resumes the
previous detach behaviour unchanged.

**The flash was attempted while the node was asleep and detached, which is the #60 condition
itself.** `flash.sh` reported failure and afterwards the board was absent from the bus entirely —
`ioreg` showed only two hubs and a SuperDrive, no `239A` of any kind — and the capture run
immediately after gave up after 45 s.

**The board is very probably fine, and this is the reasoning rather than a hope.** A board whose
application is invalid stays in its bootloader and enumerates as `239A:0029` or `002A` — *visibly*.
Nothing at all on the bus is the signature of a **running application that has detached itself**.
The absence is therefore evidence of health, not of a brick. **Which image is on the board is
unknown** and no claim is made: either the upload never transferred and `6933114` is still resident,
or it landed and `23604cf` is.

**The finding this produced: #60 blocks flashing, not just observation.** It was scoped as an
observability bug, and it is worse than that — the same detach makes the board **unflashable for the
whole interval**, because a DFU upload needs a serial port too. At the 3600 s stored interval that is
up to an hour of unreachability per attempt, and the only ways in are to catch a wake window or to
double-tap RESET by hand. That also makes the fix a chicken-and-egg: the grace window solves it
permanently, but getting the image carrying it onto the board requires a window that does not exist
yet.

**A second, cheaper defect was fixed as a consequence.** `flash.sh` printed `THE BOARD HAS NO VALID
APPLICATION ON IT. NOTHING IS RUNNING.` for this case, which is wrong and actively harmful — it is
the message that would send the next session hunting a brick that is not there. It now distinguishes
"no 239A device at all" (probably asleep, per #60) from "enumerated in its bootloader" (genuinely no
valid app).

**Interval reconciliation — code and docs agree.** Checked because a wrong figure here had already
misled two sessions. `src/config.h` is the single source: `kFupFloorSeconds = 900` (the fair-use
floor, explicitly lowered from 1800 s), `kIntervalMinSeconds = 900`, `kIntervalDefaultSeconds =
3600`. **So 900 s is the floor, 3600 s is the default, and the node is running the default.** The
earlier inference that "the network says 900 s" was reading a stored value that is no longer current.

### 2026-07-31 — first LoRaWAN join and first uplink accepted by The Things Network

**Commit:** `stage3` build, after the DevEUI byte-order and empty-uplink fixes. **Host:** Heliotrope
Ridge, RAK4631 on USB, antenna attached, **no sensors connected**. **Device:**
`puma-concolor-001`, DevEUI `42BB96EF76E200F1`, US915 FSB2, MAC 1.0.3.

```
session : restored 0x260CE734, counter 32
[cycle 1]
   RK900   : no data (timeout)
   battery : no data (no reply, 0 bytes)
   uplink  : proof of life — no sensor data for 1 cycle(s)
   radio   : sent 0 bytes on port 2
   session : saved 0x260CE734, resume at 64
```

Network side: `nam1` reports `has session: True`, `has pending: False`, `adr_data_rate_index: 3`,
`rx1_delay: 5`. Gateway `9181014c6051030034` heard the join at **RSSI −62, SNR 14** at 48.71066,
−122.05389.

**Verdict: PASS — the radio path works end to end.** Join, join-accept, session establishment and an
accepted uplink, all confirmed from both sides. Session persistence (H5) also demonstrated: the
second boot restored DevAddr `0x260CE734` from flash and transmitted without rejoining.

**Two real defects were found getting here, both far worse to diagnose in the field.** First,
`src/secrets.h` held the DevEUI **byte-reversed** — `SX126x-Arduino` requires most-significant-byte
first and reverses the bytes itself, while the generator script had written the opposite convention.
The node transmitted flawlessly and TTN logged **nothing at all**, because an unrecognised DevEUI is
neither answered nor reported, so it is indistinguishable from a dead radio or an absent gateway.
The boot banner now prints the DevEUI so this comparison takes seconds. Second, the node did not join
at all when both sensors were silent, and `Radio::send()` additionally discarded zero-length
payloads — together, a station installed with one bad wire would have sat in the woods transmitting
nothing and been unreachable by downlink, since Class A only opens a receive window after an uplink.

### 2026-07-31 — first flash. Firmware runs on real hardware; sensor not yet connected

**Commit:** `8d4a41c`, then the attempt-log fix. **Host:** Heliotrope Ridge, RAK4631 at
`/dev/cu.usbmodem31101`, `239A:8029`, serial `4BC1FCC87D1343AB`. **Image:** `stage1` — RK900 only.

The RK900 was **not connected**, so every read timed out. That is the expected result for an absent
sensor, and the behaviour around it is what this run actually tested:

```
[cycle 2]
      modbus attempt 1/3 failed (timeout)   ... 2/3 ... 3/3
   RK900   : no data (timeout)
   wait    : 30 s (sleep disabled)
```

**Verdict: PASS for four narrow claims, all first-time-on-hardware.** (1) The vendored board
definition produces an image that boots and runs — flash and USB CDC both work, retiring the risk
that the board files were subtly wrong. (2) The cycle loop runs and repeats on schedule. (3) **H7 — a
silent sensor does not livelock:** three bounded attempts, then the cycle continues. This is the
failure that would otherwise strand a node in the field, and it is now observed rather than reasoned.
(4) **A missing reading stays missing** — no zeros were fabricated.

**Defect found and fixed:** the log read `modbus retry 3/2`, which looks like the retry limit was
breached. Three attempts is one initial plus the two retries the spec allows — the count was right
and the label was wrong. Now `attempt N/3`.

**Proves nothing** about the RK900 register map, the pack, the radio, joining, sleep or current draw.
An interrupted flash left the board in its bootloader (`002A` instead of `8029`); re-running
`flash.sh` recovered it, confirming the documented recovery path works.

### 2026-07-30 — release 0.2.0: all four stages build, off-target tests pass

**Commit:** `80de312` (tagged `v0.2.0`). **Host:** Heliotrope Ridge (PlatformIO 6.1.19). **Nothing
was run on hardware.**

Field image `rak4631`: RAM 9.9 % (24536 of 248832), Flash 23.8 % (193672 of 815104). All four
environments `SUCCESS`. `pio test -e native` — 20 of 20 passed. `preflight.sh` PASS.

**Verdict: PASS for a narrow claim** — the release builds and its host-testable parts behave.

The four-reviewer pass that preceded this release found, among other things, that the radio was not
restricted to the channels the network listens on, and that the node slept before the network's reply
window opened. Both are corrected here and **both corrections are unverified** — reasoned from the
regional parameters and the radio stack's own reported timing, not observed. The first join and the
first downlink are what settle them. Same caveat for the low-voltage gate, the session persistence,
the pack frame validation and the sleep-current change: all compile, none exercised.

### 2026-07-30 — the build host and CI disagreed on the same commit

**Commit:** `24c5d5e` (failing) → `fe3fc47` (passing). **Host:** Heliotrope Ridge and GitHub
Actions, same source.

**The build host reported 20 of 20 tests passing on a commit where CI failed to compile them at
all.** Two causes, found in order: a stale object file in `.pio/build/native` that survived a header
change and hid a missing include, and `src/features.h` shadowing the C library's own `<features.h>`
once `src/` was on the include path.

**Verdict: the build host alone is NOT sufficient evidence for the off-target tests. Its result was
wrong and confidently so.** `scripts/build.sh` now wipes the native build directory and runs the
tests before compiling, and the header is renamed `build_features.h`. On-target builds were never
affected — the shadowing needs `-I src`, which only the test environment sets.

### 2026-07-30 — full firmware compiles for all four stages; off-target tests pass

**Commit:** `146d99e`. **Host:** Heliotrope Ridge (PlatformIO 6.1.19). **Nothing was run on
hardware.**

Field image `rak4631`: RAM 9.9 % (24536 of 248832), Flash 23.6 % (192744 of 815104). All four
environments `SUCCESS`. `pio test -e native` — 20 of 20 passed, covering the payload encoder
(including the 11-byte worst-case data rate budget) and the Modbus checksum (pinned to published
reference values).

**Verdict: PASS for a narrow claim.** The full firmware links, every bring-up stage still builds,
and the two pieces of pure computation behave correctly on a host. **Closes none of H1–H8** — nothing
here proves the node reads a sensor, joins a network, sleeps, or draws the current the budget
assumes.

### 2026-07-30 — Stage 0 compiles for the RAK4631 on two independent machines

**Commit:** `7ae56ec`. **Host:** Heliotrope Ridge (PlatformIO 6.1.19) and GitHub Actions
`ubuntu-latest`. **Build output only.**

Identical on both machines: RAM 4.9 % (12280 of 248832), Flash 9.4 % (76748 of 815104), `[SUCCESS]`.

**Verdict: PASS, for a narrow claim** — the toolchain, the vendored board definition and the
Adafruit nRF52 framework produce a linkable image, reproducibly, on a machine that has never seen
the project before. **Byte-identical sizes across two hosts indicate the build does not depend on
local machine state**, which was the specific risk in RAK's alternative "copy files into
`~/.platformio`" approach.

<!-- Template:

### YYYY-MM-DD — one-line summary

**Host:** Heliotrope Ridge. **Commit:** `abc1234` (asserted from the banner / inferred / NOT
OBSERVED). **Image:** `env:...`. **Verdict:** PASS — H2 satisfied / FAIL / INCONCLUSIVE.

**Measured:** sleep current between wake cycles, on battery, USB detached.

```
<raw reading / log excerpt>
```

**Not established:** the one line a later reader needs so they do not over-read this.

CITE(...): only where this entry is the source of something cited elsewhere.

-->
