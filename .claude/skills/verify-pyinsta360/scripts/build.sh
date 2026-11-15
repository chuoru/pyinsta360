#!/usr/bin/env bash
# Launch step: create the venv if missing, (re)build the extension, build the fake SDK.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
: "${INSTA360_SDK_DIR:?set INSTA360_SDK_DIR to the extracted Insta360 CameraSDK}"
[ -x "$PY" ] || "${BASE_PYTHON:-python3}" -m venv "$VENV"
"$PY" -m pip install -q --upgrade pip scikit-build-core pybind11 cmake ninja
"$PY" -m pip install -q --no-build-isolation -e "$REPO_ROOT[test]" pillow
"$REPO_ROOT/tools/build_fake_sdk.sh" >/dev/null
"$PY" -c "import pyinsta360; print('READY pyinsta360', pyinsta360.__version__, 'sdk', pyinsta360.sdk_version())" 2>/dev/null
