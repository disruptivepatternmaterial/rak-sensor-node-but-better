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

BUILD_HOST="${BUILD_HOST:-ntableman@192.168.10.223}"
REMOTE_PATH="${REMOTE_PATH:-Documents/GitHub/rak-sensor-node-but-better}"
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

echo
echo "${BLUE}-- 1/2 workstation -> build host --${NC}"
git push "${BUILD_HOST}:${REMOTE_PATH}" "HEAD:${RELAY_REF}" -f
git push "${BUILD_HOST}:${REMOTE_PATH}" --tags 2>/dev/null || true

echo
echo "${BLUE}-- 2/2 build host -> github --${NC}"
# The build host may carry its own commits: the user works there directly sometimes.
# --ff-only makes this refuse rather than silently discard that work.
ssh "$BUILD_HOST" "zsh -l -c '
  set -e
  cd ~/${REMOTE_PATH}
  git fetch origin main -q || true
  git checkout main -q
  if ! git merge --ff-only ${RELAY_REF##refs/heads/} -q; then
    echo \"build host main has commits not in this workspace — refusing to discard them.\"
    echo \"Reconcile by hand, then re-run.\"
    exit 1
  fi
  git push ${GH_SSH} main
  # Tags travel with the branch. A release tag that stays on the workstation leaves the
  # GitHub release pointing at nothing, and the version in the changelog unreachable.
  git push ${GH_SSH} --tags
  git branch -D ${RELAY_REF##refs/heads/} -q
'" || die "relay failed. The build host may have diverged; check it before retrying."

echo
echo "${GREEN}=== PUSHED ===${NC}"
echo "commit: ${SHA}"
echo "${DIM}CI: gh run list  (run it on the build host — gh here is the work account)${NC}"
