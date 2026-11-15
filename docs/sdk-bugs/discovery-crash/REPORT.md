# CameraSDK 2.1.8: `DeviceDiscovery::GetAvailableDevices` intermittently aborts the process with `std::bad_function_call`

## Summary

`ins_camera::DeviceDiscovery::GetAvailableDevices()` sometimes terminates the whole process. An internal SDK thread, `ins_camera::connection::CameraSession::ReadLoop()`, invokes an empty `std::function`. The resulting `std::bad_function_call` is not caught on that thread, so `std::terminate` aborts the process (SIGABRT, exit code 134). The calling application cannot catch or recover from it.

It reproduces with a minimal C++ program that only discovers and opens a camera. It happens more often when the CPU is busy, which points to a race inside the SDK.

## Environment

- CameraSDK 2.1.8 (Linux x86_64, `lib/libCameraSDK.so`)
- Camera: Insta360 X3, firmware v1.1.6, connected over USB (vid:pid `2e1a:0002`)
- OS: Ubuntu 24.04.5 LTS, kernel 7.0.0, glibc 2.39
- Compiler: GCC 13.3.0, `-std=c++17`
- CPU: Intel Core i9-13900HX (32 threads)

## Steps to reproduce

1. Connect an X3 over USB and make sure the user can open the USB device.
2. Build and run the attached `repro.cc`, which only calls `GetAvailableDevices`, `Camera::Open`, `FreeDeviceDescriptors` and `Close`. The attached `run.sh` builds it and runs it repeatedly with every CPU core busy:

   ```bash
   INSTA360_SDK_DIR=/path/to/CameraSDK ./run.sh 30
   ```

3. Some runs exit with code 134 and print:

   ```
   terminate called after throwing an instance of 'std::bad_function_call'
     what():  bad_function_call
   ```

## Expected

`GetAvailableDevices()` returns the device list, or an empty list on failure. It never terminates the process.

## Actual

The process aborts during `GetAvailableDevices()`. `repro.cc` prints a `STEP` line before each SDK call. In every crash the last line printed was `STEP discover`, so the abort happens inside `GetAvailableDevices()` and never in `Open()`, `FreeDeviceDescriptors()` or `Close()`.

Measured results:

| Program | Condition | Crashed |
|---|---|---|
| discover + open (`repro.cc`) | all cores busy | 7 of 72 runs, all in `GetAvailableDevices` |
| discover only | all cores busy | 4 of 10 runs |
| discover only | idle | 0 of 12 runs |
| The same calls through a Python binding | idle | between 0 of 10 and 6 of 10 runs per batch |

## Stack trace

Captured with gdb on the crashing thread (frames below `ReadLoop` are libstdc++'s thread start):

```
#7  std::terminate()                                  libstdc++.so.6
#8  __cxa_throw ()                                    libstdc++.so.6
#9  std::__throw_bad_function_call()                  libstdc++.so.6
#10 ins_camera::connection::CameraSession::ReadLoop() libCameraSDK.so
#11 ?? (std::thread start routine)                    libstdc++.so.6
```

Just before the abort, the SDK log (level VERBOSE) shows the session's first heartbeat:

```
I camera_session.cpp:0275:  heartbeat is coming
I camera_session.cpp:0277:  package version: 0
I camera_session.cpp:0250:  first heart beat package is coming
I camera_session.cpp:0063:  heartbeat package is coming
terminate called after throwing an instance of 'std::bad_function_call'
```

## Analysis

These points are inferences, not confirmed against SDK source:

- `ReadLoop` calls a `std::function` member (a packet or heartbeat handler) that is empty at that moment. It seems to be read without synchronization while it is being set or cleared, for example while the temporary session that `GetAvailableDevices` opens to read the serial number is set up or torn down.
- The application cannot work around it inside the process. We tried a `std::set_terminate` handler that recognizes this exception on the SDK thread:
  - calling `pthread_exit` from the handler re-enters `std::terminate` (the thread's start routine is `noexcept`) until the process segfaults;
  - parking the thread in the handler makes `GetAvailableDevices()` hang, because the SDK waits for that thread to finish.

## Suggested fix

Ensure `ReadLoop` never invokes an empty handler. Either check it before calling (`if (handler) handler(...)`) under the same lock that guards assignment, or set the handler before the read thread starts and clear it only after the thread has been joined. Catching exceptions at the top of `ReadLoop` would also stop a single bad packet from killing the host process.

## Impact

Any application that discovers or opens a camera can be killed at startup, at random. We currently run discovery in a supervised child process and restart it when it aborts.

## Attachments

- `repro.cc` is the minimal reproducer.
- `run.sh` builds it and runs it under CPU load.
