# Benchmark against RAKwireless reference implementations — 2026-08-12

**Scope:** measure this firmware against RAK's own published reference implementations, per the
operator's standard that it should "meet and exceed RAK's own reference implementations on
GitHub." Read-only review; no source touched.

**Method:** every reference behavior below was fetched and quoted from the actual file. Nothing
here is recalled from memory, per `.cursor/rules/20-citation-discipline.mdc`. Our side is cited
`file:line`. `docs/LIBRARIES.md` already records *which* references are preferred; this document
does not repeat that, it measures against them.

## References used (all fetched 2026-08-12)

| Key | Source | What it is |
|---|---|---|
| `[REF-LOWPOWER]` | [`WisBlock/examples/RAK4630/communications/LoRa/LoRaWAN/Low_Power_Example.md`](https://github.com/RAKWireless/WisBlock/blob/master/examples/RAK4630/communications/LoRa/LoRaWAN/Low_Power_Example.md) | RAK's official narrative on how to sleep an nRF52 WisBlock node |
| `[REF-DEEPSLEEP]` | [`WisBlock/.../RAK4631-DeepSleep-LoRaWan/RAK4631-DeepSleep-LoRaWan.ino`](https://github.com/RAKWireless/WisBlock/blob/master/examples/RAK4630/communications/LoRa/LoRaWAN/RAK4631-DeepSleep-LoRaWan/RAK4631-DeepSleep-LoRaWan.ino) | The sketch `[REF-LOWPOWER]` documents |
| `[REF-P2PSLEEP]` | [`WisBlock/tutorials/RAK4631-Deep-Sleep-P2P/README.md`](https://github.com/RAKWireless/WisBlock/blob/master/tutorials/RAK4631-Deep-Sleep-P2P/README.md) | The only RAK doc found with a **measured** sleep current |
| `[REF-API2-LOOP]` | [`WisBlock-API-V2/src/WisBlock-API.cpp`](https://github.com/beegee-tokyo/WisBlock-API-V2/blob/main/src/WisBlock-API.cpp) | The canonical WisBlock event loop |
| `[REF-API2-FN]` | [`WisBlock-API-V2/src/api_functions.cpp`](https://github.com/beegee-tokyo/WisBlock-API-V2/blob/main/src/api_functions.cpp) | Sleep/wake primitives and the periodic timer |
| `[REF-API2-LORA]` | [`WisBlock-API-V2/src/lorawan.cpp`](https://github.com/beegee-tokyo/WisBlock-API-V2/blob/main/src/lorawan.cpp) | Join, ADR, duty cycle, MAC reset |
| `[REF-RS485]` | [`WisBlock/examples/RAK4630/IO/RAK5802_RS485/Sender/Sender.ino`](https://github.com/RAKWireless/WisBlock/blob/master/examples/RAK4630/IO/RAK5802_RS485/Sender/Sender.ino) | RAK's only RAK5802 example for this board |

---

## 1. Sleep and power — the big one

### What the references do

RAK's own low-power document names our exact mechanism and rejects it. `[REF-LOWPOWER]:13`, on
`delay()`:

> This command will send the task into sleep for x milliseconds. This sounds easy to use,
> however, is not very practical. Because while in the `delay()` function, the task cannot
> receive any information about external events, like an interrupt from a sensor or from a 9DOF
> sensor. So for most scenarios the `delay` is not a good solution.

The prescribed mechanism, `[REF-LOWPOWER]:140-145`:

```cpp
	// Sleep until we are woken up by an event
	if (xSemaphoreTake(taskEvent, portMAX_DELAY) == pdTRUE)
	{
```

`[REF-DEEPSLEEP]:157` is that line in the shipped sketch. `[REF-API2-FN]:110-115` is the same
primitive inside the framework:

```cpp
void api_wait_wake(void)
{
#if defined NRF52_SERIES || defined ESP32 || defined ARDUINO_RAKWIRELESS_RAK11300
	// Wait until semaphore is released (FreeRTOS)
	xSemaphoreTake(g_task_sem, portMAX_DELAY);
#endif
```

Wake comes from a FreeRTOS timer (`[REF-API2-FN]:192`,
`xTimerCreate(..., periodic_wakeup)`) giving the semaphore from ISR context
(`[REF-LOWPOWER]:120-127`, `xSemaphoreGiveFromISR(taskEvent, pdFALSE)`).

**And the harder requirement, `[REF-LOWPOWER]:45`:**

> As we want to achieve maximum power savings, the Serial port **MUST NOT** be initialized.
> Because not only does the hardware of the Serial port consume energy, FreeRTOS is as well
> starting a task running in the background (and never sleeps), that prevents the MCU from
> sleeping.

The sketch enforces this with a compile-time switch: every `Serial` call in `[REF-DEEPSLEEP]` is
wrapped in `#ifndef MAX_SAVE`, including `Serial.begin()` itself at `:80-82`.

The only measured figure in any RAK doc I could find is `[REF-P2PSLEEP]:17` — **120 µA while
sleeping**, and note the caveat, it is for the *P2P transmit-only* build, not a LoRaWAN build:

> In the transmit only mode, a power consumption of 120uA (while sleeping) could be achieved

### What we do

`src/power.cpp:134-136`:

```cpp
    for (uint32_t i = 0; i < seconds; i++) {
        delay(1000);
    }
```

The comment above it at `:129-133` concedes the state reached is "the chip's lighter sleep state,
not its deepest one."

We do the two things `[REF-LOWPOWER]` cares about at the radio: `Radio.Sleep()` at
`src/power.cpp:79` and `SPI_LORA.end()` at `:86`, the latter with a comment putting a running SPI
bus "at close to a milliamp." Both correct and both matching the references' intent.

We do **not** do the Serial one. `src/build_features.h:66-67` defaults `FEATURE_CONSOLE` to `1`,
and no environment in `platformio.ini` overrides it to `0` — including the field `rak4631`
environment. `src/power.cpp:99-122` deliberately keeps the CDC interface configured and only calls
`TinyUSBDevice.detach()`, with `:110-114` explaining why `Serial.end()` is **not** called. The
reasoning given is sound on its own terms — a prior `Serial.end()`/`USBD->ENABLE` attempt left the
console permanently dead and made hardware sessions unverifiable — but it is reasoning about a
bench operator with a cable, and the deployment case has no cable.

### Verdict: we are behind, and this is the highest-consequence gap in the review

Ranked by consequence:

1. ~~**The console is compiled into the field image and never torn down.**~~ **WITHDRAWN
   2026-08-12 — this is not a gap. We were not behind; the reference document is wrong.**

   The original entry read: `[REF-LOWPOWER]:45` says this alone prevents the MCU from sleeping,
   because the background serial task never sleeps; `docs/LIBRARIES.md:55` carries the same rule
   from the RAK forum ("**must `Serial.end()` before sleep** or current stays ~mA"); so the fix is
   a `-D FEATURE_CONSOLE=0` field environment. That change was made on 2026-08-12 in `094d5f5`
   **and reverted the same day** once the mechanism was read in the core's source rather than
   taken from RAK's narrative.

   Why it is withdrawn, in one line each — decision and full reasoning in
   [ADR-0008](../decisions/ADR-0008-console-in-the-field-image.md):

   - `Adafruit_USBD_Device::begin()` calls `SerialTinyUSB.begin(115200)` itself
     (`Adafruit_USBD_Device.cpp:262`, commented _"Serial is always added by default"_) and creates
     the `usbd` task at `:265`, from `cores/nRF5/main.cpp:52`, **before `setup()`**. Our
     `Serial.begin()` therefore hits `if (isValid()) return;` (`Adafruit_USBD_CDC.cpp:95`) and
     does nothing. There is no "field image that never initializes the port" available to build.
   - RAK's own BSP defines `-DUSE_TINYUSB` unconditionally (`platform.txt:72`, no `usbstack` menu
     in `boards.txt`) and their `cores/nRF5/main.cpp` is byte-identical to Adafruit's, so **RAK's
     own `MAX_SAVE` builds get the task the document warns about.** The reference does not achieve
     what it claims, which means there was never a gap to be behind on.
   - The stated mechanism is wrong independently: `tud_task()` blocks on
     `xQueueReceive(.., portMAX_DELAY)` (`usbd.c:686`, `osal_freertos.h:81`), so it is not a task
     that "never sleeps", and SOF interrupts are off for a CDC-only device.
   - What is left of `[REF-LOWPOWER]` is the weaker true claim that the USBD peripheral and its
     HFXO cost energy. That is a **VBUS** question, not a `Serial.begin()` question:
     `NRF_USBD->ENABLE` is only ever set from the VBUS power-event handler
     (`dcd_nrf5x.c:927`), so with no cable the peripheral is not enabled regardless of the build.
     `src/power.cpp:135` already calls `detach()`, which is the best lever the public API offers.

   **So on this specific point we already exceeded the reference before the change was made.**
   `docs/LIBRARIES.md:55` and the `[REF-LOWPOWER]` row in `docs/CITATIONS.md` needed the
   correction attached rather than the firmware needing the change; that is done. What remains is
   a magnitude question only (#47) — mechanism is settled, and no document here may claim
   `FEATURE_CONSOLE=0` saves current until a meter says so.
2. **The `delay()` loop is the mechanism RAK explicitly rejects.** Two costs. The one RAK names is
   that the task cannot be woken by an event. For a Class A node the downlink case does not
   actually apply — Class A only receives in the windows after an uplink, so sleeping blind for
   1800 s is spec-correct, and I will not manufacture a gap there. The cost that does apply is
   that we wake the CPU once per second for 1800 iterations per cycle to do nothing, where the
   reference wakes zero times.

**A caution on adopting the semaphore path, which cuts the other way.** Our watchdog is
configured `WDT_CONFIG_SLEEP_Pause` (`src/power.cpp:51-52`), so it does not count while the CPU
sleeps. Combined with `xSemaphoreTake(..., portMAX_DELAY)`, a wake timer that fails to fire means
the node sleeps forever and **the watchdog never fires either** — an unrecoverable hang, i.e. a
hike. Our bounded `delay()` loop is structurally immune to that. If the semaphore path is adopted,
either use a bounded timeout rather than `portMAX_DELAY`, or set `WDT_CONFIG_SLEEP_Run` and accept
feeding the dog across the sleep. Do not port the reference's `portMAX_DELAY` verbatim.

---

## 2. LoRaWAN handling — we are ahead

Join failure, `[REF-API2-LORA]:224-234`, in full:

```cpp
void lpwan_join_fail_handler(void)
{
	API_LOG("LORA", "OTAA joined failed");
	API_LOG("LORA", "Check LPWAN credentials and if a gateway is in range");
	// Restart Join procedure
	API_LOG("LORA", "Restart network join request");
	g_join_result = false;

	// Notify loop task
	api_wake_loop(LORA_JOIN_FIN);
}
```

There is no backoff. The RAK sketch is worse still — `[REF-LOWPOWER]:199` on send failure is
literally `/// \todo maybe you need to retry here?`.

Ours: `src/radio.cpp:58-59` bounds backoff at 60 s growing to 3600 s; `:53` sets
`JOINREQ_NBTRIALS 1` with the explicit reasoning that "retrying inside the MAC keeps the radio
powered with no gap, whereas returning after a single attempt lets this class sleep between
tries"; `:257-262` reports the true next-attempt time as backoff × cycles rather than the
backoff alone. `:328-331` waits for `kFailuresBeforeRejoin` (3) consecutive failures before
dropping the session, with `:319-327` explaining why rejoining on a single generic failure is the
most expensive loop the node can enter.

ADR and sub-band are equivalent-or-better: `src/radio.cpp:150-152` sets `LORAWAN_ADR_ON` (same as
`[REF-API2-LORA]:125`), and `:200` calls `lmh_setSubBandChannels(2)` with `:193-196` documenting
that without it seven in eight uplinks reach nobody. RX windows: `src/radio.cpp:40-47` derives the
wait from the MAC's reported delays with a 1500 ms drift margin and a 7000 ms fallback chosen for
TTN's 5 s RX1 delay — the references simply return to sleep and let the library's own task handle
it.

`LORAWAN_DUTYCYCLE_OFF` at `src/radio.cpp:152` is correct for US915, which is FCC dwell-time
governed rather than duty-cycle governed; `[REF-API2-LORA]:71-72` notes the setting exists for
ETSI.

### The one LoRaWAN gap: `lmh_reset_mac()` is never called

`[REF-API2-LORA]:202-216` exists specifically for a known MAC-layer bug:

```cpp
/**
 * @brief Re-init LoRaWAN stack
 *     Workaround for bug after NAK
 ...
int8_t re_init_lorawan(void)
{
	lmh_reset_mac();
	return 0;
}
```

`lmh_reset_mac` appears **nowhere** in our tree. Our recovery at `src/radio.cpp:328-331` sets
`m_joined = false` and rejoins, but leaves the MAC in whatever state produced the failures.
RAK's own engineer shipped a MAC reset because rejoining on top of a wedged MAC was not enough.
**Consequence:** if we hit the NAK bug in the field, our rejoin may loop against a MAC that cannot
succeed — recoverable only by watchdog reset, which we do have, so this is a battery-cost and
data-loss issue rather than a hike. **What to change:** call `lmh_reset_mac()` on the
`kFailuresBeforeRejoin` path before `lmh_join()`.

---

## 3. Watchdog and recovery — we are far ahead

Grepped `[REF-API2-LOOP]`, `[REF-API2-FN]`, `[REF-API2-LORA]`, `WisBlock-API-V2.h`, and
`[REF-DEEPSLEEP]`: **no watchdog of any kind.** No `NRF_WDT`, no `Adafruit_SleepyDog`, no feed.
The only reset facility is `api_reset()` at `[REF-API2-FN]:88-97`, a software `NVIC_SystemReset()`
invoked by a BLE command — i.e. it requires a human standing next to the node, which is precisely
the resource the deployment does not have.

Ours: `src/power.cpp:35-58` arms the hardware WDT at 120 s, and `:43-45` reads and clears
`RESETREAS` first so `reset_was_watchdog()` can report *why* the node restarted. Fed at seven
call sites across `main.cpp`, `owscan.cpp`, `busscan.cpp`, and `battery.cpp`, so a hung Modbus or
one-wire read resets rather than parks. This has no counterpart in any reference.

The `WDT_CONFIG_SLEEP_Pause` choice at `:51-52` is right for our current design and is documented
as such at `:48-50`; the interaction risk is the one flagged in §1.

---

## 4. Sensor bus handling — we are far ahead

`[REF-RS485]` is 54 lines. Its entire `loop()`:

```cpp
void loop()
{
	RS485.beginTransmission();
	/* IO2 HIGH  3V3_S ON */
	pinMode(WB_IO2, OUTPUT);
	digitalWrite(WB_IO2, HIGH);
	delay(300);
	RS485.write("hello world\n");
	RS485.endTransmission();
	...
	delay(1000);
}
```

It writes an ASCII string. There is **no Modbus, no CRC, no slave address, no function code, no
reply parsing, no timeout, and no retry** — and it toggles `WB_IO2` *after* beginning the
transmission. There is no RAK5802 Modbus example for the RAK4631 in the repo at all; the only
`Modbus`-named RS485 assets are install screenshots (`assets/Arduino/lib-modbus-install.png`) and
an RTU master/slave pair under `examples/RAK11200/`, a different MCU.

Ours: `src/sensors/modbus.cpp:13-23` computes the inter-frame gap from baud per
`[CIT-MODBUS-SERIAL]`'s 3.5-character silence rule; `:29` sets a 1000 ms reply timeout; `:66-68`
builds and `:122-139` verifies CRC on both the exception and the data path; `:114-125`
distinguishes a retryable framing error from a genuine exception response; `:152-168` retries with
attempt-accurate logging. There is nothing to be behind on here — the reference does not attempt
the problem.

---

## 5. Where else we exceed the references

All of these have no counterpart in any file cited above:

- **Brownout gating with persistence.** `src/power.cpp:153-266`. Holds transmissions below a
  voltage threshold, persists the hold across reset (`:247-256`, one flash write per event, taken
  at the one moment the pack can afford it), and — the subtle part — `:180-237` handles a hold that
  is *no longer backed by a reading*, arming a keepalive so a silent pack cannot leave the node
  permanently mute and uncommandable. The references have no concept of pack state at all;
  `[REF-API2-FN]` reads battery only to report a level to the network.
- **Session persistence across reset.** `src/radio.cpp:210` restores the session at boot and
  `:242` stores it immediately on join, so a watchdog reset does not require a gateway handshake
  before the next reading. `[REF-DEEPSLEEP]` rejoins from scratch on every boot.
- **Decoder parity as a build gate.** `scripts/check_decoder_parity.py` against the live TTN
  formatter. No RAK example has a downstream contract to drift from, so there is nothing
  equivalent — but for us a drifted encoder makes the decoder throw and discard the whole uplink.
- **Evidence discipline.** `docs/EVIDENCE.md` requires host + commit SHA + raw observation before
  any behavioral claim. `[REF-LOWPOWER]:213` closes with "This code is written as an example and is
  not perfect in all parts" — the references make no claims to substantiate, and that asymmetry is
  the honest framing of this whole comparison. They are demo code; we are building something that
  has to survive without us.
- **Watchdog reset attribution**, §3.
- **Sub-band correctness with the reasoning recorded**, `src/radio.cpp:193-203`.

---

## 6. Does ADR-0003's decision still hold?

**Yes on the framework, but the ADR's own text has drifted and should be corrected.**

`ADR-0003-firmware-framework.md:23` states the decision as "**Arduino + WisBlock-API-V2**" while
`:3` says WisBlock-API-V2 "was not adopted," superseded in part by ADR-0005. A reader arriving at
the Decision line gets the wrong answer. That is a documentation defect, not a firmware one.

The substantive reasoning still holds, and is now stronger than when written:

- ADR-0003:38-43 adopted the framework for its "event-driven loop… to hold the MCU in sleep." That
  is the one thing we did not take, and §1 above is the price. But the value was always the
  *sleep primitive*, ~6 lines (`[REF-API2-FN]:110-115`), not the framework. Adopting a BLE stack,
  an AT-command parser, a flash settings layer, and a Cayenne encoder to obtain a
  `xSemaphoreTake()` is a poor trade — particularly the AT/BLE surface, which is remote-attack
  surface and code that must not hang on a node nobody can reach.
- The framework has **no watchdog** (§3) and **no brownout concept** (§5), both of which we would
  have had to add on top regardless.
- ADR-0003:56-58 anticipated inheriting "the framework's join-retry behavior… exactly the code
  path [CIT-RAK-SLEEP] warns about." §2 confirms that concern was well founded:
  `[REF-API2-LORA]:224-234` restarts join with no backoff. Our own backoff is better. Adopting the
  framework now would be a regression on join behavior.

**Recommendation:** keep the decision; fix the ADR's Decision line to match its Status; and port
the semaphore sleep primitive and `lmh_reset_mac()` as two small targeted changes rather than
reopening the framework question.

---

## 7. Ranked gap list

| # | Gap | Consequence | Fix |
|---|---|---|---|
| 1 | ~~Console compiled into the field image~~ **WITHDRAWN — not a gap.** `[REF-LOWPOWER]:45`'s mechanism is false, and false on RAK's own BSP: the core calls `Serial.begin()` and creates the `usbd` task before `setup()`, so our call is a no-op and RAK's `MAX_SAVE` builds get the same task. See §"Verdict" item 1, [ADR-0008](../decisions/ADR-0008-console-in-the-field-image.md) | **None.** No task, peripheral or clock is removed by the flag | Nothing. `FEATURE_CONSOLE=0` was tried in `094d5f5` and reverted the same day; `detach()` in `power.cpp:135` was already the correct lever. Magnitude-only question tracked in #47 |
| 2 | `delay(1000)` loop instead of `xSemaphoreTake(..., portMAX_DELAY)` (`power.cpp:134-136`) | Battery life; magnitude unmeasured. **Do not adopt `portMAX_DELAY` without fixing `WDT_CONFIG_SLEEP`** or you trade current for a hike | Semaphore + FreeRTOS timer, bounded timeout |
| 3 | `lmh_reset_mac()` never called (`[REF-API2-LORA]:210-216`, "Workaround for bug after NAK") | Data loss and wasted energy in a rejoin loop; not a hike (watchdog covers it) | Call it on the `kFailuresBeforeRejoin` path |
| 4 | ADR-0003 Decision line contradicts its Status line | Documentation only; misleads the next reader | Edit the ADR |

Nothing in categories 2, 3, or 4 of the reference set puts us behind on LoRaWAN policy, watchdog,
or sensor-bus handling — on those we exceed the references substantially.

---

## 8. What I could not verify

- **Any sleep-current figure for a LoRaWAN build.** The only measured RAK number I found is
  `[REF-P2PSLEEP]:17`'s **120 µA**, and it is explicitly the P2P transmit-only build. I found no
  published µA figure for `[REF-DEEPSLEEP]` itself, so "reference achieves X, we achieve Y" cannot
  be stated. Our figure is likewise unmeasured (`src/power.cpp:117-120` says so, tracked as issue
  #47). **The gap-1 and gap-2 magnitudes are therefore predictions from RAK's documentation, not
  measurements.** Tonight's meter settles both; a single reading with the console compiled out
  versus in would confirm or kill gap 1 by itself.
- **Whether the Adafruit nRF52 core's FreeRTOS uses tickless idle**, which determines how much the
  1-per-second `delay()` wakeups actually cost. Not checked — it decides gap 2's real size and is
  the one thing that could reduce gap 2 to a formality.
- **Whether `TinyUSBDevice.detach()` alone quiesces the background serial task.** `[REF-LOWPOWER]`
  addresses only the never-initialized case, so it does not answer the detach case. This is the
  crux of gap 1 and is a meter question, not a documentation question.
- **Watchdog absence in WisBlock-API-V2 beyond the files I fetched.** Verified absent in
  `WisBlock-API.cpp`, `api_functions.cpp`, `lorawan.cpp`, `WisBlock-API-V2.h`, and
  `[REF-DEEPSLEEP]`. I did not fetch `at_cmd.cpp`, `bat.cpp`, the BLE or flash back-ends, or the
  ESP32/RP2040 paths. GitHub code search needs auth, so this is a bounded rather than exhaustive
  claim.
- **RAKwireless's hosted documentation site** (`docs.rakwireless.com`) low-power and LoRaWAN
  quick-start pages — not fetched, out of time budget. The GitHub sources were the higher-value
  target since the operator's standard names GitHub specifically.
- **RUI3's sleep implementation**, which is RAK's other reference path and is closed-source at the
  API level. Out of scope for a GitHub comparison but it is where RAK's best-tuned low-power code
  probably lives.
