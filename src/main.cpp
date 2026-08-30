/*
 * Read two sensors, send the numbers, sleep. Repeat until the batteries outlast us.
 *
 * The whole job is in `loop()` below and it is deliberately boring. Anything interesting
 * lives in one of the modules, so that when something misbehaves in the field there is
 * exactly one place to look for each kind of problem.
 *
 * Subsystems are switched on by the build environment (see build_features.h), so the same
 * source can be flashed as wind-sensor-only, then with the battery, then with the radio,
 * then with sleep. Each build adds one new way to fail.
 *
 * Behavior contract:  docs/FIRMWARE_SPEC.md
 * Wiring:             docs/HARDWARE.md · docs/decisions/ADR-0004-bms-one-wire-path.md
 * Payload contract:   payload/schema.yaml
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "config.h"
#include "build_features.h"
#include "payload.h"
#include "power.h"
#include "session.h"
#include "radio.h"
#include "reading.h"
#include "sensors/battery.h"
#include "sensors/rk900.h"

// Bench instruments. Both headers are self-gating and compile to nothing unless their own
// environment is selected, so a field image carries neither. They live outside main.cpp
// because together they were 521 lines -- roughly two thirds of this file -- of code that
// never runs on a deployed node, and reading the cycle meant scrolling past all of it.
#include "diagnostics/busscan.h"
#include "diagnostics/owscan.h"

namespace {

// CITE(datasheet): [CIT-RAK19007] the base-board extension header exposes A1 as a direct core
//   GPIO; the one-wire battery link lands there, leaving Serial1 free for the RS-485 module.
// CITE(datasheet): [CIT-RAK5802] the RS-485 module occupies the IO slot and its
//   transceiver is powered from the switched rail on WB_IO2, which rk900.cpp controls.
// CITE(datasheet): [CIT-NRF-GPIOTE] P0.31 supports the GPIO edge event the half-duplex receiver
//   requires.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] SoftwareHalfSerial accepts an Arduino GPIO number and
//   derives its nRF port register and interrupt at runtime; it is not tied to WB_IO1.
//
// WB_IO1/P0.17 is the documented wiring and stays the default. Bench isolation on 2026-08-29
// found IO1 held at ~9.6 mV powered and ~3.86 ohm to ground unpowered on the three cores
// available then, while an empty baseboard was open and A1 measured ~45 kohm; issue #96 records
// the raw sequence. That is a property of those damaged cores, not of the design, so A1 is a
// per-core recovery path selected at build time rather than the wiring every node inherits.
// IO2 is not an alternative either way — it controls the RAK5802's 3V3_S rail.
#if FEATURE_BATTERY_PIN_A1
constexpr uint8_t kBatteryPin = WB_A1;
#else
constexpr uint8_t kBatteryPin = WB_IO1;
#endif

// Ceiling on one awake cycle. Two sensor reads with bounded retries plus a join attempt
// and the receive windows fit inside this with room to spare; anything longer means
// something is stuck and a reset is the correct answer. The counter pauses during sleep,
// so this measures awake time only.
constexpr uint32_t kWatchdogSeconds = 120;

// How long to wait for the USB console at boot. A deployed node has no host attached, so
// waiting forever would make a perfectly healthy board look dead.
constexpr uint32_t kConsoleWaitMs = 3000;

// kAwakeWaitCapSeconds moved to src/config.h. With sleep compiled out the cap *is* the
// reporting cadence, so it has to sit where the fair-use guard can see it — see issue #44.

Config           config;
Radio            radio;
RK900            weather_sensor;
Battery          battery_sensor(kBatteryPin);
power::Brownout  brownout;

uint32_t cycle = 0;

// Sink the brownout gate calls when its state changes, so the hold survives a reset.
// Written as a free function rather than handing Config to power.h: the gate should not
// need to know a filesystem exists, and this is the whole of the coupling.
void persist_brownout_engaged(bool engaged)
{
    config.set_brownout_engaged(engaged);
}

// The other direction of the same coupling: session persistence has to be able to ask
// whether a flash write is affordable right now, and only the brownout gate knows. Free
// function for the same reason as above — session.h stays ignorant of power.h.
bool session_flash_write_allowed()
{
    return brownout.flash_write_allowed();
}

// Session owns the decision to repair a filesystem whose stale session cannot be removed, but
// Config owns every non-session value that repair erases. Rewrite those in-RAM values
// immediately after a successful format so the reporting interval and persisted brownout state
// are not collateral damage.
bool rebuild_config_after_filesystem_format()
{
    return config.rewrite_after_filesystem_format();
}

// Consecutive cycles in which neither sensor produced a single field.
uint32_t empty_cycles = 0;

// A set-interval downlink that is live in RAM but not yet on flash — either the brownout gate
// withheld the write, or the write was attempted and failed. Held here so the command takes
// effect on the very next sleep, and retried until it lands. Zero means nothing is pending.
// Refs #65.
uint32_t pending_interval = 0;

// Attempts the retry above is allowed to spend on flash. Bounded because Config now rolls its
// value back when a write fails, which is what stops a dropped command — but an unbounded
// retry against a filesystem that is broken rather than merely busy would then attempt a
// remove-and-rewrite of the settings page on every wake, forever. That is precisely the thrash
// H3 forbids, arriving by the door opened to fix a different defect. Three, because a write
// that fails three times in a row on a pack the gate has already judged healthy is a broken
// filesystem, not a transient. The value stays applied in RAM afterwards and the console says
// so; it is the writing that stops, not the command.
//
// CITE(spec): docs/FIRMWARE_SPEC.md §7 H3 — "Brownout: no flash thrash". The bound exists to
//   keep this retry inside that requirement.
// CITE(prior-art): [CIT-LITTLEFS-DESIGN] copy-on-write metadata pairs committed with a CRC —
//   a failed commit leaves the old record readable, so abandoning the retry loses the new
//   value rather than corrupting the stored one.
// CITE(policy): docs/POWER_BUDGET.md — flash erase-and-write is the largest non-radio current
//   the node draws; spending it once per wake for the life of the deployment is not affordable.
constexpr uint8_t kPendingIntervalWriteAttempts = 3;
uint8_t           pending_interval_writes_left  = 0;

// How often to transmit proof of life while both sensors stay silent. The first quiet cycle
// reports immediately, so a wiring mistake made during installation is visible before anyone
// leaves the site; after that the rate drops to keep a permanently broken sensor from
// spending the airtime budget on saying nothing. At the shortest permitted interval this is
// roughly one uplink every four hours.
#if FEATURE_RADIO
constexpr uint32_t kQuietCyclesPerHeartbeat = 8;

bool heartbeat_due(uint32_t quiet_cycles)
{
    return quiet_cycles == 1 || (quiet_cycles % kQuietCyclesPerHeartbeat) == 0;
}

// How many cycles until one that would reach ensure_joined(), assuming both sensors stay
// silent. Radio needs this to report a real next-attempt time rather than just its backoff:
// the two differ by up to the heartbeat cadence, and the difference is what #24 is about. A
// sensor recovering brings the attempt forward, so this is an upper bound by construction.
uint32_t cycles_until_join_attempt(uint32_t quiet_cycles)
{
    for (uint32_t ahead = 1; ahead <= kQuietCyclesPerHeartbeat; ahead++) {
        if (heartbeat_due(quiet_cycles + ahead)) {
            return ahead;
        }
    }
    return kQuietCyclesPerHeartbeat; // unreachable: a multiple always falls inside the span
}
#endif

void print_banner()
{
    LOGLN();
    LOGLN(F("=== rak-sensor-node ==="));
    LOGF("firmware : %s\n", FIRMWARE_VERSION);
    LOGF("commit   : %s\n", FIRMWARE_COMMIT);
    LOGF("built    : %s %s\n", __DATE__, __TIME__);
    LOGF("features : rk900=%d battery=%d radio=%d sleep=%d wdt=%d\n", FEATURE_RK900,
         FEATURE_BATTERY, FEATURE_RADIO, FEATURE_SLEEP, FEATURE_WATCHDOG);
    // Compile-time bounds, not the live value — config.begin() prints that a moment later,
    // and the two together say whether a stored setting is in play. Printed because a build
    // running the bench cadence must be identifiable from the console alone; nothing else
    // distinguishes it from the field image at a glance.
    LOGF("interval : bench=%d, bounds %lu-%lu s, default %lu s\n", FEATURE_BENCH_INTERVAL,
         (unsigned long)kIntervalMinSeconds, (unsigned long)kIntervalMaxSeconds,
         (unsigned long)kIntervalDefaultSeconds);

#if FEATURE_RADIO
    // Printed so the identity the node is actually joining with can be compared against what
    // the network server has registered, without reading it back out of the binary. A
    // byte-order mistake in these is invisible from the network side — the join request simply
    // never matches a known device, and the network stays silent rather than reporting a
    // rejection.
    LOGF("deveui   : %s\n", radio.deveui_hex());
    LOGF("appeui   : %s\n", radio.appeui_hex());
    LOGF("region   : US915 sub-band %u\n", radio.sub_band());
#endif

    if (power::reset_was_watchdog()) {
        // Worth shouting about. A node resetting every cycle still reports data and looks
        // healthy from the network side, so this is the only place it becomes visible.
        LOGLN(F("WARNING  : last reset came from the watchdog — something hung"));
    }
}

} // namespace

void setup()
{
#if FEATURE_CONSOLE
    // Gated, because on this core initializing the port is not free even with no host attached:
    // RAK's low-power document is emphatic that the serial port "MUST NOT be initialized"
    // because FreeRTOS starts a background task for it that never sleeps and so prevents the
    // MCU from sleeping. The field environment therefore builds with FEATURE_CONSOLE=0 and
    // never reaches this line; the soak environment builds with it on and is observable.
    //
    // CITE(prior-art): RAKwireless/WisBlock Low_Power_Example.md:45 — "As we want to achieve
    //   maximum power savings, the Serial port MUST NOT be initialized." The shipped sketch
    //   wraps every Serial call, Serial.begin() included, in `#ifndef MAX_SAVE`.
    //   [CIT-RAK-LOWPOWER] — docs/CITATIONS.md
    // CITE(prior-art): docs/LIBRARIES.md:55 — the same rule from the RAK forum, recorded in
    //   this repository before the field image honored it: must Serial.end() before sleep or
    //   current stays in the milliamps.
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < kConsoleWaitMs) {
        delay(10);
    }
#endif

#if FEATURE_WATCHDOG
    // Started before anything that can hang, so a failure during bring-up is recoverable
    // rather than permanent.
    power::watchdog_begin(kWatchdogSeconds);
#endif

    print_banner();
    config.begin();

#if FEATURE_RADIO
    // Must be installed before radio.begin(), which can restore or save a session. A null
    // callback deliberately disables emergency formatting: deleting Config without having its
    // owner present to rebuild it is not a valid session repair.
    session::set_filesystem_rebuild(&rebuild_config_after_filesystem_format);
#endif

#if FEATURE_BATTERY
    // Restored from flash so a reset cannot clear the hold. Deliberately only on a build
    // that actually reads the pack: without the battery driver update() is never called, so
    // a restored hold could never be lifted and the node would be silent forever.
    brownout.begin(config.brownout_engaged(), &persist_brownout_engaged);

    // Hand the battery driver the gate, before the first read() in loop(). Without this the
    // driver's pointer stays null and the brownout half of issue #39 is compiled in but inert:
    // a node correctly holding transmissions to save the pack would still spend ~28 s per cycle
    // hunting for a pack that is not answering. The gate only suppresses the expensive fallback
    // ladder — read() issues its direct SENDAT query before consulting it — so wiring this in
    // cannot stop the battery being read, which is what detects the pack coming back.
    battery_sensor.set_brownout(&brownout);

#if FEATURE_RADIO
    // Close the H3 hole in session persistence (issue #51). Wired only alongside the battery,
    // because without the pack there is no voltage evidence, the hold can never engage, and a
    // gate that always answers "allowed" is worse than none — it reads as protection that is
    // not there. Must be installed before radio.begin() below, which can join and save.
    session::set_flash_write_gate(&session_flash_write_allowed);
#endif
#endif

    // The boot count's flash write, deferred out of Config::begin() so a gate exists in front of
    // it. It sits here, immediately after that gate is restored, and not in the cycle below: the
    // cycle runs weather_sensor.read() and battery_sensor.read() first, and a hang in either is
    // what the watchdog resets on. A node stuck in that loop would never reach the write, so the
    // counter would stop climbing in exactly the failure it exists to name — and with the field
    // image detaching USB after 180 s, the boot counter riding out on the uplink is the only
    // remote signal that separates "resetting repeatedly" from "one sensor died".
    //
    // The gate answers from the bit restored out of flash, before this run has measured anything.
    // That is the conservative direction: a node that was holding when it went down is assumed to
    // still be holding until a reading says otherwise, so the write is skipped rather than taken
    // on an unknown supply. Nothing is cleared until an attempt is made, so a boot spent entirely
    // under a hold writes on the first later boot the gate permits.
    //
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H1 — the watchdog resets a hung cycle, which is the
    //   event this counter is the only remote evidence of, and §7 H3 for the gate itself.
    // CITE(prior-art): [CIT-LITTLEFS-DESIGN] atomic commits — an interrupted write costs the
    //   update, not the record, which is what makes taking it this early acceptable.
    // CITE(policy): docs/POWER_BUDGET.md — a node that cannot be diagnosed from the uplink is a
    //   hike, and the hike is the cost this counter exists to avoid.
    if (brownout.flash_write_allowed()) {
        (void)config.persist_boot_count_if_due();
    }

#if FEATURE_RADIO
    radio.begin();
#endif

    LOGLN();
}

void loop()
{
    power::watchdog_feed();

    LOGF("[cycle %lu]\n", (unsigned long)++cycle);

#if FEATURE_BUS_SCAN
    // Nothing below this runs in a scan build. The point is to look at the line, not to
    // produce a reading or an uplink from it.
    diagnostics::bus_scan();
    delay(5000);
    return;
#endif

#if FEATURE_ONEWIRE_SCAN
    // Same deal for the battery link: measure, print, and stop. No reading is produced and
    // no uplink is built, so nothing downstream can dress a raw byte up as a voltage. The pin
    // is passed in so this file stays the only place it is declared.
    diagnostics::onewire_scan(kBatteryPin);
    delay(5000);
    return;
#endif

    WeatherReading weather;
    BatteryReading pack;

    // Each sensor is read independently and neither can prevent the other from being
    // read. A sensor that fails contributes no fields rather than zeroes, so a gap in the
    // data is visible as a gap instead of arriving as a plausible wrong number.
#if FEATURE_RK900
    weather = weather_sensor.read();
    power::watchdog_feed();
#endif

#if FEATURE_BATTERY
    pack = battery_sensor.read();
    power::watchdog_feed();

    // Spec H3. Below the threshold the node keeps waking and reading but stops spending
    // energy on transmission, because a transmit burst is the largest current it ever
    // draws and the pack's protection circuit disconnecting is the one failure that
    // requires somebody to walk in.
    brownout.update(pack.voltage.valid, pack.voltage.value);
#endif

    // Built to fit what the current data rate allows. The network decides that rate, and
    // at its slowest only 11 of the 35 bytes fit — so the encoder fills the space in
    // priority order rather than producing an uplink that would simply be refused.
    Payload payload;
#if FEATURE_RADIO
    // Sampled once. max_payload() calls LoRaMacQueryTxPossible() every time, so asking twice
    // both costs a second MAC query and risks logging a different number from the one the
    // encoder actually built against.
    const size_t budget = radio.max_payload();
    payload.build(weather, pack, budget);
    if (payload.dropped() > 0) {
        LOGF("   uplink  : %u field(s) dropped to fit %u bytes\n", payload.dropped(),
             (unsigned)budget);
    }
#else
    payload.build(weather, pack);
#endif

    if (!payload.empty()) {
        empty_cycles = 0;
    } else {
        // Counted here, unconditionally, so a brownout hold (below) still advances the
        // quiet-cycle count. It previously lived in the heartbeat branch's condition, where
        // short-circuit evaluation skipped the increment whenever the brownout branch was
        // taken instead — a silent-and-low-power node under-reported how long it had been
        // silent in the first log line after recovery. Refs #24.
        ++empty_cycles;
    }

    // An interval taken during a brownout hold is live now and persisted later. Retried here,
    // after brownout.update() has seen this cycle's reading, so the write happens on the first
    // cycle where it is affordable rather than waiting for another downlink that may never
    // come — a Class A node cannot ask for one. Refs #65.
    if (pending_interval != 0 && pending_interval_writes_left > 0 &&
        brownout.flash_write_allowed()) {
        --pending_interval_writes_left;
        if (config.set_interval_seconds(pending_interval)) {
            pending_interval = 0;
        } else if (pending_interval_writes_left == 0) {
            // Said plainly, because the earlier line promised persistence. The cadence is
            // still the one that was commanded; it is only the flash copy that is not.
            LOGF("   config  : interval %lu s stays active in RAM — flash write failed, no "
                 "further attempts, reverts to %lu s on reset\n",
                 (unsigned long)pending_interval, (unsigned long)config.interval_seconds());
        }
    }

    uint32_t sleep_for = (pending_interval != 0) ? pending_interval : config.interval_seconds();

#if FEATURE_RADIO
    // One uplink allowed through a no-evidence hold, so a node with a healthy pack and a dead
    // one-wire link is still heard from. Never granted for a hold backed by a measured low
    // voltage — see power.h. Refs #45.
    const bool keepalive = !brownout.transmit_allowed() && brownout.keepalive_due();

    // Only total silence has to wait for the next heartbeat cycle; a payload or a due keepalive
    // reaches ensure_joined() on the very next one. Handed to Radio so a failed join can report
    // the real wait instead of just its own backoff. Refs #24.
    radio.set_cycles_until_next_call((payload.empty() && !keepalive)
                                         ? cycles_until_join_attempt(empty_cycles)
                                         : 1);

    if (!brownout.transmit_allowed() && !keepalive) {
        // Deliberately still reading the sensors and still waking on schedule. The pack
        // recovers on sunlight, not on being left alone, and the node has to notice the
        // moment it can transmit again.
        if (brownout.engaged_without_evidence()) {
            LOGLN(F("   uplink  : held — no pack voltage evidence"));
        } else {
            LOGLN(F("   uplink  : held — pack too low to transmit"));
        }
    } else if (!keepalive && payload.empty() && !heartbeat_due(empty_cycles)) {
        // Both sensors silent, and the last proof-of-life was recent enough. Reading
        // continues on schedule because the fault may clear on its own.
        LOGF("   uplink  : nothing to send (%lu quiet cycle(s))\n", (unsigned long)empty_cycles);
    } else if (radio.ensure_joined()) {
        if (keepalive) {
            // The uplink is the message. Whatever the sensors managed to produce rides along,
            // but the point is that the node is alive and reachable — and being Class A, this
            // is the only thing that reopens a downlink route to command it back to health.
            LOGF("   uplink  : keepalive — %u cycle(s) with no pack voltage, transmitting "
                 "once anyway\n",
                 (unsigned)power::kNoEvidenceKeepaliveCycles);
        } else if (payload.empty()) {
            // Deliberately transmitting with no measurements in it. Silence cannot be told
            // apart from a node that is gone, a flat pack, or a dead gateway, and all three
            // want different responses from whoever is deciding whether to drive out there.
            // An uplink carrying nothing still reports that the node is running, and — being
            // Class A, where a downlink can only follow an uplink — it is also the only thing
            // that reopens the path for commanding the node back to health remotely.
            LOGF("   uplink  : proof of life — no sensor data for %lu cycle(s)\n",
                 (unsigned long)empty_cycles);
        }
        if (keepalive) {
            // The keepalive is the only route back to reachable, so it is also the one uplink
            // allowed to advance the stored frame counter while the gate withholds flash writes.
            // Without this the reserve empties after session::kCounterMargin keepalives and every
            // later uplink is refused — a mute node, and being Class A therefore an uncommandable
            // one, in exactly the hold that no action of its own can lift. See session.h.
            //
            // Granted for either hold that can arm a keepalive, deliberately including the pack
            // answering from inside the 9.60-10.20 V hysteresis band. Restricting it to the
            // no-evidence hold was considered and rejected: that would permit only the write
            // taken with no reading at all — the blind one power.h names as the unbounded risk —
            // while refusing the one taken on a pack measured above the transmit-inhibit floor,
            // and it would re-strand the winter band-hover that power.cpp:380 exists to prevent,
            // about eight days into it. A pack at or below the floor never reaches this line:
            // power.cpp disarms its keepalive, which is what #38 asked for.
            session::permit_counter_checkpoint();
        }

        if (radio.send(payload)) {
            if (keepalive) {
                // Only once it is actually on the air. A failed send leaves the count where it
                // was, so the next cycle tries again rather than waiting out another interval.
                brownout.note_keepalive_sent();
            }
            DownlinkCommand cmd;
            if (radio.take_downlink(cmd)) {
                if (cmd.set_interval) {
                    if (brownout.flash_write_allowed() &&
                        config.set_interval_seconds(cmd.interval_value)) {
                        // Live and on flash, and it governs the sleep that starts in a few
                        // lines. sleep_for was computed before the downlink was read, so
                        // leaving it alone spent one more cycle at the old cadence while the
                        // brownout branch below applied its value immediately — the same
                        // command taking effect a cycle apart depending on the pack. The spec
                        // asks for "next wake after RX" (§4), and this sleep is what produces
                        // that wake.
                        sleep_for = config.interval_seconds();
                    } else if (Config::interval_in_range(cmd.interval_value)) {
                        // The gate refuses the flash write, not the command. Dropping it here
                        // was silent and unrecoverable: take_downlink() has already consumed
                        // the frame and the network has already drained its queue, so there is
                        // nothing left to retry and — being Class A — no way to ask again. A
                        // brownout hold is also exactly when an operator reaches for this
                        // command, because a longer interval is how the node is nursed back.
                        // Applied in RAM now, written to flash on the first cycle the gate
                        // allows one. Refs #65, H3 in docs/FIRMWARE_SPEC.md §7.
                        pending_interval             = cmd.interval_value;
                        pending_interval_writes_left = kPendingIntervalWriteAttempts;
                        sleep_for                    = pending_interval;
                        if (brownout.flash_write_allowed()) {
                            // The gate allowed the write and it failed anyway. Same handling:
                            // live now, retried on the next cycles, and never reported as
                            // saved when it was not.
                            LOGF("   config  : interval %lu s active but NOT saved — write "
                                 "failed, retrying next cycle\n",
                                 (unsigned long)pending_interval);
                        } else {
                            LOGF("   config  : interval %lu s active but NOT saved — brownout "
                                 "hold, will persist when the pack recovers\n",
                                 (unsigned long)pending_interval);
                        }
                    } else {
                        // Range-checked here because Config never sees the value in this
                        // branch. Out of range is ignored entirely, not clamped, exactly as
                        // Config::set_interval_seconds() would have done.
                        LOGF("   config  : rejected interval %lu s (allowed %lu-%lu)\n",
                             (unsigned long)cmd.interval_value,
                             (unsigned long)kIntervalMinSeconds,
                             (unsigned long)kIntervalMaxSeconds);
                    }
                }
                if (cmd.request_status) {
                    // The next cycle's uplink is the answer. Rather than transmitting
                    // twice, shorten the wait so it arrives promptly.
                    sleep_for = kIntervalMinSeconds;
                }
            }
        }
    }

    // Any failure — join or send — replaces the normal interval with a backoff that grows
    // and then holds. The node keeps trying forever at that ceiling, so a gateway that
    // returns after a week is picked up without anyone going out to restart anything.
    const uint32_t backoff = radio.backoff_seconds();
    if (backoff > 0) {
        sleep_for = backoff;
    }
#endif

    power::watchdog_feed();

#if FEATURE_SLEEP
    LOGF("   sleep   : %lu s\n\n", (unsigned long)sleep_for);
    power::sleep_seconds(sleep_for);
#else
    // Without sleep the node stays awake and simply waits, which keeps the console
    // attached and every cycle observable.
    const uint32_t awake_wait =
        (sleep_for > kAwakeWaitCapSeconds) ? kAwakeWaitCapSeconds : sleep_for;
    LOGF("   wait    : %lu s (sleep disabled)\n\n", (unsigned long)awake_wait);
    for (uint32_t i = 0; i < awake_wait; i++) {
        delay(1000);
        power::watchdog_feed();
    }
#endif
}
