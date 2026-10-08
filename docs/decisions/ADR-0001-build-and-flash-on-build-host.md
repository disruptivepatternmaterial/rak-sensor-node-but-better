# ADR-0001 — Author locally, build and flash on Heliotrope Ridge

- **Status:** Accepted
- **Date:** 2026-07-30
- **Affects:** all firmware work, `scripts/*`, CI, `.cursor/rules/10-environments.mdc`

## Context

Work happens across two machines, and they are not interchangeable.

The **workstation** hosts this repo and is where code and docs get authored. It is locked
down: the agent sandbox denies writes outside the workspace, so `$HOME` is read-only and
`~/.platformio` cannot be created. It also has no RAK4631 on USB — only
`/dev/cu.Bluetooth-Incoming-Port` and `/dev/cu.debug-console`.

**Heliotrope Ridge** already has PlatformIO
6.1.19, `gh`, `GITHUB_TOKEN`, PyYAML, a clone at
`~/Documents/GitHub/lorawan/rak-sensor-node-but-better`, and — decisively — it is where the
hardware physically connects.

You also work directly on the build host or a laptop at times, so neither clone can be
assumed current.

## Options considered

| Option | Pros | Cons |
|---|---|---|
| A. Install the toolchain on the workstation too | Faster inner loop; no SSH | Sandbox blocks `~/.platformio`; still cannot flash — no device on USB. Two toolchain versions to keep in step. |
| B. Author locally, build and flash on the build host | One toolchain; the machine with the hardware is the machine that builds; already provisioned | Every build needs a sync; SSH adds friction |
| C. Work entirely on the build host over SSH | No split at all | Loses local editing; makes the workstation useless for this project |

## Decision

**Option B.** Author on the workstation; compile, flash, and soak on Heliotrope Ridge.
**Git is the only transport between the machines.**

## Rationale

Option A cannot deliver the thing that actually matters — flashing — because the device is
not on the workstation's USB. Paying for a toolchain that can never complete the loop, and
then keeping it version-matched, is cost with no payoff. Building where the hardware lives
means the artifact that gets flashed is the artifact that was built, on one toolchain.

The tradeoff accepted is a slower inner loop: every build is preceded by commit → push →
pull. That friction is deliberate. It forces every flashed binary to correspond to a
pushed commit, which is what makes a field node traceable later.

## Consequences

- `scripts/remote.sh` wraps all remote execution in `zsh -l -c`, because non-interactive
  SSH does not source `~/.zprofile` and `/opt/homebrew/bin` is otherwise missing from
  `PATH` — making installed tools appear absent. This wasted time during setup and is now
  handled in one place.
- `scripts/remote.sh sync` refuses on a dirty tree on **either** machine. Work that exists
  only on the build host is resolved by hand, never discarded.
- No `scp`/`rsync` of source, ever — that creates untracked divergence.
- Tooling must run on both hosts. The workstation lacks PyYAML and cannot install it, so
  `scripts/check_decoder_parity.py` ships a fallback YAML loader.
- Every build/flash/bench result is reported with its host and commit SHA
  (`docs/EVIDENCE.md`).

## Evidence

Environment inventory taken 2026-07-30, recorded in [`../ENVIRONMENTS.md`](../ENVIRONMENTS.md):
workstation `$HOME` write denied; build host `pio --version` → `PlatformIO Core, version 6.1.19`
only under a login shell; no `/dev/cu.usbmodem*` on either host (hardware on order).

## Citations

- CITE(sibling): build-host clone and toolchain inventory, `forest-weather-machines` @ `efc0e3c` [CIT-FWM-DECODER]
- CITE(prior-art): PlatformIO `wiscore_rak4631` as the build system of record — [`../LIBRARIES.md`](../LIBRARIES.md) item 25
- CITE(datasheet): RAK4631 target module [CIT-RAK4631]
