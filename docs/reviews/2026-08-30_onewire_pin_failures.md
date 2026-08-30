# Why the one-wire GPIO keeps dying — design review, 2026-08-30

> **Superseded in part, 2026-08-30 (later).** The count is **seven pads across two cores**, not
> five across four. The verdict below — back-powering an unpowered core — is **retracted as the
> established cause**: it predicts a pin shorted to VDD, and every measured pad reads short to
> ground [CIT-NRF-GNDLIFT]. The 1 kΩ series resistor recommended below was inline when `SDA`/P0.13
> died, so it is refuted as sufficient protection. The mechanism that matches the signature is loss
> of the ground return making the data pin carry the core's supply current [CIT-NRF-GNDLOSS], and
> that is a candidate, not a conclusion. See
> [#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102) and
> the assembly order in [`../HARDWARE.md`](../HARDWARE.md). The analysis below is kept because its
> refuted branches are worth not re-deriving.

Five pads across four cores have now been declared dead on the RAK9154 one-wire link. This
review asks the only question worth asking: is the design doing this, or is it bad luck?

## Verdict — the design is doing it, and Nordic documents the mechanism

**Cause: the pack's data line is energised while the core is unpowered.** Nordic states the rule
and the consequence directly [CIT-NRF-BACKPOWER]:

> "Max voltage on any GPIO is VDD + 0.3 V. Meaning that for an **unpowered** device, max GPIO
> voltage is **0.3 V**. Any voltage above this level will make the ESD protection diode conduct
> and you will backpower the device via the GPIO."

And on what that does to the pin, in a thread whose title is our exact symptom — *"SWDIO pin
shorted to ground"*, on a chip that kept running its last firmware [CIT-NRF-PINSHORT]:

> "If you tried powering a circuit via SWDIO or another GPIO pin, the **high current flow can
> permanently damage the pin.**"

The pack idles its data line at a logic level referenced to `3V3_In`, which this build takes from
the always-on `VDD` pad. The pack does not know or care whether the core has power. So whenever the
core is unpowered with the harness mated, the data line is roughly 3.3 V into a 0.3 V absolute
maximum — **an order of magnitude out of spec** — and the current flows in through the pad's ESD
diode, into the core's decoupling capacitance, through a diode never sized to carry it.

### Why this happens constantly on 002 and never on 001

**The buck feeds the core through the USB-C connector, so the buck and a host cable are mutually
exclusive.** Every swap between bench power and pack power therefore contains a window where the
core has no power and the pack is still mated. That window is not an accident or a mistake — it is
structural to how this node is powered, and it opens on *every* swap.

- **002** has been swapped between host USB and the buck a dozen times in one day of bring-up.
- **001** was wired once, deployed, and has never been swapped since.

That is the whole difference. It is not the firmware, not `owscan`, and not the protocol: both
nodes ran the same images against the same library, and `owscan` drove 001's `IO1` on two separate
occasions with no harm (fact 14).

### The correlation that names the pin

| Core | Pad carrying the pack | Outcome |
|---|---|---|
| three earlier cores | `IO1` (P0.17) | **dead** |
| node 002 | `A1` (P0.31) | **dead** |
| node 002 | `A0`, `SCL` — never connected to the pack | healthy |
| node 002 | `SDA` (P0.13) — connected 2026-08-30, few swaps so far | working |

**Four dead pads, four one-wire pads. The pin that dies is always the pin the pack is on.**

This supersedes the header-geography reading, which noted that every dead pad sits on the
`BAT IO2 IO1 A1 IN1` row while every healthy pad sits elsewhere. That correlation is real but
**incidental** — `IO1` and `A1` were on that row because that is where the one-wire pad happened to
be, not because the row is hazardous. The one-wire correlation is 4/4 and has a documented
mechanism; the geography correlation has neither a mechanism nor a way to explain `SDA` surviving
the same treatment. H0 below (the 12 V rail bridging the header) is not refuted, but it is no
longer needed to explain anything, and it cannot explain the three `IO1` cores that died with no
12 V ever present.

### The fix is a procedure, and it is free

**Never let the harness be mated while the core is unpowered.** Concretely, and in this order:

| Going to bench USB | Going to pack/buck |
|---|---|
| 1. Unmate the pack connector **first** | 1. Connect the buck **first** |
| 2. Then remove buck power | 2. Then mate the pack connector |
| 3. Then plug in host USB | 3. Then confirm the boot banner |

The series resistor in [`HARDWARE.md`](../HARDWARE.md) is now justified by a mechanism rather than
by caution: Nordic's damage mode is **current** through the ESD diode, and 1 kΩ bounds it to about
3 mA — survivable indefinitely — where a bare wire bounds it only by the diode's own resistance.
Fit it. It converts a procedural mistake from fatal to harmless, which matters because the
procedure above will eventually be forgotten.

The pull-up must reference the **board's own** `VDD`, never an external rail, for the same reason
[CIT-PARTICLE-BACKPOWER]:

> "If the main power supplies are cut off, it is possible to 'back power' the NRF via pull-ups on
> the I2C or GPIO pins. Always make sure your pull-ups are connected to the 3V3 pin on the device,
> and not to an external power rail."

### The second kill path was measured and is closed

The RAK19007's USB-C `VBUS` absolute maximum is **5.5 V**, feeding a TP4054 charger behind a series
diode with **no documented overvoltage clamp** [CIT-RAK19007-DS], and this build drives that
connector from a generic adjustable buck. On 2026-08-30 the buck was finally metered:
**4.98 V** at nominal pack voltage. Inside spec. **Overvoltage from the buck is refuted**
([`EVIDENCE.md`](../EVIDENCE.md) 2026-08-30).

I additionally argued that the danger was input sag as the pack discharged. **Retracted.** That
rested on a capture of a module set for 3.3 V out whose sawtooth appeared near an ~11.8 V input
threshold; an MP1584 set for 5 V needs about 6 V in, and the pack's protection — never mind this
firmware's 9.60 V inhibit — cuts off far above that. The input cannot reach that regime here.

**So the whole-core USB failure on node 002 remains unexplained.** Back-powering explains one dead
pad and predicts the rest of the chip living; it does not predict a core absent from the host's USB
tree. RAK's guidance that a regulated supply belongs on the **P2 "Green Power" connector rather than
USB-C** [CIT-RAK-GREENPOWER] is still worth adopting, but for the sequencing reason in this review,
not because the buck was found guilty.

### How to tell whether the dead core is recoverable

RAK's own discriminator [CIT-RAK-USBDEAD]: a missing SoftDevice kills USB while leaving SWD
working, so **if SWD connects, the core is software-bricked and recoverable; if SWD does not
connect, the chip is damaged.** Do not conclude "dead chip" from USB silence alone — every
documented RAK4631 no-enumeration case in the forum corpus turned out to be bootloader or
SoftDevice, not silicon.

### Three refuted hypotheses, recorded so they are not re-derived

**R1 — Ground-reference float. Wrong.** I proposed that the pack's ground reaches the core only
through the buck or the RAK5802's `GND` terminal, and therefore floats on host USB. The buck is a
**non-isolated** converter: input and output grounds are common, so `P−` is bonded to board ground
through the buck whenever the buck is *wired*, converting or not. Host USB does not break that
bond. Facts 19–21 do not support the conclusion I drew from them.

**R2 — `VDD` at 5 V overdriving the pack's reference. Wrong.** `VDD` on the RAK19007 J12 header
is **3.3 V**, generated by the core module and present whenever the board is powered from USB or
battery [CIT-RAK19007]. It is the MCU's own GPIO rail, so the pack's `3V3_In` sits at the same
potential as the nRF52's I/O — which is the correct arrangement, not a hazard.

**R3 — A miswired replacement harness. Wrong.** The operator's pin-by-pin description on
2026-08-30 matches the documented build: pin 1 to buck VIN+ and RK900 V+, pin 2 to buck negative,
RK900 negative **and the base board's `GND` pad**, pins 3+5 joined to the one-wire pad, pin 4
alone to `VDD`. The data pair is correct and the reference is correct.

**Correction to `HARDWARE.md` arising from R3:** the harness table (:170) lists `P−` as going to
"buck negative **and** RAK5802 `GND` **and** RK900 negative" and omits the base board `GND` pad
entirely. As built, `P−` lands on the base board `GND` pad and the RAK5802 `GND` terminal is not
in the path. The table understates the ground bond, which is what led me to R1. Fixed in the same
commit as this review.

Node 001 is built to this same design and has been alive in the field for weeks, and
`owscan` ran against 001's IO1 repeatedly in August with no damage — so the design is survivable
and something specific to the later builds is doing the killing. On node 002 the timeline is
tight enough to name: **A1 read the pack successfully at 15:08:48Z and was dead by 16:45Z, and
the only thing that happened in between was the physical assembly that introduced the 12 V pack
rail into the enclosure.**

A second candidate is cheap to check and cannot be ruled out yet: the `BAT` pad sits on the same
2.54 mm row as the pads that die — the silkscreen reads `BAT IO2 IO1 A1 IN1`. Battery voltage is
three pads from `A1` and two from `IO1`, on a row that gets hand-soldered.

Two further findings stand on their own and should be fixed either way:

- **The interface has no current limiting and both ends are push-pull outputs**, so a
  transmit collision is a hard short. This is a real design defect even if it turns out not to be
  what killed these pads.
- **`IO1` may never have been a free pin.** It is a module-slot signal, not a spare GPIO. If that
  is what its held-low readings mean, three of the discarded cores were never damaged at all.

Which of these killed which pad is **not** established. The mechanisms are established from
source, datasheet, and the repo's own timeline; the attribution is not, and the measurements at
the end are ordered to settle it cheaply.

## What is actually established

Facts, each with the thing that establishes it. No inference in this section.

| # | Observation | Source |
|---|---|---|
| 1 | Our end of the wire is a **push-pull output**. `setTX()` is `digitalWrite(tx, HIGH); pinMode(tx, OUTPUT);` and `write()` drives the port register with `*reg \|= reg_mask` / `*reg &= inv_mask` for every bit. There is no open-drain configuration anywhere in the library. | `SoftwareHalfSerial.cpp` @ `c58c0f0` lines 150–198, 330–341 [CIT-ONEWIRE-SERIAL] |
| 2 | **Nothing limits current** between the pack's data pin and the nRF52 pad. No series resistor appears in the harness build, the pad table, or the connector pinout. | [`HARDWARE.md`](../HARDWARE.md) §pack harness |
| 3 | Our own comments call this line "open-drain" in at least four places. | `src/sensors/battery.cpp` :78, :286, :916, and the `owscan.cpp` phase-1 header |
| 4 | The pack's **data-line logic level has never been established** — not from a datasheet, not from a measurement. `docs/research/rak9154-battery-protocol-sources.md` contains no level information. | grep of `docs/` for level/voltage terms, 2026-08-30 |
| 5 | On node 002's core: `A1` (P0.31) reads **100 % LOW against the internal pull-up** with the original harness, with a second harness, and with the harness completely removed. 1,823,490 of 1,823,490 samples. | `env:owscan_a1` @ `0d5fc39`, captures 09:45–10:01Z |
| 6 | On the same core, `IO1` (P0.17) reads **100 % LOW**, 1,822,564 of 1,822,564 samples, while wired to nothing. | `env:owscan` @ `15a9c1a`, capture 09:52Z |
| 7 | On the same core, `A0` (P0.5) reads **idle HIGH**, 0 of 1,852,145 samples low. | `env:owscan_a0` @ `0d5fc39`, capture 09:57Z |
| 8 | The census is a valid instrument: it has printed `idle HIGH` with 334 falling edges on a healthy bus, and printed HIGH on A0 today. A steady LOW is a real electrical state. | [`EVIDENCE.md`](../EVIDENCE.md) :1781, and #7 above |
| 9 | Three earlier cores measured **~3.86 Ω from IO1 to ground** unpowered, against ~45 kΩ on A1. | [`EVIDENCE.md`](../EVIDENCE.md), bench isolation 2026-08-29 |
| 10 | `WB_IO1` is **P0.17, a SLOT_A / SLOT_B module pin** — not a general-purpose spare. `WB_A1` is P0.31 on the IO slot. | `rakwireless/variants/rak4630/variant.h` :45, :53 |
| 11 | `A2`, `A3`, `A4` are P0.28/29/30, which are **QSPI and SPI pins** for the on-board flash. `A0` and `A1` are the only analog pads that are not. | same file :86–88, :146–151 |
| 12 | `A0` is **not brought out** on the RAK19007 edge header. The silkscreen is `BAT IO2 IO1 A1 IN1` and `SDA SCL TX1 RX1 GND VDD BOOT0`. | [`HARDWARE.md`](../HARDWARE.md) :161–162, confirmed by the operator |
| 13 | **Node 001 is built to this same design and is working**, in the field, having accumulated 27.37 h of continuous field runtime with the pack live at 11.76 V. | operator, 2026-08-30; [`EVIDENCE.md`](../EVIDENCE.md) :1042 |
| 14 | `owscan` — including its transmitting phases — ran against **001's IO1 (P0.17)** on 2026-08-04 and again on 2026-08-12, and 001's IO1 still works. | [`EVIDENCE.md`](../EVIDENCE.md) :1584, :1715, :2343 |
| 15 | On node 002, A1 read the pack end-to-end at **15:08:48Z** (12.54 V, −0.02 A, 100 %, 22.0 °C, delivered to TTN). In that same uplink **the wind field was absent because the harness was not assembled yet** — 15 bytes, 4 fields, no wind channel. | [`EVIDENCE.md`](../EVIDENCE.md) :111–173 |
| 16 | The assembly happened **after** that read: "later the same morning, with the node fully assembled," `env:stage1` got a full RK900 five-register read. The pack was silent from the next `battdiag_a1` capture onward, 16:28Z. | [`EVIDENCE.md`](../EVIDENCE.md) :140–158; capture 09:28 local |
| 17 | The pack's `P+` rail (~12.5 V) is routed into the enclosure to feed **both the buck and the RK900**, so 12 V is present during assembly. | [`HARDWARE.md`](../HARDWARE.md), commit `92a5434` |
| 18 | `BAT` is on the **same 2.54 mm header row** as the pads that die, two positions from `IO1` and three from `A1`. | fact 12's silkscreen order |
| 19 | The pack's `P−` goes to **the buck negative, the RAK5802 `GND` terminal, and the RK900 negative** — and **not** to the base board's `GND` pad. | [`HARDWARE.md`](../HARDWARE.md) :170 |
| 20 | The pack's `3V3_In` (pin 4) is fed **from the node's `VDD` pad**, so the pack's logic reference comes from the core while its return does not. | [`HARDWARE.md`](../HARDWARE.md) :172, :129 |
| 21 | The RS-485 ground bond is documented as optional-sounding advice: "Worth landing the RS-485 ground here, not just A/B." | [`HARDWARE.md`](../HARDWARE.md) :115 |
| 22 | The buck feeds the node **through the USB-C connector**, so the buck and a host USB cable are mutually exclusive — the node cannot be on both. | [`HARDWARE.md`](../HARDWARE.md), commit `e2c7088`; operator, 2026-08-30 |
| 23 | `A1`/`IO1` are driven only by `battery.cpp` and `owscan.cpp`. `env:stage1`, the image running between the working read and the dead pad, compiles both out (`FEATURE_RK900` only; `FEATURE_BATTERY=0`, no `FEATURE_ONEWIRE_SCAN`). | `platformio.ini` `[env:stage1]`; `src/main.cpp` :306–313 |

## What changed between 001 and 002

This is the question that matters, because 001 works. Differences the repo can establish:

| | Node 001 | Node 002 |
|---|---|---|
| One-wire pad | `IO1` (P0.17) | `A1` (P0.31), then dead |
| Core provenance | factory RAK4631 | **not captured** — die ID never read (#97); the SWD-converted RAK4631-R was discarded and is *not* this core |
| `owscan` exposure | repeated, 2026-08-04 and 2026-08-12, including transmit phases | repeated, 2026-08-30 |
| Pack read before assembly | — | **worked**, 15:08:48Z |
| Pack read after assembly | working in field | **dead**, both `A1` and `IO1` held low |

Fact 14 is what rules the diagnostic out as a sufficient cause on its own: `owscan` transmitted
into 001's IO1 across two separate sessions and that pin is alive today. So the difference is not
the tool and not the protocol — both nodes ran the same firmware against the same library.

Fact 15 paired with fact 16 is what puts assembly in the frame. A1 carried a complete
pack reading to TTN **before the harness existed**, and was held-low afterwards. The interval
between those two states contains exactly one class of event: hands, solder, and a 12 V rail
(fact 17) working on a header row where `BAT` sits three pads from `A1` (fact 18).

## The leading hypothesis — H0, the battery rail reaching the header

*Inference from facts 5, 6, 9, 15, 16, 17, 18.*

A pad exposed to ~12.5 V is destroyed immediately and permanently. The nRF52840's absolute
maximum on any GPIO is VDD + 0.3 V, so 12.5 V is roughly four times the damage threshold
[CIT-NRF-GPIO]. The failure mode for that kind of overstress is a punched-through pad shorted to
the substrate, which reads a permanent LOW that no pull-up can lift and measures a few ohms to
ground.

**That is fact 9 exactly — ~3.86 Ω — and facts 5 and 6.** It is also consistent with the ~9.6 mV
measured on a powered dead pad: once the pad is a short to ground, whatever is feeding it through
the bridge cannot raise its voltage.

What makes H0 fit better than contention:

- **It explains 001 surviving.** A bridge is a build defect, not a design property. A clean build
  never sees it; the design is fine.
- **It explains the timing.** A1 worked at 15:08Z and was dead after assembly. Contention would
  have had equal opportunity to kill it during the exchange that produced the 12.54 V reading.
- **It explains adjacency.** `IO1` and `A1` are neighbours on the row, both dead, and `A0` — not
  on that header at all — is healthy. A single conductive path touching two neighbouring pads is
  one event; two independent protocol failures on adjacent pads is a coincidence.
- **It explains the earlier three cores.** Their harness was soldered at `IO1`, which is two pads
  from `BAT` on the same row.

What H0 does not explain, and I am not going to paper over: **why `IO1` on node 002's core reads
held-low when the operator never wired it.** Either the bridge reached it too, or H3 below is
right and IO1 was never readable with a module installed. Test 3 distinguishes them.

## The mechanism — H1, driver contention

*This is inference from facts 1, 2, and 9.*

A single-wire half-duplex bus works only if neither end can source current. The standard ways to
guarantee that are an open-drain output with a shared pull-up, or a series resistor at each end.
**This design has neither.** Our end idles as a push-pull output driven HIGH — `setTX()` sets the
pin HIGH *before* setting it to OUTPUT, and `write()` returns the pin to HIGH after the stop bit.

So whenever our end drives HIGH while the pack drives LOW, the current path is:

```
3V3 ─ nRF52 pad PMOS (on) ─── wire ─── pack pad NMOS (on) ─ GND
```

Two CMOS output stages in series across the rail, with no resistance between them but their own
on-resistance. That is tens of milliamps at minimum. The nRF52840 absolute maximum is 15 mA per
pin, and absolute maximum is a damage threshold, not an operating point [CIT-NRF-GPIO].

Repeated overstress of a CMOS output typically ends with the pad shorted to the substrate, which
reads as a permanent LOW that cannot be lifted by a pull-up and measures a few ohms to ground.
**That is exactly fact 9 and facts 5–6.** The failure signature matches the mechanism.

Two things make contention likely rather than theoretical:

- **There is no arbitration.** Half-duplex with software timing means any overlap — the pack
  answering early, a wake byte, a retry landing on top of a reply — is a collision. The firmware
  has a `kTurnaroundMs` guard precisely because turnaround timing was already known to be tight.
- **The diagnostic transmits hard.** `owscan` phase 0 sends 64 bytes of `0x55` at three baud
  rates every cycle — 192 bytes of alternating bits, push-pull, per cycle — then broadcasts BOOT
  and SENDAT frames at five baud rates. `0x55` is the worst possible pattern for contention
  because it toggles every bit. If H1 is right, **the tool used to diagnose the bus was also
  stressing it**, and the A1 pad died during a session that ran that tool repeatedly.

Why the reference implementations do not show this: the RAK2560 SensorHub is an integrated
RAKwireless PCB where the pack and the MCU are laid out together, and Meshtastic's driver runs on
that board [CIT-MESHTASTIC-9154]. We copied its firmware and its pin usage onto a hand-wired
harness, and inherited none of its interface circuitry. **The bug is in the part of the reference
we did not copy, which is why reading the reference code never surfaced it.**

## The second finding — H3, IO1 may never have been free

*This is inference from facts 6 and 10, and it deserves as much attention as H1.*

`WB_IO1` is P0.17, and `variant.h` marks it `SLOT_A SLOT_B` — it is a **module slot signal**, not
a spare GPIO. With a module installed in either slot, IO1 is connected to that module, so a
held-low reading on IO1 is not evidence of a damaged pin. It could be the module holding it.

Fact 6 is the uncomfortable one: IO1 read held-low on node 002's core while wired to nothing at
all. Contention cannot explain that, because contention requires something on the other end of
the wire. The RAK5802 was installed during that measurement.

If the RAK5802 (or any slot module) holds IO1 low, then:

- The three cores discarded as "damaged IO1" may have been healthy.
- The ~3.86 Ω in fact 9 was measured with the core installed in a baseboard, and the earlier note
  that "removing the RAK5802 changed nothing" was a resistance check, not this census.
- **We may have thrown away three working $50 cores on a misdiagnosis**, and the design defect
  was choosing a module-slot pin for a sensor link in the first place.

This is a hypothesis with a one-command test, listed below. It is not a conclusion.

## What was wrong in the documentation and the code

- `battery.cpp` describes the line as open-drain in four places. It is not, and it never was.
  Every timing argument built on "the pack has just finished driving this open-drain line" was
  reasoning about a bus that does not exist as described.
- [`HARDWARE.md`](../HARDWARE.md) presents IO1 and A1 as interchangeable pads for this link. They
  are not equivalent: IO1 is a module-slot signal, A1 is an IO-slot pin, and A0 — which the
  firmware was briefly taught to use today — is not on the header at all.
- The pad options are far narrower than assumed. Per facts 11 and 12, the header offers `IO1`
  (module slot), `IO2` (owned by the 3V3_S switch), `A1`, `IN1` (not a core GPIO in the variant),
  `SDA`/`SCL` (P0.13/P0.14, genuinely free), and `TX1`/`RX1` (owned by the RS-485 module). That is
  **two usable pads**, `A1` and `SDA`/`SCL`, and one of them is now dead.
- Rule 20 requires a datasheet or spec citation for any electrical constant. The pack's data-line
  logic level (fact 4) has never had one, and it is as load-bearing as any register address.

## The fix

Three changes. The first two are cheap and independent; either alone removes the failure mode,
and together they make it structurally impossible.

### 1. Series resistor in the data line — do this regardless

A **470 Ω to 1 kΩ resistor** in series with the data wire, at the node end, bounds contention
current to roughly 3–7 mA — under the nRF52840's 15 mA absolute maximum, and low enough that a
collision is a wasted byte instead of accumulated damage. It costs one part and it is the single
highest-value change in this document. At 9600 baud the RC penalty against any realistic line
capacitance is negligible.

### 2. Drive the pin open-drain, so our end can never source current

The nRF52840 supports this natively: `PIN_CNF[n].DRIVE = S0D1` — standard drive on 0,
**disconnected** on 1 [CIT-NRF-GPIO]. With `S0D1` plus a pull-up, our end can pull the line low
and release it, but never drive it high, which is what the code has claimed to be doing all along.
This is a firmware change plus one external pull-up (4.7 kΩ–10 kΩ to 3V3).

It requires either a patch to the vendored library's `setTX()`/`write()` path or setting the DRIVE
field after `begin()` on every transmit — the library reconfigures the pin each direction change,
so the field has to be reapplied, not set once.

### 3. Stop the diagnostic from transmitting into an unqualified pad

`owscan` should run its passive census by default and require an explicit flag before it
transmits anything. The census phases — idle level and falling-edge count — are the phases that
produce the useful evidence, and they drive nothing. Phase 0's 192 bytes of `0x55` per cycle
answers a timing question we have already answered.

## Before another core is connected — five measurements

In this order. **Do not put a new core into node 002's baseboard and harness until test 1 passes**
— if there is a path from `BAT` to those pads, the next core dies the same way within seconds, and
that is how this got expensive.

1. **`BAT` pad to `IO1` pad, and `BAT` pad to `A1` pad. Resistance, everything powered down,
   pack disconnected, core removed from the baseboard.** Expect open. Anything from a few ohms to
   a few tens of kΩ is the answer to this whole review. Test the baseboard on its own first, then
   with the harness plugged in, so a bridge on the board is told apart from one in the harness.
   This is H0 and it costs nothing.
2. **Inspect that header row under magnification** — `BAT IO2 IO1 A1 IN1`. Look for a solder
   bridge, a stray wire strand, flux residue, or a wire pressed across the pads. Compare against
   node 001's header, which is the known-good build of the same thing.
3. **Pack data pin to pack ground, DC volts, harness disconnected from the node, pack powered.**
   Must be ≤ 3.3 V. If it idles at 5 V or higher, every connection has been overstressing the pad
   through its ESD diode and no series resistor makes that safe — it needs a level shifter. This
   is fact 4, the oldest unsourced electrical assumption in the project.
4. **Pack data pin to pack ground, resistance, pack unpowered.** Establishes whether the pack end
   is open-drain-with-pull-up or push-pull, which decides whether fix 2 is sufficient alone.
5. **`env:owscan` with every slot module physically removed.** This tests H3. If IO1 reads
   `idle HIGH` with the RAK5802 out, then IO1 is not damaged on this core, the pin was never free
   with a module installed, and the three discarded cores were probably fine.

Tests 1 and 2 need a meter and a light, not a $50 core. Both should have been run before the
second core went into this baseboard, and certainly before the third.

## What I got wrong

Recorded because the same errors are what cost the cores.

- I concluded "IO1 is damaged on three cores" from a resistance reading without checking what
  P0.17 is connected to on the baseboard. `variant.h` says `SLOT_A SLOT_B` on line 45 and has
  said so the whole time.
- I proposed and half-built an `A0` recovery path without reading the header pinout in our own
  `HARDWARE.md`, which lists the pads and does not include A0. That work is reverted.
- I ran `owscan` repeatedly against a pad I was trying to diagnose, without noticing that its
  phase 0 transmits 192 bytes per cycle into a bus with no current limiting. If H1 is right, the
  diagnostic contributed to the damage it was measuring.
- I wrote "open-drain" into the battery driver's comments and reasoned from it for weeks without
  reading `setTX()`, which is nine lines long and says `pinMode(tx, OUTPUT)`.
- I did not compare against node 001 until the operator told me to. 001 is the same design,
  working, with `owscan` history on the exact pin that supposedly cannot survive — which is the
  single strongest piece of evidence available and it was sitting in `EVIDENCE.md` the whole time.
  A design-defect verdict that cannot explain the working unit is not a verdict.
- I blamed the harness, then the pack, then the silicon, and reached for a new pin each time,
  before checking whether a 12 V rail runs along the same header row as the pads that keep dying.
  Three cores were replaced without that check.

## Citations

- CITE(datasheet): [CIT-NRF-GPIO] nRF52840 Product Specification, GPIO — `PIN_CNF[n].DRIVE`
  provides `S0D1` (standard 0, disconnect 1) for open-drain operation, and the per-pin absolute
  maximum current is the damage limit relied on above.
- CITE(prior-art): [CIT-ONEWIRE-SERIAL] `RAK-OneWireSerial` @ `c58c0f0`,
  `src/SoftwareHalfSerial.cpp` :150–198 and :330–341 — `write()` drives the port output register
  per bit and `setTX()` configures the pin as `OUTPUT`, establishing push-pull, not open-drain.
- CITE(prior-art): [CIT-MESHTASTIC-9154] `meshtastic/firmware` @ `02050a4`,
  `variants/rak2560/RAK9154Sensor.cpp` — the same library driven on an integrated RAK2560 PCB,
  which is why the reference shows no interface protection to copy.
- CITE(bench): [`EVIDENCE.md`](../EVIDENCE.md) 2026-08-29 isolation and 2026-08-30 censuses —
  the ~3.86 Ω / ~45 kΩ readings and the A1/IO1/A0 sample counts quoted throughout.
