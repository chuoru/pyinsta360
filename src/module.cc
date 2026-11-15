#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <camera/camera.h>

namespace py = pybind11;
using NoGil = py::call_guard<py::gil_scoped_release>;

namespace {

void BindEnums(py::module_& m) {
#include "enums_gen.inc"
}

}  // namespace

PYBIND11_MODULE(_sdk, m) {
  m.doc() = "Low-level bindings for the Insta360 CameraSDK.";
  BindEnums(m);
  m.def("sdk_version", &ins_camera::GetSDKVersion, NoGil());
  m.def("sdk_version_major", &ins_camera::GetSDKVersionMajor, NoGil());
  m.def("set_log_level", &ins_camera::SetLogLevel, py::arg("level"), NoGil());
  m.def("set_log_path", &ins_camera::SetLogPath, py::arg("path"), NoGil());
}
