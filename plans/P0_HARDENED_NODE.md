# P0 — Hardened WisBlock weather node (woods)

**Status:** 🚧 **NOT YET DEPLOYED.** Firmware is in-tree and running on hardware; the H1–H8
hardening gates are open. *(This header read "planning + specs only, parts on order, no
firmware in-tree" until 2026-08-12 — a month stale. The live status is
[`README.md`](../README.md) and the live record is [`docs/EVIDENCE.md`](../docs/EVIDENCE.md);
this plan is kept for the work-package and open-decision structure, not as a status page.)*  
**Specs:** [`docs/FIRMWARE_SPEC.md`](../docs/FIRMWARE_SPEC.md) · [`docs/HARDWARE.md`](../docs/HARDWARE.md) · [`docs/LIBRARIES.md`](../docs/LIBRARIES.md)

**Success bar:** hike-in, months unattended — no brick, no Modbus hang, no airtime death spiral.

## Goal

Replace RAK2560 Sensor Hub path with WisBlock firmware that polls RK900 + RAK9154, Class A US915, downlink interval, woods-hardened.

## Architecture

```
RAK9154 -- power (P+/P-) --> buck 12V→5V --> RAK19007
       \-- BMS one-wire, 5-pin socket --> MCU        (ADR-0004)
RK900   -- RS485 @9600 --> RAK5802 --> MCU           (ADR-0006)
MCU = RAK4631 --> LoRaWAN US915 --> TTN --> forest-weather-machines ingest
```

Both figures were decided by measurement after this plan was written: the BMS bus by
[ADR-0004](../docs/decisions/ADR-0004-bms-one-wire-path.md), the 9600 baud by
[ADR-0006](../docs/decisions/ADR-0006-rk900-baud-and-register-map.md) — this unit returns zero
bytes at the datasheet's 4800.

## Work packages

| WP | Deliverable | Gate |
|---|---|---|
| WP1 | PlatformIO skeleton `wiscore_rak4631`, secrets example, CI compile | L2 |
| WP2 | RK900 Modbus on RAK5802 + timeouts | L3 |
| WP3 | RAK9154 (Modbus or one-wire) + timeouts | L3 |
| WP4 | WisBlock-API-V2 (or chosen stack) OTAA + downlink interval | L4 24h |
| WP5 | H1–H8 hardening in FIRMWARE_SPEC | L5 |
| WP6 | TTN device + decoder parity with `rak-wx-station-default.js` | L4 live ingest |

## Open decisions

| # | Decision | State |
|---|---|---|
| 1 | BMS path: 4-pin Modbus+baud-switch vs 5-pin one-wire | **Closed** — one-wire, [ADR-0004](../docs/decisions/ADR-0004-bms-one-wire-path.md) |
| 2 | Hub shell vs Unify 910406 | **Open** — enclosure, no firmware dependency |
| 3 | Keep Probe IO junction vs direct RK900 | **Open** — [`docs/HARDWARE.md`](../docs/HARDWARE.md) treats Probe IO as an optional junction box only |
| 4 | Arduino WisBlock-API-V2 vs RUI3 | **Closed** — neither. [ADR-0003](../docs/decisions/ADR-0003-firmware-framework.md), superseded in part by [ADR-0005](../docs/decisions/ADR-0005-direct-sx126x.md): direct SX126x via `LoRaWan-Arduino`, with the loop owned in-repo |
| 5 | Same TTN app `my-app-tobi` vs new app | **Closed by observation** — the node is live in `my-app-tobi` as `puma-concolor-001`, DevEUI `42BB96EF76E200F1`, plan `US_902_928_FSB_2`. The `middle-fork-area` application is empty |
| 6 | Battery current sign convention | **Closed 2026-08-13** — positive = charging, negative = discharging, per the pack's own telemetry. No longer blocks the payload freeze. [ADR-0002](../docs/decisions/ADR-0002-payload-contract-conflicts.md) |
| 7 | Buck converter part | **Open.** Must be chosen on *no-load quiescent current*: a part idling at milliamps exceeds the node's whole average draw ([`docs/POWER_BUDGET.md`](../docs/POWER_BUDGET.md)) |

## Cloud-agent notes

- Do not invent pinouts; cite HARDWARE + `rak-4-5-wire` + RAK datasheets.  
- Do not fabricate sensor values.  
- No secrets in git.  
- Prefer libs listed in LIBRARIES.md.  
- Mark anything unverified `🚧 NOT YET DEPLOYED`.
