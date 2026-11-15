import json
import os
import sys
from pathlib import Path

EVIDENCE = Path(os.environ.get("VERIFY_EVIDENCE_DIR", "."))
BACKEND = os.environ.get("VERIFY_BACKEND", "unknown")
_failures = []


def check(ok, message):
    print(("PASS " if ok else "FAIL ") + message, flush=True)
    if not ok:
        _failures.append(message)
    return ok


def write_json(name, data):
    (EVIDENCE / name).write_text(json.dumps(data, indent=2, default=str))


def finish():
    write_json("result.json", {"backend": BACKEND, "failures": _failures})
    print("RESULT", "FAIL" if _failures else "PASS", flush=True)
    sys.exit(1 if _failures else 0)
