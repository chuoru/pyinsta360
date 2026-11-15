import os
import signal
import struct
import threading
import time

import numpy as np
import pytest

import pyinsta360 as pi

GYRO_DTYPE = np.dtype(
    [
        ("timestamp", "<i8"),
        ("ax", "<f8"),
        ("ay", "<f8"),
        ("az", "<f8"),
        ("gx", "<f8"),
        ("gy", "<f8"),
        ("gz", "<f8"),
    ]
)


def packet_counter(packet):
    assert packet.data[:4] == b"FKVD"
    return struct.unpack_from("<I", packet.data, 4)[0]


def collect_video(cam, n, timeout=2.0):
    packets = []
    deadline = time.monotonic() + timeout
    while len(packets) < n and time.monotonic() < deadline:
        packet = cam.read_video(timeout=0.5)
        if packet is not None:
            packets.append(packet)
    return packets


def collect_imu(cam, seconds):
    chunks = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        chunks.append(cam.read_imu(timeout=0.1))
    return np.concatenate(chunks)


class TestDiscovery:
    def test_returns_the_fake_device(self, fake):
        devices = pi.discover()
        assert len(devices) == 1
        d = devices[0]
        assert d.serial_number == "FAKE0001"
        assert d.camera_type == pi.CameraType.Insta360X3
        assert d.connection_type == pi.ConnectionType.USB
        assert d.camera_name == "Insta360 X3 (fake)"
        assert d.fw_version == "v0.0.0-fake"

    def test_empty_when_no_camera_is_attached(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_NO_DEVICES", "1")
        assert pi.discover() == []

    def test_sdk_version(self, fake):
        assert pi.sdk_version() == "fake-0.0.1"
        assert pi.sdk_version_major() == 0
        pi.set_log_level(pi.LogLevel.ERR)
        pi.set_log_path("/tmp/does-not-matter.log")


class TestLifecycle:
    def test_open_close_and_idempotent_close(self, fake):
        cam = pi.Camera()
        assert cam.state == pi.CameraState.CLOSED
        assert cam.info is None
        assert not cam.is_connected()
        cam.open()
        assert cam.state == pi.CameraState.OPEN
        assert cam.is_connected()
        assert cam.info.serial_number == "FAKE0001"
        assert fake.open_cameras() == 1
        cam.close()
        assert cam.state == pi.CameraState.CLOSED
        assert fake.open_cameras() == 0
        cam.close()
        assert cam.state == pi.CameraState.CLOSED

    def test_reopen_after_close(self, fake):
        cam = pi.Camera()
        cam.open()
        cam.close()
        cam.open()
        assert cam.state == pi.CameraState.OPEN
        cam.close()

    def test_context_manager(self, fake):
        with pi.Camera() as cam:
            assert cam.state == pi.CameraState.OPEN
        assert cam.state == pi.CameraState.CLOSED

    def test_context_manager_closes_on_exception(self, fake):
        with pytest.raises(KeyError):
            with pi.Camera() as cam:
                raise KeyError("boom")
        assert cam.state == pi.CameraState.CLOSED
        assert fake.open_cameras() == 0

    def test_destructor_closes_a_streaming_camera(self, fake):
        cam = pi.Camera()
        cam.open()
        cam.start_live_streaming()
        assert fake.stream_threads() == 1
        del cam
        assert fake.open_cameras() == 0
        assert fake.stream_threads() == 0

    def test_open_twice_raises(self, cam):
        with pytest.raises(pi.Insta360Error, match="already OPEN"):
            cam.open()

    def test_serial_selects_matching_device(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_DEVICE_COUNT", "3")
        with pi.Camera("FAKE0002") as cam:
            assert cam.info.serial_number == "FAKE0002"
        with pi.Camera() as cam:
            assert cam.info.serial_number == "FAKE0001"

    def test_serial_mismatch_raises_and_lists_what_was_found(self, fake):
        cam = pi.Camera("NOPE")
        with pytest.raises(pi.Insta360Error, match="NOPE.*FAKE0001"):
            cam.open()
        assert cam.state == pi.CameraState.CLOSED
        assert fake.live_descriptors() == 0

    def test_no_device_raises(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_NO_DEVICES", "1")
        with pytest.raises(pi.Insta360Error, match="no Insta360 camera found"):
            pi.Camera().open()

    def test_open_failure_raises_without_leaking(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_OPEN_FAIL", "1")
        cam = pi.Camera()
        with pytest.raises(pi.Insta360Error, match="open"):
            cam.open()
        assert cam.state == pi.CameraState.CLOSED
        assert fake.outstanding() == dict(descriptors=0, cameras=0, open=0, stream_threads=0)
        monkeypatch.delenv("FAKE_INSTA360_OPEN_FAIL")
        cam.open()
        assert cam.state == pi.CameraState.OPEN
        cam.close()

    def test_error_type(self):
        assert issubclass(pi.Insta360Error, RuntimeError)
        assert pi.Insta360Error.__module__ == "pyinsta360"


NEEDS_OPEN_CAMERA = {
    "set_timeout": lambda c: c.set_timeout(1000),
    "battery_status": lambda c: c.battery_status(),
    "storage_state": lambda c: c.storage_state(),
    "files_list": lambda c: c.files_list(),
    "download_file": lambda c: c.download_file("/a", "/tmp/a"),
    "delete_file": lambda c: c.delete_file("/a"),
    "take_photo": lambda c: c.take_photo(),
    "start_recording": lambda c: c.start_recording(),
    "stop_recording": lambda c: c.stop_recording(),
    "set_video_capture_params": lambda c: c.set_video_capture_params(
        pi.RecordParams(pi.VideoResolution.RES_1920_960P30)
    ),
    "set_exposure_settings": lambda c: c.set_exposure_settings(
        pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO, pi.ExposureSettings()
    ),
    "get_exposure_settings": lambda c: c.get_exposure_settings(
        pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO
    ),
    "sync_local_time": lambda c: c.sync_local_time(1, 0),
    "video_encode_type": lambda c: c.video_encode_type(),
    "current_function_mode": lambda c: c.current_function_mode(),
    "lens_type": lambda c: c.lens_type(),
    "preview_param": lambda c: c.preview_param(),
    "supported_video_resolutions": lambda c: c.supported_video_resolutions(
        pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO
    ),
    "http_base_url": lambda c: c.http_base_url(),
    "start_live_streaming": lambda c: c.start_live_streaming(),
    "stop_live_streaming": lambda c: c.stop_live_streaming(),
}


class TestWrongState:
    @pytest.mark.parametrize("name", sorted(NEEDS_OPEN_CAMERA))
    def test_closed_camera_raises(self, fake, name):
        cam = pi.Camera()
        with pytest.raises(pi.Insta360Error, match=name + r"\(\).*CLOSED"):
            NEEDS_OPEN_CAMERA[name](cam)

    def test_reading_while_closed_is_empty_not_an_error(self, fake):
        cam = pi.Camera()
        assert cam.read_video(timeout=0.01) is None
        assert len(cam.read_imu()) == 0
        assert cam.read_exposure() == []
        assert cam.stream_stats() == {"video_dropped": 0, "imu_dropped": 0}

    def test_stop_without_streaming_raises(self, cam):
        with pytest.raises(pi.Insta360Error, match="stop_live_streaming.*STREAMING"):
            cam.stop_live_streaming()

    def test_double_start_raises(self, cam):
        cam.start_live_streaming()
        with pytest.raises(pi.Insta360Error, match="already STREAMING"):
            cam.start_live_streaming()
        with pytest.raises(pi.Insta360Error, match="already STREAMING"):
            cam.open()
        assert cam.state == pi.CameraState.STREAMING

    @pytest.mark.parametrize(
        "name",
        [
            "battery_status",
            "storage_state",
            "download_file",
            "delete_file",
            "start_recording",
            "set_video_capture_params",
            "set_exposure_settings",
            "sync_local_time",
        ],
    )
    def test_sdk_failure_names_the_method(self, cam, monkeypatch, name):
        monkeypatch.setenv("FAKE_INSTA360_ACTION_FAIL", "1")
        with pytest.raises(pi.Insta360Error, match=name):
            NEEDS_OPEN_CAMERA[name](cam)

    def test_start_failure_leaves_camera_open_and_retryable(self, cam, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_STREAM_FAIL", "1")
        with pytest.raises(pi.Insta360Error, match="start_live_streaming"):
            cam.start_live_streaming()
        assert cam.state == pi.CameraState.OPEN
        assert cam.read_video(timeout=0) is None
        monkeypatch.delenv("FAKE_INSTA360_STREAM_FAIL")
        cam.start_live_streaming()
        assert cam.state == pi.CameraState.STREAMING

    @pytest.mark.parametrize("kwargs", [{"video_queue_size": 0}, {"imu_queue_size": 0}])
    def test_queue_sizes_must_be_positive(self, cam, kwargs):
        with pytest.raises(ValueError):
            cam.start_live_streaming(**kwargs)
        assert cam.state == pi.CameraState.OPEN


class TestStreaming:
    @pytest.fixture(autouse=True)
    def fast_stream(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_VIDEO_HZ", "100")

    def test_video_packets_arrive_in_order_with_their_payload(self, cam):
        cam.start_live_streaming()
        packets = collect_video(cam, 20)
        assert len(packets) == 20
        counters = [packet_counter(p) for p in packets]
        assert counters == list(range(counters[0], counters[0] + 20))
        for p in packets:
            c = packet_counter(p)
            assert len(p) == 128
            assert p.stream_index == 0
            assert p.timestamp == c * 33333
            assert p.data[8:] == bytes([c & 0xFF]) * 120
        assert [p.timestamp for p in packets] == sorted(p.timestamp for p in packets)
        assert cam.stream_stats() == {"video_dropped": 0, "imu_dropped": 0}

    def test_imu_matches_what_the_camera_emitted(self, cam):
        cam.start_live_streaming()
        imu = collect_imu(cam, 0.3)
        assert imu.dtype == GYRO_DTYPE
        assert imu.dtype.itemsize == 56
        assert len(imu) >= 17 * 10
        k = np.arange(len(imu), dtype=np.float64)
        np.testing.assert_array_equal(imu["timestamp"], np.arange(len(imu)) * 2000)
        np.testing.assert_array_equal(imu["ax"], k)
        np.testing.assert_array_equal(imu["ay"], 0.5 * k)
        np.testing.assert_array_equal(imu["az"], np.ones(len(imu)))
        np.testing.assert_array_equal(imu["gx"], 0.25 * k)
        np.testing.assert_array_equal(imu["gy"], -k)
        np.testing.assert_array_equal(imu["gz"], 0.125 * k)

    def test_imu_returns_whole_batches(self, cam):
        cam.start_live_streaming()
        first = cam.read_imu(timeout=1.0)
        assert len(first) % 17 == 0 and len(first) > 0

    def test_read_imu_without_data_is_an_empty_typed_array(self, cam):
        empty = cam.read_imu()
        assert empty.shape == (0,)
        assert empty.dtype == GYRO_DTYPE

    def test_exposure_samples(self, cam):
        cam.start_live_streaming()
        time.sleep(0.1)
        samples = cam.read_exposure()
        assert len(samples) >= 3
        assert samples[0][1] == 0.008
        assert [s[0] for s in samples] == sorted(s[0] for s in samples)
        time.sleep(0.05)
        later = cam.read_exposure()
        assert later and later[0][0] > samples[-1][0]

    def test_negative_timeouts_are_rejected(self, cam):
        with pytest.raises(ValueError):
            cam.read_video(timeout=-1)
        with pytest.raises(ValueError):
            cam.read_imu(timeout=-1)

    def test_stop_then_restart_resets_stats_and_buffers(self, cam):
        cam.start_live_streaming(video_queue_size=2)
        time.sleep(0.1)
        assert cam.stream_stats()["video_dropped"] > 0
        cam.stop_live_streaming()
        assert cam.state == pi.CameraState.OPEN
        assert cam.read_video(timeout=0) is None
        assert len(cam.read_imu()) == 0
        cam.start_live_streaming()
        assert cam.stream_stats() == {"video_dropped": 0, "imu_dropped": 0}
        first = cam.read_video(timeout=1.0)
        assert packet_counter(first) == 0

    def test_overflow_drops_oldest_and_counts(self, cam):
        cam.start_live_streaming(video_queue_size=4, imu_queue_size=50)
        time.sleep(0.5)
        stats = cam.stream_stats()
        assert stats["video_dropped"] > 10
        assert stats["imu_dropped"] > 100

        first = cam.read_video(timeout=0)
        counters = [packet_counter(first)]
        while True:
            p = cam.read_video(timeout=0)
            if p is None:
                break
            counters.append(packet_counter(p))
        assert counters[0] >= stats["video_dropped"]
        assert counters == list(range(counters[0], counters[0] + len(counters)))

        imu = cam.read_imu()
        assert 0 < len(imu) <= 50 + 17
        assert imu["ax"][0] >= stats["imu_dropped"]
        np.testing.assert_array_equal(np.diff(imu["ax"]), np.ones(len(imu) - 1))


class TestBlockedReaders:
    @pytest.mark.parametrize("how", ["close", "stop_live_streaming"])
    def test_reader_blocked_in_read_video_is_woken_promptly(self, fake, monkeypatch, how):
        # One packet per 10 s, so the reader is guaranteed to be blocked.
        monkeypatch.setenv("FAKE_INSTA360_VIDEO_HZ", "0.1")
        cam = pi.Camera()
        cam.open()
        cam.start_live_streaming()
        result = []
        reader = threading.Thread(target=lambda: result.append(cam.read_video(timeout=None)))
        reader.start()
        time.sleep(0.2)
        assert reader.is_alive()

        started = time.monotonic()
        getattr(cam, how)()
        elapsed = time.monotonic() - started
        reader.join(timeout=2.0)

        assert not reader.is_alive()
        assert result == [None]
        assert elapsed < 1.0
        cam.close()

    def test_other_python_threads_run_while_a_reader_blocks(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_VIDEO_HZ", "0.1")
        with pi.Camera() as cam:
            cam.start_live_streaming()
            reader = threading.Thread(target=lambda: cam.read_video(timeout=0.5))
            reader.start()
            ticks = 0
            while reader.is_alive():
                ticks += 1
                time.sleep(0.01)
            assert ticks > 10

    def test_read_video_timeout_returns_none(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_VIDEO_HZ", "0.1")
        with pi.Camera() as cam:
            cam.start_live_streaming()
            started = time.monotonic()
            assert cam.read_video(timeout=0.15) is None
            assert 0.1 < time.monotonic() - started < 1.0

    def test_keyboard_interrupt_reaches_a_blocked_main_thread(self, fake, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_VIDEO_HZ", "0.1")
        with pi.Camera() as cam:
            cam.start_live_streaming()
            timer = threading.Timer(0.2, lambda: os.kill(os.getpid(), signal.SIGINT))
            timer.start()
            started = time.monotonic()
            with pytest.raises(KeyboardInterrupt):
                cam.read_video(timeout=None)
            assert time.monotonic() - started < 2.0
            timer.join()


class TestDownload:
    def test_progress_callback_is_invoked_from_the_sdk_thread(self, cam, tmp_path):
        calls = []
        threads = []

        def progress(done, total):
            calls.append((done, total))
            threads.append(threading.current_thread())

        local = tmp_path / "a.insv"
        cam.download_file("/DCIM/a.insv", str(local), progress)
        assert calls == [(i * 100, 1000) for i in range(1, 11)]
        assert local.read_text() == "fake-download:/DCIM/a.insv"
        assert all(isinstance(t, threading.Thread) for t in threads)

    def test_without_callback(self, cam, tmp_path):
        local = tmp_path / "b.insv"
        cam.download_file("/DCIM/b.insv", str(local))
        assert local.exists()

    def test_missing_remote_raises(self, cam, tmp_path):
        with pytest.raises(pi.Insta360Error, match="download_file"):
            cam.download_file("/DCIM/missing.insv", str(tmp_path / "x"))

    def test_callback_error_is_raised_after_the_download_returns(self, cam, tmp_path):
        calls = []

        def progress(done, total):
            calls.append(done)
            raise ValueError("from callback")

        with pytest.raises(ValueError, match="from callback"):
            cam.download_file("/DCIM/a.insv", str(tmp_path / "a"), progress)
        assert calls == [100]

    def test_callback_must_be_callable(self, cam, tmp_path):
        with pytest.raises(TypeError):
            cam.download_file("/a", str(tmp_path / "a"), 42)

    def test_callable_is_released_without_the_gil_held_by_the_sdk(self, cam, tmp_path):
        import gc
        import weakref

        class Sink:
            def __call__(self, done, total):
                pass

        sink = Sink()
        ref = weakref.ref(sink)
        cam.download_file("/a", str(tmp_path / "a"), sink)
        del sink
        gc.collect()
        assert ref() is None


class TestCameraAccessors:
    def test_battery_and_storage(self, cam):
        b = cam.battery_status()
        assert (b.power_type, b.battery_level, b.battery_scale) == (pi.PowerType.BATTERY, 87, 100)
        s = cam.storage_state()
        assert (s.state, s.free_space, s.total_space) == (pi.CardState.STOR_CS_PASS, 1000, 2000)
        assert s.location == pi.CardLocation.STOR_CL_CAMERA_SD
        s = cam.storage_state(pi.CardLocation.STOR_CL_CAMERA_INNER)
        assert s.location == pi.CardLocation.STOR_CL_CAMERA_INNER

    def test_files_and_media(self, cam):
        assert cam.files_list() == [
            "/DCIM/Camera01/VID_FAKE_001.insv",
            "/DCIM/Camera01/IMG_FAKE_002.insp",
        ]
        photo = cam.take_photo(pi.RawCaptureType.Off, timeout_ms=500)
        assert photo
        assert photo.origin_urls == ["/DCIM/Camera01/IMG_FAKE_003.insp"]
        assert photo.lrv_urls == []

        assert not cam.stop_recording()
        cam.start_recording()
        video = cam.stop_recording()
        assert video.origin_urls == ["/DCIM/Camera01/VID_FAKE_004.insv"]
        assert video.lrv_urls == ["/DCIM/Camera01/LRV_FAKE_004.insv"]
        cam.delete_file(video.origin_urls[0])

    def test_failed_photo_is_a_falsy_media_url(self, cam, monkeypatch):
        monkeypatch.setenv("FAKE_INSTA360_ACTION_FAIL", "1")
        assert not cam.take_photo()

    def test_sync_local_time_passes_both_arguments(self, cam, fake):
        cam.sync_local_time(1_700_000_000, -18000)
        assert fake.last_sync() == (1_700_000_000, -18000)

    def test_exposure_settings_round_trip_by_value(self, cam):
        mode = pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO
        settings = pi.ExposureSettings(
            exposure_mode=pi.ExposureMode.SHUTTER_PRIORITY, shutter_speed=1 / 120, ev_bias=-1
        )
        cam.set_exposure_settings(mode, settings)
        settings.iso = 800
        stored = cam.get_exposure_settings(mode)
        assert stored.exposure_mode == pi.ExposureMode.SHUTTER_PRIORITY
        assert stored.shutter_speed == 1 / 120
        assert stored.ev_bias == -1
        assert stored.iso == 0

    def test_exposure_settings_properties(self):
        s = pi.ExposureSettings()
        s.iso = 400
        s.video_iso_top_limit = 1600
        s.exposure_mode = pi.ExposureMode.MANUAL
        assert (s.iso, s.video_iso_top_limit, s.exposure_mode) == (400, 1600, pi.ExposureMode.MANUAL)

    def test_preview_param_snapshot(self, cam):
        p = cam.preview_param()
        assert p.camera_name == "FakeX3"
        assert p.encode_type == pi.VideoEncodeType.H265
        assert (p.delay_timestamp, p.sweep_time) == (5, 6)
        assert (p.acceleration_range, p.gyro_range) == (7, 8)
        assert p.calibrations == ["calib-a", "calib-b"]
        assert p.crop == dict(
            src_width=3840, src_height=1920, dst_width=640, dst_height=320, offset_x=10, offset_y=-4
        )

    def test_descriptive_queries(self, cam):
        assert cam.video_encode_type() == pi.VideoEncodeType.H265
        assert cam.current_function_mode() == pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO
        assert cam.lens_type() == pi.CameraLensType.PanoDefault
        assert cam.http_base_url() == "http://fake.insta360:80"
        assert cam.supported_video_resolutions(
            pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO
        ) == [pi.VideoResolution.RES_1920_960P30, pi.VideoResolution.RES_3840_1920P30]
        cam.set_timeout(5000)
        cam.set_video_capture_params(pi.RecordParams(pi.VideoResolution.RES_3840_1920P30, bitrate=1))
        cam.set_video_capture_params(
            pi.RecordParams(pi.VideoResolution.RES_3840_1920P30),
            pi.CameraFunctionMode.FUNCTION_MODE_NORMAL_VIDEO,
        )


class TestValueTypes:
    def test_live_stream_param_defaults_follow_the_bridge(self):
        p = pi.LiveStreamParam()
        assert p.video_resolution == pi.VideoResolution.RES_1920_960P30
        assert p.lrv_video_resolution == pi.VideoResolution.RES_1920_960P30
        assert (p.enable_audio, p.enable_video, p.enable_gyro, p.using_lrv) == (True,) * 4
        assert (p.audio_samplerate, p.audio_bitrate) == (48000, 128000)
        assert (p.video_bitrate, p.lrv_video_bitrate) == (10 * 1024 * 1024, 1024 * 1024)

    def test_live_stream_param_is_writable_and_accepted_by_streaming(self, cam):
        p = pi.LiveStreamParam(using_lrv=False, enable_audio=False)
        p.video_bitrate = 8 * 1024 * 1024
        assert not p.using_lrv and not p.enable_audio
        cam.start_live_streaming(p)
        assert cam.state == pi.CameraState.STREAMING

    def test_record_params(self):
        r = pi.RecordParams(pi.VideoResolution.RES_3840_1920P30, bitrate=5, record_duration=9)
        assert (r.bitrate, r.accelerate_frequency, r.record_duration) == (5, 0, 9)
        r.accelerate_frequency = 3
        assert r.accelerate_frequency == 3

    def test_battery_and_storage_are_default_constructible(self):
        assert pi.BatteryStatus().battery_level == 0
        assert pi.StorageStatus().free_space == 0

    def test_enum_aliases_share_values(self):
        assert pi.VideoResolution.RES_4KP30 == pi.VideoResolution.RES_3840_1920P30
        assert int(pi.VideoResolution.RES_1920_960P30) == 2
        assert pi.VideoResolution(2) == pi.VideoResolution.RES_1920_960P30
        assert "RES_4KP30" in pi.VideoResolution.__members__
