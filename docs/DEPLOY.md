# Deployment procedure

🚧 **NOT YET DEPLOYED.** This is the procedure, not a record that it was followed. The record
lives in [`EVIDENCE.md`](EVIDENCE.md).

Bring-up order and first-flash mechanics: [`FIRST_FLASH.md`](FIRST_FLASH.md). Wiring:
[`HARDWARE.md`](HARDWARE.md). Behavior contract: [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md).

## Pack provisioning is the firmware's job, and it is currently not latching

**There is nothing for the operator to do here.** On the one-wire link the RAK4631 is the
**host/master** and the RAK9154 pack is the **slave**. The pack announces itself carrying
`provId = 0xFF` (unprovisioned) and waits for the host to assign it an id; assigning that id is
performed by this firmware, over the wire, in `acquire_pid()`.

**It does not latch.** The firmware answers the announcement with `0x01` — twenty-two times in
the 2026-08-05 capture, across a 45 382 ms window — and the pack re-announces as `0xFF` every
time. This is an **unresolved host-side protocol defect** in our reply frame or our handshake
sequence. It is not diagnosed yet, and it is not blocked on anything external.

> **Retraction (2026-08-05).** Earlier revisions of this file instructed the operator to
> provision the pack through RAK's WisToolBox mobile app over NFC/BLE. **That procedure was
> fabricated and has been removed.** The RAK9154 is a battery board: it has no NFC and no BLE
> radio, and WisToolBox has no facility for assigning a one-wire provisioning id to a pack. The
> claim sent work down a phantom path — see [`EVIDENCE.md`](EVIDENCE.md) 2026-08-05.

CITE(bench): [`EVIDENCE.md`](EVIDENCE.md) 2026-08-05 — 22 answered announcements in 45 382 ms
at `8720dea`, `provId` still `0xFF`; the pack returns its unsampled record template.

### How to tell it worked

Flash `stage2` and watch the console. Once the host's id assignment latches, the pack answers a
`SENDAT` addressed to `0x01` with a frame carrying a **non-zero voltage**:

```
   battery : 12.42 V, -0.31 A, 87 %, 18.4 C
```

An **all-zero** frame — `0.00 V` — is the pack's unsampled record template, not a measurement,
and the firmware discards it rather than encoding it (a live pack cannot be at 0.00 V and also
be driving the wire).

Silence from `0x01` with the fallback to `0xFF` answering instead is the current state: id
assignment has not taken.

## Acceptance criteria before the soak

All four, recorded in [`EVIDENCE.md`](EVIDENCE.md) with host and commit SHA:

| Check | What proves it |
|---|---|
| Battery reads | `SENDAT Ok` from dest `0x01`, non-zero voltage |
| Awake time is sane | Measured cycle awake duration under 5 s, logged — **current baseline 50.5 s**, of which `acquire_pid()` is **45.4 s** ([`EVIDENCE.md`](EVIDENCE.md) 2026-08-05, `8720dea`, `stage2`) |
| Survives a reset | Power-cycle the node; data returns on the next wake with no manual step |
| Sensors are independent | Unplug one sensor, confirm the other still uplinks — both directions ([ADR-0004](decisions/ADR-0004-bms-one-wire-path.md)) |

The awake-time row has a before-number to be measured against: on an **unprovisioned** pack the
wake runs **50.5 s** (110.5 s cycle less the 60 s wait), and **45.4 s of it is `acquire_pid()`**
listening for announcements. Latching the id is what removes that phase — once `0x01` answers,
the phase-0 direct probe hits and the remaining work is ~5 s. So this check is not a separate
task; it is the observable that tells you the id assignment finally took.

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
