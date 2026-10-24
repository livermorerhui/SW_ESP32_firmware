# Keep the existing Arduino source tree intact while registering it as one IDF component.
file(GLOB_RECURSE sonicwave_sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

idf_component_register(
  SRCS ${sonicwave_sources}
  INCLUDE_DIRS "."
  REQUIRES framework-arduinoespressif32
)

# The CMake code model does not show later SCons flags. Bind the generated
# configuration explicitly to the component so every application translation
# unit consumes the same reviewable identity/backend in both build layers.
set(sonicwave_build_header "${CMAKE_BINARY_DIR}/product_build_config.h")
if(NOT EXISTS "${sonicwave_build_header}")
  message(FATAL_ERROR "Missing product build configuration; build with PlatformIO")
endif()
# Keep this as one token: SCons categorizes/reorders the CMake fragments, so a
# separate path can otherwise become a second input instead of -include's arg.
target_compile_options(${COMPONENT_LIB} PRIVATE "-include${sonicwave_build_header}")
