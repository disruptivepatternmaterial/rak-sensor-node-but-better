# Deployment procedure

🚧 **NOT YET DEPLOYED.** This is the procedure, not a record that it was followed. The record
lives in [`EVIDENCE.md`](EVIDENCE.md).

Bring-up order and first-flash mechanics: [`FIRST_FLASH.md`](FIRST_FLASH.md). Wiring:
[`HARDWARE.md`](HARDWARE.md). Behavior contract: [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md).

## Pack provisioning is the firmware's job, and it works

**There is nothing for the operator to do here.** On the one-wire link the RAK4631 is the
**host/master** and the RAK9154 pack is the **slave**. The pack announces itself carrying
`provId = 0xFF` (unprovisioned) and waits for the host to assign it an id; assigning that id is
performed by this firmware, over the wire, in `acquire_pid()`. The pack latches the assigned id
and holds it across resets, after which the phase-0 direct probe is the only path that runs.

The one non-obvious requirement is **timing, not framing**: the firmware must not answer the
announcement sooner than about 2 ms after the pack's last byte. Our reply bytes were always
byte-correct and the handshake still failed for weeks because we answered under one bit time
later, before the pack's receiver had re-armed on the open-drain line. That gap is
`kTurnaroundMs` in `src/sensors/battery.cpp`, and deleting it as dead weight re-breaks the whole
subsystem.

> **Retraction (2026-08-05).** Earlier revisions of this file instructed the operator to
> provision the pack through RAK's WisToolBox mobile app over NFC/BLE. **That procedure was
> fabricated and has been removed.** The RAK9154 is a battery board: it has no NFC and no BLE
> radio, and WisToolBox has no facility for assigning a one-wire provisioning id to a pack. The
> claim sent work down a phantom path — see [`EVIDENCE.md`](EVIDENCE.md) 2026-08-05.
>
> **Second correction, same day.** After that retraction this file recorded the failure as an
> "unresolved host-side protocol defect in our reply frame or our handshake sequence", not
> diagnosed. It is now diagnosed, and it was neither of those: it was the reply turnaround
> described above. Recorded rather than deleted because "the bytes are correct, so look
> elsewhere" is the conclusion that took longest to reach.

CITE(bench): [`EVIDENCE.md`](EVIDENCE.md) 2026-08-05 — `1a203d3` latched pid `0x01` from one
answered announcement at 3031 ms and read `12.23 V, +0.00 A, 98%, 23.0 °C` across seven
consecutive cycles; re-verified on `b6bbf31`.

### How to tell it worked

Flash `stage2` and watch the console. The pack answers a `SENDAT` addressed to `0x01` with a
frame carrying a **non-zero voltage**:

```
   battery : pack answered at 0x01 — skipping provisioning
   battery : sendat FF 7E 00 15 02 01 00 01 04 03 10 02 15 BA C7 04 16 B9 00 00 17 B8 62 18 67 E6 00 35
   battery : 12.23 V  +0.00 A  98%  23.0 C
```

An **all-zero** frame — `0.00 V` — is the pack's unsampled record template, not a measurement,
and the firmware discards it rather than encoding it (a live pack cannot be at 0.00 V and also
be driving the wire).

**Expect roughly two null cycles on a fresh boot.** The id latches before the pack has sampled,
so the first cycle or two return the record template and report no data. That is the null policy
working, not a fault. If it never resolves to a number, the reply-turnaround gap is the first
thing to check.

## Acceptance criteria before the soak

All four, recorded in [`EVIDENCE.md`](EVIDENCE.md) with host and commit SHA:

| Check | What proves it |
|---|---|
| Battery reads | ✅ `SENDAT Ok` from dest `0x01`, `12.23 V` ([`EVIDENCE.md`](EVIDENCE.md) 2026-08-05, `1a203d3`, re-verified `b6bbf31`) |
| Awake time is sane | Measured cycle awake duration under 5 s, logged — **baseline now ~5 s**, down from 50.5 s |
| Survives a reset | Power-cycle the node; data returns on the next wake with no manual step |
| Sensors are independent | Unplug one sensor, confirm the other still uplinks — both directions ([ADR-0004](decisions/ADR-0004-bms-one-wire-path.md)) |

**The awake-time baseline changed materially, from ~50 s to ~5 s.** On an unprovisioned pack the
wake ran **50.5 s**, of which **45.4 s was `acquire_pid()`** listening for announcements. Two
things removed almost all of it: the id now latches, so the phase-0 direct probe answers and
`acquire_pid()` is never entered; and the announce window itself was capped from 45 s to 5 s
(3 s under `FEATURE_BATTERY_FAST`) because sustained answering turned out not to be what the pack
needed. Any power-budget arithmetic written against the 50 s figure is stale — see
[`POWER_BUDGET.md`](POWER_BUDGET.md).

## Then

- 24 h bench soak: both sensors live, TTN uplinks arriving, no watchdog resets.
- 7 d field shadow: node in its enclosure, outdoors, before anything is trusted.

Only after both does the status in [`README.md`](../README.md) change. See
[`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §7 H8.

## What this procedure does not cover

Two open items block deployment and are **not** firmware:

- **Buck converter selection** — must be chosen on no-load quiescent current, since it is a
  24/7 parallel load. See [`HARDWARE.md`](HARDWARE.md) and [`POWER_BUDGET.md`](POWER_BUDGET.md).
- **TTN device registration** for additional nodes, with MSB-order keys.

A firmware image that passes every gate above is *ready*; the node is not deployable until
those two close as well.
