#!/usr/bin/env bash
# Local gates. Same checks CI runs, so failures surface before the push, not after.
# Referenced by .cursor/rules/30-change-workflow.mdc.
#
# Usage: scripts/preflight.sh

set -uo pipefail
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
FAILED=0
step() { echo; echo "${BLUE}-- $* --${NC}"; }
ok()   { echo "${GREEN}PASS${NC} $*"; }
bad()  { echo "${RED}FAIL${NC} $*"; FAILED=1; }
warn() { echo "${YELLOW}WARN${NC} $*"; }

echo "${BLUE}=== preflight ===${NC}"

# --------------------------------------------------------------- secrets
# AGENTS.md: never commit secrets, keys, or live OTAA AppKeys.
step "secrets"
SECRET_HITS=$(git ls-files -z \
  | xargs -0 grep -nIE '(APPKEY|APP_KEY|APPEUI|APP_EUI|DEVEUI|DEV_EUI|NWKSKEY|APPSKEY)[[:space:]]*[:=][[:space:]]*[^ ]*[0-9A-Fa-f]{16}' 2>/dev/null \
  | grep -viE '(example|template|placeholder|XXXX|0{16}|docs/|\.mdc:|schema\.yaml)' || true)
if [[ -n "$SECRET_HITS" ]]; then
  echo "$SECRET_HITS"
  bad "possible live LoRaWAN credentials in tracked files"
else
  ok "no credential-shaped strings in tracked files"
fi

if git ls-files --error-unmatch secrets.h >/dev/null 2>&1; then
  bad "secrets.h is tracked -- it must stay git-ignored"
else
  ok "secrets.h not tracked"
fi

# --------------------------------------------------------------- decoder parity
# Every build verifies the TTN formatter is current and calls out drift.
step "TTN formatter parity"
if python3 scripts/check_decoder_parity.py; then :; else bad "decoder parity"; fi

# --------------------------------------------------------------- golden vectors
# Parity compares the encoder and the decoder field by field, which cannot see behavior
# the schema does not describe -- the installation offset applied to wind direction, for
# one. This runs real encoder output through the real decoder instead of comparing claims.
# Skips itself where node is unavailable, which is the workstation.
step "golden vectors"
if python3 scripts/check_golden_vectors.py; then :; else bad "golden vectors"; fi

# --------------------------------------------------------------- citations
step "citations"
if python3 scripts/check_citations.py; then :; else bad "citation discipline"; fi

# --------------------------------------------------------------- doc links
step "internal doc links"
BROKEN=0
while IFS= read -r md; do
  # relative markdown links only; skip http(s), anchors, and mailto
  while IFS= read -r link; do
    [[ -z "$link" ]] && continue
    target="${link%%#*}"
    [[ -z "$target" ]] && continue
    resolved="$(cd "$(dirname "$md")" && printf '%s' "$(pwd)/$target")"
    if [[ ! -e "$resolved" ]]; then
      echo "  ${md}: -> ${link}"
      BROKEN=$((BROKEN + 1))
    fi
  done < <(grep -oE '\]\([^)]+\)' "$md" 2>/dev/null \
            | sed -E 's/^\]\(//; s/\)$//' \
            | grep -vE '^(https?:|mailto:|#)' || true)
done < <(git ls-files '*.md')
if [[ "$BROKEN" -gt 0 ]]; then
  bad "$BROKEN broken relative link(s)"
else
  ok "all relative doc links resolve"
fi

# --------------------------------------------------------------- null policy
# AGENTS.md / FIRMWARE_SPEC.md 2.1: a failed read is null, never 0.
step "null policy"
if git ls-files 'src/*' 'lib/*' 'include/*' >/dev/null 2>&1 && [[ -n "$(git ls-files 'src/*')" ]]; then
  ZEROS=$(git ls-files 'src/*' 'lib/*' 'include/*' \
    | xargs grep -nIE '(fail|error|timeout|invalid)[^;]*=[[:space:]]*0[;,]' 2>/dev/null || true)
  if [[ -n "$ZEROS" ]]; then
    echo "$ZEROS"
    warn "possible fabricated zero on a failure path -- nulls must stay null"
  else
    ok "no obvious fabricated zeros"
  fi
else
  echo "${DIM}   no firmware sources yet -- skipped${NC}"
fi

# --------------------------------------------------------------- summary
echo
if [[ "$FAILED" -eq 0 ]]; then
  echo "${GREEN}=== PREFLIGHT OK ===${NC}"
else
  echo "${RED}=== PREFLIGHT FAILED ===${NC}"
fi
exit "$FAILED"
