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
#   scripts/flash.sh --env stage1    flash one of the staged bring-up images
#
# The --env flag exists for first bring-up (docs/FIRST_FLASH.md), where each stage adds a
# single subsystem so that a failure has exactly one candidate cause. Defaults to the full
# image.

set -euo pipefail
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
die() { echo "${RED}ERROR${NC} $*" >&2; exit 1; }

ASSUME_YES=0
PORT=""
ENV_NAME=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --yes|-y)  ASSUME_YES=1; shift ;;
    --port)    PORT="${2:?--port needs a value}"; shift 2 ;;
    --env|-e)  ENV_NAME="${2:?--env needs a value}"; shift 2 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[[ -f platformio.ini ]] || die "no platformio.ini -- there is no firmware to flash yet (WP1 not started)."

if [[ -n "$ENV_NAME" ]]; then
  grep -q "^\[env:${ENV_NAME}\]" platformio.ini \
    || die "no environment named '${ENV_NAME}' in platformio.ini.
      Available: $(grep -o '^\[env:[^]]*\]' platformio.ini | sed 's/\[env:\(.*\)\]/\1/' | tr '\n' ' ')"
fi

echo "${BLUE}== build ==${NC}"
scripts/build.sh ${ENV_NAME:+-e "$ENV_NAME"}

SHA=$(git rev-parse HEAD)
SHORT=${SHA:0:7}

echo
echo "${BLUE}== target ==${NC}"
scripts/remote.sh devices

DETECTED=$(scripts/remote.sh run 'ls /dev/cu.usbmodem* 2>/dev/null | head -1' | tr -d '\r\n' || true)
[[ -n "$PORT" || -n "$DETECTED" ]] || die "no RAK4631 found on the build host USB. Plug it in, or pass --port.
      Hardware may simply not have arrived yet -- README status is 'not deployed'."

echo "${DIM}   port:   ${PORT:-${DETECTED} (auto)}${NC}"
echo "${DIM}   env:    ${ENV_NAME:-rak4631 (full image)}${NC}"
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
#
# The exit status of that command is NOT trustworthy: `pio run -t upload` exits 0 and
# prints [SUCCESS] even when adafruit-nrfutil dies with a traceback, which on 2026-08-03
# produced a `=== FLASH OK ===` followed by a serial capture of a bricked board
# (issue #27, docs/EVIDENCE.md). So the result is decided by two independent checks
# below, and the exit status is only one input to the first of them.
#
# CITE(prior-art): adafruit-nrfutil dfu_transport_serial.py [CIT-ADA-NRFUTIL] -- get_ack_nr()
#   logs "Timed out waiting for acknowledgement from device." and raises "No data received
#   on serial port. Not able to proceed."; open() raises "Serial port could not be opened".
#   These are the tool's own strings, read from its source rather than from a log.
UPLOAD_FAIL_RE='Failed to upgrade target|Timed out waiting for acknowledgement|No data received on serial port|Serial port could not be opened|Traceback \(most recent call last\)'

UPLOAD_LOG=$(mktemp -t rak-flash)
trap 'rm -f "$UPLOAD_LOG"' EXIT

set +e
scripts/remote.sh run "pio run ${ENV_NAME:+-e $ENV_NAME} -t upload ${PORT:+--upload-port $PORT}" 2>&1 \
  | tee "$UPLOAD_LOG"
UPLOAD_RC=${PIPESTATUS[0]}
set -e

FAIL_REASON=""
[[ "$UPLOAD_RC" -ne 0 ]] && FAIL_REASON="pio exited ${UPLOAD_RC}"
if HIT=$(grep -m1 -E "$UPLOAD_FAIL_RE" "$UPLOAD_LOG"); then
  FAIL_REASON="${FAIL_REASON:+${FAIL_REASON}; }DFU tool reported: $(echo "$HIT" | sed 's/^[[:space:]]*//')"
fi

# ---------------------------------------------------------------- did it actually land?
# A DFU that reports success and a DFU that leaves the board in its bootloader look
# identical on stdout. The USB product ID does not: an application enumerates as 8029,
# a bootloader as 0029 or 002A. Full table in docs/FIRST_FLASH.md.
#
# CITE(datasheet): RAK-nRF52-Arduino boards.txt [CIT-RAK-BOARDS-TXT] --
#   WisCoreRAK4631Board.build.pid=0x8029 is the application PID; 0x0029 / 0x002A / 0x802A
#   are the other IDs the board can present, all under VID 0x239A.
# CITE(prior-art): Adafruit_nRF52_Bootloader board.h [CIT-ADA-BOOTLOADER] --
#   USB_DESC_UF2_PID 0x0029 (UF2 bootloader), USB_DESC_CDC_ONLY_PID 0x002A (serial DFU).
# CITE(bench): docs/EVIDENCE.md 2026-08-03 -- a board that reported FLASH OK enumerated as
#   239A:0029 "WisBlock RAK4631" and produced a 0-byte capture.
PID_APP="8029"
SETTLE_S=30      # bounded: a poll with no ceiling is how a session stalls silently
POLL_S=3         # (.cursor/rules/00-agent-liveness.mdc)

pid_meaning() {
  case "${1:-}" in
    8029) echo "application running" ;;
    802A) echo "application running (CDC-only descriptor)" ;;
    0029) echo "UF2 bootloader -- NO valid application" ;;
    002A) echo "serial-only DFU bootloader -- NO valid application" ;;
    "")   echo "no 239A device on the bus at all" ;;
    *)    echo "unrecognised product ID" ;;
  esac
}

echo
echo "${DIM}   waiting up to ${SETTLE_S}s for the board to re-enumerate...${NC}"
PID_SEEN=""
for (( waited = 0; waited < SETTLE_S; waited += POLL_S )); do
  PID_SEEN=$(scripts/remote.sh usbpid || true)
  [[ "$PID_SEEN" == "$PID_APP" ]] && break
  sleep "$POLL_S"
done
echo "${DIM}   USB 239A:${PID_SEEN:-????} -- $(pid_meaning "$PID_SEEN")${NC}"

if [[ -z "$FAIL_REASON" && "$PID_SEEN" == "$PID_APP" ]]; then
  echo
  echo "${GREEN}=== FLASH OK ===${NC}"
  echo "host:   Heliotrope Ridge"
  echo "commit: ${SHA}"
  echo "port:   ${PORT}"
  echo "usb:    239A:${PID_SEEN} ($(pid_meaning "$PID_SEEN"))"
  echo
  echo "${YELLOW}Not done yet.${NC} Record the result in docs/EVIDENCE.md with the date,"
  echo "commit, host, and what was actually observed. Until then the status stays"
  echo "'NOT YET DEPLOYED' -- AGENTS.md forbids aspirational deployment claims."
else
  echo
  echo "${RED}=== FLASH FAILED ===${NC}"
  echo "commit: ${SHA}"
  echo "reason: ${FAIL_REASON:-board did not come back as an application}"
  echo "usb:    239A:${PID_SEEN:-none} ($(pid_meaning "$PID_SEEN"))"
  if [[ "$PID_SEEN" != "$PID_APP" ]]; then
    echo
    echo "${RED}THE BOARD HAS NO VALID APPLICATION ON IT. NOTHING IS RUNNING.${NC}"
    echo "${YELLOW}Do not capture serial from it, and do not read anything into a capture"
    echo "you already took. An empty log from a board with no firmware is identical to an"
    echo "empty log from a sensor that is silent or miswired -- and the obvious reading of"
    echo "it sends the next session chasing RS-485 polarity on hardware that was never"
    echo "running any code. A capture taken now is not evidence (docs/EVIDENCE.md).${NC}"
    echo
    echo "Recovery: ${GREEN}double-tap RESET on the RAK19007${NC} to re-enter DFU cleanly,"
    echo "then re-run this script. See docs/FIRST_FLASH.md."
  else
    echo "${DIM}The node may be in an indeterminate state. Verify before walking away.${NC}"
  fi
  exit 1
fi
