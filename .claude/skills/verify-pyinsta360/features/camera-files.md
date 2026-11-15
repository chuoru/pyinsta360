# Camera files

A user lists the media on the camera's storage with `files_list()` and copies one to disk with `download_file(remote, local, progress=callback)`. The callback receives `(downloaded, total)`.

## Sub-features

- `list` returns remote paths such as `/DCIM/Camera01/VID_....insv`.
- `download` writes the file locally and invokes `progress`.
- `download-fail` raises `Insta360Error` when the SDK reports failure.

## How to get to it (user POV)

- `cam.files_list()`, then `cam.download_file(path, "local.insv", progress=print)` on an open camera.

## Driving it with drive.sh

Preconditions:

- Baseline from the README. Real: the SD card holds at least one file. Prefer a small `.insp` photo, because `.insv` videos can be gigabytes.

- **List and download (fake).** Run `$S/drive.sh fake files`. It shows `RESULT PASS`. `files.json` lists the two fake paths, and the evidence dir holds a non-empty `IMG_FAKE_002.insp`.
- **Download failure (fake).** Run `FAKE_INSTA360_ACTION_FAIL=1 $S/drive.sh fake files`. `stderr.log` ends in `Insta360Error` naming `download_file`.
- **List and download (real).** Run `$S/drive.sh real files`, or pick a file with `--remote /DCIM/Camera01/IMG_xxx.insp`. It shows `RESULT PASS`, and the downloaded file is non-empty in the evidence dir.
- **Prove the copy is exact (real).** Compare the file size with the last `progress_tail` total in `download.json`, then open the file with PyAV: `.venv/bin/python -c "import av,sys; c=av.open(sys.argv[1]); print(sum(1 for _ in c.decode(video=0)))" <file>`. Measured on 2026-11-16 on an X3: `LRV_20260919_171011_01_005.lrv`, 21,272,226 bytes equal to the reported total, 1302 progress calls, about 3 s including open (about 7 MB/s), and 274 H.264 frames at 1024x512 over 9.14 s.

## Gotchas

- The SDK has no file-size call. When the card holds no `.insp` photos, pass `--remote` with an `LRV_*.lrv` low-resolution proxy. Those are tens of MB, while `VID_*.insv` files can be gigabytes.
- An empty card makes the real journey fail `files_list() is non-empty`. Report that as a missing precondition, not a binding bug.
- Raise `VERIFY_TIMEOUT` for large downloads. The default of 90 s kills the journey mid-download.
- Never call `delete_file` on a real camera during verification.
