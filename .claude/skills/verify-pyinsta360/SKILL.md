---
name: verify-pyinsta360
description: Drive the pyinsta360 Python bindings for the Insta360 CameraSDK end to end and capture evidence, on a fake libCameraSDK.so (no hardware) or a real USB Insta360 camera. Use to prove a change to the bindings (src/, python/pyinsta360/) works the way a Python user would call it, beyond the pytest suite, or to smoke-test a real camera.
---

# Verify pyinsta360

pyinsta360 is a library, so the "app" is a Python process that imports it. Every drive is a journey script under `journeys/`, run by `scripts/drive.sh` against one of two backends.

- `fake` puts `build/fake_sdk/libCameraSDK.so` first on `LD_LIBRARY_PATH`. It reports one X3 with serial `FAKE0001`, streams deterministic packets at 30 Hz and 17-sample gyro batches, and lists two fake files. This proves the binding's own behavior: state machine, queues, GIL handling, value marshalling. It proves nothing about the camera.
- `real` loads the real SDK through the extension's RUNPATH and needs an Insta360 on USB (vendor id `2e1a`) set to 360/dual-lens mode. Only this backend proves hardware behavior.

All paths below are relative to the repo root. Scripts are in `.claude/skills/verify-pyinsta360/scripts/`.

## Launch

```bash
S=.claude/skills/verify-pyinsta360/scripts
$S/build.sh        # last line: READY pyinsta360 <version> sdk <sdk version>
```

`build.sh` creates `.venv` if missing, installs the build tools, runs an editable install with the `test` extra and Pillow, and builds the fake SDK. It needs `INSTA360_SDK_DIR` (the directory containing `include/` and `lib/libCameraSDK.so`). `env.sh` defaults it to `../RoboManipBaselines/third_party/insta360_sdk` when that exists. The editable install does **not** rebuild C++ on import. Rerun `build.sh` after any change under `src/` or `CMakeLists.txt`, or doctor reports the extension as STALE.

**Real backend, once per host.** The camera's USB node must be writable by your user. Without that, the SDK logs `found 1 Insta360 usb device(s)` and `discover()` still returns `[]`. Doctor reports it as `not writable`. The fix needs sudo, so it is the user's to run:

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2e1a", MODE="0666"' | sudo tee /etc/udev/rules.d/99-insta360.rules
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=2e1a
```

Replug the camera if the node's mode does not change. The `insta360_bridge` Docker container avoids this only because it runs as root.

There is no long-lived server to tear down. Each journey is one foreground process with a timeout (`VERIFY_TIMEOUT`, default 90 s).

## Doctor

```bash
$S/doctor.sh       # read-only; exit 1 on any FAIL line
```

Checks the venv, `INSTA360_SDK_DIR`, that the extension imports and is newer than `src/`, which `libCameraSDK.so` the real backend resolves, the real SDK version, whether a camera is on USB and whether another process holds its USB node, and the git revision. Run it first, and again whenever a journey fails in a way that smells like environment.

## Drive

```bash
$S/drive.sh fake|real <journey> [journey args]
```

| journey | what it drives | real-camera side effects |
|---|---|---|
| `discover_open` | `discover()`, `Camera` lifecycle, wrong-state error, identity, battery | none |
| `stream` | `start_live_streaming`, `read_video`, `read_imu`, `stream_stats`, stop. `--seconds N`, `--video-queue N`, `--read-delay S`, `--save-frame` (decode via PyAV, real only) | none |
| `files` | `files_list`, `download_file` with progress into the evidence dir. `--remote PATH` | reads media off the card |
| `capture` | `take_photo`, `start_recording`/`stop_recording` | **writes** a photo and a clip to the camera. Refuses on `real` without `--allow-camera-writes` |

Failure paths on the fake come from env knobs passed straight through: `FAKE_INSTA360_NO_DEVICES=1`, `FAKE_INSTA360_DEVICE_COUNT=N`, `FAKE_INSTA360_OPEN_FAIL=1`, `FAKE_INSTA360_STREAM_FAIL=1`, `FAKE_INSTA360_ACTION_FAIL=1`, `FAKE_INSTA360_VIDEO_HZ=F`, `FAKE_INSTA360_GYRO_BATCH=N`. Example: `$S/drive.sh fake stream --video-queue 2 --read-delay 0.2` acts as a slow consumer and forces queue drops.

For a new behavior with no journey, write `journeys/<name>.py` on the same pattern: import `check`, `write_json`, `finish` from `_common`, assert what a Python caller observes, and write artifacts into `EVIDENCE`.

**Isolation.** Fake runs are independent processes and can run in parallel. A real camera serves one process. `drive.sh real` refuses when `fuser` shows another holder of its USB node, which is typically the `insta360_bridge` container or another journey. Never stop the other holder to make room. Report it. Do not drive `real` while the user is using the camera.

## Evidence

Each run writes `verify-evidence/<UTC time>-<backend>-<journey>/` (gitignored):

- `run.txt` holds the command, git revision and backend env. `stdout.log`, `stderr.log` and `exit_code` hold the process outcome. `result.json` lists the failed checks.
- Journey artifacts. `camera.json` from discover_open. `stream_summary.json` (fps, IMU Hz, drops, median accel norm), `first_packet.bin`, `imu_head.npy` and optionally `frame.png` from stream. `files.json`, `download.json` and the downloaded file from files. `capture.json` from capture.

Proof standards:
- Drive the public `pyinsta360` API the way a user would. Never call `_sdk` internals or fake-only hooks to force a state.
- A fake-backend PASS proves binding behavior only. Claims about the camera (frame rate, IMU rate, units, decodability, file writes) need a `real` run, and the report must say which backend produced each claim.
- Check side effects, not only return values. Downloads must exist on disk with non-zero size. Captures must grow the camera's file list (the `real` capture journey checks this).
- The SDK writes `usb_device_detector` warnings to stderr on every discovery with no camera. They are not failures.

## Cleanup

```bash
$S/cleanup.sh
```

Stops journeys that `drive.sh` started and that are still alive, using the pid files it leaves in their evidence dirs (never by process name). Then it removes `build/verify-scratch`. It never deletes `verify-evidence/`. Cleanup cannot undo a real `capture` run. The photo and clip stay on the camera, and deleting them is the user's call. If a real journey was killed mid-stream and the camera stops responding, replug it or power-cycle it. Do not try to reset it from software.

## Helpers

- `scripts/build.sh` sets up the venv and builds (Launch).
- `scripts/doctor.sh` is the read-only health check.
- `scripts/drive.sh <backend> <journey> [args]` runs one journey with evidence capture.
- `scripts/cleanup.sh` stops our leftover processes.
- `scripts/env.sh` is sourced by the others and defines the repo, venv, SDK and evidence paths.
- `tools/test.sh` (repo) runs the pytest suite on the fake backend. It is a separate check from these journeys.

Per-feature recipes are in `features/README.md`.
