# Live streaming (video + IMU)

A user starts the camera's preview stream and pulls compressed video packets with `read_video(timeout)` and IMU bursts with `read_imu()`, a structured numpy array with fields `timestamp, ax, ay, az, gx, gy, gz`. Bounded queues drop the oldest data when the reader falls behind, and `stream_stats()` counts the drops.

## Sub-features

- `start-stop` moves the state between `OPEN` and `STREAMING`. Reads after a stop return `None` or an empty array.
- `video` delivers packets whose timestamps strictly increase within each `stream_index`. In dual-stream modes each lens is its own stream.
- `imu` delivers samples with the GyroData dtype. Accel is in g.
- `drops` makes a full queue drop its oldest entries and count them in `stream_stats()`.
- `stream-fail` raises `Insta360Error` when the SDK refuses to stream and leaves the camera `OPEN`.

## How to get to it (user POV)

- `cam.start_live_streaming(pi.LiveStreamParam(...), video_queue_size=64, imu_queue_size=20000)`, then loop over `cam.read_video(...)` and `cam.read_imu()`, then `cam.stop_live_streaming()` or `cam.close()`.

## Driving it with drive.sh

Preconditions:

- Baseline from the README. Real: camera in 360 mode, held still for the accel check.

- **Stream (fake).** Run `$S/drive.sh fake stream --seconds 3`. It shows `RESULT PASS`. `stream_summary.json` shows about 30 fps and about 500 Hz IMU with zero drops. `first_packet.bin` and `imu_head.npy` exist.
- **Force drops (fake).** Run `$S/drive.sh fake stream --seconds 2 --video-queue 2 --read-delay 0.2`. It prints `PASS a slow consumer makes the full video queue drop and count packets`, and `stream_summary.json` `stats.video_dropped` is greater than 0. A fast reader never drops, so raising `FAKE_INSTA360_VIDEO_HZ` alone does not force drops.
- **Stream refused (fake).** Run `FAKE_INSTA360_STREAM_FAIL=1 $S/drive.sh fake stream`. `stderr.log` ends in `Insta360Error` naming `start_live_streaming`.
- **Stream (real).** Run `$S/drive.sh real stream --seconds 10`. `RESULT PASS`, including `accel magnitude near 1 g` and a timestamp check per stream. Record `packets_per_stream`, `imu_hz` and the drops from `stream_summary.json`. Measured on 2026-11-16 with an X3 (firmware v1.1.6) in `RES_2880_2880P30` mode: two streams at 21.7 and 23.4 fps, 374 Hz IMU, 0 drops, median accel norm 0.989 g. The cause of the gap to the bridge's ~30 fps and ~500 Hz is not known.

## Gotchas

- The X3 in `RES_2880_2880P30` mode sends each lens as a separate stream (`stream_index` 0 and 1) with independent timestamps, and each frame is one 2880x2880 fisheye circle. The bridge saw a single side-by-side dual-fisheye stream in another mode. Never assume one stream.
- `LiveStreamParam()` defaults to `using_lrv=True`. The bridge saw no video in 360 mode with that setting, so the journey passes `using_lrv=False`. A user report of "no video" should be checked against this first.
- The camera may ignore the requested preview resolution. Trust the decoded frame size, not the parameter.
- Video and IMU timestamps come from different clocks in unspecified units. Do not assert alignment between them.
- IMU arrives in bursts of about 50 samples every ~100 ms on real hardware. A single `read_imu()` call returning empty is normal.
- If the camera is not in 360 mode, gyro never fires. The journey then fails `received IMU samples` because of the camera setting, not the binding.
