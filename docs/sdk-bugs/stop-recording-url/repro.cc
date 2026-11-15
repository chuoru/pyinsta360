// Records a short clip and checks that the MediaUrl returned by StopRecording
// names files that exist on the camera and can be downloaded.
// Usage: ./repro [seconds] [--delete]   (--delete removes the clip it created)
#include <camera/camera.h>
#include <camera/device_discovery.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

int main(int argc, char** argv) {
  int seconds = argc > 1 && argv[1][0] != '-' ? std::atoi(argv[1]) : 3;
  bool remove = argc > 1 && std::strcmp(argv[argc - 1], "--delete") == 0;
  ins_camera::SetLogLevel(ins_camera::LogLevel::ERR);
  ins_camera::DeviceDiscovery discovery;
  auto list = discovery.GetAvailableDevices();
  if (list.empty()) { std::fprintf(stderr, "no camera\n"); return 2; }
  std::printf("camera: %s fw %s\n", list[0].camera_name.c_str(), list[0].fw_version.c_str());
  auto cam = std::make_shared<ins_camera::Camera>(list[0].info);
  if (!cam->Open()) { std::fprintf(stderr, "open failed\n"); return 2; }
  discovery.FreeDeviceDescriptors(list);
  std::printf("function mode: %d\n", static_cast<int>(cam->GetCurrentFunctionMode()));

  auto before = cam->GetCameraFilesList();
  if (!cam->StartRecording()) { std::fprintf(stderr, "StartRecording failed\n"); return 2; }
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  auto url = cam->StopRecording();
  auto after = cam->GetCameraFilesList();

  std::vector<std::string> returned;
  for (const auto& u : url.OriginUrls()) { std::printf("returned origin: %s\n", u.c_str()); returned.push_back(u); }
  for (const auto& u : url.LRVUrls()) { std::printf("returned lrv:    %s\n", u.c_str()); returned.push_back(u); }
  std::vector<std::string> created;
  for (const auto& f : after) if (!Contains(before, f)) { std::printf("new on camera:   %s\n", f.c_str()); created.push_back(f); }

  int bad = 0;
  for (const auto& u : returned) {
    bool listed = Contains(after, u);
    std::string local = "/tmp/insta360_repro_" + u.substr(u.find_last_of('/') + 1);
    bool downloaded = cam->DownloadCameraFile(u, local);
    std::printf("%s listed=%d downloaded=%d %s\n", listed && downloaded ? "OK  " : "BAD ", listed, downloaded, u.c_str());
    if (!listed || !downloaded) ++bad;
    std::remove(local.c_str());
  }
  if (remove)
    for (const auto& f : created) std::printf("deleted=%d %s\n", cam->DeleteCameraFile(f), f.c_str());
  cam->Close();
  std::printf("%s\n", bad ? "RESULT: returned MediaUrl names a file that does not exist" : "RESULT: all returned paths valid");
  return bad ? 1 : 0;
}
