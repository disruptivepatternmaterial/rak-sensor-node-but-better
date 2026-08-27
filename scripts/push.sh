#!/usr/bin/env bash
# Push this workspace's commits to GitHub, relaying through the Heliotrope Ridge host.
#
# Why this exists:
# The workstation's git CLI cannot push to the disruptivepatternmaterial org. Its keychain
# credential and all three of its SSH keys authenticate as the WORK account
# (ntableman_sfemu), which gets HTTP 403 on this repo. The user's IDE can push because it
# has its own GitHub sign-in, but the git CLI does not share it. The build host
# authenticates as disruptivepatternmaterial and can push.
#
# This is a credential fact, not a misconfigured remote. Do not "fix" it by rewriting the
# remote URL. See .cursor/rules/10-environments.mdc.
#
# The relay pushes to a NON-checked-out ref on the build host first, because git refuses
# to update the branch a non-bare repo currently has checked out.
#
# Usage:
#   scripts/push.sh              push the current branch to origin/main
#   scripts/push.sh --dry-run    show what would happen, change nothing

set -euo pipefail
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; DIM=$'\033[2m'; NC=$'\033[0m'
die() { echo "${RED}ERROR${NC} $*" >&2; exit 1; }

# No literal address here on purpose. Point BUILD_HOST at the host, or better, define an
# ssh alias so the address stays in ~/.ssh/config where it belongs:
#
#   export RAK_BUILD_HOST=ntableman@<address>    # one shell
#   Host wx3-harness                              # ~/.ssh/config, persistent
#       HostName <address>
#       User ntableman
#
# See docs/ENVIRONMENTS.md.
BUILD_HOST="${BUILD_HOST:-${RAK_BUILD_HOST:-wx3-harness}}"
REMOTE_PATH="${REMOTE_PATH:-Documents/GitHub/lorawan/rak-sensor-node-but-better}"
# Pushed by explicit SSH URL, never by `git push origin`. The build host's tracked origin is
# an HTTPS remote, and over a non-interactive ssh it has no credential helper and no tty, so
# it dies with "fatal: could not read Username for 'https://github.com': Device not
# configured" — observed 2026-08-12. The SSH remote authenticates from the host's key and
# works. Verified the same day: main advanced 8994d02..4510763 over this URL.
GH_SSH="git@github.com:disruptivepatternmaterial/rak-sensor-node-but-better.git"
RELAY_REF="refs/heads/from-workstation"

DRY=0
[[ "${1:-}" == "--dry-run" ]] && DRY=1

BRANCH=$(git rev-parse --abbrev-ref HEAD)
SHA=$(git rev-parse HEAD)

[[ -z "$(git status --porcelain)" ]] || {
  echo "${YELLOW}WARN${NC} working tree is dirty; only committed work will be pushed:"
  git status --short | sed 's/^/       /'
}

echo "${BLUE}== push relay ==${NC}"
echo "${DIM}   branch: ${BRANCH}${NC}"
echo "${DIM}   commit: ${SHA}${NC}"
echo "${DIM}   route:  workstation -> ${BUILD_HOST} -> github${NC}"

if [[ "$DRY" -eq 1 ]]; then
  echo "${YELLOW}dry run — nothing pushed.${NC}"
  exit 0
fi

# Fail here, with instructions, rather than 60 s into a TCP timeout that reads as a broken
# script. The old default was a LAN address that stopped resolving, and the resulting
# "ssh: connect ... Operation timed out" told nobody what to do about it.
if ! ssh -o ConnectTimeout=10 -o BatchMode=yes "$BUILD_HOST" true 2>/dev/null; then
  echo "${RED}ERROR${NC} cannot reach build host '${BUILD_HOST}'." >&2
  echo "       Set RAK_BUILD_HOST to the current address, or add an ssh alias:" >&2
  echo "         export RAK_BUILD_HOST=ntableman@<address>" >&2
  echo "       The address is not stable" >&2
  echo "       (BatchMode is on here, so a host needing a password also lands here;" >&2
  echo "        load the key into the agent or use an alias with IdentityFile.)" >&2
  exit 1
fi

echo
echo "${BLUE}-- 1/2 workstation -> build host --${NC}"
# Safe while a soak runs: a push to a ref that is not checked out never touches the working
# tree, so nothing under scripts/ changes on disk.
git push "${BUILD_HOST}:${REMOTE_PATH}" "HEAD:${RELAY_REF}" -f
git push "${BUILD_HOST}:${REMOTE_PATH}" --tags 2>/dev/null || true

# Step 2/2 rewrites the build host's WORKING TREE (`git checkout main` + `git merge --ff-only`),
# and a running soak is executing a bash script that lives in that tree.
#
# bash does not slurp a script; it reads it incrementally and keeps a byte offset into the open
# file. Rewriting the file underneath a running shell makes it resume at that offset in the new
# contents, so it executes a fragment of a line — the classic symptom is a sudden syntax error
# or a truncated command, hours into a run. `git merge` replaces the file rather than editing it
# in place, which changes the inode and is the case most likely to produce garbage.
#
# This is not hypothetical here: 7b03d3a itself modified scripts/soak_ttn.sh, which is exactly
# the script the 24 h TTN soak runs. Fast-forwarding mid-soak would have rewritten it under the
# running process and cost the whole run. A soak is a day of wall-clock time that cannot be
# compressed, so the default is to refuse and park the commits on the relay ref, which is
# already safely pushed above.
#
# CITE(spec): POSIX.1-2024 Shell Command Language §2.3 Token Recognition [CIT-POSIX-SH] —
#   https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html — "The shell shall
#   read its input in terms of lines", i.e. incrementally from the open file rather than slurped
#   whole, which is why rewriting the file under a running shell resumes in new contents.
#   gnu.org bot-blocks automated fetches (403), so the normative standard is cited in place of
#   the Bash manual per .cursor/rules/20-citation-discipline.mdc.
# CITE(policy): AGENTS.md — "Never let the pack reach a state it cannot recover from by itself"
#   and the repeated operator cost of lost days; a destroyed soak is a lost day.
# The guard is deliberately narrow rather than absolute. Only the scripts the running soak
# actually has OPEN can be corrupted by a fast-forward; a docs or CHANGELOG commit cannot reach
# them. A guard that blocks every push for 24 h would be routinely overridden out of habit, and
# a guard people override by reflex protects nothing. So it names the running soak's own script
# and refuses only when the incoming diff touches it.
if [[ "${ALLOW_PUSH_DURING_SOAK:-0}" != "1" ]]; then
  SOAK_PS=$(ssh -o BatchMode=yes "$BUILD_HOST" \
    "zsh -l -c 'pgrep -fl \"soak(_ttn)?\\.sh\" 2>/dev/null || true'" 2>/dev/null | tr -d '\r')
  if [[ -n "${SOAK_PS//[[:space:]]/}" ]]; then
    SOAK_PIDS=$(awk '{print $1}' <<<"$SOAK_PS" | tr '\n' ' ')
    # Every scripts/*.sh path named in the soak command lines -- that is the set of files a
    # running shell may be mid-read on.
    AT_RISK=$(grep -oE 'scripts/[A-Za-z0-9_.-]+\.sh' <<<"$SOAK_PS" | sort -u)

    # What this push would actually change in the build host's checked-out tree.
    REMOTE_HEAD=$(ssh -o BatchMode=yes "$BUILD_HOST" \
      "zsh -l -c 'cd ${REMOTE_PATH} && git rev-parse HEAD'" 2>/dev/null | tr -d '\r')
    INCOMING=""
    if [[ -n "$REMOTE_HEAD" ]]; then
      INCOMING=$(git diff --name-only "${REMOTE_HEAD}" HEAD 2>/dev/null || echo "UNKNOWN")
    else
      INCOMING="UNKNOWN"
    fi

    COLLIDES=""
    if [[ "$INCOMING" == "UNKNOWN" ]]; then
      COLLIDES="(could not compute the incoming diff -- refusing on the safe side)"
    else
      while IFS= read -r risk; do
        [[ -z "$risk" ]] && continue
        grep -qxF "$risk" <<<"$INCOMING" && COLLIDES="${COLLIDES:+${COLLIDES} }${risk}"
      done <<<"$AT_RISK"
    fi

    if [[ -n "$COLLIDES" ]]; then
      echo
      echo "${YELLOW}HOLD${NC} a soak is running on the build host (pid ${SOAK_PIDS% })"
      echo "     and this push would rewrite the script it is executing: ${COLLIDES}"
      echo
      echo "     Commits are already pushed to the relay ref '${RELAY_REF##refs/heads/}' on the"
      echo "     build host and are safe there. Step 2/2 fast-forwards the checked-out tree,"
      echo "     which replaces that file under the running shell. Not doing that."
      echo
      echo "     After the soak ends:  scripts/push.sh"
      echo "     To override anyway:   ALLOW_PUSH_DURING_SOAK=1 scripts/push.sh"
      exit 0
    fi
    echo "${DIM}   soak running (pid ${SOAK_PIDS% }); incoming diff does not touch${NC}"
    echo "${DIM}   ${AT_RISK//$'\n'/ } -- fast-forward is safe, continuing.${NC}"
  fi
fi

echo
echo "${BLUE}-- 2/2 build host -> github --${NC}"
# The build host may carry its own commits: the user works there directly sometimes.
# --ff-only makes this refuse rather than silently discard that work.
ssh "$BUILD_HOST" "zsh -l -c '
  set -e
  cd ~/${REMOTE_PATH}
  git fetch origin main -q || true
  git checkout main -q
  if git show-ref --verify --quiet refs/heads/${RELAY_REF##refs/heads/}; then
    if ! git merge --ff-only ${RELAY_REF##refs/heads/} -q; then
      echo \"build host main has commits not in this workspace — refusing to discard them.\"
      echo \"Reconcile by hand, then re-run.\"
      exit 1
    fi
  elif [[ \"\$(git rev-parse HEAD)\" != \"${SHA}\" ]]; then
    echo \"relay branch is missing and build host main is not the requested commit.\"
    echo \"Refusing to push an unverified build-host state.\"
    exit 1
  fi
  git push ${GH_SSH} main
  # Tags travel with the branch. A release tag that stays on the workstation leaves the
  # GitHub release pointing at nothing, and the version in the changelog unreachable.
  git push ${GH_SSH} --tags
  # The branch can already be gone when this is a no-op relay (both main and the
  # relay ref were current). It is cleanup only; GitHub has already received main,
  # so do not report a successful push as a failure merely because there is nothing
  # left to delete.
  git branch -D ${RELAY_REF##refs/heads/} -q 2>/dev/null || true
'" || die "relay failed. The build host may have diverged; check it before retrying."

echo
echo "${GREEN}=== PUSHED ===${NC}"
echo "commit: ${SHA}"
echo "${DIM}CI: gh run list  (run it on the build host — gh here is the work account)${NC}"
