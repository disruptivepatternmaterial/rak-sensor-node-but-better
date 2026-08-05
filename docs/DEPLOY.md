# Deployment procedure

🚧 **NOT YET DEPLOYED.** This is the procedure, not a record that it was followed. The record
lives in [`EVIDENCE.md`](EVIDENCE.md).

Bring-up order and first-flash mechanics: [`FIRST_FLASH.md`](FIRST_FLASH.md). Wiring:
[`HARDWARE.md`](HARDWARE.md). Behavior contract: [`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md).

## The one step firmware cannot do for you

**The RAK9154 pack must be provisioned through RAK's WisToolBox mobile app before the node can
read it.** This is not a convenience and it is not a firmware bug — it is a property of the
pack, established the hard way.

Sixteen consecutive provisioning attempts from the firmware, each answering the pack's
announcement with a byte-for-byte correct response and an independently verified checksum, all
ended the same way: the pack re-announced itself carrying `provId = 0xFF`. It never latched the
assigned id. The full capture and reasoning are in [`EVIDENCE.md`](EVIDENCE.md) under
*"RAK9154 refuses provisioning"*.

The reason is architectural. RAK's configuration channel for this hardware is the **north-bound
`ATC+` command set, reachable only over NFC/BLE from the WisToolBox mobile app**
(`supportedApps: ["MOBILE"]`, `connectionType.mode: "NFC"`). The one-wire link this node speaks
is the **south-bound** probe protocol, and those `ATC+` commands are not reachable on it. A
master on the one-wire line can read a provisioned pack; it cannot provision an unprovisioned
one.

CITE(datasheet): [CIT-WISTOOLBOX-AT] WisToolBox AT specification catalogue — `supportedApps`
and `connectionType.mode` establish the configuration path is the mobile app over NFC/BLE.
CITE(bench): [`EVIDENCE.md`](EVIDENCE.md) 2026-08-04 — sixteen correct provisioning responses,
`provId` stays `0xFF` on every cycle.

### What to do

1. Install **WisToolBox** on a phone (iOS or Android, from RAK).
2. Power the RAK9154 pack.
3. Hold the phone to the pack's NFC pad — or pair over BLE — and connect in WisToolBox.
4. Provision the probe. The first probe on the bus is assigned **`0x01`**, which is what the
   firmware queries first.
5. Confirm the pack reports a probe id other than `0xFF` before walking away.

### How to tell it worked

Flash `stage2` and watch the console. A provisioned pack answers a `SENDAT` addressed to
`0x01` with a frame carrying a **non-zero voltage**:

```
   battery : 12.42 V, -0.31 A, 87 %, 18.4 C
```

An **all-zero** frame — `0.00 V` — is the pack's unsampled record template, not a measurement,
and the firmware discards it rather than encoding it (a live pack cannot be at 0.00 V and also
be driving the wire). If that is what you see, provisioning did not take.

Silence from `0x01` with the fallback to `0xFF` answering instead means the pack is still
unprovisioned.

## Acceptance criteria before the soak

All four, recorded in [`EVIDENCE.md`](EVIDENCE.md) with host and commit SHA:

| Check | What proves it |
|---|---|
| Battery reads | `SENDAT Ok` from dest `0x01`, non-zero voltage |
| Awake time is sane | Measured cycle awake duration under 5 s, logged |
| Survives a reset | Power-cycle the node; data returns on the next wake **without** re-running WisToolBox |
| Sensors are independent | Unplug one sensor, confirm the other still uplinks — both directions ([ADR-0004](decisions/ADR-0004-bms-one-wire-path.md)) |

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
