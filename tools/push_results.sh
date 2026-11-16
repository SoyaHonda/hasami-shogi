#!/usr/bin/env bash
# Pushes training results from a course VM (plan 2026-11-12), one command for both VMs:
#
#   bash tools/push_results.sh runs/b1_s10 runs/league_s10
#
# For each folder it commits log.csv, model.pt, machine.pt and human.pt (those that exist; not
# checkpoint.pt or pool/, which are only for resuming) and pushes them to the branch
# results/<host name> (results/aiexp-03 and so on), so the two VMs never collide and main is left
# alone. The commit message carries [skip ci]. The push needs the remote set once with the shared
# token (docs/training-manual.md, Git):
#   git remote set-url origin https://token:<TOKEN>@github.com/SoyaHonda/hasami-shogi.git
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "usage: bash tools/push_results.sh RUN_FOLDER..." >&2
  exit 2
fi
cd "$(git rev-parse --show-toplevel)"
host="$(hostname -s 2>/dev/null || hostname)"
branch="results/${host}"

# A name for the commits if none is set (the VM has none at first).
git config user.name >/dev/null 2>&1 || git config user.name "$host"
git config user.email >/dev/null 2>&1 || git config user.email "${host}@users.noreply.github.com"

added=0
for dir in "$@"; do
  if [ ! -d "$dir" ]; then
    echo "push_results: no folder $dir" >&2
    exit 1
  fi
  for f in log.csv model.pt machine.pt human.pt; do
    if [ -f "$dir/$f" ]; then
      git add -f "$dir/$f"
      added=$((added + 1))
    fi
  done
done
if [ "$added" -eq 0 ]; then
  echo "push_results: nothing to add in $*" >&2
  exit 1
fi

if git diff --cached --quiet; then
  echo "push_results: no changes since the last push; pushing again anyway"
else
  git commit -q -m "results from ${host}: $* [skip ci]"
fi
git push -q origin "HEAD:refs/heads/${branch}"
echo "push_results: pushed to ${branch} ($(git rev-parse --short HEAD))"
