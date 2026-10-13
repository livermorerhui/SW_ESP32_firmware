#!/usr/bin/env python3
"""Compile the maintenance image with NVS fault injection on the host."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "test_firmware/e8_legacy_config_bridge/main.cpp"

FAKES = r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

using esp_err_t = int;
using nvs_handle_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 1;
constexpr esp_err_t ESP_ERR_NVS_TYPE_MISMATCH = 2;
constexpr esp_err_t ESP_ERR_NVS_INVALID_STATE = 3;
constexpr int NVS_READONLY = 0;

struct FakeNvs {
  bool namespaceExists = false;
  bool failOpen = false;
  std::string failReadKey;
  int failReadRemaining = 0;
  std::map<std::string, std::pair<bool, uint8_t>> keys;
};
inline FakeNvs fakeNvs;

inline esp_err_t nvs_open(const char*, int mode, nvs_handle_t* handle) {
  if (fakeNvs.failOpen) return ESP_ERR_NVS_INVALID_STATE;
  if (!fakeNvs.namespaceExists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
  fakeNvs.namespaceExists = true;
  *handle = 1;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_get_u8(nvs_handle_t, const char* key, uint8_t* value) {
  if (fakeNvs.failReadRemaining > 0 && fakeNvs.failReadKey == key) {
    --fakeNvs.failReadRemaining;
    return ESP_ERR_NVS_INVALID_STATE;
  }
  const auto it = fakeNvs.keys.find(key);
  if (it == fakeNvs.keys.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (!it->second.first) return ESP_ERR_NVS_TYPE_MISMATCH;
  *value = it->second.second;
  return ESP_OK;
}

class String {
 public:
  String() = default;
  String(const char* value) : value_(value) {}
  String(std::string value) : value_(std::move(value)) {}
  bool startsWith(const char* prefix) const { return value_.rfind(prefix, 0) == 0; }
  int indexOf(const char* needle, int from = 0) const {
    const auto pos = value_.find(needle, static_cast<size_t>(from));
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  String substring(size_t start, size_t end) const { return String(value_.substr(start, end - start)); }
  String substring(size_t start) const { return String(value_.substr(start)); }
  size_t length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  bool operator==(const char* other) const { return value_ == other; }
  bool operator!=(const String& other) const { return value_ != other.value_; }
  void trim() {}
 private:
  std::string value_;
};

struct FakeSerial {
  std::string output;
  template <typename... Args> void printf(const char* format, Args... args) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    output += buffer;
  }
  void println(const char* value) { output += std::string(value) + "\n"; }
  void begin(int) {}
  void setTimeout(int) {}
  bool available() { return false; }
  String readStringUntil(char) { return String(); }
};
inline FakeSerial Serial;
inline void delay(int) {}

class Preferences {
 public:
  bool begin(const char*, bool readOnly) {
    nvs_handle_t handle;
    return nvs_open("scale_cal", readOnly ? NVS_READONLY : 1, &handle) == ESP_OK;
  }
  void end() {}
  bool remove(const char* key) { return fakeNvs.keys.erase(key) == 1; }
  size_t putUChar(const char* key, uint8_t value) {
    fakeNvs.keys[key] = {true, value};
    return 1;
  }
};

struct FirmwareDeviceIdentityProvider {
  static bool readFromEfuse(String& id) {
    id = String("sw1_00000000000000000000000000000000");
    return true;
  }
  static bool isValid(const String& id) {
    return id.length() == 36 && id.startsWith("sw1_");
  }
};
'''

HARNESS = r'''
#include <cstdlib>
#include <iostream>
#include "fake.hpp"
#include "maintenance.cpp"

void require(bool okay, const char* caseName) {
  if (!okay) { std::cerr << "FAIL: " << caseName << "\n"; std::abort(); }
}
void reset() { fakeNvs = {}; Serial.output.clear(); }
int main() {
  const String id("sw1_00000000000000000000000000000000");

  reset();
  inspect();
  require(Serial.output.find("namespace=ABSENT") != std::string::npos,
          "absent namespace is distinct from NVS failure");

  reset();
  fakeNvs.failOpen = true;
  inspect();
  require(Serial.output.find("INSPECT_ERROR reason=NVS_OPEN_FAILED") != std::string::npos,
          "NVS failure blocks inspection");

  reset();
  fakeNvs.namespaceExists = true;
  fakeNvs.keys["cfg_model"] = {true, 1};
  fakeNvs.failReadKey = "cfg_model";
  fakeNvs.failReadRemaining = 1;
  bridge(0, id);
  require(Serial.output.find("BRIDGE_REJECT reason=MODEL_READ_FAILED") != std::string::npos &&
          fakeNvs.keys["cfg_model"].second == 1,
          "failed model read must not overwrite Plus as Base");

  reset();
  fakeNvs.namespaceExists = true;
  fakeNvs.keys["cfg_model"] = {true, 1};
  fakeNvs.keys["cfg_laser"] = {true, 1};
  fakeNvs.failReadKey = "cfg_laser";
  fakeNvs.failReadRemaining = 1;
  bridge(1, id);
  require(Serial.output.find("BRIDGE_REJECT reason=LASER_READ_FAILED") != std::string::npos &&
          fakeNvs.keys["cfg_model"].second == 1,
          "failed laser read blocks bridge before writing");

  reset();
  fakeNvs.namespaceExists = true;
  fakeNvs.keys["cfg_model"] = {true, 1};
  bridge(0, id);
  require(Serial.output.find("BRIDGE_REJECT reason=MODEL_MISMATCH") != std::string::npos &&
          fakeNvs.keys["cfg_model"].second == 1,
          "existing Plus must not be overwritten as Base");

  reset();
  inspect();
  bridge(1, id);
  require(Serial.output.find("BRIDGE_WRITE_OK_PENDING_POWER_CYCLE_VERIFY") != std::string::npos &&
          fakeNvs.keys["cfg_model"].second == 1 &&
          fakeNvs.keys["cfg_laser"].second == 1,
          "absent namespace can bridge with explicit command");
  Serial.output.clear();
  inspect();
  require(Serial.output.find("key=cfg_model exists=1 type=PT_U8 value=1") != std::string::npos,
          "inspection reads committed model");

  reset();
  fakeNvs.namespaceExists = true;
  fakeNvs.keys["cfg_model"] = {true, 9};
  bridge(0, id);
  require(Serial.output.find("BRIDGE_REJECT reason=MODEL_UNKNOWN") != std::string::npos,
          "unknown model value requires manual review");

  std::cout << "maintenance bridge fault-injection PASS\n";
}
'''


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="e8-legacy-bridge-") as temporary:
        directory = Path(temporary)
        (directory / "core").mkdir()
        (directory / "fake.hpp").write_text(FAKES)
        for name in ("Arduino.h", "Preferences.h", "nvs.h"):
            (directory / name).write_text('#include "fake.hpp"\n')
        (directory / "core/FirmwareDeviceIdentityProvider.h").write_text(
            '#include "fake.hpp"\n'
        )
        (directory / "maintenance.cpp").write_text(SOURCE.read_text())
        (directory / "harness.cpp").write_text(HARNESS)
        binary = directory / "bridge-test"
        subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(directory),
             str(directory / "harness.cpp"), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
