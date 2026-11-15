#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <stream/stream_delegate.h>

namespace pyinsta360 {

struct VideoPacket {
  std::string data;
  int64_t timestamp = 0;
  int stream_index = 0;
};

struct StreamStats {
  uint64_t video_dropped = 0;
  uint64_t imu_dropped = 0;
};

enum class WaitResult { kReady, kTimeout, kInactive };

// Receives SDK stream callbacks on the SDK's own thread. The callbacks only
// copy into bounded buffers and notify, so they never block and never touch
// Python. A full buffer drops its oldest entries, so a slow consumer always
// sees the most recent data.
class StreamQueues final : public ins_camera::StreamDelegate {
 public:
  static constexpr size_t kExposureCapacity = 256;

  // Clears the buffers and statistics, then starts accepting callbacks.
  void Begin(size_t video_capacity, size_t imu_capacity);
  // Stops accepting callbacks, discards buffered data and wakes all waiters.
  void End();

  void OnAudioData(const uint8_t*, size_t, int64_t) override {}
  void OnVideoData(const uint8_t* data, size_t size, int64_t timestamp, uint8_t stream_type,
                   int stream_index) override;
  void OnGyroData(const std::vector<ins_camera::GyroData>& data) override;
  void OnExposureData(const ins_camera::ExposureData& data) override;

  // Both waits return kInactive immediately once End() has been called.
  WaitResult WaitVideo(std::chrono::nanoseconds timeout, VideoPacket& out);
  // Moves every buffered sample into `out`. A zero timeout does not wait.
  WaitResult WaitImu(std::chrono::nanoseconds timeout, std::vector<ins_camera::GyroData>& out);
  std::vector<ins_camera::ExposureData> TakeExposure();
  StreamStats Stats() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> active_{false};  // written under mutex_, read lock-free by callbacks
  size_t video_capacity_ = 0;
  size_t imu_capacity_ = 0;
  std::deque<VideoPacket> video_;
  std::vector<ins_camera::GyroData> imu_;
  std::deque<ins_camera::ExposureData> exposure_;
  StreamStats stats_;
};

}  // namespace pyinsta360
