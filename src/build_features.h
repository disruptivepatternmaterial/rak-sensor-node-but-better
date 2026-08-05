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

// The same idea for the one-wire battery link, OFF everywhere except its own environment.
// The battery driver collapses "the pack never heard us" and "the pack answered and we
// missed it" into one outcome — no reply, 0 bytes — and those need opposite responses: a
// meter and a cable in the first case, a constant in the second. This measures the pin
// itself before it assumes any protocol at all, so the two can be told apart. Never
// compiled into a field image: it holds the line for tens of seconds and addresses probe
// ids the node has no business addressing.
#ifndef FEATURE_ONEWIRE_SCAN
#define FEATURE_ONEWIRE_SCAN 0
#endif

// The battery driver's PARAMGET/PARAMSET pass. OFF by default, which is a reversal.
//
// It was added on the hypothesis that the pack's sensors were sitting at RULE_DISABLE and
// had to be armed before they would sample. The bench has since ruled that out from both
// ends: the pack's own announcement descriptors already report rule 0x0008 (RULE_PERIODIC),
// the working reference reader never sends a parameter write at all, and on this pack
// PARAMGET draws no reply while the "PARAMSET ack" we captured turned out to be the
// announcement arriving on its own schedule rather than a response to anything.
//
// So the pass buys no behaviour and costs up to 30 s of awake time per cycle — time the
// provisioning window in src/sensors/battery.cpp now needs and can spend far better. Left
// compiled-in behind a switch rather than deleted, because "the pack ignores this" is a
// claim about one pack and re-enabling it is how the next one gets tested:
//
//   pio run -e stage2 -a "--project-option=build_flags=-D FEATURE_BATTERY_PARAM_PASS=1"
#ifndef FEATURE_BATTERY_PARAM_PASS
#define FEATURE_BATTERY_PARAM_PASS 0
#endif

// Split the one-wire battery link across two pins instead of one. OFF by default, and the
// default build is byte-identical to the revision before this flag existed.
//
// The bench has the pack's pin 3 (TXD) and pin 5 (RXD) bridged onto WB_IO1. RAK's own
// documents say a genuine Sensor Hub does not do that: the hub drives pin 5 and leaves pin 3
// unconnected, so bridging puts the pack's driven TXD onto a line the master also drives.
// That contention is the leading explanation for a pack that answers short frames but never
// latches a probe id — the longer provisioning frame is the one with the most bytes to
// corrupt.
//
// Which topology the pack actually honours is not yet known, so both are buildable:
//
//   OFF (default)  pin 5 alone to WB_IO1, pin 3 floating. Half-duplex, exactly what a hub
//                  does. Needs no firmware change — this is the existing image.
//   ON             pin 3 (pack TX) to the split RX pin, pin 5 (pack RX) to WB_IO1. Only
//                  needed if the pack genuinely transmits on pin 3 and the single wire is
//                  silent.
//
// CITE(datasheet): [CIT-RAK9154] RAK9154 Datasheet, "Panel Connector Definition" —
//   https://raw.githubusercontent.com/RAKWireless/rakwireless-docs/master/docs/Product-Categories/Solar-Battery/RAK9154/Datasheet/README.md
//   the 5-pin Sensor Hub Load socket is Pin1 P+, Pin2 P-, Pin3 TXD, Pin4 3V3_In, Pin5 RXD.
//   TXD and RXD are two distinct signals on the pack; joining them is our choice, not the
//   connector's.
// CITE(datasheet): [CIT-RAK2560] RAK2560 Sensor Hub Datasheet, "Pin Definition" —
//   https://raw.githubusercontent.com/RAKWireless/rakwireless-docs/master/docs/Product-Categories/Sensor-Hub/RAK2560/Datasheet/README.md
//   on the hub side of the same socket pin 3 is Reserved / Not defined, pin 4 is Vcc_Probe,
//   and pin 5 is the One-wire UART. A real master therefore drives pin 5 only.
#ifndef FEATURE_ONEWIRE_SPLIT
#define FEATURE_ONEWIRE_SPLIT 0
#endif

// Power-cycle the probe's 3V3 rail around each exchange instead of holding it up. OFF by
// default, and independent of FEATURE_ONEWIRE_SPLIT so the two hypotheses can be tested one
// at a time.
//
// The reference example does not hold the rail: it raises WB_IO2, waits a second, runs the
// exchange, and drops it again. If the pack only accepts a provisioning assignment in the
// window just after its rail rises, a master that holds 3V3 permanently — which is what this
// firmware does — never opens that window and the pack stays at provId 0xFF forever. That
// matches the symptom exactly.
//
// IMPORTANT: on the harness described in docs/HARDWARE.md this flag changes nothing. WB_IO2
// gates the module slots' 3V3_S rail, but the pack's pin 4 is deliberately wired to the
// always-on VDD pad instead — precisely so that rk900.cpp dropping WB_IO2 after the weather
// read cannot kill the pack's reference mid-cycle. Enabling this flag without also moving
// pin 4 onto a 3V3_S source toggles a rail the pack is not connected to. It is a no-op, not
// a fix, and the firmware says so at runtime rather than implying otherwise.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] beegee-tokyo/RAK-OneWireSerial
//   examples/RAK4631-OneWireSerial/src/main.cpp, read on the build host — `pinMode(WB_IO2,
//   OUTPUT); digitalWrite(WB_IO2, HIGH);` at lines 48-49 before `mySerial.begin(9600)`,
//   `digitalWrite(WB_IO2, LOW)` at line 80, and per-cycle `digitalWrite(WB_IO2, HIGH);
//   delay(1000);` at lines 89-90 with LOW again at line 125. The rail is cycled per
//   exchange, and the settle wait is one second.
// CITE(datasheet): [CIT-RAK19007] RAK19007 Datasheet, WisBlock connector pin 30 / J11 pin 3 —
//   https://raw.githubusercontent.com/RAKWireless/rakwireless-docs/master/docs/Product-Categories/WisBlock/RAK19007/Datasheet/README.md
//   `IO2 | I/O | Used for 3V3_S enable`, and "IO2 controls the power switch of 3V3_S". So
//   WB_IO2 does gate a switched 3.3 V rail on this base board — the module slots' 3V3_S,
//   which is a different net from the VDD pad our pin 4 is on.
#ifndef FEATURE_ONEWIRE_RAIL_CYCLE
#define FEATURE_ONEWIRE_RAIL_CYCLE 0
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

#define FIRMWARE_VERSION "0.3.0"
