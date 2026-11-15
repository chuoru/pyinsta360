#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <camera/camera.h>
#include <camera/device_discovery.h>

#include "stream_queues.h"

namespace pyinsta360 {

class Insta360Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class CameraState { kClosed, kOpen, kStreaming };

const char* StateName(CameraState state);

struct DeviceInfo {
  ins_camera::CameraType camera_type = ins_camera::CameraType::Unknown;
  std::string serial_number;
  std::string camera_name;
  std::string fw_version;
  ins_camera::ConnectionType connection_type = ins_camera::ConnectionType::USB;
};

// Owned copies of the SDK's MediaUrl / PreviewParam, which cannot be default
// constructed or held by value in Python.
struct MediaUrl {
  std::vector<std::string> origin_urls;
  std::vector<std::string> lrv_urls;
  bool Any() const { return !origin_urls.empty() || !lrv_urls.empty(); }
};

struct PreviewSnapshot {
  std::string camera_name;
  ins_camera::VideoEncodeType encode_type = ins_camera::VideoEncodeType::H264;
  int64_t delay_timestamp = 0;
  int64_t sweep_time = 0;
  int32_t acceleration_range = 0;
  int32_t gyro_range = 0;
  std::vector<std::string> calibrations;
  uint32_t crop_src_width = 0;
  uint32_t crop_src_height = 0;
  uint32_t crop_dst_width = 0;
  uint32_t crop_dst_height = 0;
  int32_t crop_offset_x = 0;
  int32_t crop_offset_y = 0;
};

std::vector<DeviceInfo> Discover();

ins_camera::LiveStreamParam DefaultLiveStreamParam();

using ProgressCallback = std::function<void(int64_t downloaded, int64_t total)>;

// Python-free owner of one SDK camera and its stream buffers. Every method
// that touches the SDK camera is serialized by one mutex, so a close() from
// another thread waits for an in-flight SDK call instead of racing it. Only
// the stream readers (queues()) and state() are lock-free with respect to it.
class PyCamera {
 public:
  explicit PyCamera(std::optional<std::string> serial = std::nullopt);
  ~PyCamera();
  PyCamera(const PyCamera&) = delete;
  PyCamera& operator=(const PyCamera&) = delete;

  void Open();
  void Close();
  CameraState state() const { return state_; }
  std::optional<DeviceInfo> Info() const;
  bool IsConnected();
  void SetTimeout(int timeout_ms);

  ins_camera::BatteryStatus BatteryStatus();
  ins_camera::StorageStatus StorageState(ins_camera::CardLocation location);
  std::vector<std::string> FilesList();
  void DownloadFile(const std::string& remote, const std::string& local,
                    const ProgressCallback& progress);
  void DeleteFile(const std::string& remote);
  MediaUrl TakePhoto(ins_camera::RawCaptureType raw_type, int timeout_ms);
  void StartRecording();
  MediaUrl StopRecording();
  void SetVideoCaptureParams(const ins_camera::RecordParams& params,
                             ins_camera::CameraFunctionMode mode);
  void SetExposureSettings(ins_camera::CameraFunctionMode mode,
                           const std::shared_ptr<ins_camera::ExposureSettings>& settings);
  std::shared_ptr<ins_camera::ExposureSettings> GetExposureSettings(
      ins_camera::CameraFunctionMode mode);
  void SyncLocalTime(uint64_t utc_seconds, int32_t offset_seconds);
  ins_camera::VideoEncodeType VideoEncodeType();
  ins_camera::CameraFunctionMode CurrentFunctionMode();
  ins_camera::CameraLensType LensType();
  PreviewSnapshot PreviewParam();
  std::vector<ins_camera::VideoResolution> SupportedVideoResolutions(
      ins_camera::CameraFunctionMode mode);
  std::string HttpBaseUrl();

  void StartLiveStreaming(const ins_camera::LiveStreamParam& param, size_t video_queue_size,
                          size_t imu_queue_size);
  void StopLiveStreaming();

  StreamQueues& queues() { return *queues_; }

 private:
  // Both require mutex_ held.
  ins_camera::Camera& Connected(const char* method);
  void CloseLocked();

  const std::optional<std::string> serial_;
  mutable std::mutex mutex_;
  std::atomic<CameraState> state_{CameraState::kClosed};
  std::optional<DeviceInfo> info_;
  // Declared in destruction-safe order: the SDK camera dies first, so it
  // can never call into a destroyed delegate.
  std::shared_ptr<StreamQueues> queues_;
  std::shared_ptr<ins_camera::StreamDelegate> delegate_;
  std::unique_ptr<ins_camera::Camera> camera_;
};

}  // namespace pyinsta360
