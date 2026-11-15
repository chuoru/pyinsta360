# Sourced by the other scripts. Resolves the repo, venv and SDK paths.
VERIFY_SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$VERIFY_SKILL_DIR/../../.." && pwd)"
VENV="$REPO_ROOT/.venv"
PY="$VENV/bin/python"
EVIDENCE_ROOT="$REPO_ROOT/verify-evidence"
FAKE_SDK_DIR="$REPO_ROOT/build/fake_sdk"
if [ -z "${INSTA360_SDK_DIR:-}" ] && [ -d "$REPO_ROOT/../RoboManipBaselines/third_party/insta360_sdk" ]; then
  INSTA360_SDK_DIR="$(cd "$REPO_ROOT/../RoboManipBaselines/third_party/insta360_sdk" && pwd)"
fi
export INSTA360_SDK_DIR
