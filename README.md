# rak-sensor-node-but-better

WisBlock replacement for the RAK2560 Sensor Hub path: **RAK19007 + RAK4631 US915 + RAK5802**, polling **RK900-09** + **RAK9154**, LoRaWAN Class A, woods-hardened.

## Status

🚧 **Not deployed.** All four bring-up stages have now run on hardware: the board boots and prints, the RK900 answers a full five-register read, an OTAA join plus a real-sensor uplink have reached TTN, and as of 2026-08-05 the RAK9154 pack reports `12.23 V, +0.00 A, 98%, 23.0 °C` over one-wire across seven consecutive cycles. **Every subsystem answering once is not a deployment.** None of the H1–H8 hardening gates has closed, the ≥24 h bench soak and ≥7 d field shadow have not run, and the battery-current sign convention is still unresolved ([ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md)). Status changes only when [`docs/EVIDENCE.md`](docs/EVIDENCE.md) records those gates.

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
| Power budget | [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md) |
| Versioning / release | [`docs/RELEASE.md`](docs/RELEASE.md) |
| Decisions (ADRs) | [`docs/decisions/`](docs/decisions/) |
| Payload schema | [`payload/schema.yaml`](payload/schema.yaml) |
| Open items | [GitHub Issues](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues) |

## Where things run

Two machines, and they are not interchangeable. Full detail in [`docs/ENVIRONMENTS.md`](docs/ENVIRONMENTS.md).

- **This workstation** — author code and docs. No PlatformIO, no device on USB.
- **Heliotrope Ridge** (`192.168.10.223`) — compile, flash, soak. The RAK4631 lives here.
- **Git is the only transport between them.**

```bash
scripts/preflight.sh          # all local gates (same as CI)
scripts/remote.sh check       # build host reachability + toolchain
scripts/build.sh              # preflight + sync + compile on the build host
scripts/flash.sh              # build + USB flash on the build host (confirms first)
scripts/flash.sh --env stage1 # flash one bring-up stage instead of the full image
```

## Build stages

Four images, so a subsystem can be brought up alone and a failure has one candidate cause rather than six. Tests for the payload encoder and the Modbus checksum run on the build host with no board attached (`pio test -e native`).

| Environment | Contains |
|---|---|
| `stage1` | Wind sensor only, awake, printing over USB |
| `stage2` | Adds the battery reader |
| `stage3` | Adds the radio — still awake, so the join is watchable |
| `rak4631` | Full image: sleep and watchdog on |
| `native` | Off-target tests, no hardware |

## Working discipline

Rules live in [`.cursor/rules/`](.cursor/rules/) and are indexed in [`AGENTS.md`](AGENTS.md). The short version:

- **Cite everything.** Every register, baud rate, timeout, and RF parameter needs a manufacturer or spec source — at least 3 citations across 2 categories per firmware change. Prior art never stands alone.
- **Every build checks the TTN formatter.** The payload is a two-repo contract with `forest-weather-machines`. A drifted encoder does not lose one field — the decoder throws and discards the whole uplink.
- **Evidence, not claims.** Nothing is "working" until it is measured and recorded, with the host and commit SHA.
- **Nulls stay null.** A failed sensor read is never encoded as zero.

## Known blockers

- **RAK9154 provisioning** — the pack announces itself as unprovisioned (`provId = 0xFF`) and waits for the host to assign it an id. That is our firmware's job, over the one-wire link, in `acquire_pid()` — and it is not latching: 22 correct-looking answers, still `0xFF` ([`docs/EVIDENCE.md`](docs/EVIDENCE.md) 2026-08-05). An undiagnosed host-side protocol defect, not an operator step. Earlier revisions of this file claimed a WisToolBox NFC/BLE provisioning step was required; that claim was fabricated and is withdrawn.
- [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md) — the battery current sign convention contradicts between our spec and the live decoder. Blocks the payload freeze.
- **Buck converter** not yet selected, and it must be chosen on no-load quiescent current — a part drawing milliamps at idle exceeds the node's entire average draw.
- Open decisions for the first firmware PR: [`plans/P0_HARDENED_NODE.md`](plans/P0_HARDENED_NODE.md).

## Sibling context

Cloned at `~/Documents/GitHub/` on both machines (not beside this repo):

- `forest-weather-machines` — TTN ingest, the live payload formatter, RAK2560 settings, `rak-4-5-wire` BMS reverse-engineering
- `particle-devices` — Particle trailcam / Muon / Boron (different stack)

## License

TBD (match org default when firmware lands).
