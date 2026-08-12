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

if [[ -n "$FAIL_REASON" && "$PID_SEEN" == "$PID_APP" ]]; then
  # The DFU tool errored, but the board is running an application. Those two facts
  # contradict each other and this script used to resolve the contradiction by trusting the
  # tool, which is the wrong way round: the board resets and re-enumerates the moment the
  # DFU write completes, so the serial link can disappear before nrfutil reads its final
  # acknowledgement. The tool then reports a transport error with every page already
  # written.
  #
  # A false FAILED is not harmless. It tells the next session the board is unprogrammed, so
  # the obvious next move is another flash cycle and another operator double-tap against
  # hardware that was already fine -- and it puts a wrong verdict next to a commit SHA in
  # the flashing narrative that docs/EVIDENCE.md depends on being accurate.
  #
  # So this is neither verdict. The PID proves an application is running but cannot prove
  # WHICH -- a previously resident image enumerates as 8029 too. Only a positive check of
  # what is actually executing can settle it, and that check needs the port, which this
  # script has just finished using and may be shared with another operator. It is therefore
  # named as the required next step rather than guessed at here.
  #
  # CITE(bench): docs/EVIDENCE.md 2026-08-03, commit 998dc26 -- adafruit-nrfutil raised
  #   PortNotOpenError / "Timed out waiting for acknowledgement", flash.sh printed FLASH
  #   FAILED, and the board then came up at 239A:8029 running the newly built busscan image.
  #   The capture showed a production-frame line that exists only in 2c13fac and later, so
  #   the flash had in fact succeeded.
  # CITE(prior-art): adafruit-nrfutil dfu_transport_serial.py [CIT-ADA-NRFUTIL] -- the ack
  #   read that produces this error happens after the last page is sent, which is why a
  #   transport error there says nothing about whether the write landed.
  echo
  echo "${YELLOW}=== FLASH INDETERMINATE ===${NC}"
  echo "commit: ${SHA}"
  echo "tool:   ${FAIL_REASON}"
  echo "usb:    239A:${PID_SEEN} ($(pid_meaning "$PID_SEEN"))"
  echo
  echo "The DFU tool reported an error, but the board came back as a running"
  echo "application. Both can be true: the board re-enumerates as soon as the write"
  echo "completes, which can drop the link before the tool reads its final ack. Refs #33."
  echo
  echo "${YELLOW}Do not re-flash on the strength of the tool error alone${NC} -- that is a"
  echo "wasted cycle and a wasted double-tap on hardware that is probably fine."
  echo
  echo "Resolve it positively before recording anything in docs/EVIDENCE.md. The PID"
  echo "says an application is running; it cannot say which one, because a previously"
  echo "resident image enumerates identically. Capture serial and match a string unique"
  echo "to ${SHA}:"
  echo "  ${DIM}scripts/remote.sh run \"pio device monitor -p ${PORT} --quiet\"${NC}"
  echo "Then check the banner's ${DIM}built :${NC} line against this build, or a log line"
  echo "that exists only in this commit. Until that matches, the verdict is unresolved --"
  echo "not a success, and not a failure either."
  exit 2
fi

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
  if [[ -z "$PID_SEEN" ]]; then
    # No 239A device of ANY kind. That is NOT the same as a bricked board, and reading it that
    # way wasted a session on 2026-08-12. A board with an invalid application stays in its
    # bootloader and enumerates as 0029/002A -- visibly. A board that is absent entirely is far
    # more likely to be running an application that has detached itself: src/power.cpp clears
    # USBPULLUP before sleep whenever nothing holds the console open, which removes the device
    # from the bus for the whole interval (issue #60). At a 3600 s interval the board is
    # unreachable for up to an hour at a time, and both flashing and capturing fail in exactly
    # this way while the firmware is perfectly healthy.
    echo
    echo "${YELLOW}=== NO USB DEVICE -- PROBABLY ASLEEP, NOT BRICKED ===${NC}"
    echo "commit: ${SHA}"
    echo "reason: ${FAIL_REASON:-no 239A device on the bus at all}"
    echo
    echo "A bricked board would still show its bootloader (239A:0029 or 002A). Nothing at all"
    echo "on the bus points at ${GREEN}an application that is running and has detached USB${NC}"
    echo "before sleeping -- see issue #60 and src/power.cpp."
    echo
    echo "Options, cheapest first:"
    echo "  1. ${GREEN}Wait for the next wake${NC} and flash inside the window. The node attaches"
    echo "     on wake and, from the #60 fix onward, stays attached 180 s after every boot."
    echo "  2. ${GREEN}Double-tap RESET on the RAK19007${NC} to enter DFU immediately."
    echo
    echo "${YELLOW}Do not conclude the board is dead from this message.${NC} Check again after"
    echo "one reporting interval before escalating. See docs/FIRST_FLASH.md."
    exit 1
  fi

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
  fi
  exit 1
fi
