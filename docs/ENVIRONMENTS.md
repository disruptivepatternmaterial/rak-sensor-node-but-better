# Environments

Two machines. Knowing which one you are on prevents most of the wasted time in this
project. Agent-facing version: [`.cursor/rules/10-environments.mdc`](../.cursor/rules/10-environments.mdc).

## Topology

```
Workstation (locked down)                Heliotrope Ridge (build host)
~/Documents/Brandt/                      ~/Documents/GitHub/
  rak-sensor-node-but-better               rak-sensor-node-but-better
                                           192.168.10.223  (ssh alias: wx3-harness)
  author code + docs                       PlatformIO 6.1.19, gh, PyYAML
  git, gh                                  RAK4631 on USB  <-- the only one
  NO PlatformIO                            flash, serial capture, soak
  NO device on USB
        |                                            ^
        +------------- git push / pull --------------+
                    (the only transport)
```

## Capability matrix

| | Workstation | Heliotrope Ridge |
|---|---|---|
| Reach | local | `ssh ntableman@192.168.10.223` |
| PlatformIO | absent | `/opt/homebrew/bin/pio` (6.1.19) |
| `gh` CLI | present | `/opt/homebrew/bin/gh`, `GITHUB_TOKEN` in `~/.zprofile` |
| Python | 3.14 (Homebrew) | 3.9 system + Homebrew python3 with PyYAML 6.0.3 |
| PyYAML | **absent** | present |
| USB / DFU | no device | RAK4631 target |
| Writes outside repo | **blocked** (agent sandbox) | unrestricted |
| Sibling repos | `~/Documents/GitHub/forest-weather-machines` | same path |

Tooling must run in both places, so `scripts/check_decoder_parity.py` falls back to a
built-in YAML loader when PyYAML is unavailable rather than requiring an install the
workstation cannot perform.

## Division of labor

**Author on the workstation. Compile and flash on Heliotrope Ridge.**

Do not attempt to install PlatformIO or nRF tooling on the workstation. The agent sandbox
denies writes to `$HOME`, so `~/.platformio` cannot be created — and the hardware is on
the other machine's USB anyway. If a tool is missing on both, install it on the build host
and add it to the matrix above.

## The SSH PATH trap

**Non-interactive SSH does not source `~/.zprofile`, so `/opt/homebrew/bin` is missing from
`PATH`.** This makes installed tools look absent:

```console
$ ssh ntableman@192.168.10.223 'pio --version'
zsh: command not found: pio          # <-- WRONG. PlatformIO 6.1.19 is installed.

$ ssh ntableman@192.168.10.223 'zsh -l -c "pio --version"'
PlatformIO Core, version 6.1.19      # correct
```

`scripts/remote.sh` wraps every remote command in `zsh -l -c`. Use it rather than
hand-rolling SSH. Before concluding a tool is missing, check with
`zsh -l -c "command -v <tool>"`.

## Git is the only transport

You sometimes work directly on the build host or a laptop, so **neither machine is
authoritative.** Uncommitted local edits do not exist on the other side.

`scripts/remote.sh sync` enforces the safe path: it refuses on a dirty local tree, pushes
the branch, refuses if the **build host** tree is dirty (someone was working there — that
work gets resolved by hand, never discarded), fast-forwards, and verifies both HEADs match.

Never `scp` or `rsync` source between machines. That produces an untracked divergence that
cannot be reproduced later, which is exactly the state that makes field firmware
unexplainable.

## Commands

```bash
scripts/remote.sh check       # reachability + toolchain + remote HEAD
scripts/remote.sh sync        # push local, fast-forward remote, verify SHA match
scripts/remote.sh devices     # USB serial devices on the build host
scripts/remote.sh run '<cmd>' # run inside the remote repo under a login shell

scripts/preflight.sh          # local gates (same as CI)
scripts/build.sh              # preflight + sync + remote compile
scripts/flash.sh              # build + USB flash on the build host (asks to confirm)
```

Overridable: `BUILD_HOST`, `REMOTE_REPO`, `FWM_REPO`.

## Reporting results

Every build, flash, or bench result must state **which host** and **which commit SHA**.
A result without both is not evidence and does not belong in
[`EVIDENCE.md`](EVIDENCE.md).

## Known state (2026-07-30)

- Both clones at `5307d1d`, specs only, no firmware in-tree.
- No RAK4631 on the build host USB yet — hardware on order, so `flash.sh` will correctly
  refuse until it arrives.
- Also cloned on the build host and useful as references: `WisBlock-Seismic-Sensor`,
  `WisBlock-Solar-Env-Sensor`.
