#!/usr/bin/env bash
# Builds the fake libCameraSDK.so into build/fake_sdk/ and prints that directory.
# Needs only the SDK headers (INSTA360_SDK_DIR); the real library is not linked.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${INSTA360_SDK_DIR:?set INSTA360_SDK_DIR to the extracted Insta360 CameraSDK}"
out="$root/build/fake_sdk"
mkdir -p "$out"
g++ -std=c++17 -O1 -g -fPIC -shared -pthread -Wall -Wextra \
  -I"$INSTA360_SDK_DIR/include" \
  "$root/tests/fake_sdk/fake_camera_sdk.cc" \
  -Wl,-soname,libCameraSDK.so -o "$out/libCameraSDK.so"
echo "$out"
