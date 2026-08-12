# Does `FEATURE_CONSOLE=0` save any current?

**Date:** 2026-08-12
**Question from:** issue [#56](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/56)
**Method:** source reading only. No hardware — the board presents no USB device at all
([#58](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/58)),
so nothing here was measured and nothing here claims to have been.
**Verdict:** **the doubt in #56 is correct.** `FEATURE_CONSOLE=0` removes no task, no
peripheral, and no clock. RAK's stated mechanism is falsified on RAK's own BSP.

---

## 1. What actually happens before `setup()`

The Arduino entry point on this core is a FreeRTOS task, and the USB stack is brought up
inside it before user code gets control:

```c
static void loop_task(void* arg)
{
  (void) arg;

#ifdef USE_TINYUSB
  TinyUSB_Device_Init(0);
#endif
  ...
  setup();
```

CITE(prior-art): adafruit/Adafruit_nRF52_Arduino @ `343ab5f` —
`cores/nRF5/main.cpp:47-60`. [CIT-ADA-NRF52-CORE]
<https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/343ab5fe7fb3d5657d3176883dfb188d65b79f06/cores/nRF5/main.cpp#L47-L60>

`TinyUSB_Device_Init()` is a one-line forward to `TinyUSBDevice.begin(rhport)`:

CITE(prior-art): adafruit/Adafruit_TinyUSB_Arduino —
`src/arduino/Adafruit_TinyUSB_API.cpp:40-43`. [CIT-TINYUSB]
<https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/src/arduino/Adafruit_TinyUSB_API.cpp#L40-L43>

and `Adafruit_USBD_Device::begin()` does two things that decide this whole question:

```cpp
bool Adafruit_USBD_Device::begin(uint8_t rhport) {
  clearConfiguration();
  // Serial is always added by default
  ...
  SerialTinyUSB.begin(115200);          // <-- line 262

  // Init device hardware and call tusb_init()
  TinyUSB_Port_InitDevice(rhport);      // <-- line 265
```

CITE(prior-art): `src/arduino/Adafruit_USBD_Device.cpp:232-267`, the two calls at **:262**
and **:265**. [CIT-TINYUSB]
<https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/src/arduino/Adafruit_USBD_Device.cpp#L232-L267>

`SerialTinyUSB` is `Serial`:

```c
#define SerialTinyUSB Serial
```

CITE(prior-art): `src/arduino/Adafruit_USBD_CDC.h:36` and `:101`. [CIT-TINYUSB]
<https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/src/arduino/Adafruit_USBD_CDC.h#L36>

And `TinyUSB_Port_InitDevice()` on the nRF5x port creates the task RAK is talking about:

```cpp
static void usb_device_task(void *param) {
  NVIC_SetPriority(USBD_IRQn, 2);
  tusb_init(0, &rh_init);
  usb_hardware_init();
  while (1) {
    tud_task();
    TinyUSB_Device_FlushCDC();
  }
}

void TinyUSB_Port_InitDevice(uint8_t rhport) {
  xTaskCreate(usb_device_task, "usbd", USBD_STACK_SZ, NULL, TASK_PRIO_HIGH, NULL);
}
```

CITE(prior-art): `src/arduino/ports/nrf/Adafruit_TinyUSB_nrf.cpp:64-93`. [CIT-TINYUSB]
<https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/src/arduino/ports/nrf/Adafruit_TinyUSB_nrf.cpp#L64-L93>

**So, before `setup()` is entered: the CDC interface is registered, `Serial.begin(115200)`
has been called by the core on our behalf, the `usbd` task exists at `TASK_PRIO_HIGH`, and
the USB power-event handler is armed.** None of that is conditional on user code.

### `USE_TINYUSB` is defined in our build — proved from our own tree

`Serial` does not exist on this core unless the flag is set:

```c
#ifdef USE_TINYUSB
// Needed for declaring Serial
#include "Adafruit_USBD_CDC.h"
#endif
```

CITE(prior-art): `cores/nRF5/Arduino.h:63-66` @ `343ab5f`. [CIT-ADA-NRF52-CORE]

`src/main.cpp:165` calls `Serial.begin(115200)` and `src/power.cpp:135` calls
`TinyUSBDevice.detach()`; the `env:soak` build of those lines compiles. Therefore
`USE_TINYUSB` is defined for this project, therefore `main.cpp:51-53` is live, therefore
everything above happens on our board. `FEATURE_CONSOLE` is our macro and has no effect on
`USE_TINYUSB`, which comes from the toolchain, so the field image gets the identical
pre-`setup()` sequence.

## 2. What `Serial.begin()` adds, and what `Serial.end()` would remove

`Adafruit_USBD_CDC::begin()` is idempotent and creates nothing:

```cpp
void Adafruit_USBD_CDC::begin(uint32_t baud) {
  (void)baud;
  if (isValid()) { return; }          // already called begin()
  if (!(_instance_count < CFG_TUD_CDC)) { return; }
  _instance = _instance_count++;
  this->setStringDescriptor("TinyUSB Serial");
  TinyUSBDevice.addInterface(*this);
}
```

CITE(prior-art): `src/arduino/Adafruit_USBD_CDC.cpp:91-107`. [CIT-TINYUSB]

Because the core already called it at `Adafruit_USBD_Device.cpp:262`, `isValid()` is true
and **our `src/main.cpp:165` returns immediately having done nothing at all.** It starts no
task and touches no hardware even on the first call — it appends a descriptor and bumps a
counter.

`Adafruit_USBD_CDC::end()` (`:114-119`) calls `TinyUSBDevice.clearConfiguration()` and
resets the instance count. It does **not** stop the `usbd` task, disable `NRF_USBD`, or
release HFCLK. So `src/power.cpp`'s decision not to call `Serial.end()` costs nothing in
current; the comment there at `:124-128` is right about why it is dangerous (it discards the
configuration descriptor the host enumerated) and the omission remains correct.

## 3. Does the `usbd` task actually prevent sleep?

No — not by existing. `tud_task()` is `tud_task_ext(UINT32_MAX, false)`, whose loop body is
a blocking queue receive:

```c
if (!osal_queue_receive(_usbd_q, &event, timeout_ms)) { return; }
```

CITE(prior-art): hathach/tinyusb — `src/device/usbd.c:669-688`. [CIT-TINYUSB-CORE]
<https://github.com/hathach/tinyusb/blob/master/src/device/usbd.c#L669-L688>

and under the FreeRTOS OSAL `UINT32_MAX` maps to `portMAX_DELAY`:

```c
static inline uint32_t _osal_ms2tick(uint32_t msec) {
  if (msec == OSAL_TIMEOUT_WAIT_FOREVER) { return portMAX_DELAY; }
```

CITE(prior-art): `src/osal/osal_freertos.h:80-90` and `:257-259`. [CIT-TINYUSB-CORE]
<https://github.com/hathach/tinyusb/blob/master/src/osal/osal_freertos.h#L80-L90>

A task blocked on `xQueueReceive(..., portMAX_DELAY)` is in the Blocked state and does not
appear in the ready list, so it does not by itself defeat the core's tickless idle, which is
enabled:

```
#define configUSE_TICKLESS_IDLE                 1
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   2
#define configUSE_IDLE_HOOK                     1
#define configTICK_RATE_HZ                      1024
```

CITE(prior-art): `cores/nRF5/freertos/config/FreeRTOSConfig.h:52-97` @ `343ab5f`.
[CIT-ADA-NRF52-CORE]

Nor is there a 1 ms SOF wakeup for a CDC-only device: the nRF5x driver enables the SOF
interrupt only for isochronous endpoints or a one-shot remote-wakeup, and disables it again
afterwards (`if (!iso_enabled && !_dcd.sof_enabled) { ... NRF_USBD->INTENCLR = SOF; }`).

CITE(prior-art): `src/portable/nordic/nrf5x/dcd_nrf5x.c:295-302`, `:611-643`.
[CIT-TINYUSB-CORE]

**What costs current is not the task — it is the peripheral.** `NRF_USBD->ENABLE` and the
HFXO request are driven from the VBUS power-event handler, not from any user call:

CITE(prior-art): [CIT-TINYUSB-CORE] `dcd_nrf5x.c:927-1000` (`tusb_hal_nrf_power_event`, the
`USB_EVT_DETECTED` → errata 171/187/166 → `ENABLE` → `READY` → `hfclk_enable()` chain) and
`:847-895` (`hfclk_enable`/`hfclk_running`, which go through `sd_clock_hfclk_request()` when
the SoftDevice is enabled — it is, `-DSOFTDEVICE_PRESENT`).

CITE(datasheet): [CIT-NRF-USBD] nRF52840 Product Specification — USBD requires the 64 MHz
crystal oscillator; the HFXO is the dominant standing cost of an enabled, attached USB
device, not the FreeRTOS task.

## 4. Does RAK's guidance survive?

**No — not as stated, and it is falsified using RAK's own BSP.**

RAK's claim:

> As we want to achieve maximum power savings, the Serial port **MUST NOT** be initialized.
> Because not only does the hardware of the Serial port consume energy, FreeRTOS is as well
> starting a task running in the background (and never sleeps), that prevents the MCU from
> sleeping.

CITE(prior-art): RAKWireless/WisBlock —
`examples/RAK4630/communications/LoRa/LoRaWAN/Low_Power_Example.md:39-45`.
[CIT-RAK-LOWPOWER]

Three separate things are wrong with the mechanism:

1. **RAK's own core starts TinyUSB before `setup()`, byte-identically to Adafruit's.**
   `RAKWireless/RAK-nRF52-Arduino` `cores/nRF5/main.cpp` has the same
   `#ifdef USE_TINYUSB / TinyUSB_Device_Init(0);` block ahead of `setup()`.
   CITE(prior-art): [CIT-RAK-NRF52-CORE]
   <https://github.com/RAKWireless/RAK-nRF52-Arduino/blob/master/cores/nRF5/main.cpp>
2. **RAK defines `USE_TINYUSB` unconditionally and offers no way to turn it off.**
   `platform.txt:72` — `build.flags.usb= -DUSBCON -DUSE_TINYUSB -DUSB_VID=... ` — and
   `boards.txt` contains **zero** occurrences of a `usbstack` menu, so unlike the Adafruit
   BSP there is no "USB Stack: Arduino / TinyUSB" option to opt out with. Every RAK4631 sketch
   built on RAK's BSP, `MAX_SAVE` or not, gets the `usbd` task.
   CITE(prior-art): [CIT-RAK-NRF52-CORE]
   <https://github.com/RAKWireless/RAK-nRF52-Arduino/blob/master/platform.txt#L72>
3. **The task blocks rather than spins,** so "never sleeps" does not describe it (§3).

This is not a recent regression that RAK could have written the doc before. The
`SerialTinyUSB.begin(115200)` line inside `Adafruit_USBD_Device::begin()` is present as far
back as the `1.7.0` tag (2021), at `src/arduino/Adafruit_USBD_Device.cpp:280`.
CITE(prior-art): [CIT-TINYUSB]
<https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/1.7.0/src/arduino/Adafruit_USBD_Device.cpp#L269-L286>

**What is left of RAK's advice, and it is not nothing:** the *residual* claim — "the
hardware of the Serial port consume energy" — is true and is about the USBD peripheral and
its HFXO, not about `Serial.begin()`. A `MAX_SAVE` build still pays that cost whenever VBUS
is present. RAK's advice is best read as *do not print*, which saves the HFXO run-time and
the endpoint traffic that each `write()` provokes — a real but duty-cycle-shaped saving, not
the step change the doc's wording implies. Their sketch is also written for a board on a
bench with a cable in it, where VBUS is by definition present.

## 5. The correct knob

There is no application-level call that destroys the `usbd` task; `vTaskDelete` on it is not
exposed and would strand the stack. The levers that exist, in order of effect:

| Lever | What it does | Already done? |
|---|---|---|
| **No VBUS** (field: nothing plugged in) | `tusb_hal_nrf_power_event` never fires `DETECTED`/`READY`, so `NRF_USBD->ENABLE` is never set and HFXO is never requested for USB. The task is created and blocks forever on an empty queue. | Free — this is the deployed condition |
| `TinyUSBDevice.detach()` → `dcd_disconnect()` → `NRF_USBD->USBPULLUP = 0` (`dcd_nrf5x.c:280-282`) | Host stops polling; endpoint traffic and its wakeups stop. Peripheral stays enabled and HFCLK stays requested. | **Yes** — `src/power.cpp:135` |
| Not printing | Removes the `write()`→`tud_task()`→endpoint path and the flush loop | This is what `FEATURE_CONSOLE=0` actually buys |
| `NRF_USBD->ENABLE = 0` + HFCLK release | The only thing that would remove the standing peripheral cost | **Deliberately not done**, and correctly — the enable sequence (errata 171/187/166, the `READY` handshake) is only re-run from the VBUS event handler, so clearing it by hand permanently kills the console. `src/power.cpp:117-122` already documents this from experience |

So `power.cpp:135` is already using the best lever the public API offers, and the residual
cost it cannot remove only exists when a cable is attached — which in the woods it is not.

## 6. Recommendation: **re-scope, leaning revert**

`FEATURE_CONSOLE=0` in `[env:rak4631]` does not remove a task, a peripheral, or a clock. Its
entire effect is that the node does not print. In the field, with no VBUS, the printing path
is already nearly free: `Adafruit_USBD_CDC::write()` goes to a FIFO and the `usbd` task
drains it only when the stack has events, and unattached there are none.

Against that, the cost is concrete and was argued in `platformio.ini:62-65`: a deployed node
that prints nothing, so nothing can be diagnosed over a cable without a reflash. Given
[#58](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/58) —
a board that right now presents no USB device at all — removing the console from the
shipping image trades a real recovery path for a saving this review cannot find in the
source.

The justification comment at `platformio.ini:53-60` should not survive in its current form
either way: it asserts RAK's mechanism ("FreeRTOS starts a background task for it that never
sleeps") as the reason, and that mechanism is false on this core. Predicting "milliamps" from
it is not supportable.

Concretely:

- **Revert `[env:rak4631]` to `FEATURE_CONSOLE=1`**, making it identical to `[env:soak]`,
  until a meter says otherwise. That also restores the property `platformio.ini:63-65` says
  it wants — the soaked image and the field image being the same image.
- If the flag is kept instead, rewrite the comment to say what it actually does (suppresses
  output; does not remove the USB task or peripheral) and stop predicting milliamps.
- Keep `src/power.cpp:135`'s `detach()` — it is the right lever and is unaffected.
- Keep the absence of `Serial.end()` — §2 shows it would remove no current and would break
  the descriptor.

## 7. What only a meter can settle

Everything quantitative. This review establishes *mechanism*, not *magnitude*, and the two
must not be confused in the changelog. Specifically unresolved (issue
[#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47)):

1. **Sleep current, VBUS absent, `FEATURE_CONSOLE=1` vs `=0`.** The prediction from this
   review is that the difference is at or below the noise floor. If a meter shows a
   milliamp-scale difference, something in this analysis is wrong and it should be found
   before the flag is trusted in either direction.
2. **Whether tickless idle is actually reached between cycles at all.** `configUSE_TICKLESS_IDLE 1`
   is necessary, not sufficient: any other periodic task in the image
   (`SX126x-Arduino`'s task, the `ada_callback` task, the 1024 Hz tick with
   `configEXPECTED_IDLE_TIME_BEFORE_SLEEP 2`) could hold the CPU out of `WFE`. Not
   determinable from the core's source alone — it depends on our whole task set at runtime.
3. **The standing cost of an enabled-but-detached USBD**, i.e. the residual that
   `power.cpp:135` cannot remove. Only observable with a cable attached, which is exactly
   the condition that perturbs it.
4. **Whether the sleep-slice change (`kSleepSliceSeconds = 60`, `power.cpp:34`) matters.**
   Same category — a wakeup-count reduction whose current effect is unmeasured.

Until (1) exists, no document in this repository should claim `FEATURE_CONSOLE=0` saves
current. Per `AGENTS.md`, the honest status is that it is a change with no established
power effect.

---

## Sources

All fetched and read during this review on 2026-08-12; no library internals here are quoted
from memory.

| Key | Source |
|---|---|
| [CIT-ADA-NRF52-CORE] | adafruit/Adafruit_nRF52_Arduino @ `343ab5fe7fb3d5657d3176883dfb188d65b79f06` (2026-05-12) — `cores/nRF5/main.cpp`, `cores/nRF5/Arduino.h`, `cores/nRF5/freertos/config/FreeRTOSConfig.h` |
| [CIT-TINYUSB] | adafruit/Adafruit_TinyUSB_Arduino `master` — `src/arduino/Adafruit_TinyUSB_API.cpp`, `Adafruit_USBD_Device.cpp/.h`, `Adafruit_USBD_CDC.cpp/.h`, `src/arduino/ports/nrf/Adafruit_TinyUSB_nrf.cpp`; tag `1.7.0` for the history point |
| [CIT-TINYUSB-CORE] | hathach/tinyusb `master` — `src/device/usbd.c`, `src/osal/osal_freertos.h`, `src/portable/nordic/nrf5x/dcd_nrf5x.c` |
| [CIT-RAK-LOWPOWER] | RAKWireless/WisBlock — `examples/RAK4630/communications/LoRa/LoRaWAN/Low_Power_Example.md:39-45` |
| [CIT-RAK-NRF52-CORE] | RAKWireless/RAK-nRF52-Arduino `master` — `cores/nRF5/main.cpp`, `platform.txt:72`, `boards.txt` (new key; not previously in `docs/CITATIONS.md`) |
| [CIT-NRF-USBD] | nRF52840 Product Specification — USBD, already registered in `docs/CITATIONS.md` |

**Note for `docs/CITATIONS.md`:** `[CIT-RAK-NRF52-CORE]` and `[CIT-TINYUSB-CORE]` are used
here and are not yet in the registry. This review is read-only outside its own file, so they
have not been added; adding them is a follow-up.
