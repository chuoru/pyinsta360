#include "stream_queues.h"

#include <algorithm>

namespace pyinsta360 {

void StreamQueues::Begin(size_t video_capacity, size_t imu_capacity) {
  std::lock_guard<std::mutex> lock(mutex_);
  video_capacity_ = video_capacity;
  imu_capacity_ = imu_capacity;
  video_.clear();
  imu_.clear();
  exposure_.clear();
  stats_ = StreamStats{};
  active_ = true;
}

void StreamQueues::End() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    video_.clear();
    imu_.clear();
    exposure_.clear();
  }
  cv_.notify_all();
}

void StreamQueues::OnVideoData(const uint8_t* data, size_t size, int64_t timestamp, uint8_t,
                               int stream_index) {
  if (!active_) return;
  VideoPacket packet{std::string(reinterpret_cast<const char*>(data), size), timestamp,
                     stream_index};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;
    video_.push_back(std::move(packet));
    while (video_.size() > video_capacity_) {
      video_.pop_front();
      ++stats_.video_dropped;
    }
  }
  cv_.notify_all();
}

void StreamQueues::OnGyroData(const std::vector<ins_camera::GyroData>& data) {
  if (!active_ || data.empty()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;
    imu_.insert(imu_.end(), data.begin(), data.end());
    if (imu_.size() > imu_capacity_) {
      const size_t excess = imu_.size() - imu_capacity_;
      imu_.erase(imu_.begin(), imu_.begin() + static_cast<std::ptrdiff_t>(excess));
      stats_.imu_dropped += excess;
    }
  }
  cv_.notify_all();
}

void StreamQueues::OnExposureData(const ins_camera::ExposureData& data) {
  if (!active_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_) return;
  exposure_.push_back(data);
  if (exposure_.size() > kExposureCapacity) exposure_.pop_front();
}

WaitResult StreamQueues::WaitVideo(std::chrono::nanoseconds timeout, VideoPacket& out) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait_for(lock, timeout, [this] { return !video_.empty() || !active_; });
  if (!active_) return WaitResult::kInactive;
  if (video_.empty()) return WaitResult::kTimeout;
  out = std::move(video_.front());
  video_.pop_front();
  return WaitResult::kReady;
}

WaitResult StreamQueues::WaitImu(std::chrono::nanoseconds timeout,
                                 std::vector<ins_camera::GyroData>& out) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (timeout.count() > 0) {
    cv_.wait_for(lock, timeout, [this] { return !imu_.empty() || !active_; });
  }
  if (!active_) return WaitResult::kInactive;
  if (imu_.empty()) return WaitResult::kTimeout;
  out.swap(imu_);
  imu_.clear();
  return WaitResult::kReady;
}

std::vector<ins_camera::ExposureData> StreamQueues::TakeExposure() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ins_camera::ExposureData> out(exposure_.begin(), exposure_.end());
  exposure_.clear();
  return out;
}

StreamStats StreamQueues::Stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

}  // namespace pyinsta360
