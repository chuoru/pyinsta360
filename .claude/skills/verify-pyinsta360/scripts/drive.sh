#!/usr/bin/env bash
# Usage: drive.sh fake|real <journey> [journey args...]
# Runs journeys/<journey>.py on one backend and stores its evidence under
# verify-evidence/<UTC time>-<backend>-<journey>/. FAKE_INSTA360_* env vars pass through.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
backend="${1:?usage: drive.sh fake|real <journey> [args]}"; journey="${2:?journey name}"; shift 2
script="$VERIFY_SKILL_DIR/journeys/$journey.py"
[ -f "$script" ] || { echo "no journey $script" >&2; exit 2; }

env_cmd=(env -u LD_LIBRARY_PATH)
case "$backend" in
  fake)
    "$REPO_ROOT/tools/build_fake_sdk.sh" >/dev/null || exit 2
    env_cmd=(env LD_LIBRARY_PATH="$FAKE_SDK_DIR") ;;
  real)
    usb="$(lsusb -d 2e1a: 2>/dev/null | head -1)"
    [ -n "$usb" ] || { echo "REFUSE: no Insta360 on USB" >&2; exit 3; }
    node="$(echo "$usb" | awk '{printf "/dev/bus/usb/%s/%s", $2, substr($4,1,3)}')"
    [ -w "$node" ] || { echo "REFUSE: $node not writable by $(id -un); install the udev rule from SKILL.md Launch" >&2; exit 3; }
    holders="$(fuser "$node" 2>/dev/null)"
    [ -z "$holders" ] || { echo "REFUSE: $node already held by PID(s):$holders" >&2; exit 3; } ;;
  *) echo "backend must be fake or real" >&2; exit 2 ;;
esac

out="$EVIDENCE_ROOT/$(date -u +%Y%m%dT%H%M%SZ)-$backend-$journey"
mkdir -p "$out"
{
  echo "command: drive.sh $backend $journey $*"
  echo "git: $(git -C "$REPO_ROOT" rev-parse HEAD)$(git -C "$REPO_ROOT" diff --quiet || echo ' dirty')"
  env | grep -E '^(FAKE_INSTA360_|INSTA360_SDK_DIR)' || true
} > "$out/run.txt"

"${env_cmd[@]}" VERIFY_EVIDENCE_DIR="$out" VERIFY_BACKEND="$backend" \
  timeout --signal=INT --kill-after=10 "${VERIFY_TIMEOUT:-90}" \
  "$PY" "$script" "$@" > "$out/stdout.log" 2> "$out/stderr.log" &
pid=$!
echo "$pid" > "$out/pid"
wait "$pid"; code=$?
rm -f "$out/pid"
echo "$code" > "$out/exit_code"
cat "$out/stdout.log"
echo "--- exit $code, evidence: $out"
exit "$code"
