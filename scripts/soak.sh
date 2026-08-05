#!/usr/bin/env bash
# Instrumented soak harness. Runs the node unattended and records the evidence a soak
# is the only opportunity to collect. Procedure and pass/fail: docs/SOAK.md.
#
# The soak is a measurement, not a vigil. FIRMWARE_SPEC.md 7 H8 asks for >=24 h on the
# bench and >=7 d of field shadow; running that span without instrumenting it proves
# only that nothing caught fire. This captures serial across every sleep, cross-checks
# the frame counter at TTN, flags anomalies by name, and writes a summary that pastes
# into docs/EVIDENCE.md.
#
# Usage:
#   scripts/soak.sh start [24h|30m|600s] [--label X] [--no-ttn]   launch on the build host
#   scripts/soak.sh status                                         heartbeat + is it alive
#   scripts/soak.sh tail [n]                                       last n event lines
#   scripts/soak.sh stop                                           end the run early
#   scripts/soak.sh summary                                        print summary.md
#   scripts/soak.sh fetch <dir>                                    copy a run's logs here
#   scripts/soak.sh selftest [seconds]                             prove the harness, no board
#
# Add --local to run on the machine you are typing on (that is how it runs once it has
# been dispatched to the build host). Without it, every subcommand relays over SSH,
# because the only machine with the RAK4631 on USB is Heliotrope Ridge.

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD_HOST="${BUILD_HOST:-ntableman@192.168.10.223}"
REMOTE_REPO="${REMOTE_REPO:-\$HOME/Documents/GitHub/rak-sensor-node-but-better}"
SSH_OPTS=(-o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new)
SOAK_ROOT="${SOAK_ROOT:-soak-runs}"
LATEST="$SOAK_ROOT/latest"

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
die()  { echo "${RED}ERROR${NC} $*" >&2; exit 1; }
info() { echo "${BLUE}==${NC} $*"; }
ok()   { echo "${GREEN}OK${NC}   $*"; }
warn() { echo "${YELLOW}WARN${NC} $*"; }

LOCAL=0
ARGS=()
for a in "$@"; do
  case "$a" in
    --local) LOCAL=1 ;;
    *) ARGS+=("$a") ;;
  esac
done
set -- ${ARGS+"${ARGS[@]}"}

# Login shell or `pio`, `gh` and `ttn-lw-cli` all report "command not found" over SSH --
# non-interactive ssh does not source ~/.zprofile, so /opt/homebrew/bin is off PATH.
rsh()   { ssh "${SSH_OPTS[@]}" "$BUILD_HOST" "zsh -l -c $(printf '%q' "$1")"; }
rrepo() { rsh "cd $REMOTE_REPO && $1"; }

# Run a subcommand here, or relay it to the build host.
here_or_there() {
  local sub="$1"; shift
  if [[ "$LOCAL" -eq 1 ]]; then
    return 1  # caller handles it locally
  fi
  rrepo "scripts/soak.sh $sub $* --local"
  exit $?
}

# 24h / 30m / 600s / bare seconds -> seconds. GNU `timeout` does not exist on this
# macOS build host, so the duration is enforced by the monitor itself.
to_seconds() {
  local d="${1:-24h}"
  case "$d" in
    *h) echo $(( ${d%h} * 3600 )) ;;
    *m) echo $(( ${d%m} * 60 )) ;;
    *s) echo "${d%s}" ;;
    *[!0-9]*) die "unrecognized duration '$d' -- use 24h, 30m, 600s" ;;
    *) echo "$d" ;;
  esac
}

# --------------------------------------------------------------------------- start
cmd_start() {
  local dur="24h" label="bench" extra=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --label) label="$2"; shift 2 ;;
      --no-ttn) extra="$extra --no-ttn"; shift ;;
      --heartbeat) extra="$extra --heartbeat $2"; shift 2 ;;
      --ttn-interval) extra="$extra --ttn-interval $2"; shift 2 ;;
      *) dur="$1"; shift ;;
    esac
  done
  local secs; secs=$(to_seconds "$dur")

  if [[ "$LOCAL" -ne 1 ]]; then
    info "Dispatching a ${secs}s soak to the build host"
    scripts/remote.sh sync >/dev/null || die "sync failed -- the build host must be at this commit"
    # nohup + setsid-equivalent detachment: the run has to outlive the operator's SSH
    # session. A 24 h soak tied to a terminal is a 24 h soak that ends at lunch.
    rrepo "nohup scripts/soak.sh start ${secs}s --label $(printf '%q' "$label") ${extra} --local \
             >/dev/null 2>&1 & echo launched pid \$!"
    sleep 3
    cmd_status
    echo "${DIM}   scripts/soak.sh status | tail | stop | summary${NC}"
    return 0
  fi

  command -v python3 >/dev/null || die "python3 missing"
  python3 -c 'import serial' 2>/dev/null || die "pyserial missing: pip3 install pyserial"

  local stamp dir commit host
  stamp=$(date -u +%Y%m%dT%H%M%SZ)
  dir="$SOAK_ROOT/${stamp}_${label}"
  commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
  host=$(scutil --get ComputerName 2>/dev/null || hostname)
  mkdir -p "$dir"
  ln -sfn "$(basename "$dir")" "$LATEST"

  info "soak $secs s -> $dir (commit $commit on $host)"
  # Heartbeat every 5 min by default so a stalled run is visible inside the window the
  # agent liveness rule allows (.cursor/rules/00-agent-liveness.mdc).
  python3 scripts/_soak_monitor.py \
      --seconds "$secs" --outdir "$dir" --label "$label" \
      --commit "$commit" --host "$host" $extra 2>&1 | tee -a "$dir/monitor.log"
}

# --------------------------------------------------------------------------- status
cmd_status() {
  here_or_there status || true
  local dir="$LATEST"
  [[ -e "$dir" ]] || die "no soak run under $SOAK_ROOT/"
  local pid alive="no"
  pid=$(cat "$dir/soak.pid" 2>/dev/null || echo "")
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null && alive="yes"
  echo "run    : $(readlink "$dir" 2>/dev/null || echo "$dir")"
  echo "pid    : ${pid:-none}  running: $alive"
  echo "${DIM}-- last heartbeat --${NC}"
  grep 'SOAK HEARTBEAT' "$dir/events.log" 2>/dev/null | tail -1 || echo "  none yet"
  echo "${DIM}-- anomalies --${NC}"
  grep -c 'ANOMALY' "$dir/events.log" 2>/dev/null || echo 0
}

cmd_tail() {
  local n="${1:-40}"
  here_or_there tail "$n" || true
  tail -n "$n" "$LATEST/events.log" 2>/dev/null || die "no events.log yet"
}

cmd_stop() {
  here_or_there stop || true
  local pid; pid=$(cat "$LATEST/soak.pid" 2>/dev/null || echo "")
  [[ -n "$pid" ]] || die "no pid recorded"
  # SIGTERM, not SIGKILL: the monitor traps it, takes a final TTN sample, and writes
  # the summary. Killing it hard throws away the run's conclusion.
  kill "$pid" 2>/dev/null && ok "sent SIGTERM to $pid -- summary is being written" \
    || warn "pid $pid not running"
}

cmd_summary() {
  here_or_there summary || true
  cat "$LATEST/summary.md" 2>/dev/null || die "no summary yet -- the run has not finished"
}

# Bring a finished run's logs back to the workstation for the EVIDENCE.md entry.
cmd_fetch() {
  local run="${1:-latest}"
  mkdir -p "$SOAK_ROOT"
  info "fetching $run from the build host"
  # scp is legitimate here: these are generated artifacts, not source. Git stays the
  # only transport for anything tracked (.cursor/rules/10-environments.mdc).
  local remote_dir
  remote_dir=$(rrepo "readlink -f $SOAK_ROOT/$run" 2>/dev/null | tr -d '\r')
  [[ -n "$remote_dir" ]] || die "no such run on the build host: $run"
  scp "${SSH_OPTS[@]}" -q -r "$BUILD_HOST:$remote_dir" "$SOAK_ROOT/" \
    && ok "$SOAK_ROOT/$(basename "$remote_dir")"
}

# --------------------------------------------------------------------------- selftest
# Proves the harness without the board: a synthetic node that prints real firmware log
# lines and takes its USB device away between cycles, the way a sleeping RAK4631 does.
# This is what makes the reattach path testable at all -- with real hardware you cannot
# ask the node to disappear on cue.
cmd_selftest() {
  local secs="${1:-120}"
  if [[ "$LOCAL" -ne 1 ]]; then
    info "selftest runs wherever you are; add --local to skip the relay"
  fi
  command -v python3 >/dev/null || die "python3 missing"
  python3 -c 'import serial' 2>/dev/null || die "pyserial missing: pip3 install pyserial"

  local dir="$SOAK_ROOT/selftest"
  rm -rf "$dir"; mkdir -p "$dir/ports"
  info "synthetic node for ${secs}s, ports under $dir/ports"

  python3 scripts/_soak_fakenode.py --portdir "$dir/ports" --seconds "$((secs + 5))" \
      >"$dir/fakenode.log" 2>&1 &
  local fake=$!
  sleep 1

  python3 scripts/_soak_monitor.py --seconds "$secs" --outdir "$dir" \
      --port-glob "$dir/ports/cu.usbmodem*" --heartbeat 20 --no-ttn \
      --label selftest --commit "$(git rev-parse --short HEAD 2>/dev/null || echo unknown)" \
      2>&1 | tee "$dir/monitor.log"
  kill "$fake" 2>/dev/null || true

  echo
  local beats reattach cycles
  beats=$(grep -c 'SOAK HEARTBEAT' "$dir/events.log" || true)
  reattach=$(grep -c 'attach #' "$dir/events.log" || true)
  cycles=$(python3 -c "import json;print(json.load(open('$dir/summary.json'))['cycles_seen'])" 2>/dev/null || echo 0)
  echo "heartbeats: $beats   port attaches: $reattach   cycles parsed: $cycles"
  if [[ "$beats" -ge 1 && "$reattach" -ge 2 && "$cycles" -ge 2 ]]; then
    ok "=== SELFTEST OK === heartbeat fires, port reattaches across sleeps, cycles parse"
  else
    die "=== SELFTEST FAILED === see $dir/events.log"
  fi
}

case "${1:-}" in
  start)    shift; cmd_start "$@" ;;
  status)   shift; cmd_status "$@" ;;
  tail)     shift; cmd_tail "$@" ;;
  stop)     shift; cmd_stop "$@" ;;
  summary)  shift; cmd_summary "$@" ;;
  fetch)    shift; cmd_fetch "$@" ;;
  selftest) shift; cmd_selftest "$@" ;;
  *) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
