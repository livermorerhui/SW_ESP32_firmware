cmake_minimum_required(VERSION 3.16)

# Keep the IDF app descriptor and BLE firmware identity on the same version.
# The generated header also drives the component's actual compiler options.
if(EXISTS "${CMAKE_BINARY_DIR}/product_build_config.h")
  file(STRINGS "${CMAKE_BINARY_DIR}/product_build_config.h" sonicwave_version_line REGEX "^#define FW_VER ")
  string(REGEX REPLACE "^#define FW_VER \"([^\"]+)\"$" "\\1" PROJECT_VER "${sonicwave_version_line}")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sonicwave_esp32s3_n16r8)
