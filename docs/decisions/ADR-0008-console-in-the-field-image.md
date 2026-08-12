# ADR-0008 — The console stays in the field image

**Status:** accepted
**Date:** 2026-08-12
**Supersedes:** the `FEATURE_CONSOLE=0` field build introduced the same day in `094d5f5`
**Related:** [#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47)
(magnitude, meter-only), [#56](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/56)
(closed by this), [#58](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/58)
(the board currently presents no USB device),
[ADR-0004](ADR-0004-bms-one-wire-path.md) (the "delete a failure mode rather than handle one"
preference this applies)

## Context

`[env:rak4631]`, the image that goes into the woods, was changed on 2026-08-12 to build
`-D FEATURE_CONSOLE=0` so that `Serial.begin()` is never reached. The justification was RAK's
own low-power guidance, quoted in `docs/CITATIONS.md` under `CIT-RAK-LOWPOWER`:

> As we want to achieve maximum power savings, the Serial port **MUST NOT** be initialized.
> Because not only does the hardware of the Serial port consume energy, FreeRTOS is as well
> starting a task running in the background (and never sleeps), that prevents the MCU from
> sleeping.

`docs/LIBRARIES.md:55` carried the same rule from the RAK forum, and
`docs/reviews/2026-08-12_rak_reference_benchmark.md` ranked the un-gated console as the
highest-consequence gap between this firmware and RAK's reference implementations. The
predicted saving was stated as milliamps against a budget in microamps.

The change was made on cited authority and never measured. Issue #56 was opened to meter it,
and while writing that issue the premise was doubted, because the Adafruit core appeared to
initialize USB before `setup()` regardless of the sketch. That doubt was then settled by
reading the sources rather than the narrative:
[`docs/reviews/2026-08-12_console_sleep_question.md`](../reviews/2026-08-12_console_sleep_question.md).

## Decision

**Revert. `[env:rak4631]` builds `FEATURE_CONSOLE=1`.** The console ships in the field image.

Three related choices are confirmed rather than changed:

- **Keep `TinyUSBDevice.detach()`** in `src/power.cpp:135`. It is the correct and best lever.
- **Keep not calling `Serial.end()`.** It removes no current and would discard the configuration
  descriptor the host enumerated.
- **Keep the `FEATURE_CONSOLE` macro itself.** It still does something real — it suppresses
  output — and the `stageN` bring-up environments use it. What it does not do is save current.

`[env:soak]` is now byte-identical to `[env:rak4631]`. It survives as a named entry point for
`scripts/soak.sh` and the several documents that reference it, explicitly carrying no build
difference, which strengthens H8 rather than weakening it: a soak is now evidence about the
shipped image without needing an argument that one differing flag was harmless.

## Why — the mechanism, from primary sources

`.cursor/rules/20-citation-discipline.mdc` requires that conflicts be recorded rather than
silently resolved, and that when prior art and the primary source disagree, **the primary source
wins**. Here the primary source is the core's own code; RAK's document is narrative prior art
about that code. They disagree, and the code is what runs.

### 1. Our `Serial.begin()` is already a no-op

`Adafruit_USBD_Device::begin()` calls the CDC's `begin()` itself and then creates the USB task:

```cpp
  // Serial is always added by default
  SerialTinyUSB.begin(115200);        // Adafruit_USBD_Device.cpp:262
  TinyUSB_Port_InitDevice(rhport);    // :265 — xTaskCreate(usb_device_task, "usbd", ...)
```

That runs from `cores/nRF5/main.cpp:52` (`TinyUSB_Device_Init(0)` inside `loop_task`), **before
`setup()`**. By the time `src/main.cpp:165` calls `Serial.begin(115200)`, `isValid()` is true and
`Adafruit_USBD_CDC::begin()` returns immediately at `Adafruit_USBD_CDC.cpp:95` having done
nothing.

So `FEATURE_CONSOLE=0` does not remove a task, a peripheral, or a clock. It buys exactly one
thing: the node does not print.

### 2. RAK's guidance does not survive scrutiny, on RAK's own BSP

- `RAK-nRF52-Arduino/platform.txt:72` defines `-DUSE_TINYUSB` unconditionally, and `boards.txt`
  has no `usbstack` menu, so there is no way to opt out the way the Adafruit BSP allows.
- `RAK-nRF52-Arduino/cores/nRF5/main.cpp` is byte-identical to Adafruit's, including the
  `#ifdef USE_TINYUSB / TinyUSB_Device_Init(0);` block ahead of `setup()`.

**RAK's own `MAX_SAVE` builds therefore create the task their document warns about.** The
reference does not achieve what it claims, so there was never a gap to be behind on.

The stated mechanism is also wrong on its own terms: `tud_task()` blocks on
`xQueueReceive(..., portMAX_DELAY)` (`tinyusb/src/device/usbd.c:686`,
`osal/osal_freertos.h:81`), so it sits in the Blocked state and does not defeat tickless idle;
and the nRF5x driver leaves the SOF interrupt disabled for a CDC-only device
(`dcd_nrf5x.c:295-302`), so there is no 1 ms wakeup either.

### 3. What is true in RAK's advice, and where the real lever is

The residual claim — that the serial *hardware* costs energy — is true, and it is about the USBD
peripheral and its HFXO, not about `Serial.begin()`. `NRF_USBD->ENABLE` and the HFXO request are
driven only from the VBUS power-event handler (`dcd_nrf5x.c:927`). **In the field there is no
cable, so the peripheral is never enabled, whatever we built.** With a cable present,
`detach()` → `USBPULLUP = 0` (`dcd_nrf5x.c:280`) stops host polling, which `src/power.cpp:135`
already does.

Read correctly, RAK's advice amounts to *do not print* — a duty-cycle-shaped saving, from a
sketch written for a board with a cable in it. It is not the step change the wording implies.

## Consequences

**Good.**

- A deployed node can be diagnosed over a cable without a reflash. This is not hypothetical:
  #58 has the only board we own presenting no USB device at all, and shipping a silent image
  while the board is already unreachable is the wrong direction. Per the deployment goal in
  `AGENTS.md`, recovery paths outrank speculative savings.
- The soak image and the field image are the same image, so H8 evidence needs no caveat.
- One fewer build difference to reason about, which is the ADR-0004 preference applied again:
  delete the failure mode rather than handle it.

**Bad, or at least unresolved.**

- We give up a saving that this analysis says does not exist, but cannot prove is zero. If a
  meter later shows a milliamp-scale difference between `FEATURE_CONSOLE=1` and `=0` with VBUS
  absent, **something in this ADR is wrong and must be found before the flag is trusted in
  either direction.** That measurement is #47.
- The standing cost of an enabled-but-detached USBD is still unmeasured, and is only observable
  with a cable attached — the condition that perturbs it.
- Printing is not free when a cable *is* attached, which is the soak condition. A soak current
  figure is therefore not a field current figure, and must not be recorded as one.

**Documents that still assert the withdrawn rationale** and are owned by another agent at the
time of writing, so they are flagged rather than edited: `docs/FIRMWARE_SPEC.md` §5 (~line 202)
and `docs/LIBRARIES.md:55`. Both state RAK's mechanism as fact and should be corrected to point
here.

## Alternatives considered

| Option | Why not |
|---|---|
| Keep `FEATURE_CONSOLE=0`, rewrite the comment to claim only "does not print" | Honest, but it keeps a field/soak divergence and gives up cable diagnosis for a saving nobody can find in the source. The trade only makes sense if the saving is real, and #47 has not run. |
| Keep the flag and meter first, revert only if flat | Leaves the shipping image silent, and #58 means the window in which a console matters is open right now. Reverting is cheap and reversible; the meter can still argue for it later. |
| `NRF_USBD->ENABLE = 0` plus HFCLK release, to remove the peripheral cost properly | The enable sequence (errata 171/187/166, the `READY` handshake) is only re-run from the VBUS event handler, so clearing it by hand permanently kills the console until a power cycle. `src/power.cpp:117-122` already documents this from experience. |
| Delete `[env:soak]` now that it is identical | `scripts/soak.sh`, `docs/SOAK.md`, `README.md`, `AGENTS.md` and `docs/FIRMWARE_SPEC.md` all name it. Deleting it breaks five call sites to remove a duplicate that costs nothing. |

## Citations

- CITE(prior-art): adafruit/Adafruit_TinyUSB_Arduino — `src/arduino/Adafruit_USBD_Device.cpp`
  lines 262 and 265; `src/arduino/Adafruit_USBD_CDC.cpp:91-107`;
  `src/arduino/ports/nrf/Adafruit_TinyUSB_nrf.cpp:64-93` [CIT-TINYUSB]
  <https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/src/arduino/Adafruit_USBD_Device.cpp#L232-L267>
- CITE(prior-art): adafruit/Adafruit_nRF52_Arduino @ `343ab5f` — `cores/nRF5/main.cpp:47-60`,
  `cores/nRF5/freertos/config/FreeRTOSConfig.h:52-97` [CIT-ADA-NRF52-CORE]
  <https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/343ab5fe7fb3d5657d3176883dfb188d65b79f06/cores/nRF5/main.cpp#L47-L60>
- CITE(prior-art): hathach/tinyusb — `src/device/usbd.c:669-688`, `src/osal/osal_freertos.h:80-90`,
  `src/portable/nordic/nrf5x/dcd_nrf5x.c:280-302`, `:847-895`, `:927-1000` [CIT-TINYUSB-CORE]
  <https://github.com/hathach/tinyusb/blob/master/src/portable/nordic/nrf5x/dcd_nrf5x.c>
- CITE(prior-art): RAKWireless/RAK-nRF52-Arduino — `cores/nRF5/main.cpp`, `platform.txt:72`,
  `boards.txt` [CIT-RAK-NRF52-CORE]
  <https://github.com/RAKWireless/RAK-nRF52-Arduino/blob/master/platform.txt>
- CITE(prior-art): RAKWireless/WisBlock — `Low_Power_Example.md:39-45`, the guidance this ADR
  contradicts [CIT-RAK-LOWPOWER]
- CITE(datasheet): [CIT-NRF-USBD] nRF52840 Product Specification — the USBD peripheral, whose
  standing cost is what VBUS controls and what `detach()` cannot remove
  <https://docs.nordicsemi.com/bundle/ps_nrf52840/page/usbd.html>
