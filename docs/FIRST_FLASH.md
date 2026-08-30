# First flash

Bring-up order for the first time this firmware meets real hardware. Follow it in
sequence — each stage adds exactly one subsystem, so when something stops working there is
only one candidate.

The alternative is flashing the full image, watching nothing happen, and having six
suspects at once: wiring, sensor address, baud rate, network keys, the payload encoder, or
sleep. That is a bad afternoon. This is the same work in an order that isolates faults.

Everything here is procedure, not evidence. Record what actually happens in
[`EVIDENCE.md`](EVIDENCE.md) as you go — nothing in this project may claim a result that
was not observed.

## Before you plug anything in

- [ ] **Network keys.** Copy `src/secrets.example.h` to `src/secrets.h` and paste the
      DevEUI, JoinEUI, and AppKey from the TTN console. The file is gitignored and must
      stay that way. Without it the build warns and the node will never join.
- [ ] **Register the device in TTN** with the same three values, US915, OTAA, LoRaWAN 1.0.3.
- [ ] **Install the payload formatter** from `payload/reference/rak-wx-station-default.js`
      into the TTN application, so the first uplink is readable rather than hex.
- [ ] **Antenna attached.** Transmitting without one can damage the radio, and the first
      thing stage 3 does is transmit.
- [ ] **Check the buck output with a meter before it reaches the WisBlock.** It should read
      5 V. It is easier to be wrong here than anywhere else on the bench, and the mistake
      is not recoverable.
- [ ] **Pin 4 of the 5-pin socket is a 3.3 V reference.** Never tie it to 5 V. See
      [`HARDWARE.md`](HARDWARE.md) — and note that the pinout is still to be confirmed with
      a meter, because the two sources disagree about it (issue #6).

## Is the board actually running your firmware?

Ask this before reading anything into a serial capture. The USB product ID answers it, and
nothing else reliably does — a DFU failure and a clean flash print much the same thing, and
`pio run -t upload` exits 0 either way (issue #27).

| VID:PID | Description | What it means |
|---|---|---|
| `239A:8029` | `WisCore RAK4631 Board` | **Application running.** This is the only ID that means the flash landed. |
| `239A:0029` | `WisBlock RAK4631` | UF2 bootloader — **no valid application**. Double-tap RESET put it here, or a DFU failed. |
| `239A:002A` | `WisBlock RAK4631` | Serial-only DFU bootloader — **no valid application**. Seen 2026-07-31 after an interrupted flash. |
| `239A:802A` | — | Application with a CDC-only descriptor. Not what this project builds. |

`8029` is the application PID the RAK Arduino board definition builds against; `0029` and
`002A` are the Adafruit nRF52 bootloader's UF2 and CDC-only IDs
([CITE(datasheet): RAK-nRF52-Arduino `boards.txt`](https://github.com/RAKWireless/RAK-nRF52-Arduino/blob/master/boards.txt),
[CITE(prior-art): Adafruit\_nRF52\_Bootloader `board.h`](https://github.com/adafruit/Adafruit_nRF52_Bootloader/blob/master/src/boards/feather_nrf52840_express/board.h)).

Read it on the build host:

```
./scripts/remote.sh usbpid      # prints just the PID
./scripts/remote.sh devices     # ports plus full hardware IDs
```

**The trap this exists to close.** On 2026-08-03 a DFU failed, PlatformIO printed
`[SUCCESS]`, the harness printed `=== FLASH OK ===`, and the serial capture that followed
was 0 bytes — from a board in its bootloader with nothing to run. A 0-byte log from a board
with no firmware is indistinguishable from a 0-byte log from a silent or miswired sensor,
and the obvious reading of it costs the next session an afternoon on RS-485 polarity that
was never the problem. `flash.sh` now checks the PID after every upload and refuses to
report success without `8029`. Check it by hand before trusting any capture you did not
watch land. See [`EVIDENCE.md`](EVIDENCE.md).

**Recovery when the PID is `0029` or `002A`:** double-tap RESET on the RAK19007 to re-enter
DFU cleanly, then re-run `flash.sh`. Nothing else is needed and nothing is lost.

### An absent USB device does not mean a dead board

Since the sleep path detaches the USB device (`docs/FIRMWARE_SPEC.md` §5 step 7), a **sleeping
node has no `239A` device on the bus and no `/dev/cu.usbmodem*` at all.** That is the fix
working, not a failure.

This matters because `flash.sh`'s failure text — *"no 239A device on the bus at all"* →
*"THE BOARD HAS NO VALID APPLICATION"* — is only true immediately after a failed upload. Run it
against a node that is simply asleep and it reads like a brick. On 2026-08-05 that reading was
made twice in one session before TTN showed the node had uplinked 77 seconds earlier.

Before concluding a board is dead, check the two observers that do not depend on USB:

```
ttn-lw-cli end-device get my-app-tobi puma-concolor-001 --session --mac-state | grep last_f_cnt_up
date            # compare against the session's updated_at, in UTC
```

A `last_f_cnt_up` that advanced within the last interval means the application is running fine
and the console will return at the next wake. Wait for it rather than reflashing.

### When serial DFU fails but the bootloader is present

Symptom: the PID is `0029`, the port exists, nothing holds it (`lsof`), and the upload still
fails with *"No data received on serial port. Not able to proceed."* Two attempts is enough to
stop retrying — the serial DFU transport is not going to start answering.

The UF2 mass-storage route works when the serial one does not, and it is what recovered the
board on 2026-08-05 after two clean failures. **macOS does not always auto-mount the
bootloader's drive**, which is the step that makes this look unavailable when it isn't:

```
diskutil list | grep -i rak                 # find it -- e.g. /dev/disk6, "RAK4631", ~33 MB
diskutil mount disk6                        # macOS often leaves it unmounted
ls /Volumes/RAK4631/                        # expect CURRENT.UF2, INFO_UF2.TXT, INDEX.HTM

python3 ~/.platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py \
    .pio/build/rak4631/firmware.hex -c -f 0xADA52840 -o /tmp/fw.uf2
cp /tmp/fw.uf2 /Volumes/RAK4631/
```

`0xADA52840` is the UF2 family ID for the nRF52840 with a SoftDevice; the converter reports
`start address: 0x26000`, which is the application offset above the SoftDevice — if it reports
anything else, stop, because the image will not be bootable
([CITE(prior-art): Adafruit\_nRF52\_Bootloader UF2 family](https://github.com/adafruit/Adafruit_nRF52_Bootloader)).

The bootloader flashes on write and resets itself; the drive disappears and the application
comes up within about 12 seconds. Confirm with the PID table above before reading any capture.

**Verify the binary is not stale before converting.** `pio run` reused a `firmware.hex` from an
earlier commit on 2026-08-05 even though `src/sensors/battery.cpp` had changed underneath it, so
a UF2 built from it would have carried the wrong commit into an evidence entry. Check the mtime
against the sync, or `rm -rf .pio/build/<env>` first.

## Converting a RAK4631-R (RUI3) board over SWD

A board bought as **RAK4631-R** ships with RUI3, not the Arduino bootloader, and enumerates as
**`1915:521F`** instead of a `239A` ID. Nothing in this project can flash it: `adafruit-nrfutil`
speaks the Adafruit serial DFU protocol, RUI's bootloader does not, and Nordic's `nrfutil dfu`
never gets past MTU negotiation over USB. The board needs the Arduino bootloader written over
SWD with a RAKDAP1, and then it behaves like every other board here
([CITE(datasheet): RAK4630 Bootloader Update Manual](https://raw.githubusercontent.com/RAKWireless/WisBlock/master/bootloader/RAK4630/README.md) —
`CIT-RAK-BOOTLOADER`, which also notes SWD attaches to **the module**, not the base board).

**The one command that works.** Everything is in a single OpenOCD session on purpose:

```
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg \
  -c "adapter speed 1000" \
  -c "nrf52.cpu configure -work-area-size 0" \
  -c "init" -c "halt" \
  -c "program rak4631-arduino-s140.hex verify" \
  -c "reset run" -c "exit"
```

Expect `** Programming Finished **` then `** Verified OK **`, and expect it to take about
**200 seconds** — roughly 30× slower than a normal flash. That is the cost of the fix, not a
symptom. Then confirm `239A:0029` on the bus and flash the application over USB as usual.

`cannot read IDR` immediately after `reset run` is **success, not failure** — see below.

### Two traps, and how much time each one costs

Both of these produced hours of misleading failures on 2026-08-30 before the mechanism was
understood. Neither looks like what it is.

**1. The core cannot run OpenOCD's flash loader, because it is in lockup.** The default nRF5
write path downloads a small program into RAM and *executes it on the target*
(`CIT-OPENOCD-NRF5`, `nrf5.c:1143`). A mass-erased part has no valid vector table, so the CPU
double-faults into lockup at `pc 0xfffffffe` and the loader never runs. The visible failure is
`error waiting for target flash write algorithm` and `Failed to write to nrf5 flash` at offset 0,
after a connection that otherwise looks perfect — DPIDR read, `Cortex-M4 r0p1 processor detected`,
`Mass erase completed.` **`configure -work-area-size 0` denies OpenOCD the RAM scratch area, which
makes it fall back to writing one word at a time directly through the debug port** (`nrf5.c:1145`).
No target code executes, so lockup stops mattering.

**2. Every reset re-arms the software half of APPROTECT.** Access port protection has two halves:
the hardware half follows `UICR.APPROTECT`, but the software half is re-armed by the CPU on *every*
reset unless the running firmware writes `SwDisable` to `APPROTECT.DISABLE` at boot
(`CIT-NRF-APPROTECT`). A freshly erased part runs no such firmware. So an `ERASEALL` opens the
debug port, and the very next `reset halt` closes it again — and the session that follows shows
`SWD DPIDR` succeeding with **no core detected at all**, because only CTRL-AP survives.

The practical rule: **do not reset between erasing and programming.** A sequence like
`init; reset halt; nrf5 mass_erase; reset halt; program ...` re-locks the part in the middle of
its own recovery. Note also that erasing UICR leaves APPROTECT *disabled*, not enabled — the
opposite of the intuitive reading, and worth checking rather than assuming, via
`nrf52.dap apreg 1 0x0c` (CTRL-AP `APPROTECTSTATUS`; `1` means open).

Two corollaries that also cost time:

- **Do not hold the core in reset via CTRL-AP** (`apreg 1 0x00 0x01`) hoping to stop the lockup.
  It makes every subsequent AP access return `stalled AP operation, issuing ABORT`. Let the core
  sit in lockup; with no working area it is never asked to do anything.
- **`cannot read IDR` after `reset run` is the expected end state.** The freshly written
  bootloader boots and re-arms APPROTECT exactly as designed. Read it as confirmation the image
  is running, not as a failed flash — the preceding `** Verified OK **` is the real verdict.

### What not to do, learned the hard way

Recorded because each of these was tried, wasted time, and produced no evidence:

| Approach | Why it fails |
|---|---|
| Retrying the identical failing command in a loop | The 100 kHz clock and lockup were constant; 49 identical attempts produce 49 identical failures. `.cursor/rules/00-agent-liveness.mdc` caps this at two for a reason. |
| Lowering the adapter clock further | The failure is the core not executing, not signal integrity. 100 kHz was *slower* than needed and changed nothing. |
| `pyocd flash -M under-reset` | Holding reset costs the AP: `SoCTarget has no selected core`. |
| `pyocd flash` at 1000 kHz | `Unexpected ACK '0'` — pyOCD is less tolerant of clock here than OpenOCD, which is fine at 1000 kHz. |
| Blaming loose hand-held probe wires | Plausible, and it was genuinely unstable early on, which masked the real cause. **A stable connection changes the error message; it does not fix a core that cannot execute.** Get the wires mechanically secure first *so that the remaining error is trustworthy*, then read it. |

## Stage 1 — wind sensor only

Radio off, battery reader off, no sleep. Powered from USB, printing to the serial monitor.

```
./scripts/flash.sh --env stage1
```

`flash.sh` builds first, so there is no separate build step. It runs on the build host —
the node is plugged in there and nowhere else.

**Expect:** a line per cycle with five weather fields.

**If every read times out**, in order of likelihood: RS-485 A and B swapped (harmless, and
the usual cause — try swapping them), the sensor not powered, the wrong slave address, or
the wrong baud rate. The firmware fixes the RK900 at **9600** 8N1, slave `0x01` — not the
4800 on the datasheet, because this physical unit answers only at 9600
([ADR-0006](decisions/ADR-0006-rk900-baud-and-register-map.md)).

**If reads succeed but the numbers are wrong**, that is a register map or scaling problem,
not wiring. Record the raw values and check them against `FIRMWARE_SPEC.md` §2.1.

Do not move on until several consecutive cycles read clean. An intermittent sensor here
becomes an intermittent everything later.

## Stage 2 — add the battery

```
./scripts/flash.sh --env stage2
```

**Expect:** four more fields — voltage, current, state of charge, temperature.

Three things need confirming here, and only the hardware can answer them — issues #3, #4,
and #5 respectively:

- **Current sign.** Let the pack charge in sunlight and watch which way the number moves.
  The firmware spec and the TTN decoder disagree about which direction means charging;
  whichever the hardware says is correct, and the loser gets corrected.
- **Temperature scale.** It should read close to room temperature. If a 20 °C bench reports
  about 2, the value is in whole degrees and `src/payload.cpp` needs a factor of ten.
- **Voltage plausibility.** A 10.8 V nominal pack should read somewhere between roughly
  9 and 12.6 V. Anything outside that means the scaling is wrong.

**If the battery does not answer at all**, the one-wire bridge between TXD and RXD is the
first thing to check. The 4-pin socket remains a documented fallback — see
[ADR-0004](decisions/ADR-0004-bms-one-wire-path.md).

## Stage 3 — add the radio

Still no sleep, so the serial monitor keeps working and you can watch the join.

```
./scripts/flash.sh --env stage3
```

**Expect:** a join, then an uplink each cycle, then the decoded fields appearing in the TTN
console.

**If the join never succeeds:** keys are wrong or byte-order-reversed, the region is not
US915, or there is no gateway in range. The console shows join requests arriving if the
radio side is working at all — that single fact splits the problem cleanly in half.

**If it joins but the console shows nothing**, check the port number and the formatter.

**Compare the decoded values against what stages 1 and 2 printed over serial.** This is the
only moment where both ends are visible at once, and it is the one check that catches a
payload encoding error. A field encoded at the wrong width shifts everything after it, and
the result decodes without complaint into numbers that look entirely reasonable. If the
console disagrees with the serial output, stop and fix the encoder before going further —
after this point you lose the serial side of the comparison.

**Then reset the board while watching the console.** It should resume uplinking without a
second join, because the session is stored in flash. A rejoin here means session
persistence is not working, which matters much more in the field than it looks on a bench:
it is the difference between a reset costing one missed reading and a reset costing every
reading until a gateway happens to be reachable.

## Stage 4 — the full image

Sleep and watchdog on. Serial goes quiet between cycles by design — that is the point.

```
./scripts/flash.sh
```

**Expect:** an uplink per interval and nothing in between. Measure the sleep current now;
everything downstream of that number in [`POWER_BUDGET.md`](POWER_BUDGET.md) is a
placeholder until it is real. Tens of microamps is the target. Milliamps means something
did not go to sleep — the USB peripheral and the serial port are the usual culprits.

**Recovery.** If the full image misbehaves badly enough that the board stops enumerating,
double-tap reset to enter the bootloader and `./scripts/flash.sh --env stage1` to get back
to a known state.

## Before it goes in the woods

- [ ] Set a short interval and let it run 24 hours on the bench. Look for gaps.
- [ ] Confirm it survives a power cut mid-uplink.
- [ ] Confirm it recovers on its own after the gateway has been off for a day.
- [ ] Set the real interval by downlink and confirm the change survives a reset.
- [ ] Freeze `payload/schema.yaml` and note the firmware version in
      [`EVIDENCE.md`](EVIDENCE.md).
