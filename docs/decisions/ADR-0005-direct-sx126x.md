# ADR-0005 — Use SX126x-Arduino directly rather than WisBlock-API-V2

- **Status:** Accepted
- **Date:** 2026-07-30
- **Supersedes:** the framework half of [ADR-0003](ADR-0003-firmware-framework.md). Arduino
  and PlatformIO are unchanged.
- **Affects:** `platformio.ini`, `src/radio.cpp`, `src/main.cpp`, `src/power.cpp`

## Context

ADR-0003 chose Arduino with WisBlock-API-V2, and said the application would be structured
as WisBlock-API event handlers rather than a free-running loop. That is not what got built.
The firmware uses `beegee-tokyo/SX126x-Arduino` directly, with a plain `loop()` that reads,
sends, and sleeps.

This ADR exists because the code and the record disagreed, and a decision log that does not
match the code is worse than no decision log — the next person trusts it.

## What changed the answer

Writing the thing surfaced two facts that were not visible when ADR-0003 was written.

**The event framework's value is mostly in what this node does not do.** WisBlock-API-V2
earns its keep by managing wake sources, event queues, and AT-command configuration over
serial. This node has one wake source, which is a timer, and no interactive configuration:
its entire cycle is read two sensors, send one message, sleep. Wrapping that in an event
loop adds a layer to reason about without removing one.

**The behaviors ADR-0003 wanted from the framework are the ones worth owning here.** It
cited the framework's join-retry policy and sleep handling as the main draw. Those turned
out to be exactly the parts that needed tuning for this deployment: bounded joins with
capped backoff that never gives up, a session saved to flash so a reset is not a rejoin, a
watchdog that pauses during sleep, and a low-voltage gate that stops transmission before
the pack reaches its protection cutoff. Each is a deliberate choice about surviving a
multi-day outage on solar, and each is a few dozen lines in a file whose entire subject is
that decision. Inheriting a general-purpose version and then overriding it would have been
more code, not less, and harder to reason about at the moment it matters.

ADR-0003 already anticipated this. Its own escape hatch reads: _"WisBlock-API-V2 sits on
SX126x-Arduino, so if the framework gets in the way, the underlying LoRaWAN stack is
directly accessible."_ This is that hatch being used, one level earlier than expected.

## Decision

Depend on `beegee-tokyo/SX126x-Arduino` directly. Keep Arduino and PlatformIO. Keep the
vendored RAK4631 board definition. No WisBlock-API-V2 dependency.

## Consequences

**Owned outright:** join and backoff policy, sleep and peripheral shutdown, the watchdog,
session persistence, and the low-voltage gate. All are in `src/radio.cpp`, `src/power.cpp`,
and `src/session.cpp`, each with the failure it exists to prevent written next to it.

**Given up:** RUI3 AT-command compatibility, and the future ability to drop in a
WisBlock-API example unmodified. Neither is used. Configuration reaches this node by
downlink, which is the only channel a device in the woods actually has.

**The risk:** hand-written MAC handling can be wrong in ways a widely used framework
already has right. The mitigations are that the LoRaWAN stack underneath is the same one
the framework uses, the payload encoder is checked against the live decoder on every build,
and nothing here is trusted until it is observed on hardware and recorded in
[`../EVIDENCE.md`](../EVIDENCE.md).

**Reversible.** WisBlock-API-V2 sits on the same stack. Adopting it later means moving the
cycle in `main.cpp` into event handlers; the sensor, payload, and configuration modules
have no dependency on either choice and would not change.
