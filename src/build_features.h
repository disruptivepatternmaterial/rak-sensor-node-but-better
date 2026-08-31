/*
 * Feature switches — bring one subsystem up at a time.
 *
 * The subsystem switches default ON: RK900, BATTERY, RADIO, SLEEP, WATCHDOG, CONSOLE.
 * The diagnostic and experimental switches below them default OFF and are turned on only by
 * their own environment — a scanner that drives the bus with addresses the node has no
 * business using must never be one missing flag away from a field image.
 *
 * platformio.ini defines cut-down environments that turn subsets OFF, so a failure can be
 * isolated to one subsystem without editing code:
 *
 *   pio run -e stage1    wind sensor only, printed over USB
 *   pio run -e stage2    + battery
 *   pio run -e stage3    + radio (still awake, still printing)
 *   pio run -e rak4631   everything, including sleep — the field image
 *
 * Sleep is the last thing switched on deliberately: a sleeping board with no USB console
 * is the hardest state to debug, so nothing else should still be in question by then.
 */

/*
 * Named build_features.h, not features.h. The C library has its own <features.h> and
 * includes it from inside almost every system header. The off-target test build puts src/
 * on the include path, so a file named features.h here is found instead — and the C
 * library then compiles without any of its own configuration macros. The result is
 * hundreds of errors inside stdio.h, stdint.h, and cmath, none of which mention this file.
 */

#pragma once

// The logging macros below expand to Serial calls, so anything that logs needs this —
// pulling it in here means no module has to remember.
//
// Guarded because the off-target tests build the same sources on a workstation, where
// there is no Arduino at all. Without the guard the payload encoder and the checksum
// cannot be tested off the board, which is precisely the code most worth testing that way:
// both fail silently in the field, producing plausible wrong numbers rather than an
// obvious fault.
#if defined(ARDUINO)
#include <Arduino.h>
#endif

#ifndef FEATURE_RK900
#define FEATURE_RK900 1
#endif

#ifndef FEATURE_BATTERY
#define FEATURE_BATTERY 1
#endif

#ifndef FEATURE_RADIO
#define FEATURE_RADIO 1
#endif

#ifndef FEATURE_SLEEP
#define FEATURE_SLEEP 1
#endif

#ifndef FEATURE_WATCHDOG
#define FEATURE_WATCHDOG 1
#endif

// Console output. Left ON in the field image: with no USB host attached the CDC writes are
// discarded, so the cost is a few microseconds of formatting per cycle, and having it
// already compiled in means diagnosing a returned unit needs no reflash.
#ifndef FEATURE_CONSOLE
#define FEATURE_CONSOLE 1
#endif

// Bench cadence. OFF everywhere by default, and config.h refuses to compile it together
// with FEATURE_RADIO, so the field image cannot acquire it by accident or by someone
// copying a stage environment's flags. When ON, the reporting interval floor and default
// both drop to 60 s so the operator can watch a sensor respond at the bench instead of
// waiting out a half-hour field interval.
//
// Deliberately not a runtime setting and deliberately not a downlink. A node already in
// the woods must have no path to a 60 s cadence at all — see docs/FIRMWARE_SPEC.md §4.
#ifndef FEATURE_BENCH_INTERVAL
#define FEATURE_BENCH_INTERVAL 0
#endif

// Bring-up diagnostic, OFF everywhere except its own environment. Sweeps the RS-485 line
// and reports the raw bytes seen, instead of the driver's single "timeout" verdict. It
// answers one question the normal path cannot: did the sensor say nothing at all, or did it
// answer in a framing this build cannot read. Never compiled into a field image — it drives
// the bus with addresses the node has no business talking to.
#ifndef FEATURE_BUS_SCAN
#define FEATURE_BUS_SCAN 0
#endif

// FEATURE_ONEWIRE_SCAN, OWSCAN_CENSUS_ONLY, OWSCAN_PIN and src/diagnostics/owscan.{h,cpp} were
// DELETED 2026-08-30. DO NOT RECREATE ANY OF IT — not a passive variant, not behind a flag. A
// diagnostic that drives an unqualified pad at 14x the production rate (192 bytes of 0x55 per
// cycle, against ~14 bytes per 900 s) is wrong on its own terms, whether or not it is what
// destroyed the NINE dead pads — the electrical mechanism is NOT established, but the provenance
// is: every one of those pads died running agent-written diagnostic firmware that the operator
// never authorized and never invoked, flashed by agents over SSH across multiple sessions. Zero
// pads have been destroyed by the production image. Qualify a pad with a meter against a
// known-good pin on the same core; read an unknown line with the logic analyzer, never a pad.
// And do not flash this board unless the operator asked for that specific flash.
//
// Full reasoning, and the line between what was measured and what is inferred:
// AGENTS.md § "NEVER DRIVE THE ONE-WIRE PAD FROM A DIAGNOSTIC".
// Procedure: docs/HARDWARE.md § "Qualifying the pack harness".
//
// CITE(bench): docs/EVIDENCE.md 2026-08-30 — pack drive levels, 9,520 edges in 57 s, and the
//   5.6 kOhm / 240 kOhm damaged-versus-healthy pad comparison on one core.
// CITE(datasheet): [CIT-NRF-GPIO-TOTAL] nRF52840 standard drive sink/source 1/2/4 mA — the
//   continuous rating, distinct from the 15 mA damage limit an earlier session cleared against.
// CITE(datasheet): [CIT-SALEAE-LOGICPRO8] Logic Pro 8 tolerates -25 V to +25 V at 2 MOhm,
//   against a pad rated 3.6 V powered and 0.3 V unpowered — why the analyzer is the instrument.

// Deliberately no FEATURE_BATTERY_MODBUS. A raw Modbus read at slave 0x6E on the pack's
// one-wire line drew 0 bytes on every cycle — the Generic Probe IO adapter does not bridge a
// Modbus frame arriving from the north — while the SensorHub path on the same wire now returns
// real measurements. See src/sensors/battery.cpp and docs/EVIDENCE.md 2026-08-05.

// Diagnostic cadence for the battery link, OFF everywhere except its own environment.
// Collapses the provisioning window and the unsolicited-report listen from 45 s + 20 s to a
// couple of seconds each, so one experiment costs seconds instead of two minutes. Never in a
// field image: a short provisioning window is exactly the thing the long one exists to rule
// out.
#ifndef FEATURE_BATTERY_FAST
#define FEATURE_BATTERY_FAST 0
#endif

// Milliseconds to wait after the pack's last byte before transmitting the first byte of the
// provisioning response.
//
// The reference master cannot reply faster than about 2 ms: its drain is
// `while (available()) { read(); delay(2); }`, so the last iteration always pays one 2 ms delay
// after the final byte. Our early-exit drain returns the instant the checksum byte lands and
// transmits under one bit time later — so on an open-drain line the pack has just finished
// driving, we may be answering before it has re-armed its receiver.
//
// Swept from the build rather than hard-coded because the value is the experiment: 2 ms matches
// the reference, and 5/10 ms test whether more is needed. Defaults to the reference's 2.
#ifndef FEATURE_BATTERY_TURNAROUND_MS
#define FEATURE_BATTERY_TURNAROUND_MS 2
#endif

// Which GPIO carries the RAK9154 one-wire link. OFF means the documented WB_IO1/P0.17 wiring;
// ON moves it to WB_A1/P0.31.
//
// A build flag rather than an edited constant because the choice is a property of one physical
// core, not of the firmware. Three cores measured ~3.86 ohm from IO1 to ground on 2026-08-29
// (issue #96) and needed A1; a core with a healthy IO1 must not inherit that workaround, because
// A1 is only reachable on the base-board extension header and silently costs a node its
// documented harness. Flip it for the board in front of you and record which pin the reading
// came from in docs/EVIDENCE.md — a voltage with no pin attached to it is not attributable.
#ifndef FEATURE_BATTERY_PIN_A1
#define FEATURE_BATTERY_PIN_A1 0
#endif

// The second recovery pad, WB_I2C1_SDA/P0.13. Takes precedence over FEATURE_BATTERY_PIN_A1 so a
// board cannot be built for two pins at once.
//
// Exists because node 002's core lost A1 the same way earlier cores lost IO1: on 2026-08-30 both
// IO1 and A1 sampled 100 % low against the internal pull-up — across two separate harnesses and
// again with the harness entirely removed — while SDA read idle HIGH (issue #96). Why two pads on
// one core failed is unexplained and still open, so this is a route around a measured fault, not a
// claim about the cause. Never select a pad before metering it: A1 was chosen by decision rather
// than measurement, and that is part of how this got expensive.
#ifndef FEATURE_BATTERY_PIN_SDA
#define FEATURE_BATTERY_PIN_SDA 0
#endif

// The THIRD and LAST recovery pad on node 002's core: WB_I2C1_SCL/P0.14. Takes precedence over
// both flags above so a board cannot be built for two pins at once.
//
// **SCL IS THE LAST USABLE PAD ON THIS CORE.** IO1, A1 and SDA are gone; TX1/RX1 belong to the
// RS-485 module, IO2 gates the 3V3_S rail, and IN1 is not a core GPIO in the variant. So do not
// select this pin until the transmit path that consumed the other three is fixed — moving the
// wire without fixing the drive spends the last pad and the core with it (issue #99, #102).
//
// The meter reading that makes SDA damaged and SCL healthy (5.6 kohm against 240 kohm, same core,
// powered down), and the punched-through-ESD-clamp reasoning it supports, are in docs/EVIDENCE.md
// and AGENTS.md. Do not re-derive them here.
//
// Reachable as a spring terminal on the RAK5802 (`SCL SDA 3V3 AIN`), so unlike A1 this needs no
// solder joint. Same switched-rail caveat as SDA: that terminal sits on a module whose supply
// WB_IO2 gates, so SwitchedRailHold in battery.cpp must cover this pin too.
//
// CITE(bench): docs/EVIDENCE.md 2026-08-30 — the 5.6 kohm / 240 kohm pad comparison and the
//   measured pack drive levels behind the paragraph above.
#ifndef FEATURE_BATTERY_PIN_SCL
#define FEATURE_BATTERY_PIN_SCL 0
#endif

#if FEATURE_CONSOLE && defined(ARDUINO)
#define LOG(x)        Serial.print(x)
#define LOGLN(x)      Serial.println(x)
#define LOGF(...)     Serial.printf(__VA_ARGS__)
#else
#define LOG(x)        do {} while (0)
#define LOGLN(x)      do {} while (0)
#define LOGF(...)     do {} while (0)
#endif

// Deliberately no off-target definition of Arduino's F(). Off the board the logging macros
// discard their argument without expanding it, so nothing needs F() to exist — and
// defining a single-letter macro before the system headers are read breaks them, because
// the C library uses F as a parameter name in its own macros. That mistake turned into a
// wall of errors inside stdio.h and cmath that pointed nowhere near this file.

#define FIRMWARE_VERSION "0.4.4"

// The commit this image was built from, injected by scripts/pio_git_rev.py -- a platformio.ini
// `pre:` extra script -- as -D FIRMWARE_COMMIT="a7381e7", with `-dirty` appended when the tree
// carried anything uncommitted. Two builds of the same FIRMWARE_VERSION are otherwise
// indistinguishable on the console, and docs/EVIDENCE.md cannot accept a result without a SHA.
//
// The fallback keeps a build with no git information compiling -- a tarball export, a checkout
// without history, an off-target compile of this header on its own -- and makes it say so. A
// banner that asserts the wrong SHA is worse than one that admits it does not know.
//
// Costs nothing when the console is compiled out: with FEATURE_CONSOLE=0 the LOGF macro above
// discards its arguments without expanding them, so the literal is never referenced and never
// reaches the image.
#ifndef FIRMWARE_COMMIT
#define FIRMWARE_COMMIT "unknown"
#endif
