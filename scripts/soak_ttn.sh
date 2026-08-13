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
#   SOAK ANOMALY               silence past the expected cadence, or a counter that went backwards
#   SOAK WARN                  the TTN query failed -- says nothing about the node
#   === SOAK TTN DONE ===      the run reached its full duration

set -uo pipefail

DEV="${SOAK_DEV:-puma-concolor-001}"
APP="${SOAK_APP:-my-app-tobi}"
POLL="${SOAK_POLL:-120}"
HEARTBEAT="${SOAK_HEARTBEAT:-300}"
EXPECT="${SOAK_EXPECT:-900}"          # the node's uplink cadence, seconds
SILENT_LIMIT=$(( EXPECT * 3 ))        # three missed cycles before crying wolf

dur="${1:-24h}"
label="${2:-bench}"
case "$dur" in
  *h) SECS=$(( ${dur%h} * 3600 )) ;;
  *m) SECS=$(( ${dur%m} * 60 )) ;;
  *s) SECS="${dur%s}" ;;
  *)  SECS="$dur" ;;
esac

OUT="${SOAK_OUT:-$HOME/soak-runs/$(date -u +%Y%m%dT%H%M%SZ)_ttn_${label}}"
mkdir -p "$OUT"
ln -sfn "$OUT" "$HOME/soak-runs/latest-ttn"
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

fcnt() {
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
LAST_MOVE=$START
BEAT=0; UPLINKS=0; ANOM=0; FAILS=0

while :; do
  now=$(date +%s); elapsed=$(( now - START ))
  [[ "$elapsed" -ge "$SECS" ]] && break

  c=$(fcnt)
  if [[ -z "$c" ]]; then
    FAILS=$(( FAILS + 1 ))
    ev "SOAK WARN ttn query failed (${FAILS} so far) -- no statement about the node"
  else
    if [[ -n "$LAST" && "$c" -gt "$LAST" ]]; then
      UPLINKS=$(( UPLINKS + 1 ))
      ev "SOAK UPLINK f_cnt=$c delta=$(( c - LAST )) gap=$(( now - LAST_MOVE ))s total=$UPLINKS"
      LAST_MOVE=$now
    elif [[ -n "$LAST" && "$c" -lt "$LAST" ]]; then
      ANOM=$(( ANOM + 1 ))
      ev "SOAK ANOMALY counter-reset f_cnt went $LAST -> $c (rejoin or session reset)"
      LAST_MOVE=$now
    fi
    LAST="$c"
  fi

  if [[ $(( now - LAST_MOVE )) -ge "$SILENT_LIMIT" ]]; then
    ANOM=$(( ANOM + 1 ))
    ev "SOAK ANOMALY silence $(( now - LAST_MOVE ))s with no counter advance (limit ${SILENT_LIMIT}s)"
    LAST_MOVE=$now   # re-arm, so one outage does not spam the log every poll
  fi

  if [[ $(( elapsed / HEARTBEAT )) -gt "$BEAT" ]]; then
    BEAT=$(( elapsed / HEARTBEAT ))
    ev "=== SOAK HEARTBEAT $BEAT === elapsed=${elapsed}s of ${SECS}s uplinks=$UPLINKS f_cnt=${LAST:-none} anomalies=$ANOM query_failures=$FAILS"
  fi
  sleep "$POLL"
done

ev "=== SOAK TTN DONE === elapsed=$(( $(date +%s) - START ))s uplinks=$UPLINKS anomalies=$ANOM query_failures=$FAILS f_cnt=${BASE:-?}->${LAST:-?}"
{
  echo "### Soak (network side) — $label"
  echo
  echo "- Device \`$DEV\` / app \`$APP\`, observed only at TTN — no USB attached, so the"
  echo "  180 s console detach and the sleep path are exactly the shipped image's."
  echo "- Image: firmware \`${BANNER_FW:-UNKNOWN}\`, banner commit \`${BANNER_SHA:-NOT OBSERVED}\`."
  echo "- Duration: $(( $(date +%s) - START )) s of ${SECS} s requested."
  echo "- Uplinks observed: $UPLINKS · frame counter ${BASE:-?} → ${LAST:-?}"
  echo "- Anomalies: $ANOM · TTN query failures: $FAILS"
  echo
  echo "This counts uplinks. It does not measure sleep current, and it cannot see a"
  echo "console line, so it proves delivery and cadence and nothing further."
} > "$OUT/summary.md"
