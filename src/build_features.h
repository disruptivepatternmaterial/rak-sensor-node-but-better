/*
 * Feature switches — bring one subsystem up at a time.
 *
 * Every feature defaults ON here. platformio.ini defines cut-down environments that turn
 * subsets OFF, so a failure can be isolated to one subsystem without editing code:
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

#define FIRMWARE_VERSION "0.2.0"
