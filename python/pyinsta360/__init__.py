"""Python bindings for the Insta360 CameraSDK."""
from ._sdk import (
    BatteryStatus,
    CameraFunctionMode,
    CameraLensType,
    CameraState,
    CameraType,
    CardLocation,
    CardState,
    ConnectionType,
    DeviceInfo,
    ExposureMode,
    ExposureSettings,
    Insta360Error,
    LiveStreamParam,
    LogLevel,
    MediaUrl,
    PowerType,
    PreviewParam,
    RawCaptureType,
    RecordParams,
    StorageStatus,
    VideoEncodeType,
    VideoPacket,
    VideoResolution,
    sdk_version,
    sdk_version_major,
    set_log_level,
    set_log_path,
)

from . import _guard, _sdk
from .supervise import SupervisedProcessError, run_supervised


class Camera(_sdk.Camera):
    __doc__ = _sdk.Camera.__doc__

    def open(self):
        with _guard.discovering():
            super().open()

    def __enter__(self):
        with _guard.discovering():
            return super().__enter__()


def discover():
    with _guard.discovering():
        return _sdk.discover()


discover.__doc__ = _sdk.discover.__doc__

try:
    from importlib.metadata import version as _version

    __version__ = _version("pyinsta360")
except Exception:  # not installed (e.g. running from a source checkout)
    __version__ = "0+unknown"

__all__ = [
    "BatteryStatus",
    "Camera",
    "CameraFunctionMode",
    "CameraLensType",
    "CameraState",
    "CameraType",
    "CardLocation",
    "CardState",
    "ConnectionType",
    "DeviceInfo",
    "ExposureMode",
    "ExposureSettings",
    "Insta360Error",
    "LiveStreamParam",
    "LogLevel",
    "MediaUrl",
    "PowerType",
    "PreviewParam",
    "RawCaptureType",
    "RecordParams",
    "StorageStatus",
    "SupervisedProcessError",
    "VideoEncodeType",
    "VideoPacket",
    "VideoResolution",
    "discover",
    "run_supervised",
    "sdk_version",
    "sdk_version_major",
    "set_log_level",
    "set_log_path",
]
