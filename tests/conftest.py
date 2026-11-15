import ctypes
import gc

import pytest

import pyinsta360

FAKE_ENV_KNOBS = (
    "FAKE_INSTA360_NO_DEVICES",
    "FAKE_INSTA360_DEVICE_COUNT",
    "FAKE_INSTA360_OPEN_FAIL",
    "FAKE_INSTA360_STREAM_FAIL",
    "FAKE_INSTA360_ACTION_FAIL",
    "FAKE_INSTA360_VIDEO_HZ",
    "FAKE_INSTA360_GYRO_BATCH",
)


def pytest_configure(config):
    if not pyinsta360.sdk_version().startswith("fake"):
        pytest.exit(
            "pyinsta360 loaded the real libCameraSDK.so. Run the suite through "
            "tools/test.sh so LD_LIBRARY_PATH points at the fake SDK.",
            returncode=2,
        )


class FakeSdk:
    """Counters exported by the fake SDK, read through ctypes."""

    def __init__(self):
        # Already loaded by the extension; dlopen by soname returns that handle.
        lib = ctypes.CDLL("libCameraSDK.so")
        self.live_descriptors = lib.fake_insta360_live_descriptors
        self.live_cameras = lib.fake_insta360_live_cameras
        self.open_cameras = lib.fake_insta360_open_cameras
        self.stream_threads = lib.fake_insta360_stream_threads
        lib.fake_insta360_last_sync_utc.restype = ctypes.c_uint64
        lib.fake_insta360_last_sync_offset.restype = ctypes.c_int32
        self.last_sync = lambda: (
            lib.fake_insta360_last_sync_utc(),
            lib.fake_insta360_last_sync_offset(),
        )

    def outstanding(self):
        return dict(
            descriptors=self.live_descriptors(),
            cameras=self.live_cameras(),
            open=self.open_cameras(),
            stream_threads=self.stream_threads(),
        )


@pytest.fixture
def fake(monkeypatch):
    for knob in FAKE_ENV_KNOBS:
        monkeypatch.delenv(knob, raising=False)
    sdk = FakeSdk()
    yield sdk
    gc.collect()
    assert sdk.outstanding() == dict(descriptors=0, cameras=0, open=0, stream_threads=0)


@pytest.fixture
def cam(fake):
    camera = pyinsta360.Camera()
    camera.open()
    yield camera
    camera.close()
