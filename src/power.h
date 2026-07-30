/*
 * Watchdog and sleep.
 *
 * Two jobs, both of which decide whether the node is still reporting in March.
 *
 * The watchdog guards the awake part of the cycle only. On this chip the watchdog cannot
 * be stopped once started and its timeout is fixed at start time, so a timeout long enough
 * to span an hour of sleep would be uselessly long for catching a stuck sensor read. The
 * way out is a configuration bit that pauses the counter while the CPU sleeps: the timer
 * then measures awake time, which is exactly the window where a hang can happen.
 *
 * Sleep has to actually be sleep. Powering the CPU down does not power anything else down
 * — the radio keeps its own state, and any UART or SPI left enabled keeps its clock
 * running. Each of those costs around a milliamp, which over an hourly cycle dwarfs
 * everything the node does while awake.
 *
 * CITE(datasheet): [CIT-NRF-WDT] the watchdog's registers lock on start and only a reset
 *   clears them; timeout is (CRV + 1) / 32768 seconds; CONFIG.SLEEP chooses whether the
 *   counter runs while the CPU is asleep.
 * CITE(datasheet): [CIT-NRF-POWER] sleep modes do not disable peripherals automatically.
 * CITE(prior-art): [CIT-RAK-SLEEP] the WisBlock API author's account of the two failures
 *   this file exists to avoid: sleeping the MCU without also sleeping the SX1262, and a
 *   node that cannot join never sleeping at all. A reporter measured 6 mA in that state.
 */

#pragma once

#include <stdint.h>

namespace power {

// Starts the watchdog with a timeout covering the longest legitimate awake period. Once
// this returns the watchdog is running and cannot be turned off — only fed or reset.
void watchdog_begin(uint32_t timeout_seconds);

// Feeds the watchdog. Call at points that prove real progress, not on a timer: a feed
// inside a loop that is itself stuck defeats the entire mechanism.
void watchdog_feed();

// True when the last reset came from the watchdog rather than power-on. Worth surfacing,
// because a node quietly resetting every cycle otherwise looks perfectly healthy.
bool reset_was_watchdog();

// Shuts down peripherals, then sleeps for the requested time. Returns once the interval
// has elapsed and the console is usable again.
void sleep_seconds(uint32_t seconds);

} // namespace power
