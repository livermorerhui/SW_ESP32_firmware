# Keep the existing Arduino source tree intact while registering it as one IDF component.
file(GLOB_RECURSE sonicwave_sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

idf_component_register(
  SRCS ${sonicwave_sources}
  INCLUDE_DIRS "."
  REQUIRES framework-arduinoespressif32
)
