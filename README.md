# rak-sensor-node-but-better

WisBlock replacement for the RAK2560 Sensor Hub path: **RAK19007 + RAK4631 US915 + RAK5802**, polling **RK900-09** + **RAK9154**, LoRaWAN Class A, woods-hardened.

## Status

🚧 **Not deployed.** The node went to the field on **2026-08-14** running `v0.4.3` / `1c2df3c`, and that is **day one of the ≥7 d shadow — a beginning, not an achievement.** Day one delivered the project's **first >24 h of continuous field runtime** (27.37 h at TTN, `f_cnt` 2391 → 2600, session restored not rejoined) — **and then the node went silent at 2026-08-15T17:04:14Z and has not been heard from since.** Cause unestablished ([`docs/EVIDENCE.md`](docs/EVIDENCE.md) 2026-08-15). The status does not change because the node went outside; it changes when [`docs/EVIDENCE.md`](docs/EVIDENCE.md) records the H1–H8 gates closing.

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
| Build plan | [`plans/P0_HARDENED_NODE.md`](plans/P0_HARDENED_NODE.md) |
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

Staged images, so a subsystem can be brought up alone and a failure has one candidate cause rather than six. Tests for the payload encoder and the Modbus checksum run on the build host with no board attached (`pio test -e native`).

| Environment | Contains |
|---|---|
| `stage1` | Wind sensor only, awake, printing over USB |
| `stage2` | Adds the battery reader |
| `stage3` | Adds the radio — still awake, so the join is watchable |
| `rak4631` | Full image: sleep and watchdog on |
| `soak` | **Byte-identical to `rak4631`** — a named entry point for `scripts/soak.sh` and the docs that reference it, deliberately carrying no build difference so a soak is evidence about the shipped image ([`docs/SOAK.md`](docs/SOAK.md), [ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md)) |
| `native` | Off-target tests, no hardware |

Diagnostics, not bring-up stages — each answers one question fast:

| Environment | Answers |
|---|---|
| `battdiag` | Anything about the RAK9154 pack. **~10 s per cycle — use this, never `stage3`**, whose full-interval sleep puts exactly one cycle in a capture window and has repeatedly been misread as a dead pack |
| `busscan` | RS-485 sweep across baud rates and slave addresses — the tool that established the RK900's real baud and register map for [ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md) |

There is deliberately **no one-wire scan environment**. Every variant was deleted 2026-08-30 after seven GPIO pads were destroyed across two cores; qualify a pad with a meter and read the wire with a logic analyzer, never by driving it from firmware ([`AGENTS.md`](AGENTS.md), [`docs/HARDWARE.md`](docs/HARDWARE.md)).

## Working discipline

Rules live in [`.cursor/rules/`](.cursor/rules/) and are indexed in [`AGENTS.md`](AGENTS.md). The short version:

- **Cite everything.** Every register, baud rate, timeout, and RF parameter needs a manufacturer or spec source — at least 3 citations across 2 categories per firmware change. Prior art never stands alone.
- **Every build checks the TTN formatter.** The payload is a two-repo contract with `forest-weather-machines`. A drifted encoder does not lose one field — the decoder throws and discards the whole uplink.
- **Evidence, not claims.** Nothing is "working" until it is measured and recorded, with the host and commit SHA.
- **Nulls stay null.** A failed sensor read is never encoded as zero.

## Known blockers

- ~~[ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md) — battery current sign~~ **Closed 2026-08-13.** Positive = charging, negative = discharging, adopting the pack's own telemetry convention so that nothing in the path inverts a hardware-reported value. Read from code, neither the firmware encoder nor the live decoder needed to change — the only artifact that disagreed with the pack was our own spec line, now corrected. No longer blocks the payload freeze. Still unconfirmed against a real charge current, which is a record-accuracy question, not a node-safety one: nothing in `src/` makes a control decision from the sign ([`docs/reviews/2026-08-12_spec_drift.md`](docs/reviews/2026-08-12_spec_drift.md) §2.2 enumerates the six dependent paths).
- **Sleep current is unmeasured**, and the pack cannot measure it — its current telemetry has a 10 mA LSB while [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md) turns on ~1 mA, and a USB-powered bench node barely loads the pack ([`docs/EVIDENCE.md`](docs/EVIDENCE.md) 2026-08-12, `4510763`). A meter is the only instrument that settles it. H2 stays open until it does.
- **Downlink handling is only half exercised.** A `0x03` status request was delivered and drained on 2026-08-12, but `take_downlink()` has never been observed on the console and the malformed-downlink bounds checking is untested ([#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)).
- **Buck converter** not yet selected, and it must be chosen on no-load quiescent current — a part drawing milliamps at idle exceeds the node's entire average draw.
- Open decisions for the first firmware PR: [`plans/P0_HARDENED_NODE.md`](plans/P0_HARDENED_NODE.md).

## Sibling context

Cloned at `~/Documents/GitHub/` on both machines (not beside this repo):

- `forest-weather-machines` — TTN ingest, the live payload formatter, RAK2560 settings, `rak-4-5-wire` BMS reverse-engineering
- `particle-devices` — Particle trailcam / Muon / Boron (different stack)

## License

TBD (match org default when firmware lands).
