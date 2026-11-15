#!/usr/bin/env bash
# Builds repro.cc against the CameraSDK and runs it N times with every core busy.
# Usage: INSTA360_SDK_DIR=/path/to/sdk ./run.sh [runs]
set -uo pipefail
: "${INSTA360_SDK_DIR:?set INSTA360_SDK_DIR to the CameraSDK directory}"
runs="${1:-30}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$(mktemp -d)"
g++ -std=c++17 -g -I"$INSTA360_SDK_DIR/include" "$here/repro.cc" \
  -L"$INSTA360_SDK_DIR/lib" -lCameraSDK -Wl,-rpath,"$INSTA360_SDK_DIR/lib" -o "$out/repro" || exit 1
crashes=0
for i in $(seq 1 "$runs"); do
  for _ in $(seq 1 "$(nproc)"); do (timeout 5 sh -c 'while :; do :; done' &); done
  timeout 40 "$out/repro" > "$out/run_$i.txt" 2>&1
  code=$?
  if [ "$code" -ne 0 ]; then
    crashes=$((crashes + 1))
    echo "run $i: exit $code, last step: $(grep STEP "$out/run_$i.txt" | tail -1)"
  fi
  sleep 5
done
echo "crashed $crashes of $runs runs (logs in $out)"
