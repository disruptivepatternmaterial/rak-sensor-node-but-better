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

set -euo pipefail

BUILD_HOST="${BUILD_HOST:-ntableman@192.168.10.223}"
BUILD_HOST_NAME="${BUILD_HOST_NAME:-Heliotrope Ridge}"
REMOTE_REPO="${REMOTE_REPO:-\$HOME/Documents/GitHub/rak-sensor-node-but-better}"
SSH_OPTS=(-o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new)

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
die()  { echo "${RED}ERROR${NC} $*" >&2; exit 1; }
info() { echo "${BLUE}==${NC} $*"; }
ok()   { echo "${GREEN}OK${NC}   $*"; }
warn() { echo "${YELLOW}WARN${NC} $*"; }

# Run a command on the build host under a login shell so PATH is correct.
rsh() { ssh "${SSH_OPTS[@]}" "$BUILD_HOST" "zsh -l -c $(printf '%q' "$1")"; }

# Run a command inside the remote repo.
rrepo() { rsh "cd $REMOTE_REPO && $1"; }

cmd_check() {
  info "Build host: $BUILD_HOST_NAME ($BUILD_HOST)"
  rsh 'echo "host: $(scutil --get ComputerName 2>/dev/null || hostname)"' \
    || die "cannot reach the build host. On the VPN/LAN?"
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

  info "Pushing $branch"
  git push origin "$branch"

  # The user sometimes works directly on the build host or a laptop. Never clobber
  # work that exists only over there.
  if [[ -n "$(rrepo 'git status --porcelain' || true)" ]]; then
    rrepo 'git status --short' || true
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
  *) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
