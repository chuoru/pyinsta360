# Discovery and camera session

A user lists attached cameras with `pyinsta360.discover()`, opens one with `pyinsta360.Camera(serial=None)` (directly or as a context manager), reads its identity and status, and closes it. Calls made in the wrong lifecycle state raise `pyinsta360.Insta360Error`.

## Sub-features

- `discover` returns `DeviceInfo` values, or `[]` with no camera.
- `open-first` opens the first camera when no serial is given.
- `open-serial` opens the camera with a given serial and raises when none matches.
- `lifecycle` moves through `CameraState.CLOSED` and `OPEN`. A second `close()` is a no-op.
- `wrong-state` makes calls that need an open camera raise `Insta360Error` while it is closed.
- `status` covers `info`, `battery_status()`, `video_encode_type()` and `current_function_mode()`.

## How to get to it (user POV)

- `pi.discover()` at module level.
- `with pi.Camera() as cam:` or `pi.Camera("SERIAL").open()` / `.close()`.

## Driving it with drive.sh

Preconditions:

- Baseline from the README.

- **Discover and open (fake).** Run `$S/drive.sh fake discover_open`. Every line is `PASS` and `RESULT PASS`. `camera.json` shows serial `FAKE0001` and battery level.
- **Open by serial (fake).** Run `FAKE_INSTA360_DEVICE_COUNT=3 $S/drive.sh fake discover_open --serial FAKE0002`. It prints `PASS opened the requested serial`.
- **No camera (fake).** Run `FAKE_INSTA360_NO_DEVICES=1 $S/drive.sh fake discover_open`. It prints `FAIL discover() found at least one camera` and exits 1. That is the expected outcome for this knob.
- **Open failure (fake).** Run `FAKE_INSTA360_OPEN_FAIL=1 $S/drive.sh fake discover_open`. `stderr.log` ends in `Insta360Error` naming `open`.
- **Real camera.** Run `$S/drive.sh real discover_open`. `RESULT PASS`. `camera.json` shows the real serial, firmware and battery.

## Gotchas

- Exit 134 with `std::bad_function_call` is the known SDK discovery race (see SKILL.md Evidence). Rerun.
- SDK log `found 1 Insta360 usb device(s)` followed by `discovered: []` means the USB node is not writable by your user. Doctor shows `not writable`. Install the udev rule from SKILL.md Launch. This is not a binding bug.
- With no camera, the real SDK logs `usb_device_detector ... no Insta360 ... usb device found` to stderr. That is normal.
- `drive.sh real` refuses (exit 3) when the bridge container or another process holds the camera. Report the holder and do not kill it.
- The camera has to be in Android/USB mode to enumerate. If `lsusb -d 2e1a:` is empty, the problem is on the camera side, not in pyinsta360.
