# pyinsta360 verification map

This directory is the maintained source for verifying what a Python user of pyinsta360 can do. Read this index, then use the matching feature file as the recipe. `S` below means `.claude/skills/verify-pyinsta360/scripts`.

| feature | file | journey |
|---|---|---|
| Discovery and camera session | [discovery-and-session.md](discovery-and-session.md) | `discover_open` |
| Live streaming (video + IMU) | [live-streaming.md](live-streaming.md) | `stream` |
| Decoding frames | [decoding.md](decoding.md) | `stream --save-frame` |
| Camera files | [camera-files.md](camera-files.md) | `files` |
| Photo and video capture | [capture.md](capture.md) | `capture` |

## Baseline preconditions

- `$S/build.sh` has printed `READY` for the current revision, and `$S/doctor.sh` exits 0.
- Fake backend: no further setup. Each run is its own process, so runs can be parallel.
- Real backend: one Insta360 on USB, in 360/dual-lens mode (set on the camera's own screen), not held by any other process (doctor's `not held` line). An SD card with media is needed for `files`.

## Driving conventions

- Run every journey through `$S/drive.sh <backend> <journey>`. It sets the backend, the timeout and the evidence dir. Running a journey with a bare `python` loses the evidence and may load the wrong `libCameraSDK.so`.
- Use the fake backend for binding behavior and failure paths, via `FAKE_INSTA360_*` knobs. Use the real backend for anything about the camera.
- Real journeys that write to the camera need an explicit opt-in flag. Never delete files from a real camera during verification.

## Proof and skip reporting

- A proof is the evidence dir: `run.txt`, `stdout.log` with its PASS/FAIL lines, `exit_code`, and the journey artifacts.
- State the backend with every claim. A fake PASS is not evidence about hardware.
- When the real backend is unavailable (no camera, camera held), report that feature's real-backend proof as not run, with doctor's output as the reason. Do not substitute a fake run.

## Feature entry contract

Each feature file starts with an H1 title and one paragraph describing the user-visible behavior. It then uses exactly four H2 sections in this order: `Sub-features`, `How to get to it (user POV)`, `Driving it with drive.sh`, `Gotchas`.
