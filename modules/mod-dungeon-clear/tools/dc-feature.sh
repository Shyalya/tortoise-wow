#!/usr/bin/env bash
# RETIRED — this script created git worktrees, and the worktree workflow has
# been abandoned. The trees accumulated unmanaged and work went stale in
# directories nobody revisited. Work in the main checkout instead:
#
#   git switch -c feat/<name> master
#
# See CLAUDE.md rule 2. The original implementation is kept below for history.
cat >&2 <<'EOF'
dc-feature.sh is RETIRED: the worktree workflow was abandoned.

Work in the main checkout instead:
  git switch -c feat/<name> master

Commit before switching branches (CLAUDE.md rule 3).
EOF
exit 1

set -euo pipefail

name="${1:?usage: dc-feature.sh <feature-name> [kind]}"
kind="${2:-feat}"
repo="$(git rev-parse --show-toplevel)"
branch="${kind}/${name}"
wt="${repo}/.claude/worktrees/${name}"

if [ -e "$wt" ]; then
  echo "error: worktree already exists: $wt" >&2
  exit 1
fi

git -C "$repo" fetch gh master --quiet 2>/dev/null || true
git -C "$repo" worktree add -b "$branch" "$wt" master

echo
echo "Worktree ready:  $wt"
echo "Branch:          $branch  (forked from master)"
echo
echo "When the feature is finished and committed:"
echo "  git -C '$repo' merge --no-ff $branch"
echo "  git -C '$repo' worktree remove '$wt'"
echo "  git -C '$repo' branch -d $branch"
