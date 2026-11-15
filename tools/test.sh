#!/usr/bin/env bash
# Builds the fake SDK and runs pytest with it ahead of the real library.
# LD_LIBRARY_PATH must be set before the interpreter starts, which is why this
# is a wrapper and not a conftest fixture.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fake_dir="$("$root/tools/build_fake_sdk.sh")"
export LD_LIBRARY_PATH="$fake_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "${PYTHON:-python}" -m pytest "$@"
