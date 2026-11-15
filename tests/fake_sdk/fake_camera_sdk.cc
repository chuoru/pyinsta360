// A stand-in for the proprietary libCameraSDK.so, compiled against the real SDK
// headers so every symbol keeps its real mangled name. It lets the test suite
// run without hardware by being found first through LD_LIBRARY_PATH.
//
// Behavior knobs (read on every call, so tests can flip them per test):
//   FAKE_INSTA360_NO_DEVICES=1     discovery finds nothing
//   FAKE_INSTA360_DEVICE_COUNT=N   discovery finds FAKE0001..FAKE000N (default 1)
//   FAKE_INSTA360_OPEN_FAIL=1      Camera::Open returns false
//   FAKE_INSTA360_STREAM_FAIL=1    StartLiveStreaming returns false
//   FAKE_INSTA360_ACTION_FAIL=1    boolean camera actions return false
//   FAKE_INSTA360_VIDEO_HZ=F       video packet rate (default 30)
//   FAKE_INSTA360_GYRO_BATCH=N     gyro samples per video period (default 17)
//   FAKE_INSTA360_DISCOVERY_CRASH_FILE=PATH
//                                  if PATH holds an integer N>0, discovery rewrites it to N-1
//                                  and then aborts the process the way SDK 2.1.8 does: an
//                                  SDK-owned thread calls an empty std::function
//
// Introspection for tests (extern "C", loaded via ctypes) is at the bottom.

#include <camera/camera.h>
#include <camera/device_discovery.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace {

bool EnvFlag(const char* name) {
  const char* v = std::getenv(name);
  return v && *v && std::strcmp(v, "0") != 0;
}

double EnvNumber(const char* name, double fallback) {
  const char* v = std::getenv(name);
  return v && *v ? std::atof(v) : fallback;
}

[[noreturn]] void Die(const char* what) {
  std::fprintf(stderr, "fake CameraSDK contract violation: %s\n", what);
  std::abort();
}

void CrashIfRequested() {
  const char* path = std::getenv("FAKE_INSTA360_DISCOVERY_CRASH_FILE");
  if (!path || !*path) return;
  int remaining = 0;
  {
    std::ifstream in(path);
    in >> remaining;
  }
  if (remaining <= 0) return;
  std::ofstream(path, std::ios::trunc) << remaining - 1;
  std::thread([] {
    std::function<void()> empty;
    empty();  // throws std::bad_function_call on a thread nobody catches on
  }).join();
}

struct NativeInfo {
  int index;
};

std::mutex g_descriptor_mutex;
std::set<const void*> g_live_native;  // descriptors handed out and not yet freed
std::atomic<int> g_live_cameras{0};
std::atomic<int> g_open_cameras{0};
std::atomic<int> g_stream_threads{0};
std::atomic<uint64_t> g_sync_utc{0};
std::atomic<int32_t> g_sync_offset{0};

bool NativeIsLive(const void* p) {
  std::lock_guard<std::mutex> lock(g_descriptor_mutex);
  return g_live_native.count(p) != 0;
}

}  // namespace

namespace ins_camera {

class ExposureSettingsPrivate {
 public:
  PhotographyOptions_ExposureMode mode = PhotographyOptions_ExposureMode::AUTO;
  int32_t iso = 0;
  double shutter = 0.0;
  int32_t ev_bias = 0;
  int32_t iso_top = 0;
};

struct PreviewParam::Impl {
  std::vector<std::string> calibrations;
  uint32_t crop_src_w = 0, crop_src_h = 0, crop_dst_w = 0, crop_dst_h = 0;
  int32_t crop_off_x = 0, crop_off_y = 0;
};

class CameraImpl {
 public:
  explicit CameraImpl(const DeviceConnectionInfo& info) : native_(info.native_connection_info) {
    ++g_live_cameras;
  }

  ~CameraImpl() {
    StopStream();
    if (open_) --g_open_cameras;
    // Until Open() succeeds the SDK may still reference the descriptor.
    if (!ever_opened_ && native_ && !NativeIsLive(native_)) {
      Die("camera destroyed after its device descriptor was freed");
    }
    --g_live_cameras;
  }

  bool Open() {
    if (EnvFlag("FAKE_INSTA360_OPEN_FAIL")) return false;
    if (!native_ || !NativeIsLive(native_)) Die("Open() on a freed device descriptor");
    if (!open_) {
      open_ = true;
      ever_opened_ = true;
      ++g_open_cameras;
    }
    return true;
  }

  void Close() {
    StopStream();
    if (open_) {
      open_ = false;
      --g_open_cameras;
    }
  }

  bool open() const { return open_; }

  bool StartStream(const LiveStreamParam&) {
    if (!open_ || EnvFlag("FAKE_INSTA360_STREAM_FAIL")) return false;
    if (!delegate_) Die("StartLiveStreaming() without SetStreamDelegate()");
    if (streaming_) return true;
    streaming_ = true;
    stop_ = false;
    thread_ = std::thread([this] { StreamLoop(); });
    ++g_stream_threads;
    return true;
  }

  void StopStream() {
    if (!streaming_) return;
    {
      std::lock_guard<std::mutex> lock(stream_mutex_);
      stop_ = true;
    }
    stream_cv_.notify_all();
    thread_.join();
    --g_stream_threads;
    streaming_ = false;
  }

  void SetDelegate(StreamDelegate* delegate) { delegate_ = delegate; }

  PreviewParam MakePreviewParam() const {
    PreviewParam p;
    p.camera_name = "FakeX3";
    p.encode_type = VideoEncodeType::H265;
    p.delay_timestamp = 5;
    p.sweep_time = 6;
    p.acceleration_range = 7;
    p.gyro_range = 8;
    p.impl_->calibrations = {"calib-a", "calib-b"};
    p.impl_->crop_src_w = 3840;
    p.impl_->crop_src_h = 1920;
    p.impl_->crop_dst_w = 640;
    p.impl_->crop_dst_h = 320;
    p.impl_->crop_off_x = 10;
    p.impl_->crop_off_y = -4;
    return p;
  }

  std::map<CameraFunctionMode, std::shared_ptr<ExposureSettings>> exposure;
  bool recording = false;

 private:
  // One iteration per video period: a video packet, an audio blob, one exposure
  // sample and a batch of gyro samples, all with deterministic content.
  void StreamLoop() {
    const double hz = EnvNumber("FAKE_INSTA360_VIDEO_HZ", 30.0);
    const int batch = static_cast<int>(EnvNumber("FAKE_INSTA360_GYRO_BATCH", 17));
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / hz));
    auto next = std::chrono::steady_clock::now() + period;
    uint32_t counter = 0;
    int64_t gyro_index = 0;
    while (true) {
      {
        std::unique_lock<std::mutex> lock(stream_mutex_);
        if (stream_cv_.wait_until(lock, next, [this] { return stop_; })) return;
      }
      next += period;

      std::vector<uint8_t> video(8 + 120);
      std::memcpy(video.data(), "FKVD", 4);
      std::memcpy(video.data() + 4, &counter, 4);
      std::memset(video.data() + 8, static_cast<int>(counter & 0xff), 120);
      const uint8_t audio[4] = {1, 2, 3, 4};
      delegate_->OnAudioData(audio, sizeof(audio), static_cast<int64_t>(counter) * 33333);
      delegate_->OnVideoData(video.data(), video.size(), static_cast<int64_t>(counter) * 33333, 0,
                             0);

      std::vector<GyroData> gyro;
      for (int i = 0; i < batch; ++i, ++gyro_index) {
        const double k = static_cast<double>(gyro_index);
        gyro.push_back(GyroData{gyro_index * 2000, k, 0.5 * k, 1.0, 0.25 * k, -k, 0.125 * k});
      }
      if (!gyro.empty()) delegate_->OnGyroData(gyro);
      delegate_->OnExposureData(ExposureData{static_cast<double>(counter) * 33333.0, 0.008});
      ++counter;
    }
  }

  const void* native_;
  bool open_ = false;
  bool ever_opened_ = false;
  bool streaming_ = false;
  StreamDelegate* delegate_ = nullptr;
  std::thread thread_;
  std::mutex stream_mutex_;
  std::condition_variable stream_cv_;
  bool stop_ = false;
};

void SetLogPath(const std::string&) {}
void SetLogLevel(LogLevel) {}
std::string GetSDKVersion() { return "fake-0.0.1"; }
int GetSDKVersionMajor() { return 0; }

std::vector<DeviceDescriptor> DeviceDiscovery::GetAvailableDevices() {
  CrashIfRequested();
  std::vector<DeviceDescriptor> out;
  if (EnvFlag("FAKE_INSTA360_NO_DEVICES")) return out;
  const int count = static_cast<int>(EnvNumber("FAKE_INSTA360_DEVICE_COUNT", 1));
  for (int i = 1; i <= count; ++i) {
    char serial[32];
    std::snprintf(serial, sizeof(serial), "FAKE%04d", i);
    auto* native = new NativeInfo{i};
    {
      std::lock_guard<std::mutex> lock(g_descriptor_mutex);
      g_live_native.insert(native);
    }
    DeviceDescriptor d;
    d.camera_type = CameraType::Insta360X3;
    d.serial_number = serial;
    d.camera_name = "Insta360 X3 (fake)";
    d.fw_version = "v0.0.0-fake";
    d.info = DeviceConnectionInfo{ConnectionType::USB, d.camera_name, native};
    out.push_back(std::move(d));
  }
  return out;
}

void DeviceDiscovery::FreeDeviceDescriptors(std::vector<DeviceDescriptor> descs) {
  for (auto& d : descs) {
    const void* native = d.info.native_connection_info;
    std::lock_guard<std::mutex> lock(g_descriptor_mutex);
    if (!g_live_native.erase(native)) Die("FreeDeviceDescriptors() on an unknown or freed descriptor");
    delete static_cast<const NativeInfo*>(native);
  }
}

std::vector<DeviceDescriptor> DeviceDiscovery::GetAvailableUSBDevices() { return {}; }
std::vector<DeviceDescriptor> DeviceDiscovery::GetAvailableWifiDevices() { return {}; }

PreviewParam::PreviewParam() : impl_(new Impl) {}
PreviewParam::~PreviewParam() = default;
PreviewParam::PreviewParam(PreviewParam&&) noexcept = default;
PreviewParam& PreviewParam::operator=(PreviewParam&&) noexcept = default;
size_t PreviewParam::GetCalibrationCount() const { return impl_->calibrations.size(); }
std::string PreviewParam::GetCalibration(size_t i) const { return impl_->calibrations.at(i); }
uint32_t PreviewParam::GetCropSrcWidth() const { return impl_->crop_src_w; }
uint32_t PreviewParam::GetCropSrcHeight() const { return impl_->crop_src_h; }
uint32_t PreviewParam::GetCropDstWidth() const { return impl_->crop_dst_w; }
uint32_t PreviewParam::GetCropDstHeight() const { return impl_->crop_dst_h; }
int32_t PreviewParam::GetCropOffsetX() const { return impl_->crop_off_x; }
int32_t PreviewParam::GetCropOffsetY() const { return impl_->crop_off_y; }

MediaUrl::MediaUrl(const std::vector<std::string>& uris, const std::vector<std::string>& lrv_uris)
    : uris_(uris), lrv_uris_(lrv_uris) {}
bool MediaUrl::Empty() const { return uris_.empty() && lrv_uris_.empty(); }
bool MediaUrl::IsSingleOrigin() const { return uris_.size() == 1; }
bool MediaUrl::IsSingleLRV() const { return lrv_uris_.size() == 1; }
std::string MediaUrl::GetSingleOrigin() const { return uris_.empty() ? "" : uris_.front(); }
std::string MediaUrl::GetSingleLRV() const { return lrv_uris_.empty() ? "" : lrv_uris_.front(); }
const std::vector<std::string>& MediaUrl::OriginUrls() const { return uris_; }
const std::vector<std::string>& MediaUrl::LRVUrls() const { return lrv_uris_; }

ExposureSettings::ExposureSettings() : private_impl_(std::make_shared<ExposureSettingsPrivate>()) {}
void ExposureSettings::SetIso(int32_t v) { private_impl_->iso = v; }
void ExposureSettings::SetShutterSpeed(double v) { private_impl_->shutter = v; }
void ExposureSettings::SetExposureMode(PhotographyOptions_ExposureMode v) { private_impl_->mode = v; }
void ExposureSettings::SetEVBias(int32_t v) { private_impl_->ev_bias = v; }
void ExposureSettings::SetVideoISOTopLimit(int32_t v) { private_impl_->iso_top = v; }
int32_t ExposureSettings::Iso() const { return private_impl_->iso; }
double ExposureSettings::ShutterSpeed() const { return private_impl_->shutter; }
PhotographyOptions_ExposureMode ExposureSettings::ExposureMode() const { return private_impl_->mode; }
int32_t ExposureSettings::EVBias() const { return private_impl_->ev_bias; }
int32_t ExposureSettings::VideoISOTopLimit() const { return private_impl_->iso_top; }

Camera::Camera(const DeviceConnectionInfo& info) : impl_(std::make_shared<CameraImpl>(info)) {}

bool Camera::Open() const { return impl_->Open(); }
void Camera::Close() const { impl_->Close(); }
bool Camera::IsConnected() { return impl_->open(); }
void Camera::SetTimeout(int) {}

namespace {
bool ActionsFail() { return EnvFlag("FAKE_INSTA360_ACTION_FAIL"); }
}  // namespace

bool Camera::GetBatteryStatus(BatteryStatus& status) {
  if (ActionsFail()) return false;
  status = BatteryStatus{PowerType::BATTERY, 87, 100};
  return true;
}

bool Camera::GetStorageState(StorageStatus& status, CardLocation location) {
  if (ActionsFail()) return false;
  status = StorageStatus{STOR_CS_PASS, 1000, 2000,
                         location == CardLocation::STOR_CL_AUTO ? CardLocation::STOR_CL_CAMERA_SD
                                                                : location};
  return true;
}

std::vector<std::string> Camera::GetCameraFilesList() const {
  return {"/DCIM/Camera01/VID_FAKE_001.insv", "/DCIM/Camera01/IMG_FAKE_002.insp"};
}

bool Camera::DownloadCameraFile(const std::string& remote, const std::string& local,
                                DownloadProgressCallBack callback) const {
  if (ActionsFail() || remote.find("missing") != std::string::npos) return false;
  // Real SDKs report progress from their own thread; do the same.
  if (callback) {
    std::thread worker([&callback] {
      for (int i = 1; i <= 10; ++i) callback(i * 100, 1000);
    });
    worker.join();
  }
  std::ofstream(local, std::ios::binary) << "fake-download:" << remote;
  return true;
}

bool Camera::DeleteCameraFile(const std::string&) const { return !ActionsFail(); }

MediaUrl Camera::TakePhoto(RawCaptureType, int) const {
  if (ActionsFail()) return MediaUrl({});
  return MediaUrl({"/DCIM/Camera01/IMG_FAKE_003.insp"});
}

bool Camera::StartRecording() {
  if (ActionsFail()) return false;
  impl_->recording = true;
  return true;
}

MediaUrl Camera::StopRecording() {
  if (!impl_->recording) return MediaUrl({});
  impl_->recording = false;
  return MediaUrl({"/DCIM/Camera01/VID_FAKE_004.insv"}, {"/DCIM/Camera01/LRV_FAKE_004.insv"});
}

bool Camera::SetVideoCaptureParams(RecordParams, CameraFunctionMode) { return !ActionsFail(); }

bool Camera::SetExposureSettings(CameraFunctionMode mode,
                                 const std::shared_ptr<ExposureSettings>& settings) {
  if (ActionsFail()) return false;
  // Copy: the real camera does not alias the caller's object.
  auto stored = std::make_shared<ExposureSettings>();
  stored->SetExposureMode(settings->ExposureMode());
  stored->SetIso(settings->Iso());
  stored->SetShutterSpeed(settings->ShutterSpeed());
  stored->SetEVBias(settings->EVBias());
  stored->SetVideoISOTopLimit(settings->VideoISOTopLimit());
  impl_->exposure[mode] = stored;
  return true;
}

std::shared_ptr<ExposureSettings> Camera::GetExposureSettings(CameraFunctionMode mode) const {
  auto it = impl_->exposure.find(mode);
  return it == impl_->exposure.end() ? std::make_shared<ExposureSettings>() : it->second;
}

bool Camera::SyncLocalTimeToCamera(uint64_t utc_time, int32_t offset_time) {
  if (ActionsFail()) return false;
  g_sync_utc = utc_time;
  g_sync_offset = offset_time;
  return true;
}

PreviewParam Camera::GetPreviewParam() const { return impl_->MakePreviewParam(); }
VideoEncodeType Camera::GetVideoEncodeType() const { return VideoEncodeType::H265; }
CameraFunctionMode Camera::GetCurrentFunctionMode() const {
  return CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO;
}
CameraLensType Camera::GetCameraLensType() const { return CameraLensType::PanoDefault; }
std::string Camera::GetHttpBaseUrl() const { return "http://fake.insta360:80"; }

std::vector<VideoResolution> Camera::GetSupportedVideoResolutions(CameraFunctionMode mode) const {
  if (mode != CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO) return {};
  return {VideoResolution::RES_1920_960P30, VideoResolution::RES_3840_1920P30};
}

void Camera::SetStreamDelegate(std::shared_ptr<StreamDelegate>& delegate) {
  // Keep only a raw pointer: it catches a delegate that is destroyed too early.
  impl_->SetDelegate(delegate.get());
}

bool Camera::StartLiveStreaming(const LiveStreamParam& param) { return impl_->StartStream(param); }
bool Camera::StopLiveStreaming() {
  impl_->StopStream();
  return impl_->open() && !ActionsFail();
}

}  // namespace ins_camera

extern "C" {
int fake_insta360_live_descriptors() {
  std::lock_guard<std::mutex> lock(g_descriptor_mutex);
  return static_cast<int>(g_live_native.size());
}
int fake_insta360_live_cameras() { return g_live_cameras; }
int fake_insta360_open_cameras() { return g_open_cameras; }
int fake_insta360_stream_threads() { return g_stream_threads; }
uint64_t fake_insta360_last_sync_utc() { return g_sync_utc; }
int32_t fake_insta360_last_sync_offset() { return g_sync_offset; }
}
