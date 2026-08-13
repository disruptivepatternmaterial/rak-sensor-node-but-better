#!/usr/bin/env bash
# Downlink command matrix. Drives every downlink case in docs/FIRMWARE_SPEC.md 4 against
# the board on the build host, unattended, and records PASS / FAIL / TIMEOUT with the
# verbatim console line that decided it. Procedure: docs/DOWNLINK_MATRIX.md.
#
# Why it is a script and not a session: a Class A node only hears a downlink in the RX
# window after an uplink, so one case costs up to two reporting intervals. Two earlier
# attempts to drive this matrix by hand died when the SSH connection dropped and produced
# no result at all. This runs under nohup on the build host, survives SIGHUP, and writes
# to an append-only log with greppable sentinels so progress is polled, not watched.
#
# It never fabricates. A case whose expected line never appears is recorded TIMEOUT with
# an empty observation -- not a pass, and not a guess about why.
#
# CITE(spec): docs/FIRMWARE_SPEC.md 4 -- downlink commands on FPort 10, uplinks on FPort 2,
#             0x01 set-interval is opcode + big-endian u32 seconds, 0x03 is one byte,
#             interval bounds 900-86400 s. Every expected string below was read out of
#             src/radio.cpp and src/config.cpp, not recalled.
# CITE(policy): TTN Fair Use Policy [CIT-TTN-FUP] -- 10 downlinks per node per 24 h. The
#             matrix pushes 8, so a full run fits one day's allowance with room to retry.
# CITE(prior-art): scripts/soak.sh -- same nohup + sentinel + append-only log pattern.
#
# Usage (on the build host):
#   nohup scripts/downlink_matrix.sh run >>/tmp/downlink_matrix.log 2>&1 &
#   scripts/downlink_matrix.sh status      what has finished, is it alive
#   scripts/downlink_matrix.sh tail [n]    last n log lines
#
# Restartable: a case already recorded in the log is skipped, so re-running after a death
# resumes rather than repeating.

set -uo pipefail

APP="${APP:-my-app-tobi}"
DEV="${DEV:-puma-concolor-001}"
CONSOLE="${CONSOLE:-/tmp/stage3_cap.txt}"
LOG="${LOG:-/tmp/downlink_matrix.log}"
EVLOG="${EVLOG:-/tmp/downlink_matrix_events.log}"
LOCK="${LOCK:-/tmp/downlink_matrix.lock}"
CMD_PORT="${CMD_PORT:-10}"

# One case may wait two full reporting intervals plus slack. Case b lowers the interval
# from 1800 s to 900 s, which is why it runs first: every case after it costs half as long.
TIMEOUT_LONG="${TIMEOUT_LONG:-4200}"    # 2 x 1800 s + 600 s slack
TIMEOUT_SHORT="${TIMEOUT_SHORT:-2400}"  # 2 x  900 s + 600 s slack
HEARTBEAT_EVERY="${HEARTBEAT_EVERY:-60}"

ts() { date '+%Y-%m-%dT%H:%M:%S%z'; }
say() { printf '%s %s\n' "$(ts)" "$*" >>"$LOG"; }

# ---------------------------------------------------------------- console log helpers

console_size() { [ -f "$CONSOLE" ] && wc -c <"$CONSOLE" | tr -d ' ' || echo 0; }

# First line at or after byte offset $1 containing fixed string $2. Empty if absent.
console_find() {
  local off="$1" pat="$2"
  tail -c "+$((off + 1))" "$CONSOLE" 2>/dev/null | grep -F -m1 -- "$pat" || true
}

# ---------------------------------------------------------------- TTN

push() {
  local port="$1" hex="$2"
  ttn-lw-cli end-devices downlink push "$APP" "$DEV" \
    --f-port "$port" --frm-payload "$hex" 2>&1 | tail -20
}

queue_depth() {
  ttn-lw-cli end-devices downlink list "$APP" "$DEV" 2>/dev/null \
    | grep -c '"f_port"' || true
}

start_events() {
  # Distinguishes "TTN never sent it" from "we never processed it". Non-fatal if it fails:
  # the console log is the primary record, this is the corroborating one.
  if ! pgrep -f "ttn-lw-cli events .*$DEV" >/dev/null 2>&1; then
    nohup ttn-lw-cli events subscribe --application-id "$APP" --device-id "$DEV" \
      >>"$EVLOG" 2>&1 &
    sleep 5
    if ! pgrep -f "ttn-lw-cli events .*$DEV" >/dev/null 2>&1; then
      nohup ttn-lw-cli events --application-id "$APP" --device-id "$DEV" \
        >>"$EVLOG" 2>&1 &
      sleep 5
    fi
  fi
  if pgrep -f "ttn-lw-cli events .*$DEV" >/dev/null 2>&1; then
    say "events   : subscriber alive, logging to $EVLOG"
  else
    say "events   : WARNING subscriber would not start -- console log is the only record"
  fi
}

# ---------------------------------------------------------------- the wait

# wait_for <offset> <timeout_s> <pattern> -> prints the matching line, exit 0; empty + 1
wait_for() {
  local off="$1" limit="$2" pat="$3"
  local start now hit last_hb=0
  start=$(date +%s)
  while :; do
    hit="$(console_find "$off" "$pat")"
    if [ -n "$hit" ]; then printf '%s\n' "$hit"; return 0; fi
    now=$(date +%s)
    if [ $((now - start)) -ge "$limit" ]; then return 1; fi
    if [ $((now - last_hb)) -ge "$HEARTBEAT_EVERY" ]; then
      last_hb=$now
      say "heartbeat: waiting ${pat} -- $((now - start))s of ${limit}s, console $(console_size) B"
    fi
    sleep 5
  done
}

done_already() { grep -q "=== CASE $1 RESULT=" "$LOG" 2>/dev/null; }

# record <case> <result> <observation>
record() {
  say "case $1   : observation: ${3:-<none>}"
  say "=== CASE $1 RESULT=$2 ==="
}

# run_case <id> <port> <hex> <timeout> <pattern> <description>
run_case() {
  local id="$1" port="$2" hex="$3" limit="$4" pat="$5" desc="$6"
  if done_already "$id"; then say "case $id   : already recorded, skipping"; return 0; fi
  local off hit
  off="$(console_size)"
  say "=== CASE $id START === $desc"
  say "case $id   : push port=$port payload=$hex, console offset $off"
  say "case $id   : ttn push -> $(push "$port" "$hex" | tr '\n' '|')"
  if hit="$(wait_for "$off" "$limit" "$pat")"; then
    record "$id" PASS "$hit"
  else
    record "$id" TIMEOUT ""
  fi
}

# ---------------------------------------------------------------- cases

case_b() {  # valid 0x01 set-interval to 900 s. Runs first: halves every later wait.
  local id=b off hit hit2 hit3
  if done_already "$id"; then say "case $id   : already recorded, skipping"; return 0; fi
  off="$(console_size)"
  say "=== CASE $id START === valid 0x01 set-interval to 900 s"
  say "case $id   : push port=$CMD_PORT payload=0100000384, console offset $off"
  say "case $id   : ttn push -> $(push "$CMD_PORT" 0100000384 | tr '\n' '|')"
  if ! hit="$(wait_for "$off" "$TIMEOUT_LONG" 'set interval 900 s')"; then
    record "$id" TIMEOUT ""; return 0
  fi
  say "case $id   : accepted: $hit"
  hit2="$(console_find "$off" 'config  : interval now 900 s')"
  # The timing claim is the real acceptance: the next wait line must read 900 s.
  if hit3="$(wait_for "$off" "$TIMEOUT_SHORT" 'wait    : 900 s')"; then
    record "$id" PASS "$hit ;; ${hit2:-<no config line>} ;; $hit3"
  else
    record "$id" FAIL "$hit ;; ${hit2:-<no config line>} ;; no 900 s wait line observed"
  fi
}

case_a() {  # valid 0x03 request-status. Issue #54 wants the command observed on console.
  local id=a off hit hit2 up
  if done_already "$id"; then say "case $id   : already recorded, skipping"; return 0; fi
  off="$(console_size)"
  say "=== CASE $id START === valid 0x03 request-status"
  say "case $id   : ttn push -> $(push "$CMD_PORT" 03 | tr '\n' '|')"
  if ! hit="$(wait_for "$off" "$TIMEOUT_SHORT" 'downlink')"; then
    record "$id" TIMEOUT ""; return 0
  fi
  say "case $id   : first downlink line: $hit"
  hit2="$(console_find "$off" 'status requested')"
  if [ -n "$hit2" ]; then
    # The answering uplink is the next cycle's FPort 2 send, and it is part of the
    # acceptance, not colour. This branch used to record PASS unconditionally and paste
    # the literal string "<no uplink observed>" into the evidence field of a passing row --
    # a gate that reports green over a missing observation, which is the exact class of
    # defect this project has already been burned by. wait_for's exit status decides.
    if up="$(wait_for "$off" "$TIMEOUT_SHORT" 'sent 35 bytes on port 2')"; then
      record "$id" PASS "$hit2 ;; $up"
    else
      record "$id" FAIL "$hit2 ;; no answering uplink observed"
    fi
  else
    record "$id" FAIL "$hit"
  fi
}

case_g() {  # two queued at once -- expect one per RX window, not both in one.
  local id=g off hit1 off2 hit2
  if done_already "$id"; then say "case $id   : already recorded, skipping"; return 0; fi
  off="$(console_size)"
  say "=== CASE $id START === two 0x03 downlinks queued at once"
  say "case $id   : push 1 -> $(push "$CMD_PORT" 03 | tr '\n' '|')"
  say "case $id   : push 2 -> $(push "$CMD_PORT" 03 | tr '\n' '|')"
  say "case $id   : queue depth after both pushes: $(queue_depth)"
  if ! hit1="$(wait_for "$off" "$TIMEOUT_SHORT" 'status requested')"; then
    record "$id" TIMEOUT ""; return 0
  fi
  say "case $id   : first: $hit1"
  # Restart the search past the first hit so the second must be a separate line.
  off2=$(( $(console_size) ))
  if hit2="$(wait_for "$off2" "$TIMEOUT_SHORT" 'status requested')"; then
    record "$id" PASS "$hit1 ;; $hit2"
  else
    record "$id" FAIL "$hit1 ;; second command never observed"
  fi
}

case_h() {  # the node survived all of it: monotonic cycles, no boot banner mid-run.
  local id=h off cycles banners first last bad
  if done_already "$id"; then say "case $id   : already recorded, skipping"; return 0; fi
  off="${RUN_START_OFFSET:-0}"
  say "=== CASE $id START === node survived the matrix, cycles monotonic, no reset"
  cycles="$(tail -c "+$((off + 1))" "$CONSOLE" 2>/dev/null \
            | grep -o '\[cycle [0-9]*\]' | grep -o '[0-9]*' | tr '\n' ' ')"
  banners="$(tail -c "+$((off + 1))" "$CONSOLE" 2>/dev/null \
             | grep -c '=== rak-sensor-node ===')"
  first="${cycles%% *}"; last="$(printf '%s' "$cycles" | awk '{print $NF}')"
  bad=0
  printf '%s' "$cycles" | awk '{p=0; for(i=1;i<=NF;i++){if($i<=p){exit 1} p=$i}}' || bad=1
  if [ "$banners" -eq 0 ] && [ "$bad" -eq 0 ] && [ -n "$first" ]; then
    record "$id" PASS "cycles $first..$last monotonic across the run, 0 boot banners: $cycles"
  else
    record "$id" FAIL "banners=$banners monotonic_violation=$bad cycles: $cycles"
  fi
}

# ---------------------------------------------------------------- subcommands

do_run() {
  mkdir "$LOCK" 2>/dev/null || { say "run      : another instance holds $LOCK, exiting"; exit 0; }
  trap 'rmdir "$LOCK" 2>/dev/null' EXIT

  say "=== MATRIX START === app=$APP dev=$DEV console=$CONSOLE pid=$$"
  if [ ! -f "$CONSOLE" ]; then
    say "run      : FATAL console log $CONSOLE does not exist -- no reader attached"
    say "=== MATRIX DONE ==="
    exit 1
  fi
  export RUN_START_OFFSET="$(console_size)"
  say "run      : console offset at start $RUN_START_OFFSET"
  start_events

  case_b
  case_a
  run_case c "$CMD_PORT" 010000   "$TIMEOUT_SHORT" 'opcode 0x01 with wrong length 3, ignored' \
    'wrong-length 0x01 (3 bytes) -- must not report unknown opcode (#64)'
  run_case d "$CMD_PORT" 03000000 "$TIMEOUT_SHORT" 'opcode 0x03 with wrong length 4, ignored' \
    'wrong-length 0x03 (4 bytes) -- must be rejected (#63)'
  run_case e "$CMD_PORT" 7F       "$TIMEOUT_SHORT" 'unknown opcode 0x7F, ignored' \
    'unknown opcode 0x7F -- ignored, not treated as an error'
  run_case f 1              03    "$TIMEOUT_SHORT" 'ignoring 1 bytes on port 1' \
    'valid 0x03 on the wrong FPort -- must be ignored by port'
  case_g
  case_h

  say "results  : $(grep -o '=== CASE . RESULT=[A-Z]* ===' "$LOG" | tr '\n' ' ')"
  say "=== MATRIX DONE ==="
}

do_status() {
  if pgrep -f 'downlink_matrix.sh run' >/dev/null 2>&1; then
    echo "alive: $(pgrep -f 'downlink_matrix.sh run' | tr '\n' ' ')"
  else
    echo "alive: no"
  fi
  grep -E '=== (CASE . RESULT=|MATRIX )' "$LOG" 2>/dev/null
  echo "last: $(tail -1 "$LOG" 2>/dev/null)"
}

case "${1:-run}" in
  run)    do_run ;;
  status) do_status ;;
  tail)   tail -"${2:-40}" "$LOG" ;;
  *)      echo "usage: $0 [run|status|tail [n]]" >&2; exit 2 ;;
esac
