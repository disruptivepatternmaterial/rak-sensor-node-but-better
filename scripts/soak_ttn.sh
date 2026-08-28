#!/usr/bin/env bash
# Network-side soak watcher. Observes the node for H8 by counting uplinks at TTN, with
# no USB attached to the board at all.
#
# Why not the serial harness (scripts/soak.sh):
#   The field image detaches USB ~180 s after boot when no host is attached
#   (docs/decisions/ADR-0008-console-in-the-field-image.md, issue #60). Keeping a serial
#   reader attached for 24 h suppresses that detach, so the run would soak a bench
#   variant of the image and report power behavior the shipped image does not have.
#   The frame counter at TTN is the only observation that costs the node nothing.
#
#   CITE(spec): docs/FIRMWARE_SPEC.md 7 H8 -- >=24 h bench soak before field trust
#   CITE(prior-art): scripts/_soak_monitor.py -- sentinel and heartbeat format reused here
#   CITE(policy): CIT-TTN-FUP, docs/CITATIONS.md -- the 900 s cadence this run expects is
#                 what keeps the node inside the 30 s/24 h uplink airtime budget
#   CITE(bench): docs/EVIDENCE.md 2026-08-13 -- dev_addr 260CE734 observed advancing at TTN
#
# Usage:  scripts/soak_ttn.sh <seconds|24h> [label]
# Env:    SOAK_DEV SOAK_APP SOAK_POLL SOAK_HEARTBEAT SOAK_EXPECT SOAK_OUT
#
# Sentinels in events.log, all greppable:
#   === SOAK TTN START ===     run header, carries the firmware version and banner SHA
#   === SOAK HEARTBEAT n ===   fixed interval, whether or not anything happened
#   SOAK UPLINK                frame counter advanced
#   SOAK ANOMALY               silence past the expected cadence, a counter that went backwards,
#                              a counter step this harness cannot account for as transmissions,
#                              or a single frame delivered far sooner than the cadence allows
#                              (cadence-fast -- delivery healthy, interval wrong, airtime burning)
#   SOAK NOTE  counter-step    a step of 2..kCounterMargin, which a reset explains without any
#                              extra transmission -- see the reserve discussion below
#   SOAK WARN                  the TTN query failed -- says nothing about the node
#   === SOAK TTN DONE ===      the run reached its full duration

set -uo pipefail

DEV="${SOAK_DEV:-puma-concolor-001}"
APP="${SOAK_APP:-my-app-tobi}"
POLL="${SOAK_POLL:-120}"
HEARTBEAT="${SOAK_HEARTBEAT:-300}"
EXPECT="${SOAK_EXPECT:-900}"          # the node's uplink cadence, seconds
SILENT_LIMIT=$(( EXPECT * 3 ))        # three missed cycles before crying wolf

# A counter step is not an uplink count. session.cpp:278 stores uplink_counter + kCounterMargin,
# so a reset resumes past anything that was actually sent and the counter arrives up to
# kCounterMargin higher having transmitted nothing. Observed 2026-08-13: steps of 23, 26 and 31-32
# every time the board was reset, each with a sub-cadence gap, and TTN's storage integration held
# exactly one message per step -- frames that were never sent cannot be stored. So a step in
# 2..MARGIN is reported as a reset, a step above MARGIN is the one shape this harness cannot
# explain away and is the airtime question worth waking somebody for.
#   CITE(prior-art): src/session.h:41 kCounterMargin = 32 -- the reserve size this mirrors
#   CITE(policy): CIT-TTN-FUP, docs/CITATIONS.md -- 30 s uplink airtime per node per 24 h is what
#                 a genuine burst would breach, which is why the two cases must not read alike
MARGIN="${SOAK_COUNTER_MARGIN:-32}"

# Silence was an anomaly and a counter burst was an anomaly, but transmitting TOO OFTEN was
# not: a plain +1 step arriving well inside the cadence was logged as a clean uplink and left
# anomalies=0. That is the exact shape of the failure this harness exists to catch, because
# the FUP budget is spent by frequency, not by count -- a node whose interval was set to 60 s
# by a mistaken downlink (#63's set-interval path) delivers perfectly and burns the 30 s/24 h
# airtime allowance ~15x faster, reading as the healthiest run we have ever recorded.
# Half the cadence, so a poll-granularity early arrival is not an anomaly.
#   CITE(policy): CIT-TTN-FUP, docs/CITATIONS.md -- 30 s uplink airtime per node per 24 h
#   CITE(bench): docs/EVIDENCE.md 2026-08-13 -- a downlink set the interval 1800 s -> 900 s and
#                it persisted across a reflash, so the interval is remotely mutable in the field
SHORT_GAP="${SOAK_SHORT_GAP:-$(( EXPECT / 2 ))}"

dur="${1:-24h}"
label="${2:-bench}"
case "$dur" in
  *h) SECS=$(( ${dur%h} * 3600 )) ;;
  *m) SECS=$(( ${dur%m} * 60 )) ;;
  *s) SECS="${dur%s}" ;;
  *)  SECS="$dur" ;;
esac

# Run artifacts live inside the repository, matching scripts/soak.sh (SOAK_ROOT=soak-runs)
# and the `soak-runs/` entry in .gitignore. They used to land in $HOME/soak-runs, which put
# project evidence outside the project where it read as junk during a tidy-up and was
# deleted -- taking a completed 24 h run's log with it (docs/EVIDENCE.md 2026-08-15).
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SOAK_ROOT="${SOAK_ROOT:-$REPO/soak-runs}"
OUT="${SOAK_OUT:-$SOAK_ROOT/$(date -u +%Y%m%dT%H%M%SZ)_ttn_${label}}"
mkdir -p "$OUT"
ln -sfn "$OUT" "$SOAK_ROOT/latest-ttn"
LOG="$OUT/events.log"
echo $$ > "$OUT/soak.pid"

utc() { date -u +%Y-%m-%dT%H:%M:%SZ; }
ev()  { echo "$(utc) $*" >> "$LOG"; }

# The board's own account of itself. A soak log that cannot name the image it soaked is
# not evidence (#73) -- so the banner SHA is captured up front from the flash capture if
# one exists, and recorded even when it does not.
BANNER_SHA="${SOAK_BANNER_SHA:-}"
if [[ -z "$BANNER_SHA" && -n "${SOAK_BANNER_LOG:-}" && -r "${SOAK_BANNER_LOG}" ]]; then
  BANNER_SHA=$(grep -m1 -Eo 'commit[[:space:]]*:[[:space:]]*[0-9a-f]{7,40}' "$SOAK_BANNER_LOG" \
                 | grep -Eo '[0-9a-f]{7,40}' || true)
fi
BANNER_FW="${SOAK_BANNER_FW:-}"
if [[ -z "$BANNER_FW" && -n "${SOAK_BANNER_LOG:-}" && -r "${SOAK_BANNER_LOG}" ]]; then
  BANNER_FW=$(grep -m1 -E 'firmware[[:space:]]*:' "$SOAK_BANNER_LOG" | awk -F: '{gsub(/ /,"",$2);print $2}' || true)
fi

# SOAK_FCNT_CMD exists so the accounting above can be exercised without a board, a network,
# or 24 h. The anomaly branches are the whole value of this harness and they are the hardest
# part to get right -- #76 shipped a "check" that could not fail because a +1 step always
# looked healthy. A gate for the gate.
fcnt() {
  if [[ -n "${SOAK_FCNT_CMD:-}" ]]; then
    eval "$SOAK_FCNT_CMD"
    return
  fi
  ttn-lw-cli end-devices get "$APP" "$DEV" --session.last-f-cnt-up 2>/dev/null \
    | grep -Eo '"last_f_cnt_up"[[:space:]]*:[[:space:]]*[0-9]+' | grep -Eo '[0-9]+$'
}

START=$(date +%s)
ev "=== SOAK TTN START === label=$label duration=${SECS}s device=$DEV app=$APP"
ev "    image      : firmware=${BANNER_FW:-UNKNOWN} banner_commit=${BANNER_SHA:-NOT OBSERVED}"
ev "    tree       : $(git -C "$(dirname "$0")/.." rev-parse --short HEAD 2>/dev/null || echo unknown)"
ev "    expectation: one uplink every ${EXPECT}s; silence past ${SILENT_LIMIT}s is an anomaly"

BASE=$(fcnt); LAST="${BASE:-}"
ev "    baseline   : last_f_cnt_up=${BASE:-QUERY FAILED}"
ev "    cadence    : a +1 step sooner than ${SHORT_GAP}s is an anomaly (half of ${EXPECT}s)"
LAST_MOVE=$START
# Is the gap about to be measured a real inter-uplink interval? Not always. The baseline is
# taken mid-cycle, and both the silence re-arm and the counter-reset branch move LAST_MOVE
# without an uplink having happened -- so the NEXT gap after any of those is an artifact of
# this harness, not of the node. Flagging it would manufacture anomalies during exactly the
# outage the operator is already looking at.
GAP_VALID=0
BEAT=0; UPLINKS=0; ANOM=0; FAILS=0; RESETS=0; FAST=0

while :; do
  now=$(date +%s); elapsed=$(( now - START ))
  [[ "$elapsed" -ge "$SECS" ]] && break

  c=$(fcnt)
  if [[ -z "$c" ]]; then
    FAILS=$(( FAILS + 1 ))
    ev "SOAK WARN ttn query failed (${FAILS} so far) -- no statement about the node"
  else
    if [[ -n "$LAST" && "$c" -gt "$LAST" ]]; then
      d=$(( c - LAST )); gap=$(( now - LAST_MOVE ))
      UPLINKS=$(( UPLINKS + 1 ))
      ev "SOAK UPLINK f_cnt=$c delta=$d gap=${gap}s total=$UPLINKS"
      if [[ "$d" -gt "$MARGIN" ]]; then
        ANOM=$(( ANOM + 1 ))
        ev "SOAK ANOMALY counter-burst +$d in ${gap}s exceeds the ${MARGIN}-frame reset reserve --"
        ev "    a reset cannot account for this; suspect transmissions and check the airtime budget"
      elif [[ "$d" -gt 1 ]]; then
        RESETS=$(( RESETS + 1 ))
        ev "SOAK NOTE  counter-step +$d in ${gap}s within the ${MARGIN}-frame reset reserve --"
        ev "    one uplink after a reset, not $d transmissions (session.cpp:278); resets=$RESETS"
      elif [[ "$GAP_VALID" -eq 1 && "$gap" -lt "$SHORT_GAP" ]]; then
        # A single frame, delivered cleanly, far too soon. Nothing else in this loop can see
        # it: the counter moved forward by one, which every other branch reads as health.
        FAST=$(( FAST + 1 ))
        ANOM=$(( ANOM + 1 ))
        ev "SOAK ANOMALY cadence-fast +1 after only ${gap}s, under the ${SHORT_GAP}s floor (cadence ${EXPECT}s) --"
        ev "    delivery is fine; the INTERVAL is wrong. At ${gap}s the FUP airtime budget is"
        ev "    spent ~$(( EXPECT / (gap > 0 ? gap : 1) ))x faster than planned; fast=$FAST"
      fi
      LAST_MOVE=$now
      GAP_VALID=1
    elif [[ -n "$LAST" && "$c" -lt "$LAST" ]]; then
      ANOM=$(( ANOM + 1 ))
      ev "SOAK ANOMALY counter-reset f_cnt went $LAST -> $c (rejoin or session reset)"
      LAST_MOVE=$now
      GAP_VALID=0
    fi
    LAST="$c"
  fi

  if [[ $(( now - LAST_MOVE )) -ge "$SILENT_LIMIT" ]]; then
    ANOM=$(( ANOM + 1 ))
    ev "SOAK ANOMALY silence $(( now - LAST_MOVE ))s with no counter advance (limit ${SILENT_LIMIT}s)"
    LAST_MOVE=$now   # re-arm, so one outage does not spam the log every poll
    GAP_VALID=0      # the re-arm is not an uplink, so the next gap measures nothing
  fi

  if [[ $(( elapsed / HEARTBEAT )) -gt "$BEAT" ]]; then
    BEAT=$(( elapsed / HEARTBEAT ))
    ev "=== SOAK HEARTBEAT $BEAT === elapsed=${elapsed}s of ${SECS}s uplinks=$UPLINKS f_cnt=${LAST:-none} resets=$RESETS anomalies=$ANOM query_failures=$FAILS"
  fi
  sleep "$POLL"
done

ev "=== SOAK TTN DONE === elapsed=$(( $(date +%s) - START ))s uplinks=$UPLINKS resets=$RESETS fast=$FAST anomalies=$ANOM query_failures=$FAILS f_cnt=${BASE:-?}->${LAST:-?}"
{
  echo "### Soak (network side) — $label"
  echo
  echo "- Device \`$DEV\` / app \`$APP\`, observed only at TTN — no USB attached, so the"
  echo "  180 s console detach and the sleep path are exactly the shipped image's."
  echo "- Image: firmware \`${BANNER_FW:-UNKNOWN}\`, banner commit \`${BANNER_SHA:-NOT OBSERVED}\`."
  echo "- Duration: $(( $(date +%s) - START )) s of ${SECS} s requested."
  echo "- Uplinks observed: $UPLINKS · frame counter ${BASE:-?} → ${LAST:-?}"
  echo "- Counter steps explained by a reset (≤ ${MARGIN}, the stored reserve): $RESETS"
  echo "- Uplinks arriving under the ${SHORT_GAP} s cadence floor: $FAST"
  echo "- Anomalies: $ANOM · TTN query failures: $FAILS"
  echo
  echo "This counts uplinks. It does not measure sleep current, and it cannot see a"
  echo "console line, so it proves delivery and cadence and nothing further."
  echo
  echo "A counter step is not an uplink count: \`session.cpp:278\` stores the counter plus"
  echo "\`kCounterMargin\` = ${MARGIN}, so a reset arrives up to ${MARGIN} frames higher having sent"
  echo "nothing. Steps in that range are counted as resets above, not as airtime."
} > "$OUT/summary.md"
