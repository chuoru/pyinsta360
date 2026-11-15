#include "py_camera.h"

#include <algorithm>
#include <utility>

namespace pyinsta360 {

namespace {

// Frees the descriptors on every path. They must outlive Camera::Open(), so the
// guard is declared before the camera built from them.
class DescriptorGuard {
 public:
  DescriptorGuard(ins_camera::DeviceDiscovery& discovery,
                  std::vector<ins_camera::DeviceDescriptor>& descriptors)
      : discovery_(discovery), descriptors_(descriptors) {}
  ~DescriptorGuard() {
    try {
      discovery_.FreeDeviceDescriptors(descriptors_);
    } catch (...) {
    }
  }
  DescriptorGuard(const DescriptorGuard&) = delete;
  DescriptorGuard& operator=(const DescriptorGuard&) = delete;

 private:
  ins_camera::DeviceDiscovery& discovery_;
  std::vector<ins_camera::DeviceDescriptor>& descriptors_;
};

DeviceInfo ToDeviceInfo(const ins_camera::DeviceDescriptor& d) {
  return DeviceInfo{d.camera_type, d.serial_number, d.camera_name, d.fw_version,
                    d.info.connection_type};
}

MediaUrl ToMediaUrl(const ins_camera::MediaUrl& url) {
  return MediaUrl{url.OriginUrls(), url.LRVUrls()};
}

[[noreturn]] void Fail(const char* method) {
  throw Insta360Error(std::string(method) + "() failed: the SDK reported failure");
}

}  // namespace

const char* StateName(CameraState state) {
  switch (state) {
    case CameraState::kClosed: return "CLOSED";
    case CameraState::kOpen: return "OPEN";
    case CameraState::kStreaming: return "STREAMING";
  }
  return "?";
}

std::vector<DeviceInfo> Discover() {
  ins_camera::DeviceDiscovery discovery;
  auto descriptors = discovery.GetAvailableDevices();
  DescriptorGuard guard(discovery, descriptors);
  std::vector<DeviceInfo> out;
  out.reserve(descriptors.size());
  for (const auto& d : descriptors) out.push_back(ToDeviceInfo(d));
  return out;
}

ins_camera::LiveStreamParam DefaultLiveStreamParam() {
  ins_camera::LiveStreamParam param;
  param.video_resolution = ins_camera::VideoResolution::RES_1920_960P30;
  param.lrv_video_resulution = ins_camera::VideoResolution::RES_1920_960P30;
  return param;
}

PyCamera::PyCamera(std::optional<std::string> serial)
    : serial_(std::move(serial)),
      queues_(std::make_shared<StreamQueues>()),
      delegate_(queues_) {}

PyCamera::~PyCamera() {
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();
  } catch (...) {
  }
}

ins_camera::Camera& PyCamera::Connected(const char* method) {
  if (state_ == CameraState::kClosed) {
    throw Insta360Error(std::string(method) + "() requires an open camera (state is CLOSED)");
  }
  return *camera_;
}

void PyCamera::Open() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != CameraState::kClosed) {
    throw Insta360Error(std::string("open() called on a camera that is already ") +
                        StateName(state_));
  }
  ins_camera::DeviceDiscovery discovery;
  auto descriptors = discovery.GetAvailableDevices();
  DescriptorGuard guard(discovery, descriptors);

  auto match = descriptors.begin();
  if (serial_) {
    match = std::find_if(descriptors.begin(), descriptors.end(),
                         [this](const auto& d) { return d.serial_number == *serial_; });
  }
  if (match == descriptors.end()) {
    std::string msg = serial_ ? "no Insta360 camera with serial '" + *serial_ + "' found"
                              : "no Insta360 camera found";
    if (serial_ && !descriptors.empty()) {
      msg += " (found:";
      for (const auto& d : descriptors) msg += " '" + d.serial_number + "'";
      msg += ")";
    } else {
      msg += ". Is it connected over USB and in Android mode?";
    }
    throw Insta360Error(msg);
  }

  // `camera` is destroyed before `guard` frees the descriptor it was built from.
  auto camera = std::make_unique<ins_camera::Camera>(match->info);
  if (!camera->Open()) {
    throw Insta360Error("open() failed: the SDK could not open camera '" + match->serial_number +
                        "'");
  }
  info_ = ToDeviceInfo(*match);
  camera_ = std::move(camera);
  state_ = CameraState::kOpen;
}

void PyCamera::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  CloseLocked();
}

void PyCamera::CloseLocked() {
  if (state_ == CameraState::kClosed) return;
  if (state_ == CameraState::kStreaming) {
    // Waking readers first keeps close() prompt even if the SDK is slow to stop.
    queues_->End();
    // A failed stop cannot be retried once we are closing, so it is ignored.
    camera_->StopLiveStreaming();
  }
  camera_->Close();
  camera_.reset();
  info_.reset();
  state_ = CameraState::kClosed;
}

std::optional<DeviceInfo> PyCamera::Info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return info_;
}

bool PyCamera::IsConnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_ != CameraState::kClosed && camera_->IsConnected();
}

void PyCamera::SetTimeout(int timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  Connected("set_timeout").SetTimeout(timeout_ms);
}

ins_camera::BatteryStatus PyCamera::BatteryStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  ins_camera::BatteryStatus status{};
  if (!Connected("battery_status").GetBatteryStatus(status)) Fail("battery_status");
  return status;
}

ins_camera::StorageStatus PyCamera::StorageState(ins_camera::CardLocation location) {
  std::lock_guard<std::mutex> lock(mutex_);
  ins_camera::StorageStatus status{};
  if (!Connected("storage_state").GetStorageState(status, location)) Fail("storage_state");
  return status;
}

std::vector<std::string> PyCamera::FilesList() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("files_list").GetCameraFilesList();
}

void PyCamera::DownloadFile(const std::string& remote, const std::string& local,
                            const ProgressCallback& progress) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& camera = Connected("download_file");
  const bool ok = progress ? camera.DownloadCameraFile(remote, local, progress)
                           : camera.DownloadCameraFile(remote, local);
  if (!ok) Fail("download_file");
}

void PyCamera::DeleteFile(const std::string& remote) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Connected("delete_file").DeleteCameraFile(remote)) Fail("delete_file");
}

MediaUrl PyCamera::TakePhoto(ins_camera::RawCaptureType raw_type, int timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  return ToMediaUrl(Connected("take_photo").TakePhoto(raw_type, timeout_ms));
}

void PyCamera::StartRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Connected("start_recording").StartRecording()) Fail("start_recording");
}

MediaUrl PyCamera::StopRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  return ToMediaUrl(Connected("stop_recording").StopRecording());
}

void PyCamera::SetVideoCaptureParams(const ins_camera::RecordParams& params,
                                     ins_camera::CameraFunctionMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Connected("set_video_capture_params").SetVideoCaptureParams(params, mode)) {
    Fail("set_video_capture_params");
  }
}

void PyCamera::SetExposureSettings(
    ins_camera::CameraFunctionMode mode,
    const std::shared_ptr<ins_camera::ExposureSettings>& settings) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!settings) throw std::invalid_argument("set_exposure_settings() needs ExposureSettings");
  if (!Connected("set_exposure_settings").SetExposureSettings(mode, settings)) {
    Fail("set_exposure_settings");
  }
}

std::shared_ptr<ins_camera::ExposureSettings> PyCamera::GetExposureSettings(
    ins_camera::CameraFunctionMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("get_exposure_settings").GetExposureSettings(mode);
}

void PyCamera::SyncLocalTime(uint64_t utc_seconds, int32_t offset_seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Connected("sync_local_time").SyncLocalTimeToCamera(utc_seconds, offset_seconds)) {
    Fail("sync_local_time");
  }
}

ins_camera::VideoEncodeType PyCamera::VideoEncodeType() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("video_encode_type").GetVideoEncodeType();
}

ins_camera::CameraFunctionMode PyCamera::CurrentFunctionMode() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("current_function_mode").GetCurrentFunctionMode();
}

ins_camera::CameraLensType PyCamera::LensType() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("lens_type").GetCameraLensType();
}

PreviewSnapshot PyCamera::PreviewParam() {
  std::lock_guard<std::mutex> lock(mutex_);
  const ins_camera::PreviewParam p = Connected("preview_param").GetPreviewParam();
  PreviewSnapshot out;
  out.camera_name = p.camera_name;
  out.encode_type = p.encode_type;
  out.delay_timestamp = p.delay_timestamp;
  out.sweep_time = p.sweep_time;
  out.acceleration_range = p.acceleration_range;
  out.gyro_range = p.gyro_range;
  for (size_t i = 0; i < p.GetCalibrationCount(); ++i) {
    out.calibrations.push_back(p.GetCalibration(i));
  }
  out.crop_src_width = p.GetCropSrcWidth();
  out.crop_src_height = p.GetCropSrcHeight();
  out.crop_dst_width = p.GetCropDstWidth();
  out.crop_dst_height = p.GetCropDstHeight();
  out.crop_offset_x = p.GetCropOffsetX();
  out.crop_offset_y = p.GetCropOffsetY();
  return out;
}

std::vector<ins_camera::VideoResolution> PyCamera::SupportedVideoResolutions(
    ins_camera::CameraFunctionMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("supported_video_resolutions").GetSupportedVideoResolutions(mode);
}

std::string PyCamera::HttpBaseUrl() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Connected("http_base_url").GetHttpBaseUrl();
}

void PyCamera::StartLiveStreaming(const ins_camera::LiveStreamParam& param,
                                  size_t video_queue_size, size_t imu_queue_size) {
  if (video_queue_size == 0 || imu_queue_size == 0) {
    throw std::invalid_argument("queue sizes must be positive");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto& camera = Connected("start_live_streaming");
  if (state_ == CameraState::kStreaming) {
    throw Insta360Error("start_live_streaming() called while already STREAMING");
  }
  // Buffers must accept data before the SDK can deliver the first callback.
  queues_->Begin(video_queue_size, imu_queue_size);
  camera.SetStreamDelegate(delegate_);
  if (!camera.StartLiveStreaming(param)) {
    queues_->End();
    Fail("start_live_streaming");
  }
  state_ = CameraState::kStreaming;
}

void PyCamera::StopLiveStreaming() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& camera = Connected("stop_live_streaming");
  if (state_ != CameraState::kStreaming) {
    throw Insta360Error("stop_live_streaming() requires state STREAMING (state is OPEN)");
  }
  queues_->End();
  if (!camera.StopLiveStreaming()) Fail("stop_live_streaming");
  state_ = CameraState::kOpen;
}

}  // namespace pyinsta360
