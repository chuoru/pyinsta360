cmake_minimum_required(VERSION 3.18)
project(pyinsta360 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT INSTA360_SDK_DIR AND DEFINED ENV{INSTA360_SDK_DIR})
  set(INSTA360_SDK_DIR "$ENV{INSTA360_SDK_DIR}")
endif()
if(NOT INSTA360_SDK_DIR)
  message(FATAL_ERROR
    "INSTA360_SDK_DIR is not set. Point it at the extracted Insta360 CameraSDK "
    "(the directory containing include/ and lib/libCameraSDK.so), either as an "
    "environment variable or with -DINSTA360_SDK_DIR=... (the SDK is proprietary "
    "and is not shipped with this repository).")
endif()
get_filename_component(INSTA360_SDK_DIR "${INSTA360_SDK_DIR}" ABSOLUTE)
set(INSTA360_SDK_LIB "${INSTA360_SDK_DIR}/lib/libCameraSDK.so")
if(NOT EXISTS "${INSTA360_SDK_DIR}/include/camera/camera.h" OR NOT EXISTS "${INSTA360_SDK_LIB}")
  message(FATAL_ERROR
    "INSTA360_SDK_DIR='${INSTA360_SDK_DIR}' does not look like the Insta360 CameraSDK: "
    "expected include/camera/camera.h and lib/libCameraSDK.so.")
endif()

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_sdk MODULE
  src/module.cc)
target_include_directories(_sdk PRIVATE src "${INSTA360_SDK_DIR}/include")
target_compile_options(_sdk PRIVATE -Wall -Wextra)
target_link_libraries(_sdk PRIVATE "${INSTA360_SDK_LIB}")

# DT_RUNPATH (not DT_RPATH) so LD_LIBRARY_PATH can override the SDK location;
# the tests rely on that to substitute the fake SDK.
set_target_properties(_sdk PROPERTIES
  BUILD_WITH_INSTALL_RPATH ON
  INSTALL_RPATH "${INSTA360_SDK_DIR}/lib"
  INSTALL_RPATH_USE_LINK_PATH OFF)
target_link_options(_sdk PRIVATE LINKER:--enable-new-dtags)

install(TARGETS _sdk LIBRARY DESTINATION pyinsta360)
