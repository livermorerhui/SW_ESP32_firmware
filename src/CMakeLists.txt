# Keep the existing Arduino source tree intact while registering it as one IDF component.
file(GLOB_RECURSE sonicwave_sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

idf_component_register(
  SRCS ${sonicwave_sources}
  INCLUDE_DIRS "."
  REQUIRES framework-arduinoespressif32
)

# The rollback USB baseline is a test-only build.  PlatformIO's mixed
# Arduino/ESP-IDF builder does not forward SCons CPPDEFINES to IDF component
# sources, so bind Arduino Serial to the already-enabled USB Serial-JTAG
# console here.  Release and ordinary development builds do not receive this
# environment marker and keep their existing serial routing.
if(DEFINED ENV{SW_ESP32_ROLLBACK_USB_DIAGNOSTICS} AND
   "$ENV{SW_ESP32_ROLLBACK_USB_DIAGNOSTICS}" STREQUAL "1")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SW_ESP32_ROLLBACK_USB_DIAGNOSTICS=1
    ARDUINO_USB_MODE=1
    ARDUINO_USB_CDC_ON_BOOT=1
  )
endif()
