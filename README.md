# pyinsta360

Python bindings for the Insta360 CameraSDK: discover a camera, control it, and read its live
video, IMU and exposure streams from Python. Tested with an X3; X4 and X5 are nominal.

The SDK is proprietary and is **not** part of this repository. You supply your own copy.

## Install

```bash
INSTA360_SDK_DIR=/path/to/insta360_sdk pip install .          # or: pip install -e .
pip install '.[decode]'                                       # optional: PyAV for decoding
```

`INSTA360_SDK_DIR` is the directory that contains `include/` and `lib/libCameraSDK.so`. The build
fails with a clear message if it is missing. The extension records `${INSTA360_SDK_DIR}/lib` as its
`DT_RUNPATH`, so the library is found without setting `LD_LIBRARY_PATH`, and `LD_LIBRARY_PATH` still
takes precedence if you set it. Needs a C++17 compiler, CMake 3.18+ and Python 3.10+. The SDK itself
needs glibc 2.17 / libstdc++ GLIBCXX_3.4.30, which means Ubuntu 22.04 or newer.

> A wheel built here links against, and embeds the path of, the proprietary SDK. Do not publish
> wheels built against it.

## Usage

```python
import pyinsta360 as pi

print(pi.sdk_version(), pi.discover())        # [] when no camera is attached

with pi.Camera() as cam:                      # or Camera("<serial>") to pick one
    cam.start_live_streaming()                # LiveStreamParam() defaults to 1920x960P30
    for _ in range(100):
        packet = cam.read_video(timeout=1.0)  # VideoPacket (H.264/H.265 bytes) or None
        imu = cam.read_imu()                  # structured array, every pending sample
        ...
    print(cam.stream_stats())                 # {'video_dropped': 0, 'imu_dropped': 0}
```

Decoding the stream to RGB frames (needs PyAV):

```python
from pyinsta360.decode import VideoDecoder

decoder = VideoDecoder(cam.video_encode_type())
for frame in decoder.decode(packet):          # list of HxWx3 uint8 RGB arrays
    ...
```

`examples/stream_smoke.py` runs the whole path against a real camera and reports video fps, IMU rate
and drops: `python examples/stream_smoke.py --seconds 10 --save-frame first.png`.

Besides streaming, `Camera` offers battery and storage status, file list/download/delete, photo and
video capture, exposure and capture parameters, and clock sync. Enums such as `VideoResolution`,
`CameraFunctionMode` and `ExposureMode` are generated from the SDK headers. Type stubs ship in the
package.

## Units and clocks

Stream data is passed through in raw SDK units.

- IMU acceleration (`ax`, `ay`, `az`) is in **g**, not m/s^2. Multiply by 9.80665 for m/s^2. This
  is the finding of the reference bridge, cross-checked against another driver that uses the same
  SDK struct. Gyro (`gx`, `gy`, `gz`) is treated as rad/s there, which has not been verified
  independently.
- Video and IMU `timestamp` values are in unspecified units and come from independent clocks: on
  real hardware they sat tens of seconds apart at the start of one session. To align the two
  streams, stamp each item with your own receipt time.
- The SDK delivers gyro data in bursts (about 50 samples every ~100 ms on an X3), so
  `read_imu()` returns whole bursts, not a smooth trickle.
- Findings from the reference bridge worth knowing: `LiveStreamParam.using_lrv=True` delivered no
  video in dual-lens (360) mode, and the camera may ignore the requested preview resolution.

## Known issues

- **SDK crash during discovery.** CameraSDK 2.1.8 sometimes aborts the process with `std::bad_function_call` during `discover()` or `Camera.open()`. The exception is thrown on the SDK's own session thread, so pyinsta360 cannot catch it. A plain C++ program reproduces it (4 of 10 runs under CPU load). In Python on an idle machine it hit between 0 of 10 and 6 of 10 runs. Retrying the process works. Run discovery in a supervised process if a crash is unacceptable.
- **Multiple video streams.** Some modes deliver each lens as its own stream. The X3 in `RES_2880_2880P30` mode sends `stream_index` 0 and 1 with independent timestamps. Group packets by `stream_index` and use one `VideoDecoder` per stream.

## Threading model

- The SDK calls back on one of its own threads, and a video callback delays the gyro callback queued
  behind it. The callbacks therefore only copy into bounded buffers and notify. They never touch
  Python or the GIL.
- Buffers drop their **oldest** entries when full (`video_queue_size` 64 packets, `imu_queue_size`
  20000 samples by default, exposure 256). Drops are counted in `stream_stats()`.
- Every call that can block releases the GIL, including `read_video`, which also wakes every
  100 ms so Ctrl-C works.
- `close()` and `stop_live_streaming()` may be called from any thread. A thread blocked in
  `read_video()` is woken and gets `None`. Reads on a camera that is not streaming return `None` or
  an empty array, and a stop discards whatever was still buffered.
- Other camera calls are serialized by one lock, so `close()` waits for an SDK call that is already
  running (for example a download) instead of racing it.
- The `progress(downloaded, total)` callback of `download_file` runs on an SDK thread with the GIL
  held. An exception it raises is re-raised after the download returns, and the download is not
  cancelled.
- Lifecycle is an explicit state, `Camera.state`: `CLOSED`, `OPEN`, `STREAMING`. A call that the
  current state does not allow raises `pyinsta360.Insta360Error` (a `RuntimeError`), as does any
  SDK action that returns failure, naming the method.

## Testing without hardware

`tests/fake_sdk/fake_camera_sdk.cc` is a stand-in `libCameraSDK.so` built against the real SDK
headers. It reports one X3 (serial `FAKE0001`), streams deterministic video packets at 30 Hz and
gyro batches of 17 samples, and has environment knobs for failure paths (see its header comment).

```bash
export INSTA360_SDK_DIR=/path/to/insta360_sdk
pip install -e '.[test]'
tools/test.sh            # builds the fake, puts it first on LD_LIBRARY_PATH, runs pytest
```

`LD_LIBRARY_PATH` has to be set before Python starts, which is why this is a wrapper script. Running
plain `pytest` stops with a message if it finds the real library. The suite also checks that every
`ins_camera` symbol the extension imports exists in both the fake and the real SDK, and that the
generated enums and type stubs are current. Hardware behavior is not covered; use the smoke example.

After changing the SDK headers, regenerate the enum bindings:

```bash
python tools/gen_enums.py --sdk-dir "$INSTA360_SDK_DIR"      # add --check to only verify
```
