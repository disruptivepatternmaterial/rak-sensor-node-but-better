# Agent notes

The index. The rules in [`.cursor/rules/`](.cursor/rules/) load automatically; project state
lives in `docs/` — read it there instead of trusting any summary of it, including this one.

## Hardware safety — read before touching anything physical

**Nine GPIO pads are dead. Every one died under diagnostic firmware that agents wrote and
flashed without authorization; zero died under the production image**
([#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102)).
The consequences are absolute:

- **Never flash a board unless the operator asked for that specific flash.** Wanting data is
  not authorization.
- **Never write, recreate, or flash a diagnostic that drives the one-wire pad** — `owscan` and
  its five environments were deleted 2026-08-30 and stay deleted, not behind a flag, not
  "passive". Pack questions use `env:battdiag*` (the production driver, ~10 s cycles), never
  `stage3`, whose long cycle has repeatedly misled sessions into misreading the pack's normal
  ~2 null cycles after boot as failure.
- **Never instruct a connection to an unmeasured voltage.** Analyzer first, meter second, pad
  last — rule [`05`](.cursor/rules/05-never-instruct-an-unmeasured-connection.mdc); procedure
  in [`docs/HARDWARE.md`](docs/HARDWARE.md) § "Qualifying the pack harness".
- **Flashing is mechanically gated, not just forbidden.** `.cursor/hooks/flash_gate.py`
  (fail-closed, wired in `.cursor/hooks.json`) intercepts every flash-capable shell command —
  `pio … upload`, `nrfutil`/`nrfjprog`/`pyocd`/`openocd`, `flash.sh`, UF2 copies, 1200-baud
  touch, including any of these wrapped in `ssh` — and requires the operator to approve that
  specific command in the UI before it runs. Edits to the hooks themselves are gated the same
  way. Do not attempt to rephrase a command around the gate; ask the operator. Verified 17/17
  pattern cases 2026-08-30.

## Non-negotiables

- Specs in `docs/` are the contract. Never invent pinouts, register maps, or constants — cite
  every one (rule 20). Prefer libraries in [`docs/LIBRARIES.md`](docs/LIBRARIES.md).
- Every GitHub issue an agent creates needs operator approval first (rule 02).
- Null sensor readings stay null — never fabricate zeros.
- Never commit secrets, keys, EUIs, or live OTAA AppKeys.
- **Record outcomes, never launches.** No evidence entry for anything still in flight, and no
  "deployed" claim without bench/TTN evidence — a false "soak started" entry once propagated
  through three documents before being caught. Verify against the log, not the previous doc.
- Every build, flash, or soak result names its host and commit SHA, or it is not evidence.
- Status is **🚧 NOT YET DEPLOYED** until the H1–H8 gates and the ≥24 h bench soak / ≥7 d field
  shadow close in [`docs/EVIDENCE.md`](docs/EVIDENCE.md).

## The rules

| Rule | Covers |
|---|---|
| [00-agent-liveness](.cursor/rules/00-agent-liveness.mdc) | Progress every 2–4 min; stale at 5; bounded calls and retries |
| [01-response-style](.cursor/rules/01-response-style.mdc) | Consequences not activity; never restate the operator's hardware (generic style rules are global) |
| [02-issue-approval](.cursor/rules/02-issue-approval.mdc) | Operator approves every issue before it is filed |
| [03-bench-claims](.cursor/rules/03-bench-claims.mdc) | Observation vs hypothesis; two-guess limit; evidence names the core; closed topics |
| [05-never-instruct](.cursor/rules/05-never-instruct-an-unmeasured-connection.mdc) | No connection to an unmeasured voltage, ever |
| [10-environments](.cursor/rules/10-environments.mdc) | Author locally, build/flash on Heliotrope Ridge; SSH and git-transport rules |
| [20-citation-discipline](.cursor/rules/20-citation-discipline.mdc) | `CITE(category)` format; minimum citations per change |
| [30-change-workflow](.cursor/rules/30-change-workflow.mdc) | Issue → research → implement → review → build → flash → evidence → docs → version → push |
| [40-lorawan-compliance](.cursor/rules/40-lorawan-compliance.mdc) | US915 Class A, airtime budget, downlink validation |
| [50-power-management](.cursor/rules/50-power-management.mdc) | Sleep path, peripheral shutdown, brownout |
| [60-decoder-parity](.cursor/rules/60-decoder-parity.mdc) | Payload contract with the live TTN formatter |

## Fast orientation

```bash
scripts/preflight.sh     # all local gates (same as CI)
scripts/remote.sh check  # build host reachability + toolchain
scripts/build.sh         # preflight + sync + compile on the build host
scripts/flash.sh         # build + USB flash on the build host (confirms first)
scripts/push.sh          # push relayed via the build host (plain git push also works)
```

This machine cannot compile or flash — no PlatformIO, no device on USB, `$HOME` read-only.
Everything hardware runs on the build host, whose address the operator supplies
(`RAK_BUILD_HOST`); a failed SSH means the laptop is elsewhere, not a dead host. See rule 10
and [`docs/ENVIRONMENTS.md`](docs/ENVIRONMENTS.md).

## Where the facts live

| Question | File |
|---|---|
| What has each release proven on hardware? | [`docs/STATUS.md`](docs/STATUS.md) |
| What has been measured, on which core, at which SHA? | [`docs/EVIDENCE.md`](docs/EVIDENCE.md) |
| What exact assembly step comes next? | [`docs/BUILD.md`](docs/BUILD.md) — the only build sequence |
| Wiring research, pinouts, electrical rationale | [`docs/HARDWARE.md`](docs/HARDWARE.md) |
| Firmware behavior contract, H1–H8 hardening | [`docs/FIRMWARE_SPEC.md`](docs/FIRMWARE_SPEC.md) |
| RAK9154 protocol sourcing (and what the sibling repo is and is not authoritative for) | [`docs/research/rak9154-battery-protocol-sources.md`](docs/research/rak9154-battery-protocol-sources.md) |
| Decisions and their reasoning | [`docs/decisions/`](docs/decisions/) |
| Soak procedure and criteria | [`docs/SOAK.md`](docs/SOAK.md) |

The sibling `forest-weather-machines/rak-4-5-wire` is ESP32-C6 firmware, not RAK4631 — scope
its authority per the research doc, and pin any sibling citation to a commit SHA. The RAK4631
board definition is vendored in [`rakwireless/`](rakwireless/) — do not edit it or copy it into
`~/.platformio` ([`rakwireless/README.md`](rakwireless/README.md)). The TTN formatter lives in
`forest-weather-machines`; a drifted encoder discards the whole uplink (rule 60). Sleep current
is unmeasured — pack telemetry's 10 mA LSB is a resolution floor, never quote it as a
measurement ([`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md)).

## The deployment goal, in one line

Unattended in the woods **indefinitely**, on a solar-recharged RAK9154 — nobody hikes out to
power-cycle it. So: prefer deleting a failure mode over handling one (ADR-0004), and never let
the pack reach a state it cannot recover from by itself — stop transmitting early and keep
sleeping ([`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md)). A lost day of data is free; a hike
is not.
