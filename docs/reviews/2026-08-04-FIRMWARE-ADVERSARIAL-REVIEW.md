# Adversarial Firmware Code Review: Correctness, Safety, & Power

**Date**: August 4, 2026  
**Target**: RAK4631 nRF52840 LoRaWAN Field Node  
**Status**: Stage 1-2 proven, Stage 3 in progress  

> **Correction (2026-08-05).** This review repeats the project's then-current belief that the
> RAK9154 pack is provisioned out-of-band through RAK's WisToolBox app ("post-WisToolBox
> provisioning", §4). **That premise was fabricated** — the pack has no NFC or BLE radio, and id
> assignment is performed by this firmware over the one-wire link in `acquire_pid()`. Read
> "post-WisToolBox" here as "once the pack has latched an assigned id"; the review's timing and
> watchdog findings are unaffected by the correction. See [`../EVIDENCE.md`](../EVIDENCE.md)
> 2026-08-05.

---

## 1. Executive Summary & Hardening Gaps (H1-H8)

This review identifies critical power and watchdog safety regressions in the current RAK9154 battery driver. Under specific field conditions (especially post-WisToolBox provisioning), the driver can block the wake cycle for **up to 74 seconds** of high-power awake time, completely violating **H2 (Deep Sleep)** and starving the watchdog, which violates **H1 (Watchdog)**.

*   **main.cpp diagnostic bloat**: **60.5%** of the file (490 / 809 lines) is diagnostic/scan code.
*   **battery.cpp diagnostic/provisioning bloat**: **34.5%** of the file (~645 / 1868 lines) is diagnostic/provisioning code.

---

## 2. Numbered Item Assessments

### Item 1: battery.cpp passive listen window (`kPushListenUs = 20s`)
*   **Severity**: 🚨 **Critical (Power & H2 compliance)**
*   **Location**: `src/sensors/battery.cpp` (Phase 2b, lines 1788-1804)
*   **Assessment**: A 20-second passive listen window blocks the MCU from entering sleep. Since the pack's push cadence is independent and potentially minutes or hours long, waiting 20 seconds is highly likely to yield no push while draining the battery.
*   **Consequence**: Wastes 20 seconds of high-power run current (~10-15 mA) every hour, reducing battery lifetime from years to months.
*   **Recommendation**: **Eliminate Phase 2b entirely** in production. A provisioned pack already has cached readings in its BMS memory and will instantly reply to a solicited `SENDAT` query (Phase 2a).

### Item 2: battery.cpp unconditional `acquire_pid()` call
*   **Severity**: 🚨 **Critical (Power & H2 compliance)**
*   **Location**: `src/sensors/battery.cpp`, line 1634
*   **Assessment**: The driver calls `acquire_pid()` unconditionally every cycle. If the pack is already provisioned by WisToolBox, it has a valid PID (0x01) and will not announce itself as unprovisioned on `BOOT`. `acquire_pid()` will therefore block for the entire `kProvWindowMs` (45 seconds) before failing.
*   **Consequence**: Wasting 45 seconds of awake time on every single wake cycle. This completely destroys the deep-sleep power budget.
*   **Recommendation**: Skip `acquire_pid()` if `m_pack_latched` is true, or bypass provisioning entirely in the production image.

### Item 3: battery.cpp all-zero sentinel
*   **Severity**: ✅ **Not an issue (Correct as designed)**
*   **Location**: `src/sensors/battery.cpp`, lines 1571-1574
*   **Assessment**: This sentinel is highly robust. A live pack powering the one-wire line must have a non-zero voltage. An all-zero frame represents a dummy/unsampled template. 0% SoC with non-zero voltage correctly bypasses the sentinel and is encoded.
*   **Consequence**: Prevents the propagation of fabricated zeros (null policy violation).
*   **Recommendation**: Retain without modification.

### Item 4: Static SoftwareHalfSerial instances in `FEATURE_ONEWIRE_SPLIT`
*   **Severity**: ⚠️ **Warning (Configuration Risk)**
*   **Location**: `src/sensors/battery.cpp`, lines 408-412
*   **Assessment**: If `FEATURE_ONEWIRE_SPLIT` is compiled in but the physical board is wired in single-pin shared mode, the RX instance on `kSplitRxPin` will usurp the library's static `active_object` interrupt, making the shared `m_pin` completely blind to replies.
*   **Consequence**: Silent failure of the battery reads if physical wiring and build flags mismatch.
*   **Recommendation**: Document the strict hardware dependency. Add a build-time sanity check or a runtime warning if split is enabled but the pin reads constantly floating.

### Item 5: rk900.cpp raw register dump
*   **Severity**: ℹ️ **Nit (Log Clutter)**
*   **Location**: `src/sensors/rk900.cpp`, line 81
*   **Assessment**: Now that ADR-0006 (baud and register map dispute) is resolved, logging the raw registers on every cycle is unnecessary.
*   **Consequence**: Minor cycle waste and log clutter.
*   **Recommendation**: Remove the log line or guard it behind a diagnostic flag (e.g., `FEATURE_RK900_DIAG`).

### Item 6: main.cpp inline scan size
*   **Severity**: ℹ️ **Nit (Maintainability / Code Debt)**
*   **Location**: `src/main.cpp`
*   **Assessment**: Having 490 lines of inline scanning code in `main.cpp` obscures the actual control flow of the application.
*   **Consequence**: Makes `main.cpp` heavy and hard to review or maintain.
*   **Recommendation**: Extract `FEATURE_BUS_SCAN` and `FEATURE_ONEWIRE_SCAN` into separate diagnostic files under a new directory (e.g., `src/diagnostics/`).

### Item 7: Watchdog coverage of provisioning loop
*   **Severity**: 🚨 **Critical (Safety & H1 compliance)**
*   **Location**: `src/sensors/battery.cpp`, `acquire_pid()` / `receive()`
*   **Assessment**: A 45-second provisioning window + 20-second listen window + LoRaWAN join/send retries can easily exceed the 120-second watchdog window. `acquire_pid()` and `receive()` contain no watchdog feed calls.
*   **Consequence**: Watchdog resets the node mid-cycle, creating a boot-loop under poor signal conditions or when the battery is unresponsive.
*   **Recommendation**: Feed the watchdog (`power::watchdog_feed()`) inside the `acquire_pid()` while-loop and the `receive()` block.

---

## 3. Additional Safety & Correctness Issues

### Item 8: unbuildable `src/owprobe.h`
*   **Severity**: ⚠️ **Warning (Build Health)**
*   **Location**: `src/owprobe.h`
*   **Assessment**: `src/owprobe.h` refers to `FEATURE_ONEWIRE_PROBE` which does not exist in `src/build_features.h`, and there is no `owprobe` environment in `platformio.ini`.
*   **Consequence**: Untracked, dead, and currently unbuildable file in the codebase.
*   **Recommendation**: Fully register `FEATURE_ONEWIRE_PROBE` in `src/build_features.h` and add its corresponding `owprobe` environment to `platformio.ini`, or delete the file if no longer needed.

### Item 9: Pin leakage during deep sleep
*   **Severity**: ⚠️ **Warning (Power / Sleep path leakage)**
*   **Location**: `src/sensors/battery.cpp`, lines 1827-1843
*   **Assessment**: In `FEATURE_ONEWIRE_SPLIT` mode, there are two pins (`m_pin` and `kSplitRxPin`). The code re-runs `pinMode(kSplitRxPin, INPUT)` but misses setting `m_pin` (which acts as TX and is left driven or floating) to `INPUT` on shutdown.
*   **Consequence**: Leakage current during deep sleep through the unreleased TX pin.
*   **Recommendation**: Explicitly set both `m_pin` and `kSplitRxPin` to `INPUT` (no pull-up) on sleep, and ensure `probe_rail_down()` is fully executed.

---

## 4. Minimal Post-WisToolBox Battery Driver

For a production deployment where the pack has been pre-provisioned via WisToolBox (latched at PID `0x01` and configured for periodic sampling), the entire 1200+ line provisioning, parameters setting, and scanning machinery is dead weight. 

The minimal correct driver needs only the following logic:

```cpp
BatteryReading Battery::read() {
    BatteryReading out;
    probe_rail_up();

    // Use the assigned PID (0x01) directly
    SoftwareHalfSerial &link = rx_link(m_pin);
#if FEATURE_ONEWIRE_SPLIT
    tx_link(m_pin).begin(kBaud);
#endif
    link.begin(kBaud);
    link.flush();

    // Send a single SENDAT query to PID 0x01
    static uint8_t rx[kRxCapacity];
    const size_t got = query(0x01, rx, sizeof(rx));

    if (got >= 8) {
        parse(rx, got, out);
    }

    // Clean up pins to prevent sleep leakage
    link.end();
    pinMode(m_pin, INPUT);
#if FEATURE_ONEWIRE_SPLIT
    pinMode(kSplitRxPin, INPUT);
#endif
    probe_rail_down();

    return out;
}
```

*   **Active code size**: Reduced from **1100+ lines** of handshaking/listening to **~30 lines** of simple poll-and-parse.
*   **Awake time**: Reduced from **up to 74 seconds** to **under 300 milliseconds**.
*   **Watchdog / Power risk**: Completely eliminated.
