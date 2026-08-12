# Agent notes

Read this first. The detailed rules live in [`.cursor/rules/`](.cursor/rules/) and load
automatically; this is the index and the short version.

## Non-negotiables

- Specs in `docs/` are the contract; do not invent pinouts or Modbus maps.
- Prefer libraries in [`docs/LIBRARIES.md`](docs/LIBRARIES.md).
- Cross-check RAK9154 against `forest-weather-machines/rak-4-5-wire` (local sibling, pinned at
 `efc0e3c`, `~/Documents/GitHub/forest-weather-machines`) — but note it is **M5Stack NanoC6
 (ESP32-C6) firmware, not RAK4631**, and **scope its authority precisely**. It is useless as an
 MCU-side reference, and there is no existing RAK4631 firmware to fork.

 | Path in the sibling @ `efc0e3c` | Authoritative for | Silent on |
 |---|---|---|
 | `firmware/nanoc6-onewire-poll/lib/RAK-OneWire/src/onewire_master_protocol.c` | The **whole one-wire wire format**: `cal_chksum()` popcount algorithm and its byte span (:310), the provisioning reply construction (:398-474), pid = slot index + 1 (:458), dest/source swap (:450-456), length validation (:779) | — (this is the real reference; read it before touching `src/sensors/battery.cpp`) |
 | `firmware/nanoc6-onewire-poll/src/onewire_protocol.cpp` | **Polling a pack that is already provisioned** — query, frame RX, response parse | The announcement handshake. It has **no provisioning handler at all**; it assumes some other master already latched the pid |
 | `firmware/nanoc6-rak9154-poll/src/main.cpp` | **Nothing — the file is 0 bytes.** | Everything. The Modbus claim below has no code behind it in this repo |

 The Modbus figures previously asserted here (slave `0x6E`, 9600 8N1, 21 registers from
 `0x6000`) are **not backed by any source in the sibling** at `efc0e3c`. Treat them as
 unverified until a datasheet or a working capture confirms them — do not cite the sibling
 for them.
- Never commit secrets, `*.env`, keys, or live OTAA AppKeys.
- No aspirational "deployed" claims without bench/TTN evidence — see [`docs/EVIDENCE.md`](docs/EVIDENCE.md).
- Null sensor readings stay null — never fabricate zeros.
- **Either solve it or file it.** Anything noticed and not fixed becomes a GitHub issue in
  the same pass, with a number a comment can cite. Caveats delivered in chat and "one more
  thing" trailers are not a record of anything — they are gone the moment the window
  scrolls. Do not reintroduce a checklist file; the tracker is the one place.

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
  says otherwise. Stages 0-3 have all now run on hardware (join + uplink 2026-07-31, RK900 reply
  2026-08-03, battery 12.23 V 2026-08-05). **That does not change the status.** Deployment stays
  blocked until the H1-H8 gates and the ≥24 h soak / ≥7 d shadow in `docs/EVIDENCE.md` close —
  not merely on "every subsystem answered once."
- **The RAK4631 board definition is vendored** in [`rakwireless/`](rakwireless/) because it
  does not exist in the PlatformIO registry. Do not edit it, and do not "fix" the build by
  copying files into `~/.platformio` — see [`rakwireless/README.md`](rakwireless/README.md).

## Bring-up stages

Each stage adds exactly one new failure domain, so a failure has a short suspect list.

| Stage | Adds | State |
|---|---|---|
| 0 | LED + USB serial | run on hardware 2026-07-31 (`8d4a41c`) — firmware boots and prints over USB CDC ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| 1 | RK900 Modbus over RAK5802 @ 9600 | proven at wire level 2026-08-03 (`998dc26`, `busscan`) — full 5-register frame read at **9600** (not 4800): 25.1 °C, 50.4 %RH, 1007.0 hPa, calm; register map confirmed ([ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md), [`docs/EVIDENCE.md`](docs/EVIDENCE.md)). Remaining: same read through the production `stage1` path |
| 2 | OTAA join + first uplink | done 2026-07-31 — join + accepted uplink, `puma-concolor-001`, session restore across reset ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| 3 | RAK9154 battery telemetry over one-wire | **working on hardware 2026-08-05 (`1a203d3`, re-verified `b6bbf31`)** — pack latches pid `0x01` and reports `12.23 V, +0.00 A, 98%, 23.0 °C` across seven consecutive cycles ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)). Root cause of the long stall was reply turnaround timing, not framing: answer no sooner than 2 ms after the pack's last byte (`kTurnaroundMs`) and lead every frame with four wake bytes. Expect ~2 null cycles after boot while the pack samples. Open: [#36](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/36), [#37](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/37) |

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
