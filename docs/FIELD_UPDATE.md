# Reflashing a sealed field node

Moved from `HARDWARE.md` 2026-09-05 unchanged; the hardware file keeps only the consequence
(the enclosure may be sealed with USB-C occupied by the buck).

The power decision — 12 V from the pack into a buck, buck out to the RAK4631's **USB-C**
port — occupies the only connector the node is currently flashed through. This section
records what firmware update paths survive that, so the enclosure is not closed on a node
that can never be changed.

**Verdict: yes, firmware can be pushed to a sealed node — over Bluetooth, from a phone
standing next to the box. It is not free, and it has to be designed in before the lid goes
on.** The decision to make before sealing is in §5 below.

## 1. The bootloader already does BLE OTA DFU

The RAK4631 ships the Adafruit nRF52 UF2/CDC/**OTA** bootloader, and RAK builds and
publishes it themselves at **V0.4.4 with SoftDevice S140 6.1.1**. RAK's own bootloader
manual documents "Update over BLE" as a first-class path alongside USB, J-Link and RAKDAP1,
using **nRF Connect for Mobile** and a **`.zip` DFU package**
[CITE(datasheet): RAK4630 Bootloader Update Manual V0.4.4 — CIT-RAK-BOOTLOADER].

The V0.4.4 release note reads as though it were written for this deployment:

> BLE OTA resiliency: if a BLE OTA firmware update fails or is interrupted, the device stays
> in BLE OTA / DFU mode so another attempt can be made (instead of getting stuck in USB
> DFU/UF2 boot mode). Recommended for Meshtastic / MeshCore users who update firmware over
> BLE when the device is hard to access. [CIT-RAK-BOOTLOADER]

That is the property that makes this safe to rely on. A half-finished OTA leaves the node
advertising for another attempt rather than waiting for a USB cable that is on the other
side of a sealed gasket.

**Confirm the bootloader version on the actual board before sealing.** Double-tap RESET and
read `INFO_UF2.TXT` from the `RAK4631` drive; it prints `UF2 Bootloader 0.4.4` and
`SoftDevice: S140 6.1.1` [CIT-RAK-BOOTLOADER]. This was **not** verified on our unit — the
build host was offline when this was written — and the resiliency behavior above is a
V0.4.4 feature, so an older bootloader is a materially worse position. That check is a
prerequisite of the decision in §5.

## 2. Entering BLE DFU does not require touching the board

This is the part that decides whether a sealed box is workable, and the answer is in the
bootloader source. The application writes a magic value to the `GPREGRET` retained register
and performs a soft reset; the bootloader reads it in `check_dfu_mode()` and comes up in the
requested mode:

```c
// CITE(prior-art): Adafruit_nRF52_Bootloader src/main.c, DFU_MAGIC_OTA_RESET [CIT-ADA-BOOTLOADER-MAIN]
NRF_POWER->GPREGRET = 0xA8;  // DFU_MAGIC_OTA_RESET -> bootloader comes up in BLE OTA DFU
NVIC_SystemReset();
```

`0xA8` is `DFU_MAGIC_OTA_RESET`; `0x57` is UF2, `0x4e` is serial-only, `0xB1` is the
OTA-from-application jump used by the `BLEDfu` service
[CITE(prior-art): `src/main.c` GPREGRET magics — CIT-ADA-BOOTLOADER-MAIN]. **No double-tap
RESET, no button, no cable.** The trigger can therefore be a LoRaWAN downlink — which the
node already has to handle for the interval command — so the update path costs nothing until
the day it is used.

Two properties of that path have to be respected:

- **The bootloader's OTA mode has no timeout.** On the OTA branch the call is
  `bootloader_dfu_start(_ota_dfu, 0, false)`, and `0` is an infinite window; the UF2 and
  serial branches get 3000 ms and a fall-back-to-application flag, the OTA branch does not
  [CIT-ADA-BOOTLOADER-MAIN]. A node commanded into BLE DFU that nobody connects to **stays
  there, advertising, until the pack dies.** That directly contradicts the standing rule in
  `AGENTS.md` — never let the pack reach a state it cannot recover from by itself. The
  trigger must therefore be a deliberate, confirmed downlink sent while somebody is standing
  at the node, never an automatic or scheduled entry.
- **RAK's documented application-side route is not the cheap one.** RAK's `ble_ota_dfu`
  example gets there with `BLEDfu bledfu; bledfu.begin();` and
  `Bluefruit.Advertising.start(0)` — "Don't stop advertising after n seconds", i.e. advertise
  forever at `setTxPower(4)`
  [CITE(prior-art): `ble_ota_dfu.ino` — CIT-RAK-BLE-OTA]. That permanent advertising load is
  exactly what issue #19 rejected. The `GPREGRET` route above reaches the same bootloader
  without it.

**This is not what issue #19 decided.** #19 closed as "no BLE on the field node" on power and
usability grounds, and that reasoning is about the *application* advertising continuously.
Bootloader OTA DFU is a different thing: the radio is only on after a deliberate reset into
the bootloader, so there is **no standing power cost while the application is running**. #19
does not settle this, and should not be cited as if it did.

## 3. The package the phone needs is already a build artifact

No new tooling and no manual `adafruit-nrfutil dfu genpkg` step is needed. The vendored board
definition sets `"protocol": "nrfutil"` and `"sd_fwid": "0x00B6"` (S140 6.1.1) in
`rakwireless/boards/rak4630.json`, and for that protocol PlatformIO's nRF52 builder makes the
default build target a `PackageDfu` builder with `suffix=".zip"`, whose action is

```
adafruit-nrfutil.py dfu genpkg --dev-type 0x0052 --sd-req 0x00B6 --application firmware.hex firmware.zip
```

[CITE(prior-art): platform-nordicnrf52 `builder/main.py`, `PackageDfu` — CIT-PIO-NRF52-BUILDER].

So `pio run` already writes **`.pio/build/<env>/firmware.zip`**, and that is the file nRF
Connect for Mobile expects [CIT-RAK-BOOTLOADER]. The `--sd-req 0x00B6` in the package matches
the S140 6.1.1 the RAK bootloader ships [CIT-RAK-BOOTLOADER], which is what makes the
generated package acceptable to it. `adafruit-nrfutil` comes from PlatformIO's bundled
`tool-adafruit-nrfutil` package, not a separate install [CIT-PIO-NRF52-BUILDER].

Getting the `.zip` onto the phone is the only new logistics: build on Heliotrope Ridge, then
move `firmware.zip` to the handset.

## 4. The alternatives, and why they lose

| Path | Needs the box opened? | Verdict |
|---|---|---|
| **BLE OTA DFU from a phone** | No | **Recommended.** Bootloader capability, zero standing cost, package already built |
| USB-C cable swap (buck out, host in) | Yes, unless a pigtail is brought out | Viable only as the pigtail variant below |
| Powered hub / USB-C splitter so buck and host coexist | — | **Impossible.** See below |
| SWD via base-board test points | Yes | Not available on the RAK19007 |
| LoRaWAN FUOTA | No | **Not viable.** Airtime budget is short by orders of magnitude |

**A hub or splitter cannot make power and data coexist on that port.** The RAK4631's USB-C is
a **device** port: the connector wires straight through to the nRF52840's USBD peripheral,
which is a device-only peripheral with no host or dual-role mode
[CITE(datasheet): nRF52840 Product Specification, USBD — CIT-NRF-USBD]. On the base board,
`USB+`/`USB–` are pins 7 and 8 of the WisBlock connector and `VBUS` is pin 9, and the
datasheet states the Type-C interface "directly communicates with the connected WisBlock Core
module" and doubles as the charging input
[CITE(datasheet): RAK19007 datasheet, Type-C USB port — CIT-RAK19007-RAW]. There is one D+/D−
pair and one VBUS, and only one thing can be on the other end of them. A host cable plugged
in **instead of** the buck both powers and flashes the board — that is the whole story. There
is no arrangement in which the buck supplies VBUS and a laptop supplies data.

**SWD is not on the base board.** `SWD`, `SWDIO`, `SWCLK` and `JTAG` appear nowhere in the
RAK19007 datasheet; its feature list names the Type-C port as the programming and debugging
interface, and `BOOT0` on pin 23 is documented as an ST-MCU input, not an nRF52 one
[CITE(datasheet): RAK19007 datasheet — CIT-RAK19007-RAW]. RAK's own SWD instructions attach
the J-Link or RAKDAP1 to **the RAK4631 module**, not to the carrier
[CITE(datasheet): RAK4630 Bootloader Update Manual, "Update over JLINK" — CIT-RAK-BOOTLOADER].
Reaching those pads means opening the enclosure and getting at the module, so SWD is a
bench-recovery tool, not a field one.

**LoRaWAN FUOTA does not fit in the airtime budget.** TTN's Fair Use Policy allows **30 s of
uplink airtime per node per 24 h** and **10 downlinks per node per 24 h**
[CITE(policy): TTN Fair Use Policy — CIT-TTN-FUP], with the 10-downlink cap including ACKs for
confirmed uplinks [CITE(policy): Fair Use Policy explained — CIT-TTN-FUP-EXPLAINED]. Ten
downlink messages a day is the binding number and it is not close: a firmware image is
hundreds of kilobytes, and the daily downlink allowance carries at most a few hundred bytes.
Even reading the budget as generously as possible — SF7, the largest US915 payload, the entire
allowance spent on nothing but firmware — the throughput is on the order of kilobytes per day
against an image two orders of magnitude larger, and the node is Class A, so it has only the
two short RX windows after each of its own uplinks and no multicast session to receive a
fragmented image in [CITE(spec): LoRaWAN L2 1.0.4 §3.3 Receive Windows — CIT-LW-LINK]. FUOTA is
off the table on this network.

## 5. Recommendation

**Do both, in this order.**

1. **Design the BLE DFU trigger in now.** A downlink command that writes `GPREGRET = 0xA8`
   and soft-resets. It is a handful of lines, it costs nothing while unused, and it is the
   only path that does not involve a hike with a screwdriver. Verify it end to end on the
   bench — trigger, connect with nRF Connect, push `firmware.zip`, confirm the application
   comes back — **before** the enclosure is closed. An untested recovery path is not one.
2. **Bring a short USB-C pigtail out through a gland to a weatherproof panel-mount
   connector.** The buck plugs into that from outside, so swapping in a host cable is a
   30-second job at the node with the box still sealed. This is the belt to BLE's braces, and
   it is the thing that recovers a node whose application is so broken it cannot receive the
   downlink that triggers DFU. The enclosure already needs gland planning (issue #20) and the
   solar shell has room, so the marginal cost is one gland and one connector.

Option 2 is what makes option 1 safe to depend on. With both, there is no realistic firmware
failure that requires opening the enclosure; with neither, the first bug that needs a reflash
costs a hike and a gasket.
