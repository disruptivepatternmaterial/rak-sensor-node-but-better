#include "power.h"

#include "build_features.h"

#include <Arduino.h>

#if FEATURE_RADIO
#include <SX126x-Arduino.h>
#endif

namespace {

// The 32.768 kHz clock the watchdog counts. Timeout is (CRV + 1) / 32768 seconds.
constexpr uint32_t kWatchdogTicksPerSecond = 32768;

bool s_reset_was_watchdog = false;
bool s_watchdog_running   = false;

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
#endif

    // Someone with a cable attached is diagnosing the node, and shutting the console down
    // between cycles would disconnect them at the first sleep — leaving the field-repair
    // case worse off than the deployment it is meant to protect. Unattended, there is no
    // host to disconnect, so the saving is taken.
    const bool console_in_use = (bool)Serial;

    if (!console_in_use) {
        // Closing the USB serial device is required, not tidy-up. The USB peripheral keeps
        // its clock and its pull-up alive otherwise, and the measured difference between
        // leaving it on and shutting it down is roughly a milliamp — far more than
        // everything the node spends on actually taking a reading.
        Serial.end();

        // Peripherals are per-instance; the ones this node touches are closed by their
        // owners when a read finishes. This disables the USB device itself, which nothing
        // else owns. Written straight to the register: the core's USB stack has no public
        // "power this down" call, and leaving the peripheral enabled is the single largest
        // sleep-current mistake available on this chip.
        NRF_USBD->ENABLE = 0;
    }

    // FreeRTOS underlies the Arduino core here, so a delay parks this task and the idle
    // task drops the CPU into its low-power state until the tick that wakes us. Split into
    // one-second slices so the interval stays well inside the tick counter's range at any
    // configured length.
    //
    // This reaches the chip's lighter sleep state, not its deepest one — the deepest state
    // restarts the chip on wake, which would mean rejoining the network or reconstructing
    // the session from flash every interval. The difference is a couple of microamps
    // against a pack measured in amp-hours, so the simpler behavior is worth keeping until
    // a bench measurement says otherwise.
    for (uint32_t i = 0; i < seconds; i++) {
        delay(1000);
    }

#if FEATURE_RADIO
    // Back before anything can talk to the transceiver again.
    SPI_LORA.begin();
#endif

#if FEATURE_CONSOLE
    if (!console_in_use) {
        // Bring the console back for the awake portion. With no host attached this
        // enumerates and goes nowhere, which costs a little current only while awake.
        Serial.begin(115200);
    }
#endif
}

void Brownout::update(bool voltage_valid, uint16_t centivolts)
{
    if (!voltage_valid) {
        return; // no news is not good news; hold the previous decision
    }

    if (!m_engaged && centivolts <= kTxInhibitCentivolts) {
        m_engaged = true;
        LOGF("   power   : %u.%02u V — holding transmissions to protect the pack\n",
             centivolts / 100, centivolts % 100);
        return;
    }

    if (m_engaged && centivolts >= kTxResumeCentivolts) {
        m_engaged = false;
        LOGF("   power   : %u.%02u V — recovered, resuming\n", centivolts / 100,
             centivolts % 100);
    }
}

} // namespace power
