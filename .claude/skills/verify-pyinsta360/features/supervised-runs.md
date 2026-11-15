# Supervised runs

A user wraps a script's entry point in `pyinsta360.run_supervised(main, retries=3)`. The function runs in a spawned child process. If the CameraSDK aborts that process inside `discover()` or `Camera.open()` (the SDK 2.1.8 `std::bad_function_call` race), the function is rerun from the start. Other crashes and exceptions reach the caller. Exceptions keep their type, and process deaths become `SupervisedProcessError`.

## Sub-features

- `result` returns the function's return value, with args and kwargs passed through.
- `retry-discovery` reruns the function when it is killed by a signal while discovering or opening, and logs a warning on the `pyinsta360` logger.
- `exhausted` raises `SupervisedProcessError` with `attempts`, `exitcode` and `during_discovery` after `retries` extra attempts.
- `no-retry-elsewhere` raises a crash after open (during capture or streaming) at once, with no retry.
- `exceptions` re-raises an exception from the function in the parent. An unpicklable one becomes `RuntimeError` with the traceback.

## How to get to it (user POV)

- `pyinsta360.run_supervised(fn, *args, retries=3, **kwargs)`, where `fn` is a module-level function. Scripts call it under `if __name__ == "__main__":`.

## Driving it with drive.sh

Preconditions:

- Baseline from the README. The retry, exhausted, no-retry and exception paths are covered on the fake by `tests/test_supervise.py` (`FAKE_INSTA360_DISCOVERY_CRASH_FILE`) through `tools/test.sh`.

- **Supervised runs (real).** Run `$S/drive.sh real supervised --iterations 20`. It shows `RESULT PASS`, and `supervised_summary.json` shows `succeeded == iterations`.
- **Provoke the SDK race (real).** With an idle CPU the race can go unseen. Measured on 2026-11-16: 0 crashes in 20 runs (40 discovery calls). To prove the retry path, load every core while the journey runs: `for k in $(seq $(nproc)); do (timeout 300 sh -c 'while :; do :; done' &); done`, then run the journey. Measured on 2026-11-16 on an X3: 7 SDK aborts caught across 20 runs (one run needed 2 retries), 20/20 succeeded, and every warning read `died with SIGABRT during discovery`. The load loops end through their own `timeout`. Do not stop them with `pkill -f` on their command text, because that pattern also matches the shell running pkill.

## Gotchas

- A retry count of 0 does not prove the retry path. Check the warnings in `supervised_summary.json`.
- Each iteration spawns a fresh Python process and imports pyinsta360, which adds about a second.
- The function reruns from the beginning, so the journey's probe is idempotent. User code that writes files before `open()` repeats those writes on a retry.
