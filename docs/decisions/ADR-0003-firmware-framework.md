# ADR-0003 — Arduino + WisBlock-API-V2 as the firmware framework

- **Status:** **Superseded in part by [ADR-0005](ADR-0005-direct-sx126x.md)** — the Arduino
  and PlatformIO half of this decision stands; WisBlock-API-V2 was not adopted.
- **Date:** 2026-07-30
- **Closes:** open decision #4 in `plans/P0_HARDENED_NODE.md`
- **Affects:** `platformio.ini`, `src/`, every work package from WP1 onward

## Context

The RAK4631 can be programmed three ways, and the choice had to be made before any code
existed because each produces a completely different project layout. Hardware arrives
today, so this was the last thing blocking a first build.

| Option | What it is |
|---|---|
| **RUI3** | RAK's own turnkey API and AT-command layer |
| **Arduino + WisBlock-API-V2** | Event-driven low-power framework by Bernd Giesecke, RAK's own developer [CIT-WISBLOCK-API2] |
| **Arduino + SX126x-Arduino only** | The LoRaWAN MAC with no framework above it [CIT-SX126X-ARDUINO] |

## Decision

**Arduino + SX126x-Arduino, built with PlatformIO.** This originally read
"Arduino + WisBlock-API-V2"; the framework half was reversed by
[ADR-0005](ADR-0005-direct-sx126x.md) and the line is corrected here rather than left
contradicting the Status above it.

What WisBlock-API-V2 was wanted for was its event-driven sleep, and that turns out to be
about six lines of FreeRTOS semaphore rather than a framework that also carries a BLE
stack and an AT-command surface
[CITE(prior-art): beegee-tokyo/WisBlock-API-V2 `src/api_functions.cpp:114` — `xSemaphoreTake(taskEvent, portMAX_DELAY)`](https://github.com/beegee-tokyo/WisBlock-API-V2/blob/main/src/api_functions.cpp).
Adopt the pattern, not the dependency.

## Rationale

**RUI3 fights the environment we already committed to.** ADR-0001 puts compilation and
flashing on the Heliotrope Ridge build host driven by scripts, with GitHub Actions
compiling every change. RUI3 is distributed and supported as an Arduino-IDE board-manager
path. WisBlock-API-V2 and SX126x-Arduino are both in the PlatformIO registry and actively
maintained (2.0.28 and 2.0.32, published 2025-10-23), so they drop straight into
`lib_deps` and work identically on the workstation, the build host, and CI.

**Choosing RUI3 for its AT-command convenience turns out to be a false trade.**
WisBlock-API-V2 is described by its author as "an RUI3 AT command compatible library."
Field configurability was the main argument for RUI3, and we keep it.

**The power discipline in rule 50 needs explicit control, and this is where that
knowledge lives.** The failure mode that rule is built around — a node that cannot join
never sleeps, and the SX1262 stays awake unless `Radio.sleep()` is called separately —
is documented by the WisBlock-API author [CIT-RAK-SLEEP]. The framework's event-driven
loop exists specifically to hold the MCU in sleep between timer and interrupt wakeups.
That is the whole ballgame for a node that must still be alive in March.

**Nothing is given up at the bottom.** WisBlock-API-V2 sits on SX126x-Arduino, so if the
framework gets in the way we can drop to the MAC without changing platform, toolchain, or
board definition.

## Consequences

- `lib_deps` gains `beegee-tokyo/WisBlock-API-V2` when LoRaWAN lands in Stage 2. Stage 0
  and Stage 1 do not need it, and are deliberately kept free of it so bring-up failures
  have a short suspect list.
- The application is structured as WisBlock-API event handlers, not a free-running
  `loop()`. Stage 0's `loop()` is throwaway scaffolding and is expected to be replaced.
- We inherit the framework's join-retry behavior, which is exactly the code path
  [CIT-RAK-SLEEP] warns about. Join backoff and its current draw must be measured on the
  bench and recorded in `docs/EVIDENCE.md` before any deployment claim.
- RUI3-specific RAK examples need translating rather than copying.

## Alternatives rejected

**RUI3** — best time-to-first-uplink, but it is an Arduino-IDE-centric path that would
mean either abandoning PlatformIO and CI or maintaining a parallel build. It also puts
sleep and peripheral power behind an abstraction, which is the one area where this
project needs the most control.

**Bare SX126x-Arduino** — maximum control, but it means hand-writing the sleep loop,
join-retry policy, and event plumbing that WisBlock-API-V2 already provides and that
[CIT-RAK-SLEEP] shows are easy to get wrong. Available as a fallback at any time.

## Verification

Stage 0 compiles for `board = rak4630` on the build host: RAM 4.9% (12,280 of 248,832
bytes), flash 9.4% (76,748 of 815,104). See `docs/EVIDENCE.md`.
