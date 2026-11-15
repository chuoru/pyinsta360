#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>

#include "py_camera.h"

namespace py = pybind11;
using NoGil = py::call_guard<py::gil_scoped_release>;
using ins_camera::GyroData;

// The structured dtype exposes the SDK struct's memory as-is, so any layout
// drift must fail the build rather than corrupt samples.
static_assert(std::is_standard_layout<GyroData>::value, "GyroData must be standard layout");
static_assert(sizeof(GyroData) == 56, "GyroData size changed");
static_assert(offsetof(GyroData, timestamp) == 0 && offsetof(GyroData, ax) == 8 &&
                  offsetof(GyroData, ay) == 16 && offsetof(GyroData, az) == 24 &&
                  offsetof(GyroData, gx) == 32 && offsetof(GyroData, gy) == 40 &&
                  offsetof(GyroData, gz) == 48,
              "GyroData layout changed");

namespace pyinsta360 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::chrono::milliseconds kSignalSlice{100};
constexpr double kMaxTimeoutSeconds = 1e9;

void BindEnums(py::module_& m) {
#include "enums_gen.inc"
}

// Blocks with the GIL released, but wakes every kSignalSlice to let Python
// deliver KeyboardInterrupt. `timeout` of nullopt waits indefinitely.
template <typename WaitFn>
WaitResult SlicedWait(std::optional<double> timeout, WaitFn wait) {
  if (timeout && !(*timeout >= 0.0)) throw py::value_error("timeout must be >= 0");
  const bool bounded = timeout.has_value();
  const auto deadline =
      bounded ? Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                   std::chrono::duration<double>(
                                       std::min(*timeout, kMaxTimeoutSeconds)))
              : Clock::time_point::max();
  while (true) {
    auto slice = std::chrono::duration_cast<std::chrono::nanoseconds>(kSignalSlice);
    if (bounded) {
      const auto remaining = std::max(deadline - Clock::now(), Clock::duration::zero());
      slice = std::min(slice, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining));
    }
    WaitResult result;
    {
      py::gil_scoped_release nogil;
      result = wait(slice);
    }
    if (result != WaitResult::kTimeout) return result;
    if (PyErr_CheckSignals() != 0) throw py::error_already_set();
    if (bounded && Clock::now() >= deadline) return WaitResult::kTimeout;
  }
}

std::optional<VideoPacket> ReadVideo(PyCamera& cam, std::optional<double> timeout) {
  VideoPacket packet;
  const auto result = SlicedWait(timeout, [&](std::chrono::nanoseconds slice) {
    return cam.queues().WaitVideo(slice, packet);
  });
  if (result != WaitResult::kReady) return std::nullopt;
  return packet;
}

py::array ReadImu(PyCamera& cam, double timeout) {
  std::vector<GyroData> samples;
  SlicedWait(timeout, [&](std::chrono::nanoseconds slice) {
    return cam.queues().WaitImu(slice, samples);
  });
  const auto dtype = py::dtype::of<GyroData>();
  if (samples.empty()) return py::array(dtype, std::vector<py::ssize_t>{0});
  auto* owned = new std::vector<GyroData>(std::move(samples));
  py::capsule base(owned, [](void* p) { delete static_cast<std::vector<GyroData>*>(p); });
  return py::array(dtype, std::vector<py::ssize_t>{static_cast<py::ssize_t>(owned->size())},
                   std::vector<py::ssize_t>{static_cast<py::ssize_t>(sizeof(GyroData))},
                   owned->data(), base);
}

// Python-side state of one download_file() call. Touched only with the GIL held.
struct ProgressRelay {
  py::object callback;
  std::exception_ptr error;
};

void DownloadFile(PyCamera& cam, const std::string& remote, const std::string& local,
                  py::object progress) {
  std::shared_ptr<ProgressRelay> relay;
  ProgressCallback callback;
  if (!progress.is_none()) {
    if (!PyCallable_Check(progress.ptr())) throw py::type_error("progress must be callable");
    // The SDK may keep or copy the std::function on its own thread, so the last
    // reference can drop without the GIL.
    relay.reset(new ProgressRelay{progress, nullptr}, [](ProgressRelay* r) {
      py::gil_scoped_acquire gil;
      delete r;
    });
    callback = [relay](int64_t downloaded, int64_t total) {
      py::gil_scoped_acquire gil;
      if (relay->error) return;
      try {
        relay->callback(downloaded, total);
      } catch (py::error_already_set&) {
        // Unwinding through SDK frames is unsafe; re-raised after the call returns.
        relay->error = std::current_exception();
      }
    };
  }
  try {
    py::gil_scoped_release nogil;
    cam.DownloadFile(remote, local, callback);
  } catch (...) {
    if (relay && relay->error) std::rethrow_exception(relay->error);
    throw;
  }
  if (relay && relay->error) std::rethrow_exception(relay->error);
}

void BindValueTypes(py::module_& m) {
  py::class_<ins_camera::BatteryStatus>(m, "BatteryStatus")
      .def(py::init<>())
      .def_readwrite("power_type", &ins_camera::BatteryStatus::power_type)
      .def_readwrite("battery_level", &ins_camera::BatteryStatus::battery_level)
      .def_readwrite("battery_scale", &ins_camera::BatteryStatus::battery_scale)
      .def("__repr__", [](const ins_camera::BatteryStatus& s) {
        return "BatteryStatus(power_type=" + std::to_string(static_cast<int>(s.power_type)) +
               ", battery_level=" + std::to_string(s.battery_level) +
               ", battery_scale=" + std::to_string(s.battery_scale) + ")";
      });

  py::class_<ins_camera::StorageStatus>(m, "StorageStatus")
      .def(py::init<>())
      .def_readwrite("state", &ins_camera::StorageStatus::state)
      .def_readwrite("free_space", &ins_camera::StorageStatus::free_space)
      .def_readwrite("total_space", &ins_camera::StorageStatus::total_space)
      .def_readwrite("location", &ins_camera::StorageStatus::location)
      .def("__repr__", [](const ins_camera::StorageStatus& s) {
        return "StorageStatus(state=" + std::to_string(static_cast<int>(s.state)) +
               ", free_space=" + std::to_string(s.free_space) +
               ", total_space=" + std::to_string(s.total_space) + ")";
      });

  py::class_<ins_camera::LiveStreamParam>(m, "LiveStreamParam",
                                           "Live preview stream settings. Defaults are the SDK's, "
                                           "except both resolutions, which default to "
                                           "RES_1920_960P30 as in the reference bridge.")
      .def(py::init([](bool enable_audio, bool enable_video, uint32_t audio_samplerate,
                       uint32_t audio_bitrate, uint32_t video_bitrate,
                       ins_camera::VideoResolution video_resolution, uint32_t lrv_video_bitrate,
                       ins_camera::VideoResolution lrv_video_resolution, bool enable_gyro,
                       bool using_lrv) {
             ins_camera::LiveStreamParam p = DefaultLiveStreamParam();
             p.enable_audio = enable_audio;
             p.enable_video = enable_video;
             p.audio_samplerate = audio_samplerate;
             p.audio_bitrate = audio_bitrate;
             p.video_bitrate = video_bitrate;
             p.video_resolution = video_resolution;
             p.lrv_video_bitrate = lrv_video_bitrate;
             p.lrv_video_resulution = lrv_video_resolution;
             p.enable_gyro = enable_gyro;
             p.using_lrv = using_lrv;
             return p;
           }),
           py::arg("enable_audio") = true, py::arg("enable_video") = true,
           py::arg("audio_samplerate") = 48000u, py::arg("audio_bitrate") = 128000u,
           py::arg("video_bitrate") = 1024u * 1024u * 10u,
           py::arg("video_resolution") = ins_camera::VideoResolution::RES_1920_960P30,
           py::arg("lrv_video_bitrate") = 1024u * 1024u,
           py::arg("lrv_video_resolution") = ins_camera::VideoResolution::RES_1920_960P30,
           py::arg("enable_gyro") = true, py::arg("using_lrv") = true)
      .def_readwrite("enable_audio", &ins_camera::LiveStreamParam::enable_audio)
      .def_readwrite("enable_video", &ins_camera::LiveStreamParam::enable_video)
      .def_readwrite("audio_samplerate", &ins_camera::LiveStreamParam::audio_samplerate)
      .def_readwrite("audio_bitrate", &ins_camera::LiveStreamParam::audio_bitrate)
      .def_readwrite("video_bitrate", &ins_camera::LiveStreamParam::video_bitrate)
      .def_readwrite("video_resolution", &ins_camera::LiveStreamParam::video_resolution)
      .def_readwrite("lrv_video_bitrate", &ins_camera::LiveStreamParam::lrv_video_bitrate)
      .def_readwrite("lrv_video_resolution", &ins_camera::LiveStreamParam::lrv_video_resulution)
      .def_readwrite("enable_gyro", &ins_camera::LiveStreamParam::enable_gyro)
      .def_readwrite("using_lrv", &ins_camera::LiveStreamParam::using_lrv);

  py::class_<ins_camera::RecordParams>(m, "RecordParams")
      .def(py::init([](ins_camera::VideoResolution resolution, int32_t bitrate,
                       int accelerate_frequency, uint32_t record_duration) {
             ins_camera::RecordParams p{};
             p.resolution = resolution;
             p.bitrate = bitrate;
             p.accelerate_fequency = accelerate_frequency;
             p.record_duration = record_duration;
             return p;
           }),
           py::arg("resolution"), py::arg("bitrate") = 0, py::arg("accelerate_frequency") = 0,
           py::arg("record_duration") = 0u)
      .def_readwrite("resolution", &ins_camera::RecordParams::resolution)
      .def_readwrite("bitrate", &ins_camera::RecordParams::bitrate)
      .def_readwrite("accelerate_frequency", &ins_camera::RecordParams::accelerate_fequency)
      .def_readwrite("record_duration", &ins_camera::RecordParams::record_duration);

  py::class_<PreviewSnapshot>(m, "PreviewParam", "Read-only snapshot of the camera's preview parameters.")
      .def_readonly("camera_name", &PreviewSnapshot::camera_name)
      .def_readonly("encode_type", &PreviewSnapshot::encode_type)
      .def_readonly("delay_timestamp", &PreviewSnapshot::delay_timestamp)
      .def_readonly("sweep_time", &PreviewSnapshot::sweep_time)
      .def_readonly("acceleration_range", &PreviewSnapshot::acceleration_range)
      .def_readonly("gyro_range", &PreviewSnapshot::gyro_range)
      .def_readonly("calibrations", &PreviewSnapshot::calibrations)
      .def_property_readonly("crop", [](const PreviewSnapshot& p) {
        py::dict crop;
        crop["src_width"] = p.crop_src_width;
        crop["src_height"] = p.crop_src_height;
        crop["dst_width"] = p.crop_dst_width;
        crop["dst_height"] = p.crop_dst_height;
        crop["offset_x"] = p.crop_offset_x;
        crop["offset_y"] = p.crop_offset_y;
        return crop;
      });

  py::class_<MediaUrl>(m, "MediaUrl", "Camera-side URLs of captured media. Falsy when empty.")
      .def_readonly("origin_urls", &MediaUrl::origin_urls)
      .def_readonly("lrv_urls", &MediaUrl::lrv_urls)
      .def("__bool__", &MediaUrl::Any)
      .def("__repr__", [](const MediaUrl& u) {
        return "MediaUrl(origin_urls=" + std::string(py::repr(py::cast(u.origin_urls))) +
               ", lrv_urls=" + std::string(py::repr(py::cast(u.lrv_urls))) + ")";
      });

  using ins_camera::ExposureSettings;
  py::class_<ExposureSettings, std::shared_ptr<ExposureSettings>>(m, "ExposureSettings")
      .def(py::init([](std::optional<ins_camera::PhotographyOptions_ExposureMode> exposure_mode,
                       std::optional<int32_t> iso, std::optional<double> shutter_speed,
                       std::optional<int32_t> ev_bias,
                       std::optional<int32_t> video_iso_top_limit) {
             auto s = std::make_shared<ExposureSettings>();
             if (exposure_mode) s->SetExposureMode(*exposure_mode);
             if (iso) s->SetIso(*iso);
             if (shutter_speed) s->SetShutterSpeed(*shutter_speed);
             if (ev_bias) s->SetEVBias(*ev_bias);
             if (video_iso_top_limit) s->SetVideoISOTopLimit(*video_iso_top_limit);
             return s;
           }),
           py::arg("exposure_mode") = py::none(), py::arg("iso") = py::none(),
           py::arg("shutter_speed") = py::none(), py::arg("ev_bias") = py::none(),
           py::arg("video_iso_top_limit") = py::none())
      .def_property("exposure_mode", &ExposureSettings::ExposureMode,
                    &ExposureSettings::SetExposureMode)
      .def_property("iso", &ExposureSettings::Iso, &ExposureSettings::SetIso)
      .def_property("shutter_speed", &ExposureSettings::ShutterSpeed,
                    &ExposureSettings::SetShutterSpeed)
      .def_property("ev_bias", &ExposureSettings::EVBias, &ExposureSettings::SetEVBias)
      .def_property("video_iso_top_limit", &ExposureSettings::VideoISOTopLimit,
                    &ExposureSettings::SetVideoISOTopLimit);

  py::class_<DeviceInfo>(m, "DeviceInfo")
      .def_readonly("camera_type", &DeviceInfo::camera_type)
      .def_readonly("serial_number", &DeviceInfo::serial_number)
      .def_readonly("camera_name", &DeviceInfo::camera_name)
      .def_readonly("fw_version", &DeviceInfo::fw_version)
      .def_readonly("connection_type", &DeviceInfo::connection_type)
      .def("__repr__", [](const DeviceInfo& d) {
        return "DeviceInfo(camera_name='" + d.camera_name + "', serial_number='" +
               d.serial_number + "', fw_version='" + d.fw_version + "')";
      });

  py::class_<VideoPacket>(m, "VideoPacket",
                          "One H.264/H.265 chunk from the camera. `timestamp` is in raw SDK units "
                          "and is not comparable with GyroData timestamps.")
      .def_property_readonly("data", [](const VideoPacket& p) { return py::bytes(p.data); },
                             "Compressed bitstream as bytes.")
      .def_readonly("timestamp", &VideoPacket::timestamp)
      .def_readonly("stream_index", &VideoPacket::stream_index)
      .def("__len__", [](const VideoPacket& p) { return p.data.size(); })
      .def("__repr__", [](const VideoPacket& p) {
        return "VideoPacket(size=" + std::to_string(p.data.size()) +
               ", timestamp=" + std::to_string(p.timestamp) +
               ", stream_index=" + std::to_string(p.stream_index) + ")";
      });
}

void BindCamera(py::module_& m) {
  py::enum_<CameraState>(m, "CameraState")
      .value("CLOSED", CameraState::kClosed)
      .value("OPEN", CameraState::kOpen)
      .value("STREAMING", CameraState::kStreaming);

  py::class_<PyCamera>(m, "Camera", R"doc(
An Insta360 camera session.

Methods called in a state that does not allow them raise Insta360Error. All
blocking SDK calls release the GIL. close() may be called from any thread,
including while another thread is blocked in read_video().

Stream data is raw SDK data. IMU accelerations are in g (not m/s^2) and
gyro rates in rad/s. Video and IMU timestamps are in unspecified units and come
from clocks that are not comparable with each other; stamp samples with your
own receipt time to align the streams.
)doc")
      .def(py::init<std::optional<std::string>>(), py::arg("serial") = py::none())
      .def("open", &PyCamera::Open, NoGil(),
           "Discover cameras and open the first one (or the one matching `serial`).")
      .def("close", &PyCamera::Close, NoGil(), "Stop streaming if needed and close. Idempotent.")
      .def("__enter__",
           [](py::object self) {
             PyCamera& cam = self.cast<PyCamera&>();
             {
               py::gil_scoped_release nogil;
               cam.Open();
             }
             return self;
           })
      .def("__exit__",
           [](PyCamera& cam, py::object, py::object, py::object) {
             {
               py::gil_scoped_release nogil;
               cam.Close();
             }
             return false;
           })
      .def("__repr__",
           [](const PyCamera& cam) {
             return std::string("<pyinsta360.Camera state=") + StateName(cam.state()) + ">";
           })
      .def_property_readonly("state", &PyCamera::state, "CameraState: CLOSED, OPEN or STREAMING.")
      .def_property_readonly("info", py::cpp_function(&PyCamera::Info, NoGil()),
                             "DeviceInfo of the open camera, or None when closed.")
      .def("is_connected", &PyCamera::IsConnected, NoGil())
      .def("set_timeout", &PyCamera::SetTimeout, py::arg("timeout_ms"), NoGil())
      .def("battery_status", &PyCamera::BatteryStatus, NoGil())
      .def("storage_state", &PyCamera::StorageState,
           py::arg("location") = ins_camera::CardLocation::STOR_CL_AUTO, NoGil())
      .def("files_list", &PyCamera::FilesList, NoGil())
      .def("download_file", &DownloadFile, py::arg("remote"), py::arg("local"),
           py::arg("progress") = py::none(),
           "Download a camera file. `progress(downloaded, total)` is called from an SDK thread "
           "with the GIL held. An exception it raises is re-raised once the download returns; "
           "the download itself is not cancelled.")
      .def("delete_file", &PyCamera::DeleteFile, py::arg("remote"), NoGil())
      .def("take_photo", &PyCamera::TakePhoto,
           py::arg("raw_type") = ins_camera::RawCaptureType::PureShot, py::arg("timeout_ms") = 0,
           NoGil(), "Returns a falsy MediaUrl if the capture failed.")
      .def("start_recording", &PyCamera::StartRecording, NoGil())
      .def("stop_recording", &PyCamera::StopRecording, NoGil())
      .def("set_video_capture_params", &PyCamera::SetVideoCaptureParams, py::arg("params"),
           py::arg("mode") = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO, NoGil())
      .def("set_exposure_settings", &PyCamera::SetExposureSettings, py::arg("mode"),
           py::arg("settings"), NoGil())
      .def("get_exposure_settings", &PyCamera::GetExposureSettings, py::arg("mode"), NoGil())
      .def("sync_local_time", &PyCamera::SyncLocalTime, py::arg("utc_seconds"),
           py::arg("offset_seconds"), NoGil())
      .def("video_encode_type", &PyCamera::VideoEncodeType, NoGil())
      .def("current_function_mode", &PyCamera::CurrentFunctionMode, NoGil())
      .def("lens_type", &PyCamera::LensType, NoGil())
      .def("preview_param", &PyCamera::PreviewParam, NoGil())
      .def("supported_video_resolutions", &PyCamera::SupportedVideoResolutions, py::arg("mode"),
           NoGil())
      .def("http_base_url", &PyCamera::HttpBaseUrl, NoGil())
      .def(
          "start_live_streaming",
          [](PyCamera& cam, std::optional<ins_camera::LiveStreamParam> param,
             size_t video_queue_size, size_t imu_queue_size) {
            py::gil_scoped_release nogil;
            cam.StartLiveStreaming(param ? *param : DefaultLiveStreamParam(), video_queue_size,
                                   imu_queue_size);
          },
          py::arg("param") = py::none(), py::arg("video_queue_size") = 64,
          py::arg("imu_queue_size") = 20000,
          "Start the preview stream. When a queue is full its oldest entries are dropped "
          "and counted in stream_stats().")
      .def("stop_live_streaming", &PyCamera::StopLiveStreaming, NoGil())
      .def(
          "read_video",
          [](PyCamera& cam, std::optional<double> timeout) { return ReadVideo(cam, timeout); },
          py::arg("timeout") = py::none(),
          "Next VideoPacket, blocking up to `timeout` seconds (forever if None). Returns None "
          "on timeout or when not streaming; a blocked reader is woken with None by "
          "stop_live_streaming() or close().")
      .def(
          "read_imu", [](PyCamera& cam, double timeout) { return ReadImu(cam, timeout); },
          py::arg("timeout") = 0.0,
          "All pending IMU samples as a structured array (fields timestamp, ax, ay, az, gx, gy, "
          "gz; layout identical to the SDK's GyroData). Waits up to `timeout` seconds for at "
          "least one sample. Acceleration is in g, gyro in rad/s, timestamp in raw SDK units.")
      .def("read_exposure", [](PyCamera& cam) {
        py::gil_scoped_release nogil;
        std::vector<std::pair<double, double>> out;
        for (const auto& e : cam.queues().TakeExposure()) {
          out.emplace_back(e.timestamp, e.exposure_time);
        }
        return out;
      }, "Pending (timestamp, exposure_time) pairs.")
      .def("stream_stats", [](PyCamera& cam) {
        const StreamStats s = cam.queues().Stats();
        py::dict d;
        d["video_dropped"] = s.video_dropped;
        d["imu_dropped"] = s.imu_dropped;
        return d;
      }, "Packets and samples dropped to overflow since the stream started.");
}

}  // namespace
}  // namespace pyinsta360

PYBIND11_MODULE(_sdk, m) {
  using namespace pyinsta360;
  m.doc() = "Low-level bindings for the Insta360 CameraSDK.";

  PYBIND11_NUMPY_DTYPE(GyroData, timestamp, ax, ay, az, gx, gy, gz);

  py::register_exception<Insta360Error>(m, "Insta360Error", PyExc_RuntimeError)
      .attr("__module__") = "pyinsta360";

  BindEnums(m);
  BindValueTypes(m);
  BindCamera(m);

  m.def("sdk_version", &ins_camera::GetSDKVersion, NoGil());
  m.def("sdk_version_major", &ins_camera::GetSDKVersionMajor, NoGil());
  m.def("set_log_level", &ins_camera::SetLogLevel, py::arg("level"), NoGil());
  m.def("set_log_path", &ins_camera::SetLogPath, py::arg("path"), NoGil());
  m.def("discover", &Discover, NoGil(),
        "List attached cameras as DeviceInfo. Opens nothing; descriptors are freed immediately.");
}
