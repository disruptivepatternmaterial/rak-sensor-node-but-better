#!/usr/bin/env bash
# Talk to the Heliotrope Ridge build host.
#
# Everything that compiles, flashes, or touches USB happens there -- the workstation
# has no PlatformIO and no device on USB. See .cursor/rules/10-environments.mdc.
#
# The trap this wraps: non-interactive SSH does not source ~/.zprofile, so
# /opt/homebrew/bin is missing from PATH and `pio` reports "command not found" even
# though it is installed. Every remote command therefore runs under `zsh -l -c`.
#
# Usage:
#   scripts/remote.sh check              toolchain + reachability
#   scripts/remote.sh sync               push local, fast-forward remote, verify SHA
#   scripts/remote.sh run '<command>'    run in the remote repo under a login shell
#   scripts/remote.sh sha                remote HEAD
#   scripts/remote.sh devices            USB serial devices on the build host
#   scripts/remote.sh usbpid [serial]     RAK4631 USB product ID (8029 = app, 0029/002A = DFU)
#                                          pin against a serial (or $RAK_SERIAL) when more
#                                          than one 239A device is on the bus -- see #29

set -euo pipefail

# The build host is a laptop, so its address moves with the network, and no literal address
# belongs in this file -- the repository is public. Resolution order, first non-empty wins:
#
#   BUILD_HOST=ntableman@<address>                # one command; honored first for
#                                                # compatibility with existing shells
#   export RAK_BUILD_HOST=ntableman@<address>     # this shell only
#   ~/.rak-build-host                             # untracked, one line, persistent
BUILD_HOST="${BUILD_HOST:-${RAK_BUILD_HOST:-}}"
if [[ -z "$BUILD_HOST" && -r "$HOME/.rak-build-host" ]]; then
  IFS= read -r BUILD_HOST < "$HOME/.rak-build-host" || BUILD_HOST=""
  BUILD_HOST="${BUILD_HOST//[[:space:]]/}"
fi
BUILD_HOST_NAME="${BUILD_HOST_NAME:-Heliotrope Ridge}"
REMOTE_REPO="${REMOTE_REPO:-\$HOME/Documents/GitHub/lorawan/rak-sensor-node-but-better}"
SSH_OPTS=(-o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new)

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
die()  { echo "${RED}ERROR${NC} $*" >&2; exit 1; }
info() { echo "${BLUE}==${NC} $*"; }
ok()   { echo "${GREEN}OK${NC}   $*"; }
warn() { echo "${YELLOW}WARN${NC} $*"; }

# Checked at first remote use rather than at parse time, so `--help`-ish subcommands still
# work with no address set. Failing by name beats SSHing to a guess.
require_host() {
  [[ -n "$BUILD_HOST" ]] || die "no build host set -- export RAK_BUILD_HOST=ntableman@<address>, or put that address alone on the first line of ~/.rak-build-host"
}

# Run a command on the build host under a login shell so PATH is correct.
rsh() { require_host; ssh "${SSH_OPTS[@]}" "$BUILD_HOST" "zsh -l -c $(printf '%q' "$1")"; }

# Run a command inside the remote repo.
rrepo() { rsh "cd $REMOTE_REPO && $1"; }

# Say which address was tried and how to change it. The build host is a laptop; an SSH
# failure is usually about which network it is on, not about the host being broken.
unreachable() {
  echo "${RED}ERROR${NC} cannot reach the build host at '${BUILD_HOST}'." >&2
  echo "       It is a laptop and may be on a different network now. Ask for the" >&2
  echo "       current address -- it is not recorded in this public repo -- then:" >&2
  echo "         export RAK_BUILD_HOST=ntableman@<address>" >&2
  echo "       (BatchMode is on, so a host that wants a password also lands here.)" >&2
  exit 1
}

cmd_check() {
  info "Build host: $BUILD_HOST_NAME ($BUILD_HOST)"
  echo "${DIM}   override with RAK_BUILD_HOST=user@address (BUILD_HOST also honored)${NC}"
  ssh "${SSH_OPTS[@]}" -o BatchMode=yes "$BUILD_HOST" true 2>/dev/null || unreachable
  rsh 'echo "host: $(scutil --get ComputerName 2>/dev/null || hostname)"' || unreachable
  echo "${DIM}-- toolchain --${NC}"
  rsh 'for t in pio git gh python3; do
         p=$(command -v $t 2>/dev/null || echo MISSING)
         printf "  %-8s %s\n" "$t" "$p"
       done
       printf "  %-8s %s\n" "pio-ver" "$(pio --version 2>/dev/null || echo n/a)"'
  echo "${DIM}-- repo --${NC}"
  rrepo 'echo "  path: $(pwd)"; echo "  head: $(git rev-parse --short HEAD) $(git log -1 --format=%s)"' \
    || die "repo missing on build host. Clone it to $REMOTE_REPO first."
}

cmd_sha() { rrepo 'git rev-parse HEAD'; }

cmd_devices() {
  info "USB serial devices on $BUILD_HOST_NAME"
  rsh 'ls /dev/cu.* 2>/dev/null || echo "  none"'
  echo "${DIM}-- hardware IDs --${NC}"
  rsh 'pio device list --serial 2>/dev/null || echo "  pio device list unavailable"'
  echo "${DIM}   239A:8029 app · 239A:0029/002A bootloader, no app (docs/FIRST_FLASH.md)${NC}"
}

# The RAK4631's USB product ID is the only thing that separates a board running an
# application from one sitting in DFU with nothing to run -- both print the same
# reassuring nothing on a serial capture. Prints the 4-hex PID, or nothing when no RAK is
# on the bus. Enumeration only: this never opens the port. Table: docs/FIRST_FLASH.md.
#
# 239A is Adafruit's vendor ID, not RAK's -- it is shared by any Adafruit board or
# Adafruit-bootloader device, including a second WisBlock. Picking "the first 239A match"
# is a silent guess that lands on the evidence path (issue #29): a fresh board could
# enumerate ahead of the RAK4631 and this would confidently report the wrong PID. Pin
# against a known serial (arg 1, or $RAK_SERIAL) to disambiguate; unpinned is only safe
# when exactly one 239A device is on the bus, and this refuses -- rather than guesses --
# when more than one is present and no serial was given.
cmd_usbpid() {
  local pin="${1:-${RAK_SERIAL:-}}"
  local out
  out=$(rsh 'pio device list --serial 2>/dev/null' || true)

  if [[ -n "$pin" ]]; then
    printf '%s\n' "$out" \
      | grep -i "SER=${pin}" \
      | grep -oE 'VID:PID=239A:[0-9A-Fa-f]{4}' \
      | head -1 | cut -d: -f3 | tr '[:lower:]' '[:upper:]' || true
    return
  fi

  local matches
  matches=$(printf '%s\n' "$out" | grep -oE 'VID:PID=239A:[0-9A-Fa-f]{4}')
  local count
  count=$(printf '%s\n' "$matches" | grep -c . || true)
  if [[ "$count" -gt 1 ]]; then
    warn "usbpid: ${count} devices matching Adafruit VID 239A on the bus; refusing to guess" >&2
    warn "usbpid: pin one -- scripts/remote.sh usbpid <serial>, or export RAK_SERIAL=<serial>" >&2
    return 1
  fi
  printf '%s\n' "$matches" | head -1 | cut -d: -f3 | tr '[:lower:]' '[:upper:]' || true
}

# Git is the ONLY transport between machines. Never scp/rsync source across --
# that creates untracked divergence nobody can reproduce later.
cmd_sync() {
  info "Syncing workstation -> $BUILD_HOST_NAME"

  if [[ -n "$(git status --porcelain)" ]]; then
    git status --short
    die "local tree is dirty. Commit or stash first -- uncommitted work does not exist on the build host."
  fi

  local branch local_sha
  branch=$(git rev-parse --abbrev-ref HEAD)
  local_sha=$(git rev-parse HEAD)
  echo "${DIM}   local  ${branch} @ ${local_sha:0:7}${NC}"

  # Relayed through the build host rather than pushed straight to GitHub. This
  # workstation's git credentials belong to the work account, which has no write access to
  # the personal repo, so a direct push returns HTTP 403. push.sh sends the branch to the
  # build host over SSH and lets it do the GitHub push with its own credentials.
  # Background: docs/ENVIRONMENTS.md, "Two GitHub identities".
  info "Pushing $branch via the build host relay"
  "$(dirname "${BASH_SOURCE[0]}")/push.sh" >/dev/null \
    || die "relay push failed -- run scripts/push.sh directly to see why."

  # The user sometimes works directly on the build host or a laptop. Never clobber
  # work that exists only over there. `|| true` on the status call would turn an
  # unreachable host into "tree is clean", which is the same class of bug as issue #27 --
  # a check that cannot run must not read as a check that passed.
  local remote_status rc=0
  remote_status=$(rrepo 'git status --porcelain') || rc=$?
  [[ "$rc" -eq 0 ]] || die "could not read the build host's git status (exit ${rc}). Refusing to sync blind."
  if [[ -n "$remote_status" ]]; then
    printf '%s\n' "$remote_status" | sed 's/^/       /'
    die "build host tree is dirty. Someone was working there -- resolve it by hand, do not discard it."
  fi

  info "Fast-forwarding build host"
  rrepo "git fetch origin && git checkout $branch && git pull --ff-only origin $branch" \
    || die "remote fast-forward failed. The build host may have diverging commits -- inspect, do not force."

  local remote_sha
  remote_sha=$(cmd_sha)
  if [[ "$remote_sha" != "$local_sha" ]]; then
    die "SHA mismatch after sync: local ${local_sha:0:7} vs remote ${remote_sha:0:7}"
  fi
  ok "both hosts at ${local_sha:0:7} on $branch"
}

cmd_run() {
  [[ $# -ge 1 ]] || die "usage: remote.sh run '<command>'"
  info "Running on $BUILD_HOST_NAME: $*"
  rrepo "$*"
}

case "${1:-}" in
  check)   shift; cmd_check "$@" ;;
  sync)    shift; cmd_sync "$@" ;;
  run)     shift; cmd_run "$@" ;;
  sha)     shift; cmd_sha "$@" ;;
  devices) shift; cmd_devices "$@" ;;
  usbpid)  shift; cmd_usbpid "$@" ;;
  *) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
