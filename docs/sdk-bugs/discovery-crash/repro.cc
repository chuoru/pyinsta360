#include <camera/camera.h>
#include <camera/device_discovery.h>
#include <cstdio>
int main() {
  ins_camera::SetLogLevel(ins_camera::LogLevel::ERR);
  ins_camera::DeviceDiscovery discovery;
  fprintf(stderr, "STEP discover\n");
  auto list = discovery.GetAvailableDevices();
  if (list.empty()) { fprintf(stderr, "STEP none\n"); return 3; }
  fprintf(stderr, "STEP construct\n");
  auto cam = std::make_shared<ins_camera::Camera>(list[0].info);
  fprintf(stderr, "STEP open\n");
  bool ok = cam->Open();
  fprintf(stderr, "STEP free (open=%d)\n", ok);
  discovery.FreeDeviceDescriptors(list);
  fprintf(stderr, "STEP close\n");
  cam->Close();
  fprintf(stderr, "STEP done\n");
  return 0;
}
