#include "power.h"

#include "build_features.h"

#include <Arduino.h>

#if FEATURE_CONSOLE
// CITE(prior-art): Adafruit TinyUSB for Arduino — `Adafruit_USBD_Device::detach()`/`attach()`
// wrap `tud_disconnect()`/`tud_connect()`, which on the nRF5x port set `USBD->USBPULLUP` and
// raise the stack's unplugged event. The one attach/detach pair the core exposes as public
// API, and symmetric — unlike a bare `USBD->ENABLE` write, which the core only ever restores
// from its VBUS power-event handler. [CIT-TINYUSB] — docs/CITATIONS.md
// CITE(datasheet): nRF52840 Product Specification — USBD [CIT-NRF-USBD]. Endpoint traffic is
// host-paced, so a host that still believes it is enumerated will keep issuing IN tokens
// against endpoints the device is no longer servicing. Detaching is what makes the host stop.
#include <Adafruit_TinyUSB.h>
#endif

#if FEATURE_RADIO
#include <SX126x-Arduino.h>
#endif

namespace {

// The 32.768 kHz clock the watchdog counts. Timeout is (CRV + 1) / 32768 seconds.
constexpr uint32_t kWatchdogTicksPerSecond = 32768;

bool s_reset_was_watchdog = false;
bool s_watchdog_running   = false;

// Slice length for the sleep wait below. Coarse enough that the per-cycle wakeup count is
// negligible, short enough that the tick conversion stays far inside a 32-bit tick count at
// any interval the configuration permits.
constexpr uint32_t kSleepSliceSeconds = 60;

// Created on first use rather than statically, because a static FreeRTOS object constructed
// before the scheduler starts is not usable. Never destroyed — it lives as long as the node.
SemaphoreHandle_t s_sleep_sem = nullptr;

#if FEATURE_CONSOLE
// How long after boot the node stays attached to USB even when nothing has opened the port.
//
// Without this the node is unobservable on a bench, which is issue #60 and which cost most of
// 2026-08-12. The detach below removes the D+ pull-up, so the device leaves the USB bus
// entirely — and `(bool)Serial` is only true once a host has opened the port and asserted DTR.
// Nothing has the port open in the seconds after a flash, so the node detached at the end of
// its first cycle and the port could not exist again until the next wake, an interval later.
// A capture started after the flash therefore waited for a device that could not appear. Three
// captures that day reproduced it, and the same shape killed the H8 soak attempt and was read
// as a hardware fault for hours (#58).
//
// 180 s because it has to cover the whole path from "flash finished" to "a human or a script
// has the port open": scripts/flash.sh itself waits up to 30 s for re-enumeration before it
// even returns, macOS then takes a second or two to publish the tty node, and an operator who
// walks over to the bench is the slow case this exists for. Three minutes once per boot is not
// a number worth optimising.
//
// WHY THIS CANNOT COST FIELD CURRENT, which is the only reason it is allowed to be
// unconditional. In the field there is no cable, so there is no VBUS, and NRF_USBD->ENABLE is
// only ever set from the VBUS power-event handler — the peripheral is never enabled at all, so
// there is nothing for detach() to switch off and skipping it changes nothing. The saving that
// ADR-0008 established detach() as the real lever for is a saving that only exists while a
// cable is attached, which is the bench case, and on the bench three minutes of USB is not a
// power question. After the window the previous behavior resumes exactly: detach() is NOT
// weakened, only deferred, and only once per boot.
//
// CITE(prior-art): [CIT-TINYUSB-CORE] dcd_nrf5x.c:280 — dcd_disconnect() clears
//   NRF_USBD->USBPULLUP, which is why detach() removes the device from the bus rather than
//   merely dropping its tty node.
// CITE(prior-art): [CIT-TINYUSB-CORE] dcd_nrf5x.c:927 — tusb_hal_nrf_power_event() is the only
//   writer of NRF_USBD->ENABLE, and it runs from the VBUS event. This is the fact that makes
//   the grace window free in the field.
// CITE(bench): docs/EVIDENCE.md 2026-08-12 (later) — the three captures that never attached,
//   and the ioreg showing the whole device gone rather than just the port.
constexpr uint32_t kBootGraceMs = 180UL * 1000UL;

// Latched rather than recomputed, because millis() wraps at ~49 days and `millis() <
// kBootGraceMs` would silently become true again on the far side of the wrap — handing a
// deployed node a second grace window a month and a half in, for no reason and with nobody
// there to use it. Once the window has closed it stays closed until the next reset.
bool s_boot_grace_expired = false;

bool in_boot_grace()
{
    if (s_boot_grace_expired) {
        return false;
    }
    if (millis() >= kBootGraceMs) {
        s_boot_grace_expired = true;
        return false;
    }
    return true;
}
#endif

} // namespace

namespace power {

void watchdog_begin(uint32_t timeout_seconds)
{
    if (s_watchdog_running) {
        return; // registers are locked; a second call cannot change anything
    }

    // RESETREAS latches every reset cause and is only cleared by writing back the bits.
    // Read it before the watchdog can contribute a new one.
    const uint32_t reason = NRF_POWER->RESETREAS;
    s_reset_was_watchdog  = (reason & POWER_RESETREAS_DOG_Msk) != 0;
    NRF_POWER->RESETREAS  = reason;

    // HALT: pause while a debugger has the core stopped, so a breakpoint is not a reset.
    // SLEEP: pause while the CPU sleeps, which is what lets a short timeout coexist with
    // a long sleep interval. Without this bit the timeout would have to exceed the whole
    // cycle and would no longer catch anything useful.
    NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
                      (WDT_CONFIG_SLEEP_Pause << WDT_CONFIG_SLEEP_Pos);

    NRF_WDT->CRV  = (timeout_seconds * kWatchdogTicksPerSecond) - 1;
    NRF_WDT->RREN = WDT_RREN_RR0_Msk;
    NRF_WDT->TASKS_START = 1;

    s_watchdog_running = true;
}

void watchdog_feed()
{
    if (s_watchdog_running) {
        NRF_WDT->RR[0] = WDT_RR_RR_Reload;
    }
}

bool reset_was_watchdog()
{
    return s_reset_was_watchdog;
}

void sleep_seconds(uint32_t seconds)
{
#if FEATURE_RADIO
    // The transceiver has its own power state. Sleeping the CPU leaves it drawing as if
    // it were still listening, which is one of the two documented ways this board's
    // battery life disappears.
    Radio.Sleep();

    // Putting the transceiver to sleep does not stop the bus that talks to it. The
    // controller on this chip stays clocked once started, and measurements put a bus left
    // running at close to a milliamp — an order of magnitude more than everything else the
    // node draws while asleep, and enough to turn a solar-sustained deployment into one
    // that slowly loses ground through a cloudy stretch.
    SPI_LORA.end();
#endif

#if FEATURE_CONSOLE
    Serial.flush();

    // Someone with a cable attached is diagnosing the node, and shutting the console down
    // between cycles would disconnect them at the first sleep — leaving the field-repair
    // case worse off than the deployment it is meant to protect. Unattended, there is no
    // host to disconnect, so the saving is taken.
    //
    // The whole block is inside FEATURE_CONSOLE now. It was not: the detach below sat outside
    // the guard while the matching attach and the Adafruit_TinyUSB include sat inside it, so a
    // FEATURE_CONSOLE=0 build did not compile. Nothing set that flag to 0 until the field
    // environment below started doing exactly that, which turned a latent break into a live
    // one. Refs docs/reviews/2026-08-12_adversarial_review.md.
    const bool console_in_use = (bool)Serial;

    // The grace window is checked here rather than folded into console_in_use because the two
    // say different things and the log line below has to be able to tell them apart: one is
    // "somebody is watching", the other is "nobody is watching yet, but they still might".
    const bool grace = in_boot_grace();

    if (grace && !console_in_use) {
        // Announced, because a silent deferral is indistinguishable from the bug it fixes. An
        // operator who sees no port and no explanation goes looking for a hardware fault, which
        // is exactly what happened before this existed.
        LOGF("   power   : USB kept attached for the first %lu s so a console can attach (#60)\n",
             (unsigned long)(kBootGraceMs / 1000));
    }

    if (!console_in_use && !grace) {
        // Detaching releases the D+ pull-up and tells the device stack it is unplugged, so
        // the host stops polling and the endpoints stop being serviced across the sleep.
        //
        // This deliberately does not touch NRF_USBD->ENABLE. Clearing that register by hand
        // is not reversible from application code: the core only re-runs the enable sequence
        // — errata 171/187/166, the READY handshake, the HFCLK start — from its USB power
        // event handler, which fires on a VBUS transition and never again. A node that slept
        // once with no host attached came back with a permanently dead console for the rest
        // of the boot, which is what made every hardware session unverifiable.
        //
        // Serial.end() is not called either, and that is the second half of the same bug: it
        // clears the whole configuration descriptor, so the interface the host enumerated no
        // longer matches the one the device offers, and there is no re-enumeration to
        // reconcile them. Leaving the CDC interface in place and cycling only the attach
        // state keeps the descriptor and the host's view of it identical.
        //
        // The peripheral itself therefore stays enabled while asleep, where the old code
        // intended to shut it down. Both the old and the new residual draw are unmeasured —
        // the old write left the pull-up, the USBD interrupt, and HFCLK all running, so it
        // was never the documented teardown either. Measurement is issue #47; a console that
        // works is worth more than an unverified microamp claim in the meantime.
        TinyUSBDevice.detach();
    }
#endif

    // Block on a semaphore with a bounded timeout, in coarse slices.
    //
    // RAK's low-power document rejects the delay() loop this replaces — "for most scenarios the
    // delay is not a good solution," because a task inside delay() cannot be woken by an event
    // — and both the shipped sketch and WisBlock-API-V2 use xSemaphoreTake() instead.
    //
    // What is deliberately NOT copied is their portMAX_DELAY. Our watchdog is configured
    // WDT_CONFIG_SLEEP_Pause above, so it does not count while the CPU sleeps: an indefinite
    // wait whose wake source fails to arrive is a node asleep forever with no watchdog left to
    // rescue it, which is the one outcome that costs a hike. The timeout is the bound, and it
    // is what makes this safe where the reference is not. A slice, rather than one long wait,
    // keeps the tick arithmetic small at any configured interval and gives the loop a place to
    // re-check its own progress.
    //
    // Sixty seconds a slice rather than one: same bounded structure, 1/60th of the scheduler
    // wakeups per cycle — 30 per 1800 s interval instead of 1800, each of which previously woke
    // the CPU only to find nothing to do. Nothing gives this semaphore yet, so today it behaves
    // as a coarser bounded wait; the value now is the reduced wakeup count, and the shape is
    // what lets a sensor interrupt wake the node later without reopening the watchdog question.
    //
    // Honest about magnitude: this is unmeasured, and it is the smaller of the two sleep-path
    // changes in this commit. Whether it matters at all depends on the core's tickless-idle
    // behavior, which is unverified — see issue #47 and the benchmark's §8.
    //
    // This reaches the chip's lighter sleep state, not its deepest one — the deepest state
    // restarts the chip on wake, which would mean rejoining the network or reconstructing
    // the session from flash every interval.
    //
    // CITE(prior-art): RAKwireless/WisBlock Low_Power_Example.md:13 rejects delay(); :140-145
    //   uses `xSemaphoreTake(taskEvent, portMAX_DELAY)`. [CIT-RAK-LOWPOWER] — docs/CITATIONS.md
    // CITE(prior-art): beegee-tokyo/WisBlock-API-V2 src/api_functions.cpp:110-115 —
    //   `api_wait_wake()` is the same primitive inside the framework. [CIT-WISBLOCK-API2]
    // CITE(datasheet): [CIT-NRF-WDT] — CONFIG.SLEEP pauses the counter while the CPU sleeps,
    //   which is the fact that makes portMAX_DELAY unsafe here and the timeout mandatory.
    if (s_sleep_sem == nullptr) {
        s_sleep_sem = xSemaphoreCreateBinary();
    }

    for (uint32_t remaining = seconds; remaining > 0;) {
        const uint32_t slice = (remaining > kSleepSliceSeconds) ? kSleepSliceSeconds : remaining;

        if (s_sleep_sem != nullptr) {
            // pdTRUE means something woke us early and the rest of the interval is forfeit
            // on purpose — an event worth waking for is worth acting on now.
            if (xSemaphoreTake(s_sleep_sem, pdMS_TO_TICKS(slice * 1000)) == pdTRUE) {
                break;
            }
        } else {
            // The semaphore could not be allocated. Falling back keeps the sleep bounded,
            // which is the property that matters; a node that cannot allocate 80 bytes has a
            // larger problem and the watchdog will find it once awake.
            delay(slice * 1000);
        }

        remaining -= slice;
    }

#if FEATURE_RADIO
    // Back before anything can talk to the transceiver again.
    SPI_LORA.begin();
#endif

#if FEATURE_CONSOLE
    if (!console_in_use) {
        // The exact inverse of the detach above. Restoring the pull-up lets a host enumerate
        // again, so a cable plugged in mid-deployment gets a console at the next awake
        // window instead of never.
        TinyUSBDevice.attach();
    }
#endif
}

void Brownout::begin(bool persisted_engaged, BrownoutPersistFn persist)
{
    m_persist       = persist;
    m_engaged       = persisted_engaged;
    m_invalid_reads = 0;

    if (m_engaged) {
        // The whole point of persisting the bit. Before this, any reset — watchdog, panel
        // flicker, the pack's own brownout reset — returned the node to transmit-allowed,
        // and it only found its way back if the pack was still answering.
        LOGLN(F("   power   : brownout hold restored from flash — transmissions held"));
    }
}

void Brownout::set_engaged(bool engaged, bool persist)
{
    m_engaged = engaged;
    if (persist && m_persist != nullptr) {
        m_persist(engaged);
    }
}

void Brownout::note_keepalive_sent()
{
    m_keepalive.note_sent();
}

void Brownout::update(bool voltage_valid, uint16_t centivolts)
{
    if (!voltage_valid) {
        if (m_engaged) {
            // A hold that is not already a no-evidence hold has to be able to become one, or
            // the keepalive clock below never starts. Two ways in, and both end with a node
            // that is mute and uncommandable for as long as the link stays down:
            //
            //   - begin() restored the hold from flash. It cannot know why the bit was set, so
            //     it leaves m_without_evidence false. Every later cycle then hits this branch.
            //   - The hold was set by a measured low voltage, and the pack has since stopped
            //     answering. The hold is now resting on a reading we can no longer confirm.
            //
            // The comment this replaces was right that a hold backed by a *current* low
            // reading earns no keepalive. It stops being current once the pack goes silent.
            if (!m_without_evidence) {
                // The hold is already in progress, so these unreadable cycles advance its
                // reachability clock without permitting a transmission yet. Restarting the
                // clock when the fourth miss becomes "no evidence" lets a link that answers
                // once every five cycles erase elapsed time forever.
                m_keepalive.advance(false);
                if (m_invalid_reads < kInvalidReadsBeforeInhibit) {
                    ++m_invalid_reads;
                }
                if (m_invalid_reads >= kInvalidReadsBeforeInhibit) {
                    m_without_evidence = true;
                    m_keepalive.set_permitted(true);
                    const uint16_t remaining =
                        (m_keepalive.held_cycles() < kNoEvidenceKeepaliveCycles)
                            ? (uint16_t)(kNoEvidenceKeepaliveCycles -
                                         m_keepalive.held_cycles())
                            : 0;
                    LOGF("   power   : hold no longer backed by a reading after %u silent "
                         "cycles — keepalive in %u cycles\n",
                         (unsigned)kInvalidReadsBeforeInhibit,
                         (unsigned)remaining);
                }
                return;
            }

            m_keepalive.advance(true);
            return; // already holding; no news is certainly not good news
        }

        if (m_invalid_reads < kInvalidReadsBeforeInhibit) {
            ++m_invalid_reads;
        }

        if (m_invalid_reads >= kInvalidReadsBeforeInhibit) {
            // Not persisted, unlike the voltage-driven transition below. Engaging here
            // means the pack voltage is unknown, and an unknown voltage is exactly the
            // condition under which a flash write is not worth risking — that is what
            // flash_write_allowed() exists to say. The cost of not writing is that a reset
            // clears this particular hold, and the node then re-engages within
            // kInvalidReadsBeforeInhibit cycles for the same reason it did the first time.
            // Bounded and self-correcting, which the old fail-open behavior was not.
            set_engaged(true, false);
            m_without_evidence = true;
            m_keepalive.start(true);
            LOGF("   power   : pack silent for %u cycles — holding transmissions, no "
                 "voltage evidence (keepalive in %u cycles)\n",
                 (unsigned)kInvalidReadsBeforeInhibit,
                 (unsigned)kNoEvidenceKeepaliveCycles);
        }
        return;
    }

    m_invalid_reads = 0;

    // Any valid reading ends the no-evidence condition, whatever it says.
    m_without_evidence = false;

    if (!m_engaged && centivolts <= kTxInhibitCentivolts) {
        // Persisted, and this is the write the whole scheme is built around. It happens on
        // the transition into brownout, which by definition happens while the pack is still
        // answering and still above the threshold below which a write is unsafe — so it
        // costs exactly one write per brownout event, at the one moment it is affordable.
        set_engaged(true, true);
        m_keepalive.start(false);
        LOGF("   power   : %u.%02u V — holding transmissions to protect the pack\n",
             centivolts / 100, centivolts % 100);
        return;
    }

    if (!m_engaged) {
        // A valid reading above the inhibit floor while no hold is active is the healthy path.
        // There is no silence to remember and no keepalive to arm.
        m_keepalive.clear();
        return;
    }

    if (m_engaged && centivolts >= kTxResumeCentivolts) {
        // Clearing the bit matters as much as setting it. A stale hold left in flash would
        // silence a recovered node across its next reset, turning a protective measure into
        // the outage it exists to prevent.
        set_engaged(false, true);
        m_keepalive.clear();
        LOGF("   power   : %u.%02u V — recovered, resuming\n", centivolts / 100,
             centivolts % 100);
        return;
    }

    // The hold remains engaged. The keepalive clock measures its total duration since the last
    // keepalive, not a consecutive run in one voltage class. That distinction is load-bearing:
    // the previous code reset this clock on every reading at or below the inhibit floor. A
    // solar pack alternating 9.5 V / 9.7 V therefore disarmed at 9.5 V, re-armed at 9.7 V with
    // the clock back at zero, and repeated forever. The keepalive could never become due, so
    // the Class A node was permanently mute and uncommandable through exactly the threshold
    // oscillation hysteresis is supposed to survive.
    //
    // Low evidence still wins for the *current* cycle: at or below the floor, armed is false
    // and keepalive_due() cannot authorize a transmission. What low evidence may not do is
    // erase elapsed hold time. Once a later reading is inside the hysteresis band, the node may
    // use that accumulated time and transmit immediately if the bound has expired. Thus the
    // pack is never charged a keepalive on a measured-low cycle, while no pattern of crossings
    // can postpone the next reachable cycle forever.
    //
    // CITE(policy): docs/POWER_BUDGET.md — the pack must never reach a state it cannot recover
    //   from unaided; erasing the reachability clock on every threshold crossing violates it.
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H3 — the brownout hold protects the pack; it is not
    //   licensed to end the deployment.
    // CITE(policy): [CIT-TTN-FUP] — one bounded keepalive sits far inside the airtime allowance.
    const bool was_armed = m_keepalive.armed();
    m_keepalive.advance(centivolts > kTxInhibitCentivolts);
    if (!m_keepalive.armed()) {
        return;
    }

    if (!was_armed) {
        const uint16_t remaining = kNoEvidenceKeepaliveCycles - m_keepalive.held_cycles();
        LOGF("   power   : %u.%02u V — holding, above the %u.%02u V floor but below the "
             "%u.%02u V resume point, so a keepalive is armed (in %u cycles)\n",
             centivolts / 100, centivolts % 100, kTxInhibitCentivolts / 100,
             kTxInhibitCentivolts % 100, kTxResumeCentivolts / 100, kTxResumeCentivolts % 100,
             (unsigned)remaining);
    }
}

} // namespace power
