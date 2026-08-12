# ADR-0004 — RAK9154 on one-wire; RAK5802 dedicated to the RK900

- **Status:** Accepted
- **Date:** 2026-07-30
- **Closes:** open decision #1 in `plans/P0_HARDENED_NODE.md`; the bus-conflict note in `docs/FIRMWARE_SPEC.md` §2.2
- **Affects:** `docs/HARDWARE.md` wiring, `docs/FIRMWARE_SPEC.md` §2.2 and §5, Stage 1 and Stage 3 firmware

## Context

Both sensors speak Modbus-RTU, but at different line rates: the RK900-09 at **4800** 8N1
as slave `0x01`, and the RAK9154 BMS at **9600** 8N1 as slave `0x6E`. There is one RAK5802
RS-485 transceiver.

The spec left this open with two candidate answers: share the RAK5802 and switch baud
between polls, or put the battery on the alternate one-wire path via the 5-pin Sensor Hub
Load socket and leave the RAK5802 to the RK900 alone.

The operator has the physical adapter for the one-wire path from the 5-pin socket.

## Decision

**The RAK9154 uses the one-wire half-duplex path on the 5-pin Sensor Hub Load socket. The
RAK5802 is dedicated to the RK900 at a fixed baud.**

> **The baud figure in this ADR is superseded.** Written 2026-07-30 as 4800, from the
> datasheet. [ADR-0006](ADR-0006-rk900-baud-and-register-map.md) settled it at **9600** on
> 2026-08-03 by measurement — this unit gives zero bytes at 4800. What ADR-0004 actually
> decides is *one device per bus, no baud switching*, and that is unaffected. Read every
> "4800" below as historical context for the decision, not as a wiring instruction.

## Rationale

**It deletes a failure mode rather than managing one.** Baud switching on a shared RS-485
segment means reconfiguring the UART mid-cycle, and both devices sit on the same
differential pair. Any mis-sequencing — a late reply from the RK900 arriving after the port
has moved to 9600 — produces framing errors that look like a dead sensor. Separate buses
cannot interfere by construction, and for a node nobody will visit, a failure that cannot
happen beats one that is handled.

**Neither device has to tolerate the other's traffic.** On a shared segment, every device
sees every frame and rejects the ones addressed elsewhere. Two buses means the RK900 sees
only RK900 traffic.

**A hung sensor cannot take the other down.** With one bus, a device holding the line
blocks both reads. Separated, an RK900 failure cannot prevent a battery reading — and the
battery reading is the one that tells us whether the node is about to die. This directly
serves H6 and H7, which require that either sensor going silent does not livelock the
other.

**The hardware for it is already in hand,** and both paths have prior art in
`forest-weather-machines/rak-4-5-wire`: `nanoc6-onewire-poll` and `nanoc6-rak9154-poll`
[CIT-RAK45WIRE]. Note both are M5Stack NanoC6 (ESP32-C6) implementations — they establish
the protocol, not the MCU-side code.

## Consequences

- Stage 1 (RK900) gets simpler: fixed 4800, no bus arbitration, no baud switching.
- Stage 3 (battery) needs a half-duplex one-wire implementation on a second UART rather
  than a second Modbus address. References: `beegee-tokyo/RAK-OneWireSerial`
  [CIT-ONEWIRE-SERIAL] and Meshtastic's `RAK9154Sensor` [CIT-MESHTASTIC-9154].
- The one-wire protocol is IPSO TLV, not Modbus, so the battery decode path is separate
  code from the RK900's. That is the cost of this decision: two protocols instead of one.
  Accepted, because the alternative shares a physical bus between two devices with
  different timing, and bus contention on an unattended node is worse than a second parser.
- The 5-pin socket carries `3V3_In` on pin 4. `docs/HARDWARE.md` already warns: tie
  carefully to 3V3, **never 5 V**.
- The 4-pin Gateway Load socket stays free, so the Modbus path remains available as a
  fallback if one-wire proves unreliable on the bench. Reverting is a wiring change plus a
  driver swap, not a redesign.

## Alternatives rejected

**Shared RAK5802 with baud switching.** One protocol and one parser, which is genuinely
simpler in code. Rejected on reliability: it puts two devices with different line rates on
one differential pair and makes correctness depend on timing that is hard to test and
harder to diagnose from a hillside.

**One-wire for both.** The RK900 has no one-wire mode; it is an RS-485 Modbus device.

## Verification

**Partly verified on hardware. Updated 2026-08-12 — this section previously read "Not yet
verified," which was stale by a week and risked a reader re-running a bench test that had
already passed.**

| What this ADR asked Stage 3 to demonstrate | State |
|---|---|
| One good BMS frame over one-wire | **Done.** 2026-08-05, `1a203d3`, re-verified `b6bbf31`: the pack latches pid `0x01` and reports `12.23 V, +0.00 A, 98%, 23.0 °C` across seven consecutive cycles. Re-confirmed 2026-08-12 at `b436aa9` — 19 of 20 `battdiag` cycles live, latched at `0x01` throughout, `provId FF` absent from the whole capture |
| Both sensors on their separate buses in one cycle | **Done.** 2026-08-12, `4510763`, in the `rak4631` field image — the RK900 on RS-485 and the pack on one-wire both read in the same cycle. This is the observation that actually vindicates the two-bus choice |
| An RK900 read that still succeeds while the battery link is unplugged (H6/H7) | **Not done.** Requires physically unplugging each sensor mid-cycle. H6 is `🟡 partial` and H7 is `⬜ none` in `docs/EVIDENCE.md` |

So the decision is validated; the **failure-isolation** half of it is not. A code audit is
explicitly not sufficient for H6/H7 — the unplug test is the evidence, and it is still owed.

Use `env:battdiag` (~10 s per cycle) for any question about the pack, never `stage3`: its
1800 s cycle means a capture window holds exactly one cycle, and several sessions misread the
pack's normal post-boot settling null as a provisioning failure for exactly that reason.
