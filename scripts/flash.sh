#!/usr/bin/env bash
# Flash firmware to the RAK4631 over USB on the Heliotrope Ridge build host.
#
# The device is physically attached to the build host and nowhere else, so this can
# never run locally (.cursor/rules/10-environments.mdc).
#
# Flashing is DESTRUCTIVE. It overwrites whatever is on the node -- possibly
# known-good field firmware that took a hike to install. Confirmation is required.
#
# Usage:
#   scripts/flash.sh                 confirm interactively
#   scripts/flash.sh --yes           skip the prompt (CI / repeat bench cycles)
#   scripts/flash.sh --port /dev/cu.usbmodemXXXX

set -euo pipefail
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
die() { echo "${RED}ERROR${NC} $*" >&2; exit 1; }

ASSUME_YES=0
PORT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --yes|-y) ASSUME_YES=1; shift ;;
    --port)   PORT="${2:?--port needs a value}"; shift 2 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ -f platformio.ini ]] || die "no platformio.ini -- there is no firmware to flash yet (WP1 not started)."

echo "${BLUE}== build ==${NC}"
scripts/build.sh

SHA=$(git rev-parse HEAD)
SHORT=${SHA:0:7}

echo
echo "${BLUE}== target ==${NC}"
scripts/remote.sh devices

DETECTED=$(scripts/remote.sh run 'ls /dev/cu.usbmodem* 2>/dev/null | head -1' | tr -d '\r\n' || true)
[[ -n "$PORT" || -n "$DETECTED" ]] || die "no RAK4631 found on the build host USB. Plug it in, or pass --port.
      Hardware may simply not have arrived yet -- README status is 'not deployed'."

echo "${DIM}   port:   ${PORT:-${DETECTED} (auto)}${NC}"
echo "${DIM}   commit: ${SHORT}${NC}"

if [[ "$ASSUME_YES" -ne 1 ]]; then
  echo
  echo "${YELLOW}About to OVERWRITE the firmware on ${PORT}.${NC}"
  echo "${YELLOW}If this node is carrying known-good field firmware, that is lost.${NC}"
  read -r -p "Type the commit short SHA (${SHORT}) to proceed: " reply
  [[ "$reply" == "$SHORT" ]] || die "confirmation did not match. Nothing was flashed."
fi

echo
echo "${BLUE}== flash ==${NC}"
# The board definition sets use_1200bps_touch with wait_for_upload_port, so the node
# reboots into its bootloader and re-enumerates under a DIFFERENT port partway through
# (rakwireless/boards/rak4630.json). Pinning --upload-port to the pre-touch name can
# therefore point at a port that no longer exists, so we only pass it when the operator
# explicitly asked for one and otherwise let PlatformIO track the port across the reset.
if scripts/remote.sh run "pio run -t upload ${PORT:+--upload-port $PORT}"; then
  echo
  echo "${GREEN}=== FLASH OK ===${NC}"
  echo "host:   Heliotrope Ridge"
  echo "commit: ${SHA}"
  echo "port:   ${PORT}"
  echo
  echo "${YELLOW}Not done yet.${NC} Record the result in docs/EVIDENCE.md with the date,"
  echo "commit, host, and what was actually observed. Until then the status stays"
  echo "'NOT YET DEPLOYED' -- AGENTS.md forbids aspirational deployment claims."
else
  echo
  echo "=== FLASH FAILED ==="
  echo "commit: ${SHA}"
  echo "${DIM}The node may be in an indeterminate state. Verify before walking away.${NC}"
  exit 1
fi
