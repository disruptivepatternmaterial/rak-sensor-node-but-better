#!/usr/bin/env bash
# Compile the firmware on the Heliotrope Ridge build host.
#
# Runs the local gates first (including the TTN formatter parity check -- every build
# verifies the decoder is current and calls out when it must change), then syncs via
# git and compiles remotely. The workstation has no PlatformIO by design; see
# .cursor/rules/10-environments.mdc.
#
# Usage: scripts/build.sh [pio args...]      e.g. scripts/build.sh -e rak4631
#
# The environment is named rak4631 and the board id is rak4630 -- the board id comes from
# RAK's vendored definition in rakwireless/ and is not ours to rename.

set -euo pipefail
cd "$(dirname "$0")/.."

BLUE=$'\033[34m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; DIM=$'\033[2m'; NC=$'\033[0m'

echo "${BLUE}== 1/3 preflight ==${NC}"
scripts/preflight.sh

echo
echo "${BLUE}== 2/3 sync ==${NC}"
scripts/remote.sh sync

echo
echo "${BLUE}== 3/3 compile ==${NC}"
SHA=$(git rev-parse HEAD)

if [[ ! -f platformio.ini ]]; then
  echo "${YELLOW}SKIP${NC} no platformio.ini yet -- no firmware in-tree (WP1 not started)."
  echo "${DIM}     plans/P0_HARDENED_NODE.md WP1 creates the PlatformIO skeleton.${NC}"
  echo "=== BUILD SKIPPED (no firmware) ==="
  exit 0
fi

# The off-target tests build the same sources on the host, so they catch anything the
# firmware picks up from the Arduino core without asking for it. The build directory is
# removed first: a stale object survived a missing include once and let the build host pass
# while CI failed on the same commit, which is the worst possible split — the machine that
# says yes is the one nobody re-checks.
scripts/remote.sh run "rm -rf .pio/build/native && pio test -e native" \
  || { echo; echo "=== TESTS FAILED ==="; echo "commit: ${SHA}"; exit 1; }

echo

# Preflight skips this on the workstation, which has no node. The build host has both node
# and a live checkout of the decoder, so this is the machine where the encoder's real bytes
# can be pushed through the real formatter.
scripts/remote.sh run "python3 scripts/check_golden_vectors.py" \
  || { echo; echo "=== GOLDEN VECTORS FAILED ==="; echo "commit: ${SHA}"; exit 1; }

echo

# Sentinels keep long runs greppable and let notify_on_output watch progress
# (.cursor/rules/00-agent-liveness.mdc).
if scripts/remote.sh run "pio run ${*:-}"; then
  echo
  echo "${GREEN}=== BUILD OK ===${NC}"
  echo "host:   Heliotrope Ridge (${BUILD_HOST:-ntableman@192.168.10.223})"
  echo "commit: ${SHA}"
  echo "${DIM}Report the host and commit with any result -- a result without both is not evidence.${NC}"
else
  echo
  echo "=== BUILD FAILED ==="
  echo "commit: ${SHA}"
  exit 1
fi
