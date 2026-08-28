#!/usr/bin/env bash
# Register a node with The Things Network, and prove the key byte order is right before
# anything is flashed.
#
# Why this script exists at all. Registering a device is a handful of console clicks, so it
# never seemed worth automating -- but the failure mode it guards is the worst-behaved one in
# this project. SX126x-Arduino takes the DevEUI and JoinEUI most-significant-byte-first and
# reverses them itself when it builds the join request. A different and widely-copied Arduino
# LoRaWAN library wants them reversed, so reversed examples are the easy thing to find. Paste a
# reversed DevEUI and you have described a device that does not exist; an unrecognised join
# request is not answered and not logged, so the console shows no join attempt, no error and
# nothing to bisect, while the node transmits perfectly and forever. It cost a debugging
# session on 2026-07-31, and it is invisible from the network side by construction.
#
# So the useful thing to automate is not the clicking. It is the three checks in issue #23,
# which were a human checklist and are now machine-checkable:
#
#   verify  src/secrets.h DevEUI reads left-to-right identically to the TTN console
#   banner  the boot banner deveui line matches the console exactly
#   session the Network Server reports a session after the first uplink
#
# verify distinguishes "reversed" from "wrong device", which is the diagnosis the network
# cannot give you: byte-reversal is reported as byte-reversal, by name, with the 2026-07-31
# reference, instead of as a generic mismatch.
#
# CITE(prior-art): src/radio.cpp:91 and src/secrets.example.h -- SX126x-Arduino's own OTAA
#   example is emphatic ("OTAA keys !!!! KEYS ARE MSB !!!!"), and the firmware follows it
# CITE(spec): [CIT-LW-LINK] LoRaWAN 1.0.3 Link Layer, join-request DevEUI/JoinEUI fields --
#   an unrecognised DevEUI yields no join-accept and no obligation to report anything, which
#   is why this is undiagnosable from the console
# CITE(policy): [CIT-TTN-FREQ] US915 uses FSB2; the Identity Server is eu1 while the Network,
#   Application and Join Servers are nam1 -- getting this split wrong creates a device that
#   exists but never joins
# CITE(spec): IEEE EUI-64 -- bit 1 of the first byte marks a locally administered address,
#   which is what a DevEUI must be without a TTN-assigned block
#
# Usage:
#   scripts/register_device.sh selftest                 prove the reversal check can fail
#   scripts/register_device.sh gen                      generate MSB-order keys for a new node
#   scripts/register_device.sh create <device-id>       create it on TTN (needs ttn-lw-cli login)
#   scripts/register_device.sh verify <device-id>       TTN vs src/secrets.h, reversal-aware
#   scripts/register_device.sh check <deveui>           same check fully offline, no credentials
#   scripts/register_device.sh banner <capture.log>     boot banner vs src/secrets.h
#   scripts/register_device.sh session <device-id>      has it ever joined?
#
# Env: TTN_APP (default my-app-tobi), SECRETS (default src/secrets.h)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TTN_APP:-my-app-tobi}"
SECRETS="${SECRETS:-$ROOT/src/secrets.h}"

# US915. The eu1/nam1 split is not a typo -- see the CITE above.
FREQ_PLAN="US_902_928_FSB_2"
MAC_VER="MAC_V1_0_3"
PHY_VER="PHY_V1_0_3_REV_A"

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

die() { red "FAIL $*"; exit 1; }

# Lowercase hex digits only. Accepts the console's spaced form, a colon-separated form, or a C
# array, so a value can be pasted from wherever the operator happens to be looking.
norm_hex() { tr -d ' \t\n\r:,-' | tr 'A-F' 'a-f' | sed 's/0x//g'; }

# Checked before any command substitution that reads the header, not inside secrets_bytes.
# die() inside $(...) only exits the subshell, so the error text became the "hex" the caller
# then tried to parse -- which reported a missing file as "80 hex digits, expected 16". Caught
# by the missing-file test case below; the lesson is that a guard has to run somewhere its
# failure can actually stop the script.
require_secrets() {
  [[ -r "$SECRETS" ]] || die \
"no $SECRETS

     Copy src/secrets.example.h to src/secrets.h and fill it in from the TTN console, or
     point SECRETS= at the header to check. secrets.h is gitignored by design."
}

# Pull a #define'd byte array out of a C header and return it as flat lowercase hex.
# Handles backslash continuation, which OTAA_APPKEY uses.
secrets_bytes() {
  local macro="$1"
  awk -v m="$macro" '
    index($0, "#define " m) { collecting = 1 }
    collecting {
      buf = buf $0
      if ($0 !~ /\\[[:space:]]*$/) { print buf; exit }
    }
  ' "$SECRETS" \
    | grep -oE '0x[0-9A-Fa-f]{1,2}' \
    | sed 's/0x//' \
    | awk '{ if (length($0) == 1) printf "0%s", tolower($0); else printf "%s", tolower($0) }'
}

reverse_bytes() {
  local h="$1" out=""
  local i
  for (( i = ${#h} - 2; i >= 0; i -= 2 )); do out+="${h:i:2}"; done
  printf '%s' "$out"
}

pretty() { printf '%s' "$1" | sed 's/../& /g' | sed 's/ $//'; }

need_cli() {
  command -v ttn-lw-cli >/dev/null 2>&1 || die \
"ttn-lw-cli not found. This subcommand needs it, and needs it logged in to TTN.
     The credential is not in this repo and never will be. Run this on the machine that
     has 'ttn-lw-cli login' done, or use 'check' / 'banner', which need no credentials."
}

ttn_field() { # $1 = device id, $2 = field path, prints flat hex
  ttn-lw-cli end-devices get "$APP" "$1" "--$2" 2>/dev/null \
    | grep -oE "\"${2##*.}\"[[:space:]]*:[[:space:]]*\"[0-9A-Fa-f]+\"" \
    | grep -oE '"[0-9A-Fa-f]+"$' | tr -d '"' | tr 'A-F' 'a-f'
}

# The whole point of the script. Compares two DevEUIs and names byte-reversal as byte-reversal.
compare_deveui() { # $1 = source label, $2 = source hex, $3 = local label, $4 = local hex
  local src_label="$1" src="$2" loc_label="$3" loc="$4"

  [[ -n "$src" ]] || die "could not read a DevEUI from $src_label"
  [[ -n "$loc" ]] || die "could not read a DevEUI from $loc_label"

  if [[ ${#src} -ne 16 ]]; then
    die "$src_label DevEUI is ${#src} hex digits, expected 16 (8 bytes): $(pretty "$src")"
  fi
  if [[ ${#loc} -ne 16 ]]; then
    die "$loc_label DevEUI is ${#loc} hex digits, expected 16 (8 bytes): $(pretty "$loc")"
  fi

  echo "   $src_label : $(pretty "$src")"
  echo "   $loc_label : $(pretty "$loc")"

  if [[ "$src" == "$loc" ]]; then
    # Locally administered check. Not fatal: TTN will happily join a device whose DevEUI has
    # no local-admin bit, and a real assigned block would not set it. Worth saying, since this
    # application has no assigned block, so an unset bit here means the value came from
    # somewhere unexpected.
    local first=$(( 0x${src:0:2} ))
    if (( (first & 0x02) == 0 )); then
      echo "   note      : first byte 0x${src:0:2} has the locally-administered bit clear."
      echo "               Fine if this DevEUI came from an assigned EUI-64 block; suspicious"
      echo "               if it was generated, since 'gen' always sets it."
    fi
    green "PASS DevEUI matches, same order, no reversal"
    return 0
  fi

  if [[ "$src" == "$(reverse_bytes "$loc")" ]]; then
    red "FAIL DevEUI is BYTE-REVERSED — this is the 2026-07-31 mistake, issue #23"
    cat <<EOF

   The two values are the same eight bytes in opposite order, so this is not the wrong
   device. It is the byte order.

   SX126x-Arduino wants MSB-first and reverses the bytes itself when it builds the join
   request. Some widely-copied Arduino LoRaWAN examples want them reversed, which is where
   this comes from.

   Fix $loc_label to read left-to-right exactly as $src_label does:

       #define OTAA_DEVEUI {$(printf '0x%s, ' $(echo "$src" | sed 's/../& /g') | sed 's/, $//')}

   Left unfixed, this node will transmit join requests forever and the console will show no
   join attempt, no error, and nothing to bisect.
EOF
    return 2
  fi

  red "FAIL DevEUI mismatch, and not a reversal either — these are different devices"
  echo "   Reversing $loc_label would give $(pretty "$(reverse_bytes "$loc")"), which still does not match."
  echo "   Check that '$src_label' names the device you meant."
  return 3
}

cmd_gen() {
  command -v openssl >/dev/null 2>&1 || die "openssl not found; needed to generate keys"
  bold "== MSB-order keys for a new node =="
  echo
  # Locally administered EUI-64: set bit 1 of the first byte, clear the group bit. This
  # application has no TTN-assigned DevEUI block.
  local first rest deveui appkey
  first=$(printf '%02x' $(( ( (0x$(openssl rand -hex 1)) | 0x02 ) & 0xfe )))
  rest=$(openssl rand -hex 7)
  deveui="${first}${rest}"
  appkey=$(openssl rand -hex 16)

  echo "DevEUI  $(pretty "$deveui")   (locally administered, bit 1 of the first byte set)"
  echo "JoinEUI 0000000000000000        (TTN accepts all-zero when the app has no assigned JoinEUI)"
  echo
  bold "-- paste into src/secrets.h, left to right, reversing nothing --"
  echo
  echo "#define OTAA_DEVEUI {$(printf '0x%s, ' $(echo "$deveui" | sed 's/../& /g') | sed 's/, $//')}"
  echo "#define OTAA_APPEUI {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}"
  printf '#define OTAA_APPKEY {'
  printf '0x%s, ' $(echo "${appkey:0:16}" | sed 's/../& /g') | sed 's/, $//'
  printf ', \\\n                     '
  printf '0x%s, ' $(echo "${appkey:16:16}" | sed 's/../& /g') | sed 's/, $//'
  printf '}\n'
  echo
  red "The AppKey above is a secret and is shown once."
  echo "   src/secrets.h is gitignored. Do not paste it into an issue, a commit, or a log."
  echo
  echo "Next: scripts/register_device.sh create <device-id>"
  echo "      then  scripts/register_device.sh verify <device-id>  before flashing anything."
}

cmd_create() {
  local dev="${1:-}"
  [[ -n "$dev" ]] || die "usage: register_device.sh create <device-id>"
  need_cli
  require_secrets
  local deveui appkey
  deveui=$(secrets_bytes OTAA_DEVEUI)
  appkey=$(secrets_bytes OTAA_APPKEY)
  [[ ${#deveui} -eq 16 ]] || die "OTAA_DEVEUI in $SECRETS is ${#deveui} hex digits, expected 16"
  [[ ${#appkey} -eq 32 ]] || die "OTAA_APPKEY in $SECRETS is ${#appkey} hex digits, expected 32"

  bold "== creating $dev in $APP =="
  echo "   DevEUI      : $(pretty "$deveui")"
  echo "   frequency   : $FREQ_PLAN"
  echo "   versions    : $MAC_VER / $PHY_VER"
  echo "   servers     : identity eu1, network/application/join nam1"
  echo

  # Deliberately passing the same bytes that are in secrets.h rather than letting TTN generate
  # them: it removes the transcription step that the reversal bug lives in. verify still runs
  # afterwards, because "I passed the right bytes" and "the right bytes are stored" are
  # different claims.
  ttn-lw-cli end-devices create "$APP" "$dev" \
    --dev-eui "$deveui" \
    --join-eui "0000000000000000" \
    --root-keys.app-key.key "$appkey" \
    --frequency-plan-id "$FREQ_PLAN" \
    --lorawan-version "$MAC_VER" \
    --lorawan-phy-version "$PHY_VER" \
    --supports-join \
    || die "ttn-lw-cli end-devices create failed for $dev"

  green "created $dev"
  echo "   Now run: scripts/register_device.sh verify $dev"
}

cmd_verify() {
  local dev="${1:-}"
  [[ -n "$dev" ]] || die "usage: register_device.sh verify <device-id>"
  need_cli
  require_secrets
  bold "== $dev at TTN vs $SECRETS =="
  local remote local_h
  remote=$(ttn_field "$dev" "ids.dev_eui")
  local_h=$(secrets_bytes OTAA_DEVEUI)
  compare_deveui "TTN     " "$remote" "secrets " "$local_h"
}

cmd_check() {
  local pasted="${1:-}"
  [[ -n "$pasted" ]] || die "usage: register_device.sh check <deveui-from-console>"
  require_secrets
  bold "== pasted console value vs $SECRETS (offline) =="
  local remote local_h
  remote=$(printf '%s' "$pasted" | norm_hex)
  local_h=$(secrets_bytes OTAA_DEVEUI)
  compare_deveui "console " "$remote" "secrets " "$local_h"
}

cmd_banner() {
  local log="${1:-}"
  [[ -n "$log" && -r "$log" ]] || die "usage: register_device.sh banner <capture.log>"
  require_secrets
  bold "== boot banner in $log vs $SECRETS =="
  # The banner prints the DevEUI the firmware actually compiled in, which is the only value
  # that matters -- src/secrets.h could have been edited after the flash.
  local banner local_h
  banner=$(grep -iE 'deveui' "$log" | head -1 | grep -oE '[0-9A-Fa-f]{2}([: ]?[0-9A-Fa-f]{2}){7}' | head -1 | norm_hex)
  [[ -n "$banner" ]] || die "no deveui line found in $log — was FEATURE_CONSOLE on, and did the capture catch the boot?"
  local_h=$(secrets_bytes OTAA_DEVEUI)
  compare_deveui "banner  " "$banner" "secrets " "$local_h"
}

cmd_session() {
  local dev="${1:-}"
  [[ -n "$dev" ]] || die "usage: register_device.sh session <device-id>"
  need_cli
  bold "== has $dev ever joined? =="
  local addr fcnt
  addr=$(ttn-lw-cli end-devices get "$APP" "$dev" --session.dev-addr 2>/dev/null \
         | grep -oE '"dev_addr"[[:space:]]*:[[:space:]]*"[0-9A-Fa-f]+"' \
         | grep -oE '"[0-9A-Fa-f]+"$' | tr -d '"')
  if [[ -z "$addr" ]]; then
    red "FAIL no session — the Network Server has never accepted a join from $dev"
    echo "   This is what a reversed DevEUI looks like, and also what no gateway coverage"
    echo "   looks like. Run 'verify $dev' first: it separates the two, and the console"
    echo "   cannot."
    return 1
  fi
  fcnt=$(ttn-lw-cli end-devices get "$APP" "$dev" --session.last-f-cnt-up 2>/dev/null \
         | grep -oE '"last_f_cnt_up"[[:space:]]*:[[:space:]]*[0-9]+' | grep -oE '[0-9]+$')
  green "PASS session present: dev_addr $addr, last_f_cnt_up ${fcnt:-0}"
  echo "   A session proves the join was accepted. It does not prove the node is transmitting"
  echo "   now — for that, scripts/soak_ttn.sh watches the counter advance."
}

# A gate for the gate. #76 shipped a soak "check" that could not fail, so a checker whose
# whole job is to catch one specific mistake has to demonstrate it catches it. Needs no
# credentials, no network and no board, so preflight runs it.
cmd_selftest() {
  local tmp rc=0
  tmp=$(mktemp -d) || die "mktemp failed"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" RETURN

  cat > "$tmp/ok.h" <<'EOF'
#define OTAA_DEVEUI {0x02, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67}
EOF
  cat > "$tmp/short.h" <<'EOF'
#define OTAA_DEVEUI {0x02, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45}
EOF
  printf '  deveui   : 02ABCDEF01234567\n' > "$tmp/good.log"
  printf '  deveui   : 67:45:23:01:EF:CD:AB:02\n' > "$tmp/rev.log"

  # name : expected exit : command
  run_case() {
    local name="$1" want="$2"; shift 2
    local got
    "$@" >/dev/null 2>&1; got=$?
    if [[ "$got" == "$want" ]]; then
      printf '   ok   %-38s exit %s\n' "$name" "$got"
    else
      printf '   FAIL %-38s exit %s, wanted %s\n' "$name" "$got" "$want"
      rc=1
    fi
  }

  echo "== register_device.sh selftest =="
  SECRETS="$tmp/ok.h"    run_case "identical DevEUI passes"        0 "${BASH_SOURCE[0]}" check "02ABCDEF01234567"
  SECRETS="$tmp/ok.h"    run_case "reversed DevEUI is caught"      2 "${BASH_SOURCE[0]}" check "6745230 1EFCDAB02"
  SECRETS="$tmp/ok.h"    run_case "different device is caught"     3 "${BASH_SOURCE[0]}" check "0212345678901234"
  SECRETS="$tmp/short.h" run_case "wrong-length secrets is caught" 1 "${BASH_SOURCE[0]}" check "02ABCDEF01234567"
  SECRETS="$tmp/nope.h"  run_case "missing secrets file is caught" 1 "${BASH_SOURCE[0]}" check "02ABCDEF01234567"
  SECRETS="$tmp/ok.h"    run_case "banner match passes"            0 "${BASH_SOURCE[0]}" banner "$tmp/good.log"
  SECRETS="$tmp/ok.h"    run_case "reversed banner is caught"      2 "${BASH_SOURCE[0]}" banner "$tmp/rev.log"
  SECRETS="$tmp/ok.h"    run_case "banner with no deveui line"     1 "${BASH_SOURCE[0]}" banner "$tmp/short.h"
  run_case               "no arguments prints usage"               2 "${BASH_SOURCE[0]}"

  # The reversal branch must not fire on a palindrome-free value by accident, and must fire on
  # a real reversal -- checked above. Also confirm gen produces something this script accepts,
  # so the two halves cannot drift apart.
  local gen_deveui
  gen_deveui=$("${BASH_SOURCE[0]}" gen 2>/dev/null | grep -m1 '#define OTAA_DEVEUI' \
               | grep -oE '0x[0-9a-f]{2}' | sed 's/0x//' | tr -d '\n')
  if [[ ${#gen_deveui} -eq 16 && $(( 0x${gen_deveui:0:2} & 0x02 )) -ne 0 ]]; then
    printf '   ok   %-38s %s\n' "gen emits a locally-administered EUI" "${gen_deveui:0:2}..."
  else
    printf '   FAIL %-38s got %s\n' "gen emits a locally-administered EUI" "${gen_deveui:-empty}"
    rc=1
  fi

  if [[ $rc -eq 0 ]]; then green "PASS register_device.sh selftest"; else red "FAIL register_device.sh selftest"; fi
  return $rc
}

case "${1:-}" in
  selftest) shift; cmd_selftest "$@" ;;
  gen)     shift; cmd_gen "$@" ;;
  create)  shift; cmd_create "$@" ;;
  verify)  shift; cmd_verify "$@" ;;
  check)   shift; cmd_check "$@" ;;
  banner)  shift; cmd_banner "$@" ;;
  session) shift; cmd_session "$@" ;;
  *)
    sed -n '/^# Usage:/,/^# Env:/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
