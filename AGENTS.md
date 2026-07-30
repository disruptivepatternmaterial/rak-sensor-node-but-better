# Agent notes

Read this first. The detailed rules live in [`.cursor/rules/`](.cursor/rules/) and load
automatically; this is the index and the short version.

## Non-negotiables

- Specs in `docs/` are the contract; do not invent pinouts or Modbus maps.
- Prefer libraries in [`docs/LIBRARIES.md`](docs/LIBRARIES.md).
- Cross-check RAK9154 against `forest-weather-machines/rak-4-5-wire` (local sibling,
  `~/Documents/GitHub/forest-weather-machines`) — but note it is **M5Stack NanoC6
  (ESP32-C6) firmware, not RAK4631**. It is authoritative for the RAK9154 protocol
  (slave `0x6E`, 9600 8N1, 21 registers from `0x6000`) and useless as an MCU-side
  reference. There is no existing RAK4631 firmware to fork.
- Never commit secrets, `*.env`, keys, or live OTAA AppKeys.
- No aspirational "deployed" claims without bench/TTN evidence — see [`docs/EVIDENCE.md`](docs/EVIDENCE.md).
- Null sensor readings stay null — never fabricate zeros.

## The rules

| Rule | Covers |
|---|---|
| [00-agent-liveness](.cursor/rules/00-agent-liveness.mdc) | Report progress every 2–4 min; stale at 5; bounded retries; no unbounded calls |
| [10-environments](.cursor/rules/10-environments.mdc) | Author locally, build and flash on Heliotrope Ridge; the SSH PATH trap; git is the only transport |
| [20-citation-discipline](.cursor/rules/20-citation-discipline.mdc) | Multiple citations per change; `CITE(category)` format; no unsourced constants |
| [30-change-workflow](.cursor/rules/30-change-workflow.mdc) | Issue → research → implement → review → build → flash → evidence → docs → version → push |
| [40-lorawan-compliance](.cursor/rules/40-lorawan-compliance.mdc) | US915 Class A, airtime budget, downlink validation |
| [50-power-management](.cursor/rules/50-power-management.mdc) | Sleep path, `Serial.end()`, brownout, months unattended |
| [60-decoder-parity](.cursor/rules/60-decoder-parity.mdc) | Every build verifies the TTN formatter and calls out when it must change |

## Fast orientation

```bash
scripts/preflight.sh          # all local gates (same as CI)
scripts/remote.sh check       # build host reachability + toolchain
scripts/build.sh              # preflight + sync + compile on the build host
scripts/flash.sh              # build + USB flash on the build host (confirms first)
scripts/push.sh               # push to GitHub (this machine cannot push directly)
```

- **Two GitHub identities, and this machine has the wrong one.** The git CLI, all three
  SSH keys, and the keychain credential authenticate as the work account, which gets 403
  on this repo. Use `scripts/push.sh`, which relays through the build host. Do not "fix"
  the remote URL. Commits from the user that you did not write are **normal** — they push
  from their IDE, which has a separate GitHub sign-in. Fetch and carry on.

- **This machine cannot compile or flash.** No PlatformIO, no device on USB, `$HOME` is
  read-only. Everything that touches hardware runs on Heliotrope Ridge
  (`ssh ntableman@192.168.10.223`) — and remote commands need `zsh -l -c` or `pio` will
  look like it is not installed. Use `scripts/remote.sh`.
- **The payload is a two-repo contract.** The TTN formatter lives in
  `forest-weather-machines`. A drifted encoder does not lose one field; the decoder throws
  and discards the entire uplink. `scripts/check_decoder_parity.py` runs on every build.
- **Status is `🚧 NOT YET DEPLOYED`** and stays that way until [`docs/EVIDENCE.md`](docs/EVIDENCE.md)
  says otherwise. Stage 0 firmware compiles; nothing has been run on hardware.
- **The RAK4631 board definition is vendored** in [`rakwireless/`](rakwireless/) because it
  does not exist in the PlatformIO registry. Do not edit it, and do not "fix" the build by
  copying files into `~/.platformio` — see [`rakwireless/README.md`](rakwireless/README.md).

## Bring-up stages

Each stage adds exactly one new failure domain, so a failure has a short suspect list.

| Stage | Adds | State |
|---|---|---|
| 0 | LED + USB serial | compiles; not yet run on hardware |
| 1 | RK900 Modbus over RAK5802 @ 4800 | not started |
| 2 | OTAA join + first uplink | not started; needs a TTN device |
| 3 | RAK9154 battery telemetry over one-wire | not started; unblocked by ADR-0004 |

## Open blockers

- [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md) — battery current sign
  is contradictory between the spec and the live decoder. Blocks the payload freeze. Do not
  guess it.
- Remaining open decisions are in [`plans/P0_HARDENED_NODE.md`](plans/P0_HARDENED_NODE.md).
  Decision #1 (BMS bus) is closed by [ADR-0004](docs/decisions/ADR-0004-bms-one-wire-path.md);
  decision #4 (framework) by [ADR-0003](docs/decisions/ADR-0003-firmware-framework.md).

## The deployment goal, in one line

Unattended in the woods **indefinitely**, on a solar-recharged RAK9154. Nobody is going to
walk out and power-cycle it. Two consequences that outrank feature work:

- **Prefer deleting a failure mode over handling one.** ADR-0004 chose two separate sensor
  buses over one shared bus for exactly this reason.
- **Never let the pack reach a state it cannot recover from by itself.** Stop transmitting
  early and keep sleeping. A lost day of data is free; a hike is not.
  See [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md).
