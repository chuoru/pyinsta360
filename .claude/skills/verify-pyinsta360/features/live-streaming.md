# Live streaming (video + IMU)

A user starts the camera's preview stream and pulls compressed video packets with `read_video(timeout)` and IMU bursts with `read_imu()`, a structured numpy array with fields `timestamp, ax, ay, az, gx, gy, gz`. Bounded queues drop the oldest data when the reader falls behind, and `stream_stats()` counts the drops.

## Sub-features

- `start-stop` moves the state between `OPEN` and `STREAMING`. Reads after a stop return `None` or an empty array.
- `video` delivers packets with strictly increasing timestamps.
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
- **Stream (real).** Run `$S/drive.sh real stream --seconds 10`. `RESULT PASS`, including `accel magnitude near 1 g`. Record `video_fps`, `imu_hz` and the drops from `stream_summary.json`. On an X3 with firmware v1.1.6, the bridge measured about 30 fps and about 500 Hz IMU.

## Gotchas

- `LiveStreamParam()` defaults to `using_lrv=True`. The bridge saw no video in 360 mode with that setting, so the journey passes `using_lrv=False`. A user report of "no video" should be checked against this first.
- The camera may ignore the requested preview resolution. Trust the decoded frame size, not the parameter.
- Video and IMU timestamps come from different clocks in unspecified units. Do not assert alignment between them.
- IMU arrives in bursts of about 50 samples every ~100 ms on real hardware. A single `read_imu()` call returning empty is normal.
- If the camera is not in 360 mode, gyro never fires. The journey then fails `received IMU samples` because of the camera setting, not the binding.
