#!/usr/bin/env bash
# Read-only: is this checkout worth driving? Prints OK/WARN/FAIL lines; exit 1 on any FAIL.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
fail=0
ok()   { echo "OK    $*"; }
warn() { echo "WARN  $*"; }
bad()  { echo "FAIL  $*"; fail=1; }

[ -x "$PY" ] && ok "venv $("$PY" --version 2>&1)" || bad "no venv at $VENV (run scripts/build.sh)"
if [ -f "${INSTA360_SDK_DIR:-}/lib/libCameraSDK.so" ] && [ -f "$INSTA360_SDK_DIR/include/camera/camera.h" ]; then
  ok "INSTA360_SDK_DIR=$INSTA360_SDK_DIR"
else
  bad "INSTA360_SDK_DIR unset or missing lib/include (${INSTA360_SDK_DIR:-unset})"
fi
case "${LD_LIBRARY_PATH:-}" in *fake_sdk*) warn "your shell's LD_LIBRARY_PATH contains the fake SDK; real runs would load it";; esac

so="$(env -u LD_LIBRARY_PATH "$PY" -c 'import pyinsta360._sdk as m; print(m.__file__)' 2>/dev/null)"
if [ -n "$so" ]; then
  ok "extension $so"
  newest="$(ls -t "$REPO_ROOT"/src/* "$REPO_ROOT"/CMakeLists.txt | head -1)"
  [ "$newest" -nt "$so" ] && bad "extension is STALE ($newest is newer; run scripts/build.sh)" || ok "extension newer than src/"
  lib="$(env -u LD_LIBRARY_PATH ldd "$so" | awk '/libCameraSDK/ {print $3}')"
  ok "real backend loads $lib"
  ver="$(env -u LD_LIBRARY_PATH "$PY" -c 'import pyinsta360; print(pyinsta360.sdk_version())' 2>/dev/null)"
  [ -n "$ver" ] && ok "real SDK version $ver" || bad "real SDK failed to load"
else
  bad "pyinsta360 not importable (run scripts/build.sh)"
fi
[ -f "$FAKE_SDK_DIR/libCameraSDK.so" ] && ok "fake SDK built at $FAKE_SDK_DIR" || warn "fake SDK not built (scripts/build.sh or drive.sh fake builds it)"

usb="$(lsusb -d 2e1a: 2>/dev/null)"
if [ -n "$usb" ]; then
  ok "camera on USB: $usb"
  node="$(echo "$usb" | head -1 | awk '{printf "/dev/bus/usb/%s/%s", $2, substr($4,1,3)}')"
  holders="$(fuser "$node" 2>/dev/null)"
  if [ -w "$node" ]; then ok "camera $node writable by $(id -un)"; else
    bad "camera $node not writable by $(id -un) ($(stat -c '%U:%G %A' "$node")); SDK will discover nothing. Install a udev rule, see SKILL.md Launch"; fi
  [ -n "$holders" ] && warn "camera $node held by PID(s):$holders (bridge container? another journey?)" || ok "camera $node not held by any process"
else
  warn "no Insta360 (vid 2e1a) on USB; only the fake backend can be driven"
fi
ok "git $(git -C "$REPO_ROOT" rev-parse --short HEAD)$(git -C "$REPO_ROOT" diff --quiet || echo ' (dirty)')"
exit $fail
