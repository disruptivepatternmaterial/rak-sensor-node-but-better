#!/usr/bin/env bash
# Local gates. Same checks CI runs, so failures surface before the push, not after.
# Referenced by .cursor/rules/30-change-workflow.mdc.
#
# Usage: scripts/preflight.sh

set -uo pipefail
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
FAILED=0
BLOCKED=0
# --strict / PREFLIGHT_STRICT=1 makes a BLOCKED contract item exit non-zero. Off by default so
# routine CI is not red for a conflict that is deliberately open; on for the release checklist,
# where shipping against an unresolved payload field is the thing we are trying to prevent.
STRICT="${PREFLIGHT_STRICT:-0}"
[[ "${1:-}" == "--strict" ]] && STRICT=1

step()    { echo; echo "${BLUE}-- $* --${NC}"; }
ok()      { echo "${GREEN}PASS${NC} $*"; }
bad()     { echo "${RED}FAIL${NC} $*"; FAILED=1; }
warn()    { echo "${YELLOW}WARN${NC} $*"; }
blocked() { echo "${RED}BLOCKED${NC} $*"; BLOCKED=$((BLOCKED + 1)); }

echo "${BLUE}=== preflight ===${NC}"

# --------------------------------------------------------------- secrets
# AGENTS.md: never commit secrets, keys, or live OTAA AppKeys.
step "secrets"
# `git grep` rather than `git ls-files | xargs grep`: xargs aborts with
# "sysconf(_SC_ARG_MAX) failed" under some sandboxes, and because empty output was read as a
# clean result, this gate reported PASS having never run a single grep. A gate that cannot
# distinguish "found nothing" from "never looked" is not a gate.
SECRET_RC=0
SECRET_RAW=$(git grep -nIE '(APPKEY|APP_KEY|APPEUI|APP_EUI|DEVEUI|DEV_EUI|NWKSKEY|APPSKEY)[[:space:]]*[:=][[:space:]]*[^ ]*[0-9A-Fa-f]{16}' -- ':/' 2>&1) || SECRET_RC=$?
if [[ "$SECRET_RC" -gt 1 ]]; then
  echo "$SECRET_RAW"
  bad "the secret scan itself failed to run (git grep exit $SECRET_RC) -- treating as a failure, not a pass"
  SECRET_HITS=""
else
  SECRET_HITS=$(printf '%s' "$SECRET_RAW" \
    | grep -viE '(example|template|placeholder|XXXX|0{16}|docs/|\.mdc:|schema\.yaml)' || true)
fi
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

# The build host's address must never enter this repo, which is public. On 2026-08-27 it did:
# a commit put the host's public IP in README.md, where it survived review because nothing
# looked for it. Documentation of the rule was not enforcement of the rule.
# First octet must be 2-3 digits. A single-digit first octet is almost always a version or a
# spec section (`v1.02 §2.5.1.1` matched a plain dotted-quad pattern and failed this gate on
# its first run), while every address that has actually leaked here began 130. or 192.
IP_RC=0
IP_RAW=$(git grep -nIE '(^|[^0-9.])[0-9]{2,3}(\.[0-9]{1,3}){3}([^0-9.]|$)' -- ':/' 2>&1) || IP_RC=$?
if [[ "$IP_RC" -gt 1 ]]; then
  echo "$IP_RAW"
  bad "the address scan itself failed to run (git grep exit $IP_RC) -- treating as a failure, not a pass"
  IP_HITS=""
else
  # Allowlist only addresses that cannot identify a machine: the unspecified address,
  # loopback, and the broadcast address.
  IP_HITS=$(printf '%s' "$IP_RAW" | grep -vE '(^|[^0-9.])(0\.0\.0\.0|127\.0\.0\.1|255\.255\.255\.255)([^0-9.]|$)' || true)
fi
if [[ -n "$IP_HITS" ]]; then
  echo "$IP_HITS"
  bad "IPv4 literal in a tracked file -- addresses belong in \$RAK_BUILD_HOST or ~/.rak-build-host, never in the repo"
else
  ok "no IPv4 literals in tracked files"
fi

# --------------------------------------------------------------- evidence SHAs
# docs/EVIDENCE.md: a result without a host and a commit SHA is not evidence. These SHAs are
# the ones that cannot be regenerated -- three were read back from a board's own boot banner,
# and that board is in the field. Anything that rewrites history (a filter-repo purge, a
# rebase of an old commit) renames them and silently breaks every claim that cites them.
# ADR-0009 chose not to rewrite history for exactly this reason; this gate is the tripwire
# that makes a reversal announce itself instead of being found later by a confused reader.
step "evidence SHAs"
SHA_MISSING=""
for entry in \
  "1c2df3c:banner-asserted 2026-08-14, v0.4.3, the field-bound image" \
  "d568574:banner-asserted 2026-08-13, first board to name its own commit" \
  "65f8615:banner-asserted 2026-08-13" \
  "572bcfa:the 19.03 h soak run" \
  "4510763:Stage 4, both sensors in one field-image cycle"
do
  s="${entry%%:*}"
  git cat-file -e "${s}^{commit}" 2>/dev/null || SHA_MISSING="$SHA_MISSING
  $s -- ${entry#*:}"
done
if [[ -n "$SHA_MISSING" ]]; then
  echo "$SHA_MISSING"
  bad "irreplaceable evidence commit(s) no longer resolve -- history was rewritten; see ADR-0009 before proceeding"
else
  ok "all irreplaceable evidence SHAs resolve"
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
  # git grep, for the same reason as the secrets gate above.
  ZERO_RC=0
  ZEROS=$(git grep -nIE '(fail|error|timeout|invalid)[^;]*=[[:space:]]*0[;,]' -- 'src/*' 'lib/*' 'include/*' 2>&1) || ZERO_RC=$?
  if [[ "$ZERO_RC" -gt 1 ]]; then
    echo "$ZEROS"
    bad "the null-policy scan itself failed to run (git grep exit $ZERO_RC)"
    ZEROS=""
  fi
  if [[ -n "$ZEROS" ]]; then
    echo "$ZEROS"
    warn "possible fabricated zero on a failure path -- nulls must stay null"
  else
    ok "no obvious fabricated zeros"
  fi
else
  echo "${DIM}   no firmware sources yet -- skipped${NC}"
fi

# --------------------------------------------------------------- blocked payload fields
# A field marked BLOCKED in payload/schema.yaml is an unresolved half of the two-repo ingest
# contract -- today, the ADR-0002 battery-current sign. The parity checker reported these as
# WARN and the run still printed "PREFLIGHT OK", so the one summary line a human actually reads
# said green while a shipped field had no agreed meaning. It gets its own named state now.
step "payload contract"
BLOCKED_FIELDS=$(grep -B12 '^    status: BLOCKED' payload/schema.yaml 2>/dev/null \
  | grep -E '^  - name:' | sed 's/^  - name: //' || true)
if [[ -n "$BLOCKED_FIELDS" ]]; then
  while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    blocked "payload/schema.yaml: field '$f' is BLOCKED -- no agreed meaning on the wire"
  done <<< "$BLOCKED_FIELDS"
  echo "${DIM}   see docs/decisions/ADR-0002-payload-contract-conflicts.md${NC}"
else
  ok "no payload field is BLOCKED"
fi

# --------------------------------------------------------------- summary
echo
if [[ "$FAILED" -ne 0 ]]; then
  echo "${RED}=== PREFLIGHT FAILED ===${NC}"
  exit 1
fi

if [[ "$BLOCKED" -gt 0 ]]; then
  # Deliberately NOT the word "OK". The checks passed; the contract is still open, and that is
  # a release-blocking fact, not a footnote. Exit 0 unless --strict, so an open ADR does not
  # turn routine CI red -- but nothing here can be misread as a clean run.
  echo "${RED}=== PREFLIGHT BLOCKED -- $BLOCKED unresolved payload contract item(s) ===${NC}"
  echo "${YELLOW}    Checks passed. The ingest contract is NOT settled. Not releasable as final;"
  echo "    resolve the ADR or ship knowing the field's meaning is undecided.${NC}"
  if [[ "$STRICT" -eq 1 ]]; then
    echo "${RED}    --strict: exiting 2.${NC}"
    exit 2
  fi
  exit 0
fi

echo "${GREEN}=== PREFLIGHT OK ===${NC}"
exit 0
