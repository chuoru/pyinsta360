# Photo and video capture

A user triggers `take_photo()` and `start_recording()` / `stop_recording()` from Python. Each capture returns a `MediaUrl`. A falsy `MediaUrl` means the capture failed, as the SDK documents.

## Sub-features

- `photo` returns a non-empty `MediaUrl` and adds a file on the camera.
- `record` returns a `MediaUrl` with origin and optional LRV paths and adds a clip.
- `capture-fail` returns a falsy `MediaUrl` from `take_photo()` and `stop_recording()` when the SDK fails. `start_recording()` raises.

## How to get to it (user POV)

- `url = cam.take_photo()`, then `cam.start_recording()`, and later `url = cam.stop_recording()`.

## Driving it with drive.sh

Preconditions:

- Baseline from the README. Real: free space on the card, and the user has agreed that new media may be written to the camera.

- **Capture (fake).** Run `$S/drive.sh fake capture --record-seconds 1`. It shows `RESULT PASS`. `capture.json` shows `IMG_FAKE_003.insp` and `VID_FAKE_004.insv` plus the LRV path.
- **Capture failure (fake).** Run `FAKE_INSTA360_ACTION_FAIL=1 $S/drive.sh fake capture`. It prints `FAIL take_photo() returned a non-empty MediaUrl` and then `Insta360Error` from `start_recording` in `stderr.log`. That is the expected outcome.
- **Capture (real).** Run `$S/drive.sh real capture --allow-camera-writes --record-seconds 3`. It shows `RESULT PASS`, including `camera file list grew`. `capture.json` lists the new files.
- **Prove the media is real (real).** Download each new file with `cam.download_file` and open it. Measured on 2026-11-16 on an X3: a 5952x2976 JPEG `.insp` of 4.9 MB, and a 3840x1920 H.264+AAC `.insv` of 22 MB, 3.27 s and 98 frames for a 3 s recording.

## Gotchas

- **SDK returns a wrong clip path.** On an X3 with SDK 2.1.8, `stop_recording()` returned origin URL `VID_20261116_051448_00_007.lrv`, but the file on the card is `VID_..._007.insv`. Downloading the returned path fails, and the `.insv` downloads fine. pyinsta360 passes the SDK's URLs through unchanged. The journey prints a `NOTE` line when this happens. Find the clip by diffing `files_list()` before and after.
- Without `--allow-camera-writes`, the real journey refuses on purpose.
- The fake's file list is static, so the "file list grew" check runs only on real hardware.
- The new media stays on the camera after cleanup. Tell the user which files were created (`capture.json` `new_files`).
