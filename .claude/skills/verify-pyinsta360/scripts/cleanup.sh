#!/usr/bin/env bash
# Stops journeys that drive.sh started and that are still alive (their pid files),
# then removes the scratch download dir. Never touches verify-evidence/ contents.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
for f in "$EVIDENCE_ROOT"/*/pid; do
  [ -f "$f" ] || continue
  pid="$(cat "$f")"
  if kill -0 "$pid" 2>/dev/null; then
    echo "stopping timeout wrapper $pid ($(dirname "$f"))"
    kill -INT "$pid"; sleep 3; kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
  fi
  rm -f "$f"
done
rm -rf "$REPO_ROOT/build/verify-scratch"
echo "cleanup done; evidence kept in $EVIDENCE_ROOT"
