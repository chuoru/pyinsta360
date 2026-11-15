# CameraSDK 2.1.8: `Camera::StopRecording()` returns an origin URL with the wrong extension (`.lrv` instead of `.insv`)

## Summary

On an Insta360 X3, the `MediaUrl` returned by `Camera::StopRecording()` names an origin file that does not exist. The returned origin path ends in `.lrv`, while the recorded file on the camera ends in `.insv`. Downloading the returned path with `DownloadCameraFile` fails, while the `.insv` path downloads correctly. Applications that use the returned URL to fetch a recording cannot get the file.

## Environment

- CameraSDK 2.1.8 (Linux x86_64, `lib/libCameraSDK.so`)
- Camera: Insta360 X3, firmware v1.1.6, connected over USB (vid:pid `2e1a:0002`)
- OS: Ubuntu 24.04.5 LTS, kernel 7.0.0, glibc 2.39
- Camera state before recording: `GetCurrentFunctionMode()` returned `FUNCTION_MODE_HDR_IMAGE`. No `SetVideoCaptureParams` call was made before `StartRecording()`.

## Steps to reproduce

1. Connect an X3 over USB.
2. Build and run the attached `repro.cc`. It records for 3 seconds, prints the returned `MediaUrl`, compares it with the camera's file list, and tries to download each returned path:

   ```bash
   g++ -std=c++17 -I$SDK/include repro.cc -L$SDK/lib -lCameraSDK -Wl,-rpath,$SDK/lib -o repro
   ./repro 3 --delete     # --delete removes the clip afterwards
   ```

## Expected

`MediaUrl::OriginUrls()` lists the recorded `.insv` file, and `LRVUrls()` lists its `.lrv` proxy. Every returned path exists in `GetCameraFilesList()` and can be downloaded.

## Actual

Observed on 2026-11-16 (3-second recording):

| Source | Path |
|---|---|
| `StopRecording().OriginUrls()` | `/DCIM/Camera01/VID_20261116_051448_00_007.lrv` |
| `StopRecording().LRVUrls()` | `/DCIM/Camera01/LRV_20261116_051448_01_007.lrv` |
| New in `GetCameraFilesList()` | `/DCIM/Camera01/VID_20261116_051448_00_007.insv` |
| New in `GetCameraFilesList()` | `/DCIM/Camera01/LRV_20261116_051448_01_007.lrv` |

- `DownloadCameraFile("/DCIM/Camera01/VID_20261116_051448_00_007.lrv", ...)` returned `false`.
- `DownloadCameraFile("/DCIM/Camera01/VID_20261116_051448_00_007.insv", ...)` returned `true` and produced a valid 22,028,003-byte file (3840x1920 H.264 video with AAC audio, 3.27 s, 98 frames).
- The `LRVUrls()` entry was correct and present on the camera.
- `TakePhoto()` in the same session returned a correct path (`IMG_20261116_051438_00_006.insp`), so the problem appears specific to `StopRecording()`.

The origin URL has the right base name (`VID_…_00_007`) and the wrong extension. It looks like the extension is taken from the LRV entry when the origin URL is built. That is an inference from the output, not confirmed.

## Reproduced with the plain C++ API

The first observation was made through a Python binding that copies `MediaUrl::OriginUrls()` and `LRVUrls()` without modification. The attached `repro.cc` makes the same calls directly against the C++ API, with no binding involved. Run on the same camera on 2026-11-16 (`./repro 3 --delete`, camera in function mode 8, `FUNCTION_MODE_HDR_IMAGE`), it reproduced the problem:

```
returned origin: /DCIM/Camera01/VID_20261116_055409_00_006.lrv
returned lrv:    /DCIM/Camera01/LRV_20261116_055409_01_006.lrv
new on camera:   /DCIM/Camera01/LRV_20261116_055409_01_006.lrv
new on camera:   /DCIM/Camera01/VID_20261116_055409_00_006.insv
BAD  listed=0 downloaded=0 /DCIM/Camera01/VID_20261116_055409_00_006.lrv
OK   listed=1 downloaded=1 /DCIM/Camera01/LRV_20261116_055409_01_006.lrv
RESULT: returned MediaUrl names a file that does not exist
```

Two out of two recordings showed the same wrong extension.

## Suggested fix

Build each origin URL from the recorded file's own name, with its `.insv` extension, and check that every returned path exists in the camera's file list before returning it.

## Impact

Any application that downloads a recording right after `StopRecording()` fails to fetch it. The current workaround is to compare `GetCameraFilesList()` from before and after the recording.

## Attachments

- `repro.cc` is the reproducer. It exits 1 when a returned path is missing or cannot be downloaded.
