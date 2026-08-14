# Environments

Two machines. Knowing which one you are on prevents most of the wasted time in this
project. Agent-facing version: [`.cursor/rules/10-environments.mdc`](../.cursor/rules/10-environments.mdc).

## Topology

```
Workstation (locked down)                Heliotrope Ridge (build host)
    ~/Documents/Brandt/                      ~/Documents/GitHub/lorawan/
  rak-sensor-node-but-better               rak-sensor-node-but-better
                                           $RAK_BUILD_HOST (ssh alias: wx3-harness)
  author code + docs                       PlatformIO 6.1.19, gh, PyYAML
  git, gh                                  RAK4631 on USB  <-- the only one
  NO PlatformIO                            flash, serial capture, soak
  NO device on USB
        |                                            ^
        +------------- git push / pull --------------+
                    (the only transport)
```

## The build host address — not stable, and not committed

**The address has already changed once, and will change again. Treat every address you
find written down as expired until a probe says otherwise.**

| Period | Address | State |
|---|---|---|
| until 2026-08-12 | `192.168.10.223` (LAN) | **dead** — now fails `No route to host` |
| 2026-08-12 → | a **public** address, ask the operator | working, verified 2026-08-13 |

The current address is deliberately **not recorded anywhere in this repository** — a public
address plus the account name in a tracked file is an invitation, and this repo is public
(confirmed `PUBLIC` via `gh repo view`, 2026-08-13). So an old transcript or an old commit
showing `192.168.10.223` is not wrong about history; it is simply stale. Two sessions have
now been lost to reading it as current and concluding the host was down.

### Confirm reachability in one command

Do this **before** concluding anything about the host. It is one round trip:

```bash
ssh -o ConnectTimeout=8 -o BatchMode=yes "$RAK_BUILD_HOST" 'zsh -l -c "hostname"'
# → Heliotrope-Ridge
```

`BatchMode=yes` is deliberate: key auth is already set up (see below), so a prompt for a
password means the key is not loaded, not that the host is unreachable.

### Setting the address

```bash
export RAK_BUILD_HOST=ntableman@<address>       # this shell only
```

```
Host wx3-harness                                # ~/.ssh/config — preferred, persists
    HostName <address>
    User ntableman
```

**As of 2026-08-14 the `wx3-harness` alias on the workstation is a trap, not the fix.** Its
`HostName` is still the dead LAN address `192.168.10.223`, so falling back to the alias fails
with `No route to host` — which reads as "the build host is down" and is the exact
misdiagnosis that has already cost two sessions. Until someone repoints that `HostName` at the
current address, **always `export RAK_BUILD_HOST=ntableman@<address>` explicitly** and confirm
with one `hostname` call before diagnosing anything else. The agent sandbox cannot fix this:
`$HOME` is read-only, so `~/.ssh/config` can only be edited by the operator.

Separately, the build host does go away without warning. On 2026-08-14 it answered normally at
15:12Z and 15:19Z, then stopped answering both SSH and ICMP by 15:36Z (100% packet loss) with a
24 h soak still in flight. A workstation session that loses the host mid-task cannot push at
all — `git push origin` is 403 for the work account (see below), and the relay needs the host
that just vanished. The correct move is to leave the work **committed locally** and say so,
never to improvise around the credential boundary.

`scripts/push.sh`, `scripts/remote.sh`, `scripts/build.sh`, `scripts/flash.sh` and
`scripts/soak.sh` all resolve the host as
`${BUILD_HOST:-${RAK_BUILD_HOST:-wx3-harness}}` — so `BUILD_HOST` still works for existing
shells, `RAK_BUILD_HOST` is the name to use going forward, and with neither set they fall
back to the ssh alias, which keeps the address in `~/.ssh/config` where it belongs.

`scripts/remote.sh check` prints the host it resolved before it tries it, and both it and
`push.sh` probe reachability first so a wrong address fails in seconds with instructions
rather than stalling in a TCP timeout that reads as a broken script.

Ask the operator for the current address. Do not guess it, and do not paste it into a
commit, an issue, a doc, or a comment.

## Capability matrix

| | Workstation | Heliotrope Ridge |
|---|---|---|
| Reach | local | `ssh "$BUILD_HOST"` — **see the address note below** |
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
$ ssh "$RAK_BUILD_HOST" 'pio --version'
zsh: command not found: pio          # <-- WRONG. PlatformIO 6.1.19 is installed.

$ ssh "$RAK_BUILD_HOST" 'zsh -l -c "pio --version"'
PlatformIO Core, version 6.1.19      # correct
```

`scripts/remote.sh` wraps every remote command in `zsh -l -c`. Use it rather than
hand-rolling SSH. Before concluding a tool is missing, check with
`zsh -l -c "command -v <tool>"`.

## The workstation's SSH key is authorized on the build host

**Added 2026-08-12.** The workstation's `~/.ssh/id_rsa.pub` is in the build host's
`~/.ssh/authorized_keys`, so `ssh` to the build host works without a password.

This is load-bearing, not a convenience. `scripts/push.sh` — and therefore
`scripts/build.sh`, `scripts/flash.sh` and `scripts/remote.sh sync`, all of which relay
through it — runs SSH with **`BatchMode` on**, which disables every interactive
authentication method. Before the key was authorized, all of them failed with a message
that reads like a network fault:

```console
ERROR cannot reach build host 'wx3-harness'.
      (BatchMode is on here, so a host needing a password also lands here;
       load the key into the agent or use an alias with IdentityFile.)
```

The address is fine and the host is up; there is simply no way to answer a password prompt
from inside those scripts. That failure cost time repeatedly on 2026-08-12 and sent more
than one session hand-rolling `ssh` calls around the tooling instead of using it.

To verify the key still works, or to diagnose the same failure recurring:

```bash
ssh -o BatchMode=yes "$BUILD_HOST" 'echo ok'      # must print ok, with no prompt
```

If that prompts or fails, re-append the workstation public key to the build host's
`~/.ssh/authorized_keys`. Per the section above, **the host address stays out of this
repository** — pass it as `BUILD_HOST=ntableman@<address>` or via an SSH alias.

## Git is the only transport

You sometimes work directly on the build host or a laptop, so **neither machine is
authoritative.** Uncommitted local edits do not exist on the other side.

`scripts/remote.sh sync` enforces the safe path: it refuses on a dirty local tree, pushes
the branch, refuses if the **build host** tree is dirty (someone was working there — that
work gets resolved by hand, never discarded), fast-forwards, and verifies both HEADs match.

Never `scp` or `rsync` source between machines. That produces an untracked divergence that
cannot be reproduced later, which is exactly the state that makes field firmware
unexplainable.

## Two GitHub identities

You have a work account and a personal one, and they land in different places. Written
down here so nobody — human or agent — has to work it out again.

| Where | Authenticates as | Push to `disruptivepatternmaterial`? |
|---|---|---|
| Workstation git CLI, all three SSH keys, and the keychain credential | `ntableman_sfemu` | **No** — HTTP 403 |
| Your IDE, using its own GitHub sign-in | personal | Yes |
| Heliotrope Ridge build host | `disruptivepatternmaterial` | Yes |

Verified 2026-07-30: `id_rsa`, `id_ed25519_gh`, and `id_ed25519_particle` all return
`Hi ntableman_sfemu!` from `ssh -T git@github.com`. Your IDE can push because its GitHub
sign-in is separate from the git CLI's credentials.

The practical effects:

- **An agent on this machine cannot `git push origin`.** It has to relay through the build
  host. `scripts/push.sh` does exactly that and is the only thing an agent should use.
- **`gh` on this machine talks as the work account.** For CI status, run `gh` on the build
  host.
- **You pushing mid-session is normal.** Agents are told to expect unfamiliar commits on
  `origin/main` and just fetch and carry on, rather than treating them as a mystery. Be
  aware the reverse also happens: committing from the IDE can capture an agent's
  half-finished working tree and send a failing gate to CI.

## Commands

```bash
scripts/remote.sh check       # reachability + toolchain + remote HEAD
scripts/remote.sh sync        # push local, fast-forward remote, verify SHA match
scripts/remote.sh devices     # USB serial devices on the build host
scripts/remote.sh run '<cmd>' # run inside the remote repo under a login shell

scripts/preflight.sh          # local gates (same as CI)
scripts/build.sh              # preflight + sync + remote compile
scripts/flash.sh              # build + USB flash on the build host (asks to confirm)
scripts/push.sh               # push to GitHub, relayed via the build host's identity
```

Overridable: `BUILD_HOST`, `REMOTE_REPO`, `FWM_REPO`.

## Reporting results

Every build, flash, or bench result must state **which host** and **which commit SHA**.
A result without both is not evidence and does not belong in
[`EVIDENCE.md`](EVIDENCE.md).

## Known state (2026-07-30)

- Firmware is in-tree and compiles on the build host for all four stages
  (`stage1`, `stage2`, `stage3`, `rak4631`). Off-target tests run there too:
  `pio test -e native`.
- Nothing has run on hardware. No reading has ever been taken.
- No RAK4631 on the build host USB yet, so `flash.sh` will correctly refuse until one is
  plugged in. Bring-up order: [`FIRST_FLASH.md`](FIRST_FLASH.md).
- Also cloned on the build host and useful as references: `WisBlock-Seismic-Sensor`,
  `WisBlock-Solar-Env-Sensor`.
