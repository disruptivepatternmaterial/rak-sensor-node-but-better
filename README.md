# rak-sensor-node-but-better

WisBlock replacement for the RAK2560 Sensor Hub path: **RAK19007 + RAK4631 US915 + RAK5802**, polling **RK900-09** + **RAK9154**, LoRaWAN Class A, woods-hardened.

## Status

**The field image now identifies itself and cycles correctly.** On 2026-08-14 `1c2df3c` printed `commit   : 1c2df3c` in its own boot banner after a single RESET — a SHA read off the device, not asserted by the tooling — and then ran **cycles 3 through 8 unattended on the 900 s cadence**, roughly 1 h 15 m of continuous correct cycling at ~908 s wake-to-wake. Both sensors read on **every** cycle (RK900 23.1–25.4 °C / 56.3–64.9 %RH / 1002.3 hPa / calm; the RAK9154 pack latched at `0x01`, 11.75–11.76 V, −0.01 A, 78 %, 23.0 °C, **no BOOT spent**), every cycle closed `sleep : 900 s`, and every cycle sent 35 bytes on port 2. TTN agrees independently: the soak's `f_cnt` matched the banner's restored counter across the reset.

**Six clean cycles are not a soak, and every subsystem answering is not a deployment.** None of the H1–H8 gates has closed. **H8 is started on both halves and met on neither:** the longest bench soak is **19.03 h on `572bcfa`** (76 uplinks, 0 anomalies), **stopped deliberately** short of 24 h to ship the `#75` battery fix, and the run on the shipping `1c2df3c` had a **deliberate RESET inside its window**, so neither is an uninterrupted 24 h — and a partial run on one image cannot be topped up by another. The >24 h field runtime on 2026-08-15 does **not** close the bench half: it ran in the field with no console, so seven of the ten bench criteria in [`docs/SOAK.md`](docs/SOAK.md) could not be evaluated at all. Sleep current has still never been metered; the pack's own telemetry cannot answer it (10 mA LSB against a ~1 mA question). The battery-current sign convention is decided ([ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md), 2026-08-13 — positive = charging) but has never been confirmed against a real charge current. The brownout, rejoin and keepalive paths remain **believed correct, unobserved** — one clean afternoon does not reach them.

Bring-up mechanics: [`docs/FIRST_FLASH.md`](docs/FIRST_FLASH.md). Deployment procedure: [`docs/DEPLOY.md`](docs/DEPLOY.md).

| Doc | Path |
|---|---|
| Firmware contract | [`docs/FIRMWARE_SPEC.md`](docs/FIRMWARE_SPEC.md) |
| First flash / bring-up | [`docs/FIRST_FLASH.md`](docs/FIRST_FLASH.md) |
| Deployment procedure | [`docs/DEPLOY.md`](docs/DEPLOY.md) |
| Hardware / wiring | [`docs/HARDWARE.md`](docs/HARDWARE.md) |
| Libraries & examples | [`docs/LIBRARIES.md`](docs/LIBRARIES.md) |
| Decisions (ADRs) | [`docs/decisions/`](docs/decisions/) |
| Environments | [`docs/ENVIRONMENTS.md`](docs/ENVIRONMENTS.md) |
| Citation registry | [`docs/CITATIONS.md`](docs/CITATIONS.md) |
| Evidence ledger | [`docs/EVIDENCE.md`](docs/EVIDENCE.md) |
| Soak procedure (H8) | [`docs/SOAK.md`](docs/SOAK.md) |
| Reviews & audits | [`docs/reviews/`](docs/reviews/) |
| Power budget | [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md) |
| Versioning / release | [`docs/RELEASE.md`](docs/RELEASE.md) |
| Decisions (ADRs) | [`docs/decisions/`](docs/decisions/) |
| Payload schema | [`payload/schema.yaml`](payload/schema.yaml) |
| Open items | [GitHub Issues](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues) |

## Where things run

Two machines, and they are not interchangeable. Full detail in [`docs/ENVIRONMENTS.md`](docs/ENVIRONMENTS.md).

- **This workstation** — author code and docs. No PlatformIO, no device on USB.
- **Heliotrope Ridge** (`$RAK_BUILD_HOST`) — compile, flash, soak. The RAK4631 lives here.
  Set the address with `export RAK_BUILD_HOST=ntableman@<address>`; it is not recorded in
  this repository, which is public.
- **Git is the only transport between them.**

```bash
scripts/preflight.sh          # all local gates (same as CI)
scripts/remote.sh check       # build host reachability + toolchain
scripts/build.sh              # preflight + sync + compile on the build host
scripts/flash.sh              # build + USB flash on the build host (confirms first)
scripts/flash.sh --env stage1 # flash one bring-up stage instead of the full image
```

## Build stages


There is deliberately **no one-wire scan environment**. Every variant was deleted 2026-08-30 after **nine GPIO pads were destroyed — every one of them running diagnostic firmware that agents wrote and flashed over SSH without the operator's authorization**, across multiple sessions. Zero pads have been destroyed by the production image. Qualify a pad with a meter and read the wire with a logic analyzer, never by driving it from firmware, and never flash a board that was not explicitly asked for ([`AGENTS.md`](AGENTS.md), [`docs/HARDWARE.md`](docs/HARDWARE.md)).

## Working discipline

Rules live in [`.cursor/rules/`](.cursor/rules/) and are indexed in [`AGENTS.md`](AGENTS.md). The short version:

- **Cite everything.** Every register, baud rate, timeout, and RF parameter needs a manufacturer or spec source — at least 3 citations across 2 categories per firmware change. Prior art never stands alone.
- **Every build checks the TTN formatter.** The payload is a two-repo contract with `forest-weather-machines`. A drifted encoder does not lose one field — the decoder throws and discards the whole uplink.
- **Evidence, not claims.** Nothing is "working" until it is measured and recorded, with the host and commit SHA.
- **Nulls stay null.** A failed sensor read is never encoded as zero.

## Known blockers

- ~~[ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md) — battery current sign~~ **Closed 2026-08-13.** Positive = charging, negative = discharging, adopting the pack's own telemetry convention so that nothing in the path inverts a hardware-reported value. Read from code, neither the firmware encoder nor the live decoder needed to change — the only artifact that disagreed with the pack was our own spec line, now corrected. No longer blocks the payload freeze. Confirmed against a real charge current 2026-08-30: field telemetry showed +0.01 A in daylight and −0.02 to −0.05 A after dark ([`docs/EVIDENCE.md`](docs/EVIDENCE.md), f_cnt 3644–3660). Nothing in `src/` makes a control decision from the sign ([`docs/reviews/2026-08-12_spec_drift.md`](docs/reviews/2026-08-12_spec_drift.md) §2.2 enumerates the six dependent paths).
- **Sleep current is unmeasured**, and the pack cannot measure it — its current telemetry has a 10 mA LSB while [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md) turns on ~1 mA, and a USB-powered bench node barely loads the pack ([`docs/EVIDENCE.md`](docs/EVIDENCE.md) 2026-08-12, `4510763`). A meter is the only instrument that settles it. H2 stays open until it does.
- **Downlink handling is only half exercised.** A `0x03` status request was delivered and drained on 2026-08-12, but `take_downlink()` has never been observed on the console and the malformed-downlink bounds checking is untested ([#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)).
- Open decisions are tracked as [GitHub issues](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues); the architectural ones become ADRs in [`docs/decisions/`](docs/decisions/).

## Sibling context

Cloned at `~/Documents/GitHub/` on both machines (not beside this repo):

- `forest-weather-machines` — TTN ingest, the live payload formatter, RAK2560 settings, `rak-4-5-wire` BMS reverse-engineering
- `particle-devices` — Particle trailcam / Muon / Boron (different stack)

## License

TBD (match org default when firmware lands).
