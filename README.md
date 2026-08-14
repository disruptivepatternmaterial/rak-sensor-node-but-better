# rak-sensor-node-but-better

WisBlock replacement for the RAK2560 Sensor Hub path: **RAK19007 + RAK4631 US915 + RAK5802**, polling **RK900-09** + **RAK9154**, LoRaWAN Class A, woods-hardened.

## Status

🚧 **Not deployed.** **One** full cycle of the field image has been observed end to end — one, not a run of them. On 2026-08-12 at `4510763` both sensors read in the **same cycle of the `rak4631` image** for the first time — `RK900 : wind 0.00 m/s @ 0 deg, 24.7 C, 58.7 %RH, 1004.4 hPa` and `battery : 12.12 V -0.01 A 91% 23.0 C` — the cycle reached sleep, and the uplink went out. Network side, TTN shows the session live and advancing: `dev_addr 260CE734`, `last_f_cnt_up` climbing, gateway `3356-gateway-002` at 13–14 dB SNR, with `f_cnt 1792` landing the same second as the console's `radio : sent 35 bytes on port 2`. The first downlink ever delivered to this node — a `0x03` status request — drained from the queue across one uplink.

**Every subsystem answering once is not a deployment.** None of the H1–H8 hardening gates has closed. **Zero soak hours exist** — the harness and procedure are built ([`docs/SOAK.md`](docs/SOAK.md), `scripts/soak.sh`, `env:soak`), but the one attempt on 2026-08-12 never attached to the board and logged nothing, so the ≥24 h bench soak has not started and the ≥7 d field shadow has not begun; sleep current has never been metered — the pack's own telemetry cannot answer it (10 mA LSB against a ~1 mA question); and while the battery-current sign convention is now decided ([ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md), 2026-08-13 — positive = charging), it has still never been confirmed against a real charge current. Status changes only when [`docs/EVIDENCE.md`](docs/EVIDENCE.md) records those gates.

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
- **Heliotrope Ridge** (`$RAK_BUILD_HOST`, ssh alias `wx3-harness`) — compile, flash, soak.
  The RAK4631 lives here. The address is not stable and is not recorded in this repo; the
  old `192.168.10.223` is dead. See [`docs/ENVIRONMENTS.md`](docs/ENVIRONMENTS.md).
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
| `owscan` | One-wire line behaviour below the protocol |
| `busscan` | RS-485 sweep across baud rates and slave addresses — the tool that established the RK900's real baud and register map for [ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md) |

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
