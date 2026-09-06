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

# `pio run -t upload` exits 0 even when the DFU tool fails (issue #27), so an upload run
# through here would report BUILD OK on a board it just bricked. flash.sh is the only path
# allowed to upload -- it scans the DFU output and re-checks the USB product ID afterwards.
for arg in "$@"; do
  case "$arg" in
    upload|*=upload)
      echo "${YELLOW}build.sh does not upload.${NC} Use scripts/flash.sh --" \
           "it verifies the flash actually landed (issue #27)." >&2
      exit 2 ;;
  esac
done

echo "${BLUE}== 1/3 preflight ==${NC}"
scripts/preflight.sh

echo
echo "${BLUE}== 2/3 sync ==${NC}"
scripts/remote.sh sync

echo
echo "${BLUE}== 3/3 compile ==${NC}"
SHA=$(git rev-parse HEAD)

if [[ ! -f platformio.ini ]]; then
  echo "${YELLOW}SKIP${NC} no platformio.ini yet -- no firmware in-tree."
  echo "=== BUILD SKIPPED (no firmware) ==="
  exit 0
fi

# The off-target tests build the same sources on the host, so they catch anything the
# firmware picks up from the Arduino core without asking for it. The build directory is
# removed first: a stale object survived a missing include once and let the build host pass
# while CI failed on the same commit, which is the worst possible split — the machine that
# says yes is the one nobody re-checks.
#
# The suites were removed in 5a9d584. `pio test` against an empty tree is not a pass, so the
# absence is announced rather than skipped quietly: a gate that vanishes without saying so is
# indistinguishable from one that ran.
if [[ -d test ]]; then
  scripts/remote.sh run "rm -rf .pio/build/native && pio test -e native" \
    || { echo; echo "=== TESTS FAILED ==="; echo "commit: ${SHA}"; exit 1; }
else
  echo "${YELLOW}SKIP${NC} no test/ directory -- the off-target suites were removed in 5a9d584."
  echo "${DIM}     payload.cpp, crc16.cpp and battery_frame.cpp now compile unverified.${NC}"
fi

echo

# Preflight skips this on the workstation, which has no node. The build host has both node
# and a live checkout of the decoder, so this is the machine where the encoder's real bytes
# can be pushed through the real formatter.
#
# tools/ went with the test suites in 5a9d584; the checker announces its own skip when the
# emitter and the expectation file are absent, so this call stays and reports it.
scripts/remote.sh run "python3 scripts/check_golden_vectors.py" \
  || { echo; echo "=== GOLDEN VECTORS FAILED ==="; echo "commit: ${SHA}"; exit 1; }

echo

# Sentinels keep long runs greppable and let notify_on_output watch progress
# (.cursor/rules/00-agent-liveness.mdc).
if scripts/remote.sh run "pio run ${*:-}"; then
  echo
  echo "${GREEN}=== BUILD OK ===${NC}"
  echo "host:   Heliotrope Ridge (${BUILD_HOST:-${RAK_BUILD_HOST:-address unset}})"
  echo "commit: ${SHA}"
  echo "${DIM}Report the host and commit with any result -- a result without both is not evidence.${NC}"
else
  echo
  echo "=== BUILD FAILED ==="
  echo "commit: ${SHA}"
  exit 1
fi
