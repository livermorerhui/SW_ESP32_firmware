#include <Arduino.h>
#include <Preferences.h>
#include <nvs.h>
#include <cstring>

#include "core/FirmwareDeviceIdentityProvider.h"

// USB-only maintenance image for inventorying old Base/Plus boards before the
// profile-aware firmware is installed. It never writes merely because a key is
// missing. The operator must first save the original INSPECT output and match
// the stable device ID against the physical device record.
namespace {
constexpr const char* kNamespace = "scale_cal";
constexpr const char* kModelKey = "cfg_model";
constexpr const char* kLaserKey = "cfg_laser";

// Preferences::getUChar returns the supplied default on a read failure. A
// maintenance image must keep "missing" distinct from "could not read".
esp_err_t readU8(const char* key, uint8_t& value) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened != ESP_OK) return opened;
  const esp_err_t read = nvs_get_u8(handle, key, &value);
  nvs_close(handle);
  return read;
}

bool printKey(const char* key) {
  uint8_t value = 0;
  const esp_err_t read = readU8(key, value);
  if (read == ESP_OK) {
    Serial.printf(" key=%s exists=1 type=PT_U8 value=%u\n", key,
                  static_cast<unsigned>(value));
    return true;
  }
  if (read == ESP_ERR_NVS_NOT_FOUND) {
    Serial.printf(" key=%s exists=0 type=ABSENT\n", key);
    return true;
  }
  if (read == ESP_ERR_NVS_TYPE_MISMATCH) {
    Serial.printf(" key=%s exists=1 type=NON_U8\n", key);
    return true;
  }
  Serial.printf("INSPECT_ERROR reason=NVS_READ_FAILED key=%s err=%d\n", key,
                static_cast<int>(read));
  return false;
}

void inspect() {
  String deviceId;
  if (!FirmwareDeviceIdentityProvider::readFromEfuse(deviceId) ||
      !FirmwareDeviceIdentityProvider::isValid(deviceId)) {
    Serial.println("INSPECT_ERROR reason=IDENTITY_UNAVAILABLE");
    return;
  }
  Serial.printf("INSPECT device_id=%s\n", deviceId.c_str());
  // Read-only open does not create a namespace. Distinguish an absent old
  // namespace (repairable with evidence) from an unreadable NVS partition.
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) {
    Serial.println("INSPECT namespace=ABSENT");
    Serial.printf(" key=%s exists=0 type=ABSENT\n", kModelKey);
    Serial.printf(" key=%s exists=0 type=ABSENT\n", kLaserKey);
    return;
  }
  if (opened != ESP_OK) {
    Serial.printf("INSPECT_ERROR reason=NVS_OPEN_FAILED err=%d\n",
                  static_cast<int>(opened));
    return;
  }
  nvs_close(handle);
  if (!printKey(kModelKey)) return;
  printKey(kLaserKey);
}

bool parseBridgeCommand(const String& line, uint8_t& model, String& expectedId) {
  constexpr const char* kPrefix = "BRIDGE model=";
  if (!line.startsWith(kPrefix)) return false;
  const int idStart = line.indexOf(" id=", strlen(kPrefix));
  if (idStart < 0) return false;
  const String rawModel = line.substring(strlen(kPrefix), idStart);
  if (rawModel == "BASE") model = 0;
  else if (rawModel == "PLUS") model = 1;
  else return false;
  const int evidenceStart = line.indexOf(" evidence_saved=1", idStart + 4);
  if (evidenceStart < 0 || static_cast<size_t>(evidenceStart + 17) != line.length()) return false;
  expectedId = line.substring(idStart + 4, evidenceStart);
  return FirmwareDeviceIdentityProvider::isValid(expectedId);
}

void bridge(uint8_t model, const String& expectedId) {
  String actualId;
  if (!FirmwareDeviceIdentityProvider::readFromEfuse(actualId) ||
      actualId != expectedId) {
    Serial.println("BRIDGE_REJECT reason=DEVICE_ID_MISMATCH");
    return;
  }
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    Serial.println("BRIDGE_REJECT reason=NVS_OPEN_FAILED");
    return;
  }
  uint8_t storedModel = 0;
  const esp_err_t modelRead = readU8(kModelKey, storedModel);
  if (modelRead == ESP_OK) {
    if (storedModel > 3) {
      Serial.printf("BRIDGE_REJECT reason=MODEL_UNKNOWN stored=%u\n",
                    static_cast<unsigned>(storedModel));
      preferences.end();
      return;
    }
    if (storedModel != model) {
      Serial.printf("BRIDGE_REJECT reason=MODEL_MISMATCH stored=%u requested=%u\n",
                    static_cast<unsigned>(storedModel), static_cast<unsigned>(model));
      preferences.end();
      return;
    }
  } else if (modelRead != ESP_ERR_NVS_NOT_FOUND &&
             modelRead != ESP_ERR_NVS_TYPE_MISMATCH) {
    Serial.printf("BRIDGE_REJECT reason=MODEL_READ_FAILED err=%d\n",
                  static_cast<int>(modelRead));
    preferences.end();
    return;
  }

  uint8_t storedLaser = 0;
  const esp_err_t laserRead = readU8(kLaserKey, storedLaser);
  if (laserRead != ESP_OK && laserRead != ESP_ERR_NVS_NOT_FOUND &&
      laserRead != ESP_ERR_NVS_TYPE_MISMATCH) {
    Serial.printf("BRIDGE_REJECT reason=LASER_READ_FAILED err=%d\n",
                  static_cast<int>(laserRead));
    preferences.end();
    return;
  }

  // Replacing a bad type requires removal first; the pre-write INSPECT log is
  // deliberately required by the command so the original evidence survives.
  if (modelRead == ESP_ERR_NVS_TYPE_MISMATCH && !preferences.remove(kModelKey)) {
    Serial.println("BRIDGE_REJECT reason=MODEL_REMOVE_FAILED");
    preferences.end();
    return;
  }
  if (laserRead == ESP_ERR_NVS_TYPE_MISMATCH &&
      !preferences.remove(kLaserKey)) {
    Serial.println("BRIDGE_REJECT reason=LASER_REMOVE_FAILED");
    preferences.end();
    return;
  }

  const uint8_t laserInstalled = model == 1 ? 1 : 0;
  const bool wroteModel = preferences.putUChar(kModelKey, model) == 1;
  const bool wroteLaser = wroteModel &&
      preferences.putUChar(kLaserKey, laserInstalled) == 1;
  uint8_t verifiedModel = 0;
  uint8_t verifiedLaser = 0;
  const bool verified = wroteLaser &&
      readU8(kModelKey, verifiedModel) == ESP_OK && verifiedModel == model &&
      readU8(kLaserKey, verifiedLaser) == ESP_OK && verifiedLaser == laserInstalled;
  preferences.end();
  Serial.printf("BRIDGE_WRITE_%s device_id=%s model=%s laser_installed=%u\n",
                verified ? "OK_PENDING_POWER_CYCLE_VERIFY" : "FAILED_BLOCK_UPGRADE",
                actualId.c_str(), model == 0 ? "BASE" : "PLUS",
                static_cast<unsigned>(laserInstalled));
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(1000);
  delay(1000);
  Serial.println("E8_LEGACY_CONFIG_BRIDGE_USB_ONLY");
  Serial.println("Commands: INSPECT | BRIDGE model=BASE|PLUS id=sw1_<32hex> evidence_saved=1");
  inspect();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "INSPECT") {
    inspect();
    return;
  }
  uint8_t model = 0;
  String expectedId;
  if (parseBridgeCommand(line, model, expectedId)) {
    bridge(model, expectedId);
    return;
  }
  Serial.println("NACK:INVALID_COMMAND");
}
