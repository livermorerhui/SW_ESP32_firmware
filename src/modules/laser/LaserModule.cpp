#include "LaserModule.h"
#include "core/HumanReadableLog.h"
#include "core/LogMarkers.h"
#include "core/VerboseSerial.h"
#include <math.h>
#include <string.h>

namespace {
constexpr uint32_t kLaserLoopIntervalNoLaserMs = 200UL;
constexpr uint32_t kLaserLoopIntervalUnavailableIdleMs = 250UL;
constexpr uint32_t kLaserUnavailableIdleReadBackoffMs = 3000UL;
constexpr uint32_t kLaserRuntimeReadFailBackoffMs = 0UL;
// J100's empty reference is entered in the physical unit shown by the Demo
// APP. The record keeps the runtime-unit equivalent for the state chain.
constexpr float kJ100EmptyDistanceToleranceMm = 0.10f;
// Provisional CYD tolerance for the independent anchor only. CYD state
// thresholds remain out of scope until sensor data is collected.
constexpr float kCydEmptyDistanceToleranceMm = 1.0f;
constexpr float kModelReferenceEpsilonRuntime = 0.00001f;
// Temporary transport freshness guard derived from the existing poll periods.
// Product acceptance must replace/confirm it with each sensor's captured
// inter-sample distribution; it is not a CYD state-classification threshold.
uint32_t sampleFreshnessLimitMs(SensorProfileId profile) {
  switch (profile) {
    case SensorProfileId::J100_485:
      return LASER_MEASUREMENT_READ_INTERVAL_MS * 3UL;
    case SensorProfileId::CYD3S1612_IIC:
      return LASER_IIC_MEASUREMENT_READ_INTERVAL_MS * 3UL;
    case SensorProfileId::NONE:
      return 0;
  }
  return 0;
}

ProfileEmptyBackendId profileEmptyBackendFor(SensorProfileId profile) {
  switch (profile) {
    case SensorProfileId::J100_485:
      return ProfileEmptyBackendId::UART485;
    case SensorProfileId::CYD3S1612_IIC:
      return ProfileEmptyBackendId::IIC;
    case SensorProfileId::NONE:
      return ProfileEmptyBackendId::NONE;
  }
  return ProfileEmptyBackendId::NONE;
}

constexpr const char* kModelBindingKey = "mdl_bind_v1";
constexpr size_t kModelBindingLength = 24;

uint32_t modelBindingCrc(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }
  return ~crc;
}

uint32_t modelFloatBits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float32 required for model binding");
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void writeModelU32(uint8_t* bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t readModelU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
      (static_cast<uint32_t>(bytes[1]) << 8) |
      (static_cast<uint32_t>(bytes[2]) << 16) |
      (static_cast<uint32_t>(bytes[3]) << 24);
}

uint32_t modelFingerprint(const CalibrationModel& model) {
  uint8_t bytes[17]{};
  bytes[0] = static_cast<uint8_t>(model.type);
  writeModelU32(bytes + 1, modelFloatBits(model.referenceDistance));
  writeModelU32(bytes + 5, modelFloatBits(model.coefficients[0]));
  writeModelU32(bytes + 9, modelFloatBits(model.coefficients[1]));
  writeModelU32(bytes + 13, modelFloatBits(model.coefficients[2]));
  return modelBindingCrc(bytes, sizeof(bytes));
}

void encodeModelBinding(
    SensorProfileId profile,
    uint32_t configGeneration,
    uint32_t emptyRevision,
    const CalibrationModel& model,
    uint8_t (&bytes)[kModelBindingLength]) {
  memset(bytes, 0, sizeof(bytes));
  memcpy(bytes, "SWMB", 4);
  bytes[4] = 1;
  bytes[5] = static_cast<uint8_t>(profile);
  writeModelU32(bytes + 8, configGeneration);
  writeModelU32(bytes + 12, emptyRevision);
  writeModelU32(bytes + 16, modelFingerprint(model));
  writeModelU32(bytes + 20, modelBindingCrc(bytes, 20));
}

bool modelBindingMatches(
    Preferences& preferences,
    SensorProfileId profile,
    uint32_t configGeneration,
    uint32_t emptyRevision,
    const CalibrationModel& model) {
  if (preferences.getType(kModelBindingKey) != PT_BLOB ||
      preferences.getBytesLength(kModelBindingKey) != kModelBindingLength) {
    return false;
  }
  uint8_t expected[kModelBindingLength]{};
  uint8_t actual[kModelBindingLength]{};
  encodeModelBinding(profile, configGeneration, emptyRevision, model, expected);
  return preferences.getBytes(kModelBindingKey, actual, sizeof(actual)) ==
             sizeof(actual) &&
      readModelU32(actual + 20) == modelBindingCrc(actual, 20) &&
      memcmp(actual, expected, sizeof(expected)) == 0;
}

bool clearModelBinding(Preferences& preferences) {
  if (!preferences.isKey(kModelBindingKey)) return true;
  return preferences.remove(kModelBindingKey) &&
      !preferences.isKey(kModelBindingKey);
}

bool saveModelBinding(
    Preferences& preferences,
    SensorProfileId profile,
    uint32_t configGeneration,
    uint32_t emptyRevision,
    const CalibrationModel& model) {
  uint8_t bytes[kModelBindingLength]{};
  encodeModelBinding(profile, configGeneration, emptyRevision, model, bytes);
  if (preferences.putBytes(kModelBindingKey, bytes, sizeof(bytes)) != sizeof(bytes)) {
    return false;
  }
  return modelBindingMatches(
      preferences, profile, configGeneration, emptyRevision, model);
}

class ScopedSemaphore {
public:
  explicit ScopedSemaphore(SemaphoreHandle_t semaphore)
      : semaphore_(semaphore),
        held_(semaphore && xSemaphoreTake(semaphore, portMAX_DELAY) == pdTRUE) {}
  ~ScopedSemaphore() {
    if (held_) xSemaphoreGive(semaphore_);
  }
  bool held() const { return held_; }

private:
  SemaphoreHandle_t semaphore_ = nullptr;
  bool held_ = false;
};

}  // namespace

const char* LaserModule::calibrationModelTypeName(CalibrationModelType type) {
  switch (type) {
    case CalibrationModelType::LINEAR:
      return "LINEAR";
    case CalibrationModelType::QUADRATIC:
      return "QUADRATIC";
  }
  return "UNKNOWN";
}

bool LaserModule::measurementBypassActive() const {
  return !deviceConfig.laserInstalled;
}

void LaserModule::logConfigTruth(const char* source, const char* reason) {
  const bool measurementBypass = measurementBypassActive();
  SW_VERBOSE_SERIAL_PRINTF(
      "[LAYER:CONFIG_TRUTH] measurement_bypass=%d platform_model=%s laser_installed=%d laser_available=%d measurement_health=%s protection_degraded=%d source=%s",
      measurementBypass ? 1 : 0,
      platformModelName(deviceConfig.platformModel),
      deviceConfig.laserInstalled ? 1 : 0,
      laserAvailable() ? 1 : 0,
      measurementHealthStateName(measurementHealth()),
      protectionDegraded() ? 1 : 0,
      source ? source : "unknown");
  if (reason && reason[0] != '\0') {
    SW_VERBOSE_SERIAL_PRINTF(" reason=%s", reason);
  }
  SW_VERBOSE_SERIAL_PRINTF("\n");
  hasLoggedMeasurementBypassState = true;
  lastLoggedMeasurementBypassState = measurementBypass;
}

void LaserModule::begin(EventBus* eb, SystemStateMachine* fsm, WaveObservation* waveObservation) {
  bus = eb;
  sm = fsm;
  this->waveObservation = waveObservation;
  if (sm) sm->attachLaserModule(this);
  lastObservedTopState = sm ? sm->state() : TopState::IDLE;
  phase2Thresholds = phase2ThresholdConfig();

  profileTransitionMutex = xSemaphoreCreateMutex();
  preferencesOpened = preferences.begin("scale_cal", false);
  loadDeviceConfig();
  resetMeasurementHealth("boot", millis());
  const LegacyScaleParams legacyParams = preferencesOpened
      ? calibrationModelStore.loadLegacyParams(preferences)
      : LegacyScaleParams{};
  zeroDistance = legacyParams.zeroDistance;
  scaleFactor  = legacyParams.scaleFactor;
  // The persistent profile selects one physical reader. Base does not touch
  // UART/IIC or wait for either external sensor during startup.
  const bool bootConfigReadable =
      configStatus == DeviceConfigStoreStatus::BLANK ||
      configStatus == DeviceConfigStoreStatus::LEGACY ||
      configStatus == DeviceConfigStoreStatus::LEGACY_RESERVED ||
      configStatus == DeviceConfigStoreStatus::READY;
  const SensorProfileId bootProfile = bootConfigReadable
      ? deviceConfig.sensorProfile
      : SensorProfileId::NONE;
  if (!measurementReader.configure(bootProfile)) {
    configStatus = DeviceConfigStoreStatus::UNSUPPORTED_PROFILE;
    measurementReader.stop();
  }
  loadEmptyDistance();
  loadCalibrationModel();

  SW_VERBOSE_SERIAL_PRINTF("\n=== LaserModule boot ===\nZero=%.2f K=%.4f\n", zeroDistance, scaleFactor);
  SW_VERBOSE_SERIAL_PRINTF("[DEVICE] CONFIG platform_model=%s laser_installed=%d\n",
      platformModelName(deviceConfig.platformModel),
      deviceConfig.laserInstalled ? 1 : 0);
  logConfigTruth("boot");
  SW_VERBOSE_SERIAL_PRINTF("[CAL] MODEL type=%s ref=%.2f c0=%.6f c1=%.6f c2=%.6f\n",
      calibrationModelTypeName(calibrationModel.type),
      calibrationModel.referenceDistance,
      calibrationModel.coefficients[0],
      calibrationModel.coefficients[1],
      calibrationModel.coefficients[2]);
  HumanReadableLog::measurement("激光测量模块已初始化");

  configureDistanceStateChainForProfile("boot");

  needSendParams = true;
  rhythmStateJudge.configure(phase2Thresholds.rhythm);
  rhythmStateJudge.reset("boot");
  refreshEffectiveZero();
  resetStableSignalFilter();
  syncStableLiveContract(millis());
  clearStableContractBridge("boot");
  resetMeasurementPlane("boot", false);
}

void LaserModule::startTask() {
  xTaskCreatePinnedToCore(taskThunk, "LaserTask", 4096, this, 2, NULL, 1);
  SW_VERBOSE_SERIAL_PRINTLN("[OK] LaserModule started");
  HumanReadableLog::measurement("测量任务已启动");
}

void LaserModule::triggerZero() {
  needZero = true;
}

bool LaserModule::setParams(float zero, float factor, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  // Guard calibration factor from invalid/non-positive input.
  if (factor <= 0.0f) factor = 1.0f;

  CalibrationModel model{};
  model.type = CalibrationModelType::LINEAR;
  model.referenceDistance = zero;
  model.coefficients[0] = 0.0f;
  model.coefficients[1] = factor;
  model.coefficients[2] = 0.0f;

  if (!applyCalibrationModel(model, true, "scale_cal", reason)) {
    SW_VERBOSE_SERIAL_PRINTF("[CAL] SET legacy linear rejected reason=%s\n", reason.c_str());
    return false;
  }

  resetStableTracking("scale_cal", true);
  resetMeasurementPlane("scale_cal", true);
  releaseOccupiedCycle("scale_cal", millis());
  rhythmStateJudge.reset("scale_cal_reset");
  clearStableContractBridge("scale_cal");
  if (sm) {
    logStartReadyWriteback(millis(), "scale_cal", sm->state(), false, 0.0f, "scale_cal");
    sm->setStartReadiness(false, 0.0f);
  }
  needSendParams = true;
  return true;
}

void LaserModule::getParams(float &zero, float &factor) const {
  zero = zeroDistance;
  factor = scaleFactor;
}

float LaserModule::getWeightKg() const {
  return latestWeightKg;
}

bool LaserModule::isUserPresent() const {
  return stableContract.userPresent;
}

bool LaserModule::baselineReady() const {
  return stableContract.baselineReadyLatched;
}

float LaserModule::stableWeightKg() const {
  return stableContract.stableReadyLive ? stableBaselineWeight : 0.0f;
}

PlatformModel LaserModule::platformModel() const {
  return deviceConfig.platformModel;
}

bool LaserModule::laserInstalled() const {
  return deviceConfig.laserInstalled;
}

bool LaserModule::laserAvailable() const {
  return configOperational() && deviceConfig.laserInstalled &&
      lastMeasurementValid &&
      measurementHealthMachine.state() == MeasurementHealthState::READY &&
      currentSampleValid &&
      currentSampleProfile == deviceConfig.sensorProfile &&
      currentSampleReaderEpoch == measurementReader.readerEpoch() &&
      currentSampleAtMs != 0 &&
      (millis() - currentSampleAtMs) <=
          sampleFreshnessLimitMs(deviceConfig.sensorProfile);
}

MeasurementHealthState LaserModule::measurementHealth() const {
  if (!deviceConfig.laserInstalled) {
    return MeasurementHealthState::FAULT;
  }
  return measurementHealthMachine.state();
}

bool LaserModule::measurementFaultConfirmed() const {
  return configOperational() && deviceConfig.laserInstalled &&
      measurementHealthMachine.faultConfirmed();
}

bool LaserModule::measurementStartupResolved() const {
  if (!configOperational() || !deviceConfig.laserInstalled) {
    return true;
  }
  return measurementHealthMachine.startupResolved();
}

bool LaserModule::protectionDegraded() const {
  if (!deviceConfig.laserInstalled) {
    return true;
  }
  if (!configOperational()) return false;
  return measurementFaultConfirmed();
}

void LaserModule::getDeviceConfig(DeviceConfigSnapshot& out) const {
  out = deviceConfig;
}

bool LaserModule::configOperational() const {
  if (!profileTransitionMutex || configTransitionActive ||
      !configIdentityValid() ||
      configStatus == DeviceConfigStoreStatus::LEGACY_RESERVED) {
    return false;
  }
  if (deviceConfig.platformModel == PlatformModel::BASE) {
    return deviceConfig.sensorProfile == SensorProfileId::NONE;
  }
  return deviceConfig.platformModel == PlatformModel::PLUS &&
      deviceConfig.sensorProfile != SensorProfileId::NONE &&
      isKnownSensorProfile(deviceConfig.sensorProfile) &&
      measurementReader.selectedProfile() == deviceConfig.sensorProfile;
}

bool LaserModule::configIdentityValid() const {
  return configStatus == DeviceConfigStoreStatus::BLANK ||
      configStatus == DeviceConfigStoreStatus::LEGACY ||
      configStatus == DeviceConfigStoreStatus::LEGACY_RESERVED ||
      configStatus == DeviceConfigStoreStatus::READY;
}

bool LaserModule::startConfigurationComplete() const {
  if (!configOperational()) return false;
  if (deviceConfig.platformModel == PlatformModel::BASE) {
    return true;
  }
  return deviceConfig.platformModel == PlatformModel::PLUS &&
      emptyDistanceReady &&
      isfinite(emptyDistanceRecord.distanceRuntime) &&
      isfinite(emptyDistanceRecord.toleranceRuntime);
}

bool LaserModule::protectedStartPolicyAvailable() const {
  if (!configOperational()) return false;
  if (deviceConfig.platformModel == PlatformModel::BASE) return true;
  // J100 is the sole current profile with an accepted protected path.
  // CYD can collect measurements/D_e but its protection policy awaits
  // labelled, real-device validation.
  return deviceConfig.platformModel == PlatformModel::PLUS &&
      deviceConfig.sensorProfile == SensorProfileId::J100_485;
}

bool LaserModule::getProfileConfig(DeviceConfigSnapshot& out, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  const DeviceConfigLoadResult persisted =
      deviceConfigStore.load(preferences, preferencesOpened);
  if (persisted.status == DeviceConfigStoreStatus::LEGACY_RESERVED &&
      configStatus == DeviceConfigStoreStatus::LEGACY_RESERVED &&
      persisted.config.platformModel == deviceConfig.platformModel &&
      persisted.config.sensorProfile == deviceConfig.sensorProfile &&
      persisted.config.generation == deviceConfig.generation &&
      persisted.config.schema1Managed == deviceConfig.schema1Managed) {
    // The new Base/Plus contract does not represent reserved legacy models.
    // A maintenance GET must not mutate an otherwise valid Pro/Ultra device.
    reason = "UNSUPPORTED_MODEL";
    return false;
  }
  const bool readable =
      persisted.status == DeviceConfigStoreStatus::BLANK ||
      persisted.status == DeviceConfigStoreStatus::LEGACY ||
      persisted.status == DeviceConfigStoreStatus::READY;
  const bool matchesMemory =
      persisted.config.platformModel == deviceConfig.platformModel &&
      persisted.config.sensorProfile == deviceConfig.sensorProfile &&
      persisted.config.generation == deviceConfig.generation &&
      persisted.config.schema1Managed == deviceConfig.schema1Managed;
  if (!readable || !matchesMemory) {
    configStatus = DeviceConfigStoreStatus::CONFIG_ERROR;
    measurementReader.stop();
    stopWaveForConfigurationFault("config_get_error");
    applyDeviceConfigRuntimeEffects("config_get_error");
    reason = "CONFIG_ERROR";
    SW_VERBOSE_SERIAL_PRINTF(
        "[DEVICE] CONFIG GET result=reject status=%s match=%d\n",
        DeviceConfigStore::statusName(persisted.status),
        matchesMemory ? 1 : 0);
    return false;
  }
  configStatus = persisted.status;
  out = persisted.config;
  return true;
}

bool LaserModule::setProfileConfig(
    PlatformModel nextPlatformModel,
    SensorProfileId nextSensorProfile,
    String& reason) {
  return applyStoredConfigTransition(
      nextPlatformModel, nextSensorProfile, false, "device_set_profile", reason);
}

bool LaserModule::setDeviceConfig(
    PlatformModel nextPlatformModel,
    bool nextLaserInstalled,
    String& reason) {
  if (!isKnownPlatformModel(nextPlatformModel)) {
    reason = "INVALID_PLATFORM_MODEL";
    return false;
  }

  if (!isLaserInstallAllowedForPlatformModel(nextPlatformModel, nextLaserInstalled)) {
    reason = "LASER_INSTALL_CONSTRAINT_VIOLATION";
    SW_VERBOSE_SERIAL_PRINTF(
        "[DEVICE] CONFIG REJECT source=device_set_config platform_model=%s laser_installed=%d constraint=%s\n",
        platformModelName(nextPlatformModel),
        nextLaserInstalled ? 1 : 0,
        laserInstallConstraintName(platformModelLaserInstallConstraint(nextPlatformModel)));
    return false;
  }

  SensorProfileId nextProfile = SensorProfileId::NONE;
  if (nextPlatformModel == PlatformModel::PLUS &&
      deviceConfig.platformModel == PlatformModel::PLUS) {
    nextProfile = deviceConfig.sensorProfile;
  }
  const bool reserved = nextPlatformModel == PlatformModel::PRO ||
      nextPlatformModel == PlatformModel::ULTRA;
  return applyStoredConfigTransition(
      nextPlatformModel, nextProfile, reserved, "device_set_config_legacy", reason);
}

bool LaserModule::applyStoredConfigTransition(
    PlatformModel nextPlatformModel,
    SensorProfileId nextSensorProfile,
    bool legacyReserved,
    const char* source,
    String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  const bool readable =
      configStatus == DeviceConfigStoreStatus::BLANK ||
      configStatus == DeviceConfigStoreStatus::LEGACY ||
      configStatus == DeviceConfigStoreStatus::LEGACY_RESERVED ||
      configStatus == DeviceConfigStoreStatus::READY;
  if (!readable) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (!isKnownSensorProfile(nextSensorProfile) ||
      !LaserMeasurementReader::supportsProfile(nextSensorProfile)) {
    reason = "UNSUPPORTED_PROFILE";
    return false;
  }
  if (nextPlatformModel == PlatformModel::BASE &&
      nextSensorProfile != SensorProfileId::NONE) {
    reason = "INVALID_COMBINATION";
    return false;
  }

  const DeviceConfigSnapshot previous = deviceConfig;
  const bool shouldChange =
      nextPlatformModel != previous.platformModel ||
      nextSensorProfile != previous.sensorProfile;
  if (shouldChange) {
    // LaserTask holds the same transition mutex while consuming a sample.
    // Once we own it no old sample can finish processing after the SET ACK.
    configTransitionActive = true;
    resetStreamSubscription(source);
    measurementReader.stop();
    currentSampleValid = false;
    applyDeviceConfigRuntimeEffects("profile_switch_prepare");
  }

  const DeviceConfigSaveResult saved = legacyReserved
      ? deviceConfigStore.saveLegacyReserved(
            preferences, preferencesOpened, previous, nextPlatformModel)
      : deviceConfigStore.save(
            preferences, preferencesOpened, previous,
            nextPlatformModel, nextSensorProfile);
  if (!saved.ok()) {
    // NVS writes span distinct keys. A failed write can leave the mirror and
    // slots inconsistent; only a fresh direct load may authorize the old
    // reader to resume, and its readiness must build from new samples.
    const DeviceConfigLoadResult reloaded =
        deviceConfigStore.load(preferences, preferencesOpened);
    const bool previousRecovered =
        (reloaded.status == DeviceConfigStoreStatus::BLANK ||
         reloaded.status == DeviceConfigStoreStatus::LEGACY ||
         reloaded.status == DeviceConfigStoreStatus::LEGACY_RESERVED ||
         reloaded.status == DeviceConfigStoreStatus::READY) &&
        reloaded.config.platformModel == previous.platformModel &&
        reloaded.config.sensorProfile == previous.sensorProfile &&
        reloaded.config.generation == previous.generation;
    if (previousRecovered &&
        measurementReader.configure(previous.sensorProfile)) {
      deviceConfig = reloaded.config;
      configStatus = reloaded.status;
      // A first schema-1 migration may have committed its baseline slot
      // before the target write failed. Re-read D_e/model under the recovered
      // identity; an old legacy anchor cannot be promoted into schema 1.
      loadEmptyDistance();
      loadCalibrationModel();
    } else {
      measurementReader.stop();
      configStatus = DeviceConfigStoreStatus::CONFIG_ERROR;
      stopWaveForConfigurationFault("profile_set_storage_uncertain");
    }
    configTransitionActive = false;
    applyDeviceConfigRuntimeEffects("profile_switch_failed");
    switch (saved.error) {
      case DeviceConfigStoreError::UNSUPPORTED_MODEL:
        reason = "UNSUPPORTED_MODEL";
        break;
      case DeviceConfigStoreError::INVALID_CONFIG:
        reason = "INVALID_COMBINATION";
        break;
      case DeviceConfigStoreError::STORAGE_FAILED:
        reason = "STORAGE_FAILED";
        break;
      case DeviceConfigStoreError::VERIFY_FAILED:
        reason = "VERIFY_FAILED";
        break;
      default:
        reason = "CONFIG_ERROR";
        break;
    }
    SW_VERBOSE_SERIAL_PRINTF(
        "[DEVICE] CONFIG SET result=reject source=%s reason=%s storage_error=%s reload_status=%s old_recovered=%d\n",
        source ? source : "unknown", reason.c_str(),
        DeviceConfigStore::errorName(saved.error),
        DeviceConfigStore::statusName(reloaded.status),
        previousRecovered ? 1 : 0);
    return false;
  }

  deviceConfig = saved.config;
  configStatus = (deviceConfig.platformModel == PlatformModel::PRO ||
                  deviceConfig.platformModel == PlatformModel::ULTRA)
      ? DeviceConfigStoreStatus::LEGACY_RESERVED
      : (deviceConfig.schema1Managed
             ? DeviceConfigStoreStatus::READY
             : configStatus);
  if (saved.changed) {
    emptyDistanceReady = false;
    calibrationModelReady = false;
    emptyDistanceRecord = EmptyDistanceRecord{};
    currentSampleValid = false;
    if (!measurementReader.configure(deviceConfig.sensorProfile)) {
      // Persistence already succeeded. Keep the new identity and fail closed
      // instead of silently resuming the old reader.
      configStatus = DeviceConfigStoreStatus::UNSUPPORTED_PROFILE;
      configTransitionActive = false;
      applyDeviceConfigRuntimeEffects("profile_reader_unsupported");
      reason = "UNSUPPORTED_PROFILE";
      return false;
    }
    loadEmptyDistance();
    loadCalibrationModel();
    configTransitionActive = false;
    applyDeviceConfigRuntimeEffects(source);
  } else {
    configTransitionActive = false;
  }

  SW_VERBOSE_SERIAL_PRINTF(
      "[DEVICE] CONFIG SET result=ok source=%s platform_model=%s sensor_profile=%s generation=%lu changed=%d reader_epoch=%lu\n",
      source ? source : "unknown",
      platformModelName(deviceConfig.platformModel),
      sensorProfileName(deviceConfig.sensorProfile),
      static_cast<unsigned long>(deviceConfig.generation),
      saved.changed ? 1 : 0,
      static_cast<unsigned long>(measurementReader.readerEpoch()));
  return true;
}

bool LaserModule::getCalibrationModel(CalibrationModel& out) const {
  // Do not expose stale coefficients from a previous profile/D_e revision as
  // if they were the current device's calibration parameters.
  const bool ready = calibrationModelReady && configOperational() &&
      deviceConfig.laserInstalled;
  out = ready ? calibrationModel : CalibrationModel{};
  return ready;
}

const char* LaserModule::sensorBackendName() const {
  return laserMeasurementBackendName(measurementReader.backend());
}

bool LaserModule::getEmptyDistance(EmptyDistanceRecord& out) const {
  if (!emptyDistanceReady ||
      !isfinite(emptyDistanceRecord.distanceRuntime) ||
      !isfinite(emptyDistanceRecord.toleranceRuntime)) {
    out = EmptyDistanceRecord{};
    return false;
  }
  out = emptyDistanceRecord;
  return true;
}

bool LaserModule::getProfileEmptyDistance(
    ProfileEmptyDistanceRecord& out, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held() || !deviceConfig.schema1Managed) {
    reason = "UNSUPPORTED";
    return false;
  }
  if (configStatus != DeviceConfigStoreStatus::READY) {
    reason = "CONFIG_ERROR";
    return false;
  }
  const ProfileEmptyLoadResult loaded = profileEmptyDistanceStore.load(
      preferences, preferencesOpened,
      deviceConfig.sensorProfile, deviceConfig.generation);
  if (loaded.status == ProfileEmptyStoreStatus::STORAGE_UNAVAILABLE ||
      loaded.status == ProfileEmptyStoreStatus::CONFIG_ERROR ||
      loaded.status == ProfileEmptyStoreStatus::UNSUPPORTED_SCHEMA ||
      loaded.status == ProfileEmptyStoreStatus::UNSUPPORTED_PROFILE) {
    invalidateEmptyReferenceAfterStorageFailure("empty_get_storage_error");
    reason = "CONFIG_ERROR";
    return false;
  }
  out = ProfileEmptyDistanceRecord{};
  out.sensorProfile = deviceConfig.sensorProfile;
  out.configGeneration = deviceConfig.generation;
  out.emptyRevision = loaded.record.emptyRevision;
  if (loaded.status == ProfileEmptyStoreStatus::SET) {
    const float runtimeDistance =
        loaded.record.distanceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
    if (!emptyDistanceReady ||
        !isfinite(emptyDistanceRecord.distanceRuntime) ||
        fabsf(runtimeDistance - emptyDistanceRecord.distanceRuntime) >
            kModelReferenceEpsilonRuntime) {
      invalidateEmptyReferenceAfterStorageFailure("empty_get_mismatch");
      reason = "CONFIG_ERROR";
      return false;
    }
    out = loaded.record;
  } else if (emptyDistanceReady) {
    invalidateEmptyReferenceAfterStorageFailure("empty_get_unexpected_unset");
    reason = "CONFIG_ERROR";
    return false;
  }
  return true;
}

bool LaserModule::setEmptyDistanceFromCurrent(
    EmptyDistanceRecord& out, String& reason) {
  ProfileEmptyDistanceRecord profileRecord{};
  if (!setEmptyDistanceFromCurrent(profileRecord, reason)) return false;
  return getEmptyDistance(out);
}

bool LaserModule::setEmptyDistanceFromCurrent(
    ProfileEmptyDistanceRecord& out, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  if (!deviceConfig.schema1Managed ||
      deviceConfig.platformModel != PlatformModel::PLUS ||
      deviceConfig.sensorProfile == SensorProfileId::NONE) {
    reason = "UNSUPPORTED_PROFILE";
    return false;
  }
  if (!configOperational()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  const uint32_t now = millis();
  const uint32_t freshLimitMs = sampleFreshnessLimitMs(deviceConfig.sensorProfile);
  if (!currentSampleValid || !lastMeasurementValid ||
      measurementHealthMachine.state() != MeasurementHealthState::READY ||
      !isfinite(currentSamplePhysicalMm) || currentSamplePhysicalMm <= 0.0f ||
      currentSampleAtMs == 0 || freshLimitMs == 0 ||
      now - currentSampleAtMs > freshLimitMs ||
      currentSampleSequence == 0 ||
      currentSampleReaderEpoch != measurementReader.readerEpoch() ||
      currentSampleProfile != deviceConfig.sensorProfile ||
      currentSampleConfigGeneration != deviceConfig.generation) {
    reason = "NO_FRESH_SAMPLE";
    return false;
  }

  const float toleranceMm =
      deviceConfig.sensorProfile == SensorProfileId::CYD3S1612_IIC
          ? kCydEmptyDistanceToleranceMm
          : kJ100EmptyDistanceToleranceMm;
  // Revoke the old weight model before committing a new D_e revision. A
  // damaged newest slot must not let an older anchor revive that old model.
  if (!clearModelBinding(preferences)) {
    invalidateEmptyReferenceAfterStorageFailure(
        "empty_set_binding_failed");
    reason = "NVS_WRITE_FAILED";
    return false;
  }
  const ProfileEmptySaveResult saved = profileEmptyDistanceStore.set(
      preferences, preferencesOpened,
      deviceConfig.sensorProfile, deviceConfig.generation,
      currentSamplePhysicalMm, toleranceMm,
      profileEmptyBackendFor(deviceConfig.sensorProfile));
  if (!saved.ok()) {
    invalidateEmptyReferenceAfterStorageFailure("empty_set_storage_failed");
    reason = ProfileEmptyDistanceStore::errorName(saved.error);
    return false;
  }
  out = saved.record;
  emptyDistanceRecord.distanceRuntime =
      saved.record.distanceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  emptyDistanceRecord.toleranceRuntime =
      saved.record.toleranceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  emptyDistanceRecord.backend = static_cast<uint8_t>(measurementReader.backend());
  emptyDistanceReady = true;
  // Any new D_e revision invalidates a previously fitted model. The old
  // model bytes remain available for audit but cannot be used as current.
  calibrationModelReady = false;
  dualZero.calibrationZeroDistance = emptyDistanceRecord.distanceRuntime;
  refreshEffectiveZero();
  distanceStateChain.reset("empty_set_current");
  distanceStateChain.setEmptyReference(emptyDistanceRecord.distanceRuntime, true);
  resetRuntimeZero("empty_set_current");
  resetStableTracking("empty_set_current", true);
  resetMeasurementPlane("empty_set_current", true);
  rhythmStateJudge.reset("empty_set_current");
  clearStableContractBridge("empty_set_current");
  if (sm) {
    sm->setRuntimeReady(false);
    sm->setStartReadiness(false, 0.0f);
  }
  SW_VERBOSE_SERIAL_PRINTF(
      "[EMPTY_DISTANCE] action=set_current result=ok distance_mm=%.2f tolerance_mm=%.2f profile=%s generation=%lu empty_revision=%lu sample_seq=%lu sample_age_ms=%lu\n",
      saved.record.distanceMm,
      saved.record.toleranceMm,
      sensorProfileName(saved.record.sensorProfile),
      static_cast<unsigned long>(saved.record.configGeneration),
      static_cast<unsigned long>(saved.record.emptyRevision),
      static_cast<unsigned long>(currentSampleSequence),
      static_cast<unsigned long>(now - currentSampleAtMs));
  return true;
}

bool LaserModule::setEmptyDistance(float distanceMm, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (deviceConfig.schema1Managed) {
    reason = "USE_SET_CURRENT";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  if (!deviceConfig.laserInstalled) {
    reason = "UNSUPPORTED";
    return false;
  }
  const LaserMeasurementBackend backend = measurementReader.backend();
  if (backend != LaserMeasurementBackend::LASER_485 &&
      backend != LaserMeasurementBackend::CYD3S1612_IIC) {
    reason = (backend == LaserMeasurementBackend::SENSOR_CONFLICT)
        ? "SENSOR_CONFLICT"
        : "SENSOR_UNAVAILABLE";
    return false;
  }
  if (!isfinite(distanceMm) || distanceMm <= 0.0f) {
    reason = "INVALID_DISTANCE";
    return false;
  }

  const float runtimeDistance = distanceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  const float minRuntime = backend == LaserMeasurementBackend::LASER_485
      ? LASER_WANG_VALID_RUNTIME_DISTANCE_MIN
      : LASER_VALID_RUNTIME_DISTANCE_MIN;
  const float maxRuntime = backend == LaserMeasurementBackend::LASER_485
      ? LASER_WANG_VALID_RUNTIME_DISTANCE_MAX
      : LASER_VALID_RUNTIME_DISTANCE_MAX;
  if (!isfinite(runtimeDistance) || runtimeDistance < minRuntime || runtimeDistance > maxRuntime) {
    reason = "DISTANCE_OUT_OF_RANGE";
    return false;
  }

  const bool modelReferenceChanged = calibrationModelReady &&
      fabsf(calibrationModel.referenceDistance - runtimeDistance) >
          kModelReferenceEpsilonRuntime;
  EmptyDistanceRecord record{};
  record.distanceRuntime = runtimeDistance;
  const float emptyToleranceMm = backend == LaserMeasurementBackend::CYD3S1612_IIC
      ? kCydEmptyDistanceToleranceMm
      : kJ100EmptyDistanceToleranceMm;
  record.toleranceRuntime = emptyToleranceMm *
      LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
  record.backend = static_cast<uint8_t>(backend);
  if (!calibrationModelStore.saveEmptyDistance(preferences, record)) {
    reason = "NVS_WRITE_FAILED";
    SW_VERBOSE_SERIAL_PRINTLN("[EMPTY_DISTANCE] action=set result=failed reason=NVS_WRITE_FAILED");
    return false;
  }

  // A weight model is defined in the coordinate system whose origin is D_e.
  // Changing D_e invalidates the old model; clear only that model and keep
  // the newly stored empty-distance record.
  if (modelReferenceChanged) {
    if (!calibrationModelStore.clearModel(preferences)) {
      reason = "NVS_WRITE_FAILED";
      SW_VERBOSE_SERIAL_PRINTLN("[EMPTY_DISTANCE] action=set result=failed reason=NVS_WRITE_FAILED model_clear=1");
      return false;
    }
  }
  emptyDistanceRecord = record;
  emptyDistanceReady = true;
  if (modelReferenceChanged) {
    calibrationModelReady = false;
    calibrationModel = CalibrationModel{};
    calibrationModel.referenceDistance = runtimeDistance;
    calibrationModel.coefficients[0] = 0.0f;
    calibrationModel.coefficients[1] = 1.0f;
    calibrationModel.coefficients[2] = 0.0f;
    zeroDistance = runtimeDistance;
    scaleFactor = 1.0f;
  }
  dualZero.calibrationZeroDistance = runtimeDistance;
  refreshEffectiveZero();

  distanceStateChain.reset("empty_distance_set");
  distanceStateChain.setEmptyReference(runtimeDistance, true);
  resetRuntimeZero("empty_distance_set");
  resetStableTracking("empty_distance_set", true);
  resetMeasurementPlane("empty_distance_set", true);
  rhythmStateJudge.reset("empty_distance_set");
  clearStableContractBridge("empty_distance_set");
  if (sm) {
    sm->setRuntimeReady(false);
    sm->setStartReadiness(false, 0.0f);
  }
  SW_VERBOSE_SERIAL_PRINTF(
      "[EMPTY_DISTANCE] action=set distance_mm=%.2f tolerance_mm=%.2f backend=%s model_cleared=%d\n",
      distanceMm,
      record.toleranceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
      sensorBackendName(),
      modelReferenceChanged ? 1 : 0);
  return true;
}

bool LaserModule::clearEmptyDistance(String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  if (deviceConfig.schema1Managed) {
    if (configStatus != DeviceConfigStoreStatus::READY ||
        deviceConfig.platformModel != PlatformModel::PLUS ||
        deviceConfig.sensorProfile == SensorProfileId::NONE) {
      reason = "UNSUPPORTED_PROFILE";
      return false;
    }
    // Revoke the old model before writing the D_e tombstone. Even if a later
    // slot is damaged and an older SET record reappears, its model binding
    // cannot become current again after a successful CLEAR.
    if (!clearModelBinding(preferences)) {
      invalidateEmptyReferenceAfterStorageFailure(
          "empty_clear_binding_failed");
      reason = "NVS_WRITE_FAILED";
      return false;
    }
    const ProfileEmptySaveResult saved = profileEmptyDistanceStore.clear(
        preferences, preferencesOpened,
        deviceConfig.sensorProfile, deviceConfig.generation);
    if (!saved.ok()) {
      invalidateEmptyReferenceAfterStorageFailure("empty_clear_storage_failed");
      reason = ProfileEmptyDistanceStore::errorName(saved.error);
      return false;
    }
    emptyDistanceRecord = EmptyDistanceRecord{};
    emptyDistanceReady = false;
    calibrationModelReady = false;
    dualZero.calibrationZeroDistance = 0.0f;
    refreshEffectiveZero();
    distanceStateChain.reset("empty_distance_clear");
    distanceStateChain.clearEmptyReference();
    resetRuntimeZero("empty_distance_clear");
    resetStableTracking("empty_distance_clear", true);
    resetMeasurementPlane("empty_distance_clear", true);
    rhythmStateJudge.reset("empty_distance_clear");
    clearStableContractBridge("empty_distance_clear");
    if (sm) {
      sm->setRuntimeReady(false);
      sm->setStartReadiness(false, 0.0f);
    }
    SW_VERBOSE_SERIAL_PRINTF(
        "[EMPTY_DISTANCE] action=clear result=ok profile=%s generation=%lu empty_revision=%lu\n",
        sensorProfileName(saved.record.sensorProfile),
        static_cast<unsigned long>(saved.record.configGeneration),
        static_cast<unsigned long>(saved.record.emptyRevision));
    return true;
  }
  // Complete both persistent clears before mutating the in-memory contract.
  // A failed NVS operation must reach the command handler as a NACK.
  if (!calibrationModelStore.clearModel(preferences)) {
    reason = "NVS_WRITE_FAILED";
    return false;
  }
  if (!calibrationModelStore.clearEmptyDistance(preferences)) {
    reason = "NVS_WRITE_FAILED";
    return false;
  }
  emptyDistanceRecord = EmptyDistanceRecord{};
  emptyDistanceReady = false;
  calibrationModelReady = false;
  calibrationModel = CalibrationModel{};
  zeroDistance = 0.0f;
  scaleFactor = 1.0f;
  dualZero.calibrationZeroDistance = 0.0f;
  refreshEffectiveZero();
  distanceStateChain.reset("empty_distance_clear");
  distanceStateChain.clearEmptyReference();
  resetRuntimeZero("empty_distance_clear");
  resetStableTracking("empty_distance_clear", true);
  resetMeasurementPlane("empty_distance_clear", true);
  rhythmStateJudge.reset("empty_distance_clear");
  clearStableContractBridge("empty_distance_clear");
  if (sm) {
    sm->setRuntimeReady(false);
    sm->setStartReadiness(false, 0.0f);
  }
  SW_VERBOSE_SERIAL_PRINTLN("[EMPTY_DISTANCE] action=clear result=success");
  return true;
}

bool LaserModule::clearWeightModel(String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  if (!preferencesOpened || !configIdentityValid()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (deviceConfig.schema1Managed && !clearModelBinding(preferences)) {
    calibrationModelReady = false;
    reason = "NVS_WRITE_FAILED";
    return false;
  }
  if (!calibrationModelStore.clearModel(preferences)) {
    calibrationModelReady = false;
    reason = "NVS_WRITE_FAILED";
    return false;
  }
  calibrationModelReady = false;
  calibrationModel = CalibrationModel{};
  calibrationModel.referenceDistance = emptyDistanceReady
      ? emptyDistanceRecord.distanceRuntime
      : 0.0f;
  calibrationModel.coefficients[0] = 0.0f;
  calibrationModel.coefficients[1] = 0.0f;
  calibrationModel.coefficients[2] = 0.0f;
  zeroDistance = calibrationModel.referenceDistance;
  scaleFactor = 1.0f;
  dualZero.calibrationZeroDistance = calibrationModel.referenceDistance;
  refreshEffectiveZero();
  distanceStateChain.reset("weight_model_clear");
  distanceStateChain.setEmptyReference(
      emptyDistanceRecord.distanceRuntime,
      emptyDistanceReady && isfinite(emptyDistanceRecord.distanceRuntime));
  resetRuntimeZero("weight_model_clear");
  resetStableTracking("weight_model_clear", true);
  resetMeasurementPlane("weight_model_clear", true);
  rhythmStateJudge.reset("weight_model_clear");
  clearStableContractBridge("weight_model_clear");
  if (sm) {
    sm->setRuntimeReady(false);
    sm->setStartReadiness(false, 0.0f);
  }
  // CAL:ZERO/SCALE:ZERO used to publish EVT:PARAM after the asynchronous
  // reset. Keep that existing consumer update, but only after NVS confirms
  // the synchronous clear succeeded.
  needSendParams = true;
  SW_VERBOSE_SERIAL_PRINTF(
      "[CAL] CLEAR_WEIGHT_MODEL result=success empty_distance_ready=%d\n",
      emptyDistanceReady ? 1 : 0);
  return true;
}

bool LaserModule::clearCalibrationParameters(String& reason) {
  // Parameter reset is independent of both the selected reader and D_e.
  // The command handler can acknowledge only after NVS readback succeeds.
  return clearWeightModel(reason);
}

bool LaserModule::setCalibrationModel(const CalibrationModel& model, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held() || !configOperational()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (configurationMutationBlocked(reason)) return false;
  if (!applyCalibrationModel(model, true, "cal_set_model", reason)) {
    return false;
  }

  resetStableTracking("cal_model", true);
  resetMeasurementPlane("cal_model", true);
  releaseOccupiedCycle("cal_model", millis());
  rhythmStateJudge.reset("cal_model_reset");
  clearStableContractBridge("cal_model");
  if (sm) {
    logStartReadyWriteback(millis(), "cal_model", sm->state(), false, 0.0f, "cal_model");
    sm->setStartReadiness(false, 0.0f);
  }
  needSendParams = true;
  return true;
}

bool LaserModule::captureCalibrationPoint(float referenceWeightKg, CalibrationCapture& out, String& reason) {
  ScopedSemaphore lock(profileTransitionMutex);
  if (!lock.held() || !configOperational()) {
    reason = "CONFIG_ERROR";
    return false;
  }
  if (!isfinite(referenceWeightKg) || referenceWeightKg < 0.0f) {
    reason = "INVALID_REFERENCE_WEIGHT";
    return false;
  }

  if (!laserAvailable()) {
    reason = "INVALID_SAMPLE";
    return false;
  }

  // Laser calibration is distance-owned. This path remains usable after
  // CAL:ZERO, when no weight model exists yet, so the APP can first record
  // the device-specific 0 kg distance and then record loaded points.
  const bool distanceBackendActive =
      deviceConfig.laserInstalled &&
      (measurementReader.backend() == LaserMeasurementBackend::CYD3S1612_IIC ||
       measurementReader.backend() == LaserMeasurementBackend::LASER_485);
  if (distanceBackendActive) {
    const DistanceStateChainResult& distanceState = distanceStateChain.last();
    const bool isEmptyAnchor = referenceWeightKg <= 0.0001f;
    // The 0 kg point is recorded while the platform is empty, so there is no
    // occupied/user baseline yet. Non-zero points still require the normal
    // occupied baseline contract.
    const bool captureReady = isEmptyAnchor
        ? (distanceState.valid && distanceState.stable && isfinite(distanceState.distance))
        : (distanceState.valid && distanceState.baselineReady &&
           isfinite(distanceState.baselineDistance));
    if (!captureReady) {
      reason = "NOT_STABLE";
      return false;
    }

    calibrationCaptureCounter++;
    out.index = calibrationCaptureCounter;
    out.ts_ms = millis();
    const float capturedDistance = isEmptyAnchor
        ? distanceState.distance
        : distanceState.baselineDistance;
    out.distanceMm = capturedDistance * LASER_DISTANCE_RUNTIME_DIVISOR;
    out.referenceWeightKg = referenceWeightKg;
    const float predicted = calibrationModelReady
        ? evaluateCalibrationWeight(distanceState.baselineDistance)
        : NAN;
    out.predictedWeightKg = isfinite(predicted) ? predicted : 0.0f;
    out.stableFlag = true;
    out.validFlag = true;

    SW_VERBOSE_SERIAL_PRINTF("[CAL] CAPTURE idx=%lu dist_mm=%.2f ref_kg=%.2f pred_kg=%.2f source=DISTANCE_CHAIN\n",
        (unsigned long)out.index,
        out.distanceMm,
        out.referenceWeightKg,
        out.predictedWeightKg);
    return true;
  }

  if (stableState != StableState::STABLE_LATCHED) {
    reason = "NOT_STABLE";
    return false;
  }

  if (!isfinite(stableBaselineDistanceMm) || !isfinite(stableBaselineWeight)) {
    reason = "INVALID_STABLE_BASELINE";
    return false;
  }

  calibrationCaptureCounter++;
  out.index = calibrationCaptureCounter;
  out.ts_ms = stableLatchedAtMs ? stableLatchedAtMs : millis();
  out.distanceMm = stableBaselineDistanceMm;
  out.referenceWeightKg = referenceWeightKg;
  out.predictedWeightKg = stableBaselineWeight;
  out.stableFlag = true;
  out.validFlag = true;

  SW_VERBOSE_SERIAL_PRINTF("[CAL] CAPTURE idx=%lu dist_mm=%.2f ref_kg=%.2f pred_kg=%.2f\n",
      (unsigned long)out.index, out.distanceMm, out.referenceWeightKg, out.predictedWeightKg);

  return true;
}

bool LaserModule::setStreamSubscription(bool enabled, uint8_t rateHz, String& reason) {
  if (rateHz < ProtocolCodec::kStreamMinRateHz || rateHz > ProtocolCodec::kStreamMaxRateHz) {
    reason = "INVALID_PARAM";
    return false;
  }
  streamEnabled = enabled;
  streamRateHz = rateHz;
  lastStreamPublishedAtMs = 0;
  SW_VERBOSE_SERIAL_PRINTF(
      "[STREAM_CONTROL] action=set enabled=%d rate_hz=%u source=STREAM_SET\n",
      streamEnabled ? 1 : 0,
      static_cast<unsigned>(streamRateHz));
  return true;
}

void LaserModule::resetStreamSubscription(const char* reason) {
  const bool wasEnabled = streamEnabled;
  streamEnabled = false;
  streamRateHz = ProtocolCodec::kStreamDefaultRateHz;
  lastStreamPublishedAtMs = 0;
  SW_VERBOSE_SERIAL_PRINTF(
      "[STREAM_CONTROL] action=reset enabled=0 rate_hz=%u previous_enabled=%d reason=%s\n",
      static_cast<unsigned>(streamRateHz),
      wasEnabled ? 1 : 0,
      reason ? reason : "unspecified");
}

bool LaserModule::streamSubscriptionEnabled() const {
  return streamEnabled;
}

uint8_t LaserModule::streamSubscriptionRateHz() const {
  return streamRateHz;
}

float LaserModule::getMean(const float* values) const {
  float sum = 0;
  for (int i = 0; i < bufCount; i++) sum += values[i];
  return (bufCount == 0) ? 0 : (sum / bufCount);
}

float LaserModule::getStdDev(const float* values) const {
  if (bufCount < 2) return 999.0f;
  float mean = getMean(values);
  float sumSqDiff = 0;
  for (int i = 0; i < bufCount; i++) sumSqDiff += pow(values[i] - mean, 2);
  return sqrt(sumSqDiff / bufCount);
}

void LaserModule::taskThunk(void* arg) {
  static_cast<LaserModule*>(arg)->taskLoop();
}

void LaserModule::resetMeasurementPlane(const char* reason, bool logReset) {
  measurementPlane.reset(reason, logReset);
}

void LaserModule::resetMeasurementHealth(const char* reason, uint32_t now) {
  const MeasurementHealthTransition transition =
      measurementHealthMachine.reset(now, deviceConfig.laserInstalled);
  if (transition.changed) {
    logMeasurementHealthChange(now, transition.previous, transition.next, reason);
  }
}

void LaserModule::logMeasurementHealthChange(
    uint32_t now,
    MeasurementHealthState previous,
    MeasurementHealthState next,
    const char* reason) const {
  SW_VERBOSE_SERIAL_PRINTF(
      "[MEASUREMENT_HEALTH] state=%s previous=%s reason=%s ever_ready=%d success=%u failure=%u uptime_ms=%lu\n",
      measurementHealthStateName(next),
      measurementHealthStateName(previous),
      reason ? reason : "unknown",
      measurementHealthMachine.everReady() ? 1 : 0,
      static_cast<unsigned>(measurementHealthMachine.successSamples()),
      static_cast<unsigned>(measurementHealthMachine.failureSamples()),
      static_cast<unsigned long>(now));
  if (next == MeasurementHealthState::READY) {
    HumanReadableLog::measurement("激光传感器已恢复，可以继续测量");
  } else if (next == MeasurementHealthState::FAULT) {
    HumanReadableLog::measurement("激光传感器暂不可用，正在等待恢复");
  }
}

void LaserModule::syncSensorHealthToStateMachine() {
  if (!sm) return;
  if (!deviceConfig.laserInstalled) {
    sm->setSensorHealthy(false);
    return;
  }
  if (measurementHealthMachine.state() == MeasurementHealthState::READY) {
    sm->setSensorHealthy(true);
    return;
  }
  if (measurementHealthMachine.state() == MeasurementHealthState::FAULT) {
    sm->setSensorHealthy(false);
  }
}

void LaserModule::updateMeasurementHealth(
    uint32_t now,
    bool transportOk,
    bool validDistance,
    const char* reason) {
  const MeasurementHealthTransition transition =
      measurementHealthMachine.observe(
          now,
          deviceConfig.laserInstalled,
          transportOk,
          validDistance);
  if (transition.changed) {
    logMeasurementHealthChange(now, transition.previous, transition.next, reason);
  }
  syncSensorHealthToStateMachine();
}

void LaserModule::publishMeasurementSample(
    uint32_t now,
    bool valid,
    float distance,
    float weight,
    const char* reason) {
  publishMeasurementSample(now, valid, valid, distance, weight, reason, reason);
}

void LaserModule::publishMeasurementSample(
    uint32_t now,
    bool distanceValid,
    bool weightValid,
    float distance,
    float weight,
    const char* distanceReason,
    const char* weightReason) {
  const MeasurementPlaneRecordResult sample =
      measurementPlane.record(
          now,
          distanceValid,
          weightValid,
          distance,
          weight,
          distanceReason,
          weightReason,
          laserMeasurementBackendName(measurementReader.backend()));
  if (!sample.shouldPublish) {
    return;
  }
  if (!shouldPublishStreamSample(now)) {
    return;
  }
  if (bus) {
    bus->publish(sample.event);
  }
  measurementPlane.notePublished(sample);
}

bool LaserModule::shouldPublishStreamSample(uint32_t now) {
  if (!streamEnabled) {
    return false;
  }
  const uint8_t rateHz = streamRateHz;
  if (rateHz == 0) {
    return false;
  }
  const uint32_t minIntervalMs = 1000UL / static_cast<uint32_t>(rateHz);
  if (lastStreamPublishedAtMs != 0 && (now - lastStreamPublishedAtMs) < minIntervalMs) {
    return false;
  }
  lastStreamPublishedAtMs = now;
  return true;
}

void LaserModule::logLatestMeasurementPlaneSummary(const char* trigger) {
  measurementPlane.logLatest(trigger);
}

void LaserModule::loadDeviceConfig() {
  const DeviceConfigLoadResult result =
      deviceConfigStore.load(preferences, preferencesOpened);
  deviceConfig = result.config;
  configStatus = result.status;
  SW_VERBOSE_SERIAL_PRINTF(
      "[DEVICE] CONFIG LOAD status=%s platform_model=%s sensor_profile=%s generation=%lu managed=%d laser_installed=%d mirror_normalization_needed=%d storage_opened=%d\n",
      DeviceConfigStore::statusName(configStatus),
      platformModelName(deviceConfig.platformModel),
      sensorProfileName(deviceConfig.sensorProfile),
      static_cast<unsigned long>(deviceConfig.generation),
      deviceConfig.schema1Managed ? 1 : 0,
      deviceConfig.laserInstalled ? 1 : 0,
      result.laserInstalledNormalized ? 1 : 0,
      preferencesOpened ? 1 : 0);
  // A missing/malformed old key is evidence for maintenance, not permission
  // to overwrite NVS with a default Base record during boot.
}

void LaserModule::loadEmptyDistance() {
  if (!preferencesOpened ||
      configStatus == DeviceConfigStoreStatus::STORAGE_UNAVAILABLE ||
      configStatus == DeviceConfigStoreStatus::CONFIG_ERROR ||
      configStatus == DeviceConfigStoreStatus::UNSUPPORTED_SCHEMA ||
      configStatus == DeviceConfigStoreStatus::UNSUPPORTED_PROFILE) {
    emptyDistanceRecord = EmptyDistanceRecord{};
    emptyDistanceReady = false;
    return;
  }
  if (deviceConfig.schema1Managed) {
    emptyDistanceRecord = EmptyDistanceRecord{};
    emptyDistanceReady = false;
    const ProfileEmptyLoadResult loaded = profileEmptyDistanceStore.load(
        preferences, preferencesOpened,
        deviceConfig.sensorProfile, deviceConfig.generation);
    if (loaded.status == ProfileEmptyStoreStatus::SET) {
      const LaserMeasurementBackend backend = measurementReader.backend();
      const bool matchingReader =
          (deviceConfig.sensorProfile == SensorProfileId::J100_485 &&
           backend == LaserMeasurementBackend::LASER_485) ||
          (deviceConfig.sensorProfile == SensorProfileId::CYD3S1612_IIC &&
           backend == LaserMeasurementBackend::CYD3S1612_IIC);
      const float runtimeDistance =
          loaded.record.distanceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
      const float minRuntime = backend == LaserMeasurementBackend::LASER_485
          ? LASER_WANG_VALID_RUNTIME_DISTANCE_MIN
          : LASER_VALID_RUNTIME_DISTANCE_MIN;
      const float maxRuntime = backend == LaserMeasurementBackend::LASER_485
          ? LASER_WANG_VALID_RUNTIME_DISTANCE_MAX
          : LASER_VALID_RUNTIME_DISTANCE_MAX;
      if (matchingReader && isfinite(runtimeDistance) &&
          runtimeDistance >= minRuntime && runtimeDistance <= maxRuntime) {
        emptyDistanceRecord.distanceRuntime = runtimeDistance;
        emptyDistanceRecord.toleranceRuntime =
            loaded.record.toleranceMm * LASER_DISTANCE_MM_TO_RUNTIME_UNITS;
        emptyDistanceRecord.backend = static_cast<uint8_t>(backend);
        emptyDistanceReady = true;
      }
    }
    SW_VERBOSE_SERIAL_PRINTF(
        "[EMPTY_DISTANCE] boot source=profile_store status=%s active_profile=%s generation=%lu ready=%d stored_revision=%lu\n",
        ProfileEmptyDistanceStore::statusName(loaded.status),
        sensorProfileName(deviceConfig.sensorProfile),
        static_cast<unsigned long>(deviceConfig.generation),
        emptyDistanceReady ? 1 : 0,
        static_cast<unsigned long>(loaded.record.emptyRevision));
    return;
  }

  EmptyDistanceRecord loaded{};
  if (!calibrationModelStore.loadEmptyDistance(preferences, loaded)) {
    emptyDistanceRecord = EmptyDistanceRecord{};
    emptyDistanceReady = false;
    return;
  }

  const LaserMeasurementBackend backend = measurementReader.backend();
  const bool backendKnown =
      backend == LaserMeasurementBackend::LASER_485 ||
      backend == LaserMeasurementBackend::CYD3S1612_IIC;
  const bool backendMatches =
      loaded.backend == static_cast<uint8_t>(backend);
  if (backendKnown && !backendMatches) {
    // A saved anchor belongs to the sensor backend that produced it. Do not
    // silently reuse a J100 value after a CYD switch (or vice versa).
    emptyDistanceRecord = EmptyDistanceRecord{};
    emptyDistanceReady = false;
    SW_VERBOSE_SERIAL_PRINTF(
        "[EMPTY_DISTANCE] boot result=inactive reason=BACKEND_MISMATCH stored=%u active=%s\n",
        static_cast<unsigned>(loaded.backend),
        sensorBackendName());
    return;
  }
  if (loaded.backend > static_cast<uint8_t>(LaserMeasurementBackend::LASER_485)) {
    emptyDistanceRecord = EmptyDistanceRecord{};
    emptyDistanceReady = false;
    SW_VERBOSE_SERIAL_PRINTLN("[EMPTY_DISTANCE] boot result=inactive reason=INVALID_BACKEND");
    return;
  }

  emptyDistanceRecord = loaded;
  // Retain a legacy record for diagnostics while the configured backend is
  // unavailable, but never use it in the current distance state chain.
  emptyDistanceReady = backendKnown && backendMatches;
  SW_VERBOSE_SERIAL_PRINTF(
      "[EMPTY_DISTANCE] boot status=%s distance_mm=%.2f tolerance_mm=%.2f backend=%s\n",
      emptyDistanceReady ? "SET" : "WAITING_BACKEND",
      loaded.distanceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
      loaded.toleranceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
      laserMeasurementBackendName(static_cast<LaserMeasurementBackend>(loaded.backend)));
}

void LaserModule::configureDistanceStateChainForProfile(const char* source) {
  DistanceStateChainConfig distanceConfig{};
  if (deviceConfig.sensorProfile == SensorProfileId::CYD3S1612_IIC) {
    // This is a provisional observation preset, not an accepted CYD safety
    // policy. Protected CYD starts remain blocked pending labelled data.
    distanceConfig.emptyEnterDelta = 0.10f;
    distanceConfig.emptyExitDelta = 0.05f;
    distanceConfig.stableRange = 0.10f;
    distanceConfig.stableDrift = 0.10f;
    distanceConfig.baselineExitDelta = 0.20f;
  }
  distanceStateChain.configure(distanceConfig);
  distanceStateChain.reset(source ? source : "device_config");
  distanceStateChain.setEmptyReference(
      emptyDistanceRecord.distanceRuntime,
      emptyDistanceReady && isfinite(emptyDistanceRecord.distanceRuntime));
}

bool LaserModule::configurationMutationBlocked(String& reason) const {
  if (sm && sm->state() == TopState::RUNNING) {
    reason = "NOT_IDLE";
    return true;
  }
  // stopSoft() can leave the actuator physically active after the state
  // machine has already entered IDLE. Do not change reader/calibration then.
  if (waveObservation && waveObservation->isOutputActive()) {
    reason = "BUSY";
    return true;
  }
  return false;
}

void LaserModule::applyDeviceConfigRuntimeEffects(const char* source) {
  releaseOccupiedCycle(source, millis());
  resetStableTracking(source, true);
  resetRuntimeZero(source);
  resetStableSignalFilter();
  resetMeasurementPlane(source, true);
  configureDistanceStateChainForProfile(source);
  measurementProbePolicy.reset();
  rhythmStateJudge.reset(source ? source : "device_config");
  clearStableContractBridge(source ? source : "device_config");
  stableContract.userPresent = false;
  presenceEnterConfirmCount = 0;
  presenceExitConfirmCount = 0;
  invalidPresenceSamples = 0;
  stableExitConfirmCount = 0;
  stableExitPendingReason = nullptr;
  latestWeightKg = 0.0f;
  lastMeasurementValid = false;
  currentSampleValid = false;
  currentSamplePhysicalMm = NAN;
  currentSampleAtMs = 0;
  currentSampleSequence = 0;
  currentSampleReaderEpoch = measurementReader.readerEpoch();
  currentSampleProfile = deviceConfig.sensorProfile;
  currentSampleConfigGeneration = deviceConfig.generation;
  lastInvalidReason = deviceConfig.laserInstalled ? nullptr : "LASER_NOT_INSTALLED";
  lastLogDist = -999.0f;
  resetMeasurementHealth(source ? source : "device_config", millis());

  logConfigTruth(source ? source : "device_config");

  if (!sm) return;

  sm->setRuntimeReady(false);
  logStartReadyWriteback(
      millis(),
      source ? source : "device_config",
      sm->state(),
      false,
      0.0f,
      source ? source : "device_config");
  sm->setStartReadiness(false, 0.0f);
  syncSensorHealthToStateMachine();
}

void LaserModule::stopWaveForConfigurationFault(const char* source) {
  if (!sm || sm->state() != TopState::RUNNING) return;
  SW_VERBOSE_SERIAL_PRINTF(
      "[DEVICE] CONFIG_FAULT source=%s action=stop_for_config_fault reason=CONFIG_ERROR\n",
      source ? source : "unknown");
  sm->stopForConfigurationFault();
}

void LaserModule::invalidateEmptyReferenceAfterStorageFailure(
    const char* source) {
  // A two-slot write can fail after one key changed. No in-memory D_e may
  // authorize a start until the persistent record is explicitly reconciled.
  emptyDistanceRecord = EmptyDistanceRecord{};
  emptyDistanceReady = false;
  calibrationModelReady = false;
  stopWaveForConfigurationFault(source);
  applyDeviceConfigRuntimeEffects(source);
  SW_VERBOSE_SERIAL_PRINTF(
      "[EMPTY_DISTANCE] action=invalidate reason=%s profile=%s generation=%lu\n",
      source ? source : "storage_error",
      sensorProfileName(deviceConfig.sensorProfile),
      static_cast<unsigned long>(deviceConfig.generation));
}

void LaserModule::loadCalibrationModel() {
  // Only the versioned, CRC-checked model record is active. Legacy zero/factor
  // keys remain readable for the old command path but cannot silently make a
  // new weight model or empty-distance anchor appear calibrated.
  const bool hasPersistedModel = preferencesOpened &&
      calibrationModelStore.hasStoredModel(preferences);
  CalibrationModel model = preferencesOpened
      ? calibrationModelStore.loadModel(preferences, zeroDistance, scaleFactor)
      : CalibrationModel{};
  bool bindingValid = true;
  if (deviceConfig.schema1Managed) {
    const ProfileEmptyLoadResult empty = profileEmptyDistanceStore.load(
        preferences, preferencesOpened,
        deviceConfig.sensorProfile, deviceConfig.generation);
    bindingValid = empty.status == ProfileEmptyStoreStatus::SET &&
        emptyDistanceReady &&
        modelBindingMatches(
            preferences, deviceConfig.sensorProfile,
            deviceConfig.generation, empty.record.emptyRevision, model);
    if (hasPersistedModel && !bindingValid) {
      SW_VERBOSE_SERIAL_PRINTF(
          "[CAL] MODEL inactive reason=PROFILE_BINDING_MISMATCH profile=%s generation=%lu empty_status=%s empty_revision=%lu\n",
          sensorProfileName(deviceConfig.sensorProfile),
          static_cast<unsigned long>(deviceConfig.generation),
          ProfileEmptyDistanceStore::statusName(empty.status),
          static_cast<unsigned long>(empty.record.emptyRevision));
    }
  }
  calibrationModelReady = hasPersistedModel && bindingValid;

  String reason;
  if (!applyCalibrationModel(model, false, "boot", reason)) {
    CalibrationModel fallback{};
    fallback.type = CalibrationModelType::LINEAR;
    fallback.referenceDistance = zeroDistance;
    fallback.coefficients[0] = 0.0f;
    fallback.coefficients[1] = (scaleFactor > 0.0f) ? scaleFactor : 1.0f;
    fallback.coefficients[2] = 0.0f;

    String unusedReason;
    applyCalibrationModel(fallback, false, "boot_fallback", unusedReason);
    calibrationModelReady = false;
  } else {
    // applyCalibrationModel() is also used by explicit CAL:SET_MODEL and
    // marks a model ready. Boot restores readiness from the persisted-record
    // check so a finite placeholder cannot become a weight source.
    calibrationModelReady = hasPersistedModel && bindingValid;
  }
}

void LaserModule::saveCalibrationModel() {
  calibrationModelStore.saveModel(preferences, calibrationModel);
}

void LaserModule::syncLegacyParamsFromModel() {
  zeroDistance = calibrationModel.referenceDistance;
  scaleFactor = (calibrationModel.type == CalibrationModelType::LINEAR)
      ? calibrationModel.coefficients[1]
      : 0.0f;
  dualZero.calibrationZeroDistance = calibrationModel.referenceDistance;
  refreshEffectiveZero();
}

bool LaserModule::applyCalibrationModel(
    const CalibrationModel& model,
    bool persist,
    const char* source,
    String& reason) {
  if (!calibrationModelStore.isModelFinite(model)) {
    reason = "INVALID_MODEL";
    return false;
  }

  // Linear and quadratic APP fits both pass through the explicit 0 kg point.
  // Reject a non-zero intercept at the firmware boundary as a final guard
  // against empty-load drift from an old or manually entered model.
  if (fabsf(model.coefficients[2]) > 0.0001f) {
    reason = "EMPTY_ANCHOR_REQUIRED";
    return false;
  }

  const bool wangBackend =
      measurementReader.backend() == LaserMeasurementBackend::LASER_485 ||
      (!DISTANCE_BACKEND_UNIFIED && !DISTANCE_BACKEND_CYD3S1612_IIC);
  const float monotonicMin = wangBackend
      ? LASER_WANG_VALID_RUNTIME_DISTANCE_MIN
      : LASER_VALID_RUNTIME_DISTANCE_MIN;
  const float monotonicMax = wangBackend
      ? LASER_WANG_VALID_RUNTIME_DISTANCE_MAX
      : LASER_VALID_RUNTIME_DISTANCE_MAX;
  if (!calibrationModelStore.isModelMonotonic(model, monotonicMin, monotonicMax)) {
    reason = "NON_MONOTONIC";
    return false;
  }

  if (emptyDistanceReady && isfinite(emptyDistanceRecord.distanceRuntime)) {
    if (fabsf(model.referenceDistance - emptyDistanceRecord.distanceRuntime) >
        (emptyDistanceRecord.toleranceRuntime + kModelReferenceEpsilonRuntime)) {
      reason = "EMPTY_DISTANCE_MISMATCH";
      return false;
    }
  } else {
    // D_e is an explicit型号页 setting. A CAL:SET_MODEL reference is never
    // promoted to D_e, including for legacy callers; this keeps the weight
    // model and the distance-state anchor independent.
    reason = "EMPTY_DISTANCE_REQUIRED";
    return false;
  }

  if (persist) {
    uint32_t emptyRevision = 0;
    if (deviceConfig.schema1Managed) {
      const ProfileEmptyLoadResult empty = profileEmptyDistanceStore.load(
          preferences, preferencesOpened,
          deviceConfig.sensorProfile, deviceConfig.generation);
      if (empty.status != ProfileEmptyStoreStatus::SET || !emptyDistanceReady) {
        reason = "EMPTY_DISTANCE_REQUIRED";
        return false;
      }
      emptyRevision = empty.record.emptyRevision;
      if (!clearModelBinding(preferences)) {
        reason = "NVS_WRITE_FAILED";
        return false;
      }
      calibrationModelReady = false;
    }
    const float persistedZeroDistance = model.referenceDistance;
    const float persistedScaleFactor =
        model.type == CalibrationModelType::LINEAR ? model.coefficients[1] : 0.0f;
    if (!calibrationModelStore.saveModel(preferences, model) ||
        !calibrationModelStore.saveLegacyParams(
            preferences, persistedZeroDistance, persistedScaleFactor)) {
      reason = "NVS_WRITE_FAILED";
      return false;
    }
    if (deviceConfig.schema1Managed &&
        !saveModelBinding(
            preferences, deviceConfig.sensorProfile,
            deviceConfig.generation, emptyRevision, model)) {
      reason = "NVS_WRITE_FAILED";
      return false;
    }
  }
  calibrationModel = model;
  calibrationModelReady = true;
  syncLegacyParamsFromModel();
  distanceStateChain.reset(source ? source : "model_apply");
  distanceStateChain.setEmptyReference(
      emptyDistanceRecord.distanceRuntime,
      emptyDistanceReady && isfinite(emptyDistanceRecord.distanceRuntime));
  resetRuntimeZero(source);

  SW_VERBOSE_SERIAL_PRINTF("[CAL] MODEL APPLY source=%s type=%s ref=%.2f c0=%.6f c1=%.6f c2=%.6f\n",
      source ? source : "unknown",
      calibrationModelTypeName(calibrationModel.type),
      calibrationModel.referenceDistance,
      calibrationModel.coefficients[0],
      calibrationModel.coefficients[1],
      calibrationModel.coefficients[2]);
  return true;
}

float LaserModule::evaluateCalibrationWeight(
    const CalibrationModel& model,
    float distance,
    float zeroReferenceDistance) const {
  return CalibrationRuntime::evaluateWeight(model, distance, zeroReferenceDistance);
}

float LaserModule::evaluateCalibrationWeight(const CalibrationModel& model, float distance) const {
  return evaluateCalibrationWeight(model, distance, model.referenceDistance);
}

float LaserModule::evaluateCalibrationWeight(float distance) const {
  if (!calibrationModelReady) {
    return NAN;
  }
  // The persisted calibration reference is the only formal zero used by the
  // model. Runtime-zero observation remains diagnostic state and must never
  // move the deployed model's intercept after calibration.
  return CalibrationRuntime::evaluateClampedWeight(
      calibrationModel,
      distance,
      calibrationModel.referenceDistance);
}

float LaserModule::computeUnlockedEffectiveZeroDistance() const {
  EffectiveZeroInput input{};
  input.calibrationZeroDistance = dualZero.calibrationZeroDistance;
  input.calibrationModelReferenceDistance = calibrationModel.referenceDistance;
  input.legacyZeroDistance = zeroDistance;
  input.applyRuntimeZero = phase2Thresholds.runtimeZero.applyToWeightConversion;
  input.runtimeZeroValid = dualZero.runtimeZeroValid;
  input.runtimeZeroDistance = dualZero.runtimeZeroDistance;
  input.clampMaxOffsetFromCalibration =
      phase2Thresholds.runtimeZero.clampMaxOffsetFromCalibration;
  return CalibrationRuntime::computeUnlockedEffectiveZero(input).effectiveZeroDistance;
}

float LaserModule::computeEffectiveZeroDistance() const {
  EffectiveZeroInput input{};
  input.calibrationZeroDistance = dualZero.calibrationZeroDistance;
  input.calibrationModelReferenceDistance = calibrationModel.referenceDistance;
  input.legacyZeroDistance = zeroDistance;
  input.applyRuntimeZero = phase2Thresholds.runtimeZero.applyToWeightConversion;
  input.runtimeZeroValid = dualZero.runtimeZeroValid;
  input.runtimeZeroDistance = dualZero.runtimeZeroDistance;
  input.clampMaxOffsetFromCalibration =
      phase2Thresholds.runtimeZero.clampMaxOffsetFromCalibration;
  input.effectiveZeroLocked = dualZero.effectiveZeroLocked;
  input.lockedEffectiveZeroDistance = dualZero.effectiveZeroDistance;
  return CalibrationRuntime::computeEffectiveZero(input).effectiveZeroDistance;
}

void LaserModule::refreshEffectiveZero() {
  dualZero.effectiveZeroDistance = computeEffectiveZeroDistance();
}

void LaserModule::resetStableSignalFilter() {
  stableFilterValid = false;
  stableFilteredDistance = 0.0f;
  stableFilteredWeight = 0.0f;
}

bool LaserModule::updateStableSignalFilter(
    float distance,
    float& filteredDistance,
    float& filteredWeight) {
  if (!isfinite(distance)) {
    resetStableSignalFilter();
    filteredDistance = NAN;
    filteredWeight = NAN;
    return false;
  }

  const float alpha = phase2Thresholds.stable.filterDistanceAlpha;
  if (!stableFilterValid || !isfinite(stableFilteredDistance)) {
    stableFilteredDistance = distance;
    stableFilterValid = true;
  } else {
    stableFilteredDistance =
        stableFilteredDistance + alpha * (distance - stableFilteredDistance);
  }

  stableFilteredWeight = evaluateCalibrationWeight(stableFilteredDistance);
  if (!isfinite(stableFilteredWeight)) {
    resetStableSignalFilter();
    filteredDistance = NAN;
    filteredWeight = NAN;
    return false;
  }

  filteredDistance = stableFilteredDistance;
  filteredWeight = stableFilteredWeight;
  return true;
}

void LaserModule::lockEffectiveZeroForOccupiedCycle(uint32_t now, const char* reason) {
  if (dualZero.effectiveZeroLocked) {
    return;
  }

  dualZero.occupiedCycleActive = true;
  dualZero.effectiveZeroLocked = true;
  dualZero.effectiveZeroDistance = computeUnlockedEffectiveZeroDistance();
  dualZero.effectiveZeroUsesRuntime =
      phase2Thresholds.runtimeZero.applyToWeightConversion &&
      dualZero.runtimeZeroValid &&
      fabsf(dualZero.effectiveZeroDistance - dualZero.calibrationZeroDistance) > 0.0001f;
  dualZero.effectiveZeroLockedAtMs = now;

  SW_VERBOSE_SERIAL_PRINTF(
      "[ZERO_EFFECTIVE] action=lock reason=%s occupied_cycle=1 source=%s effective_zero=%.2f calibration_zero=%.2f runtime_zero=%.2f\n",
      reason ? reason : "occupied_cycle_start",
      dualZero.effectiveZeroUsesRuntime ? "runtime_zero" : "calibration_zero",
      dualZero.effectiveZeroDistance,
      dualZero.calibrationZeroDistance,
      dualZero.runtimeZeroValid ? dualZero.runtimeZeroDistance : dualZero.calibrationZeroDistance);
}

void LaserModule::releaseOccupiedCycle(const char* reason, uint32_t now) {
  const bool wasLocked = dualZero.effectiveZeroLocked || dualZero.occupiedCycleActive;
  dualZero.occupiedCycleActive = false;
  dualZero.effectiveZeroLocked = false;
  dualZero.effectiveZeroUsesRuntime = false;
  dualZero.effectiveZeroLockedAtMs = 0;
  resetStableSignalFilter();
  refreshEffectiveZero();

  if (wasLocked) {
    SW_VERBOSE_SERIAL_PRINTF(
        "[ZERO_EFFECTIVE] action=unlock reason=%s occupied_cycle=0 effective_zero=%.2f now_ms=%lu\n",
        reason ? reason : "occupied_cycle_clear",
        dualZero.effectiveZeroDistance,
        static_cast<unsigned long>(now));
  }
}

void LaserModule::resetRuntimeZero(const char* reason) {
  const bool hadRuntimeZero = dualZero.runtimeZeroValid;
  dualZero.runtimeZeroDistance = 0.0f;
  dualZero.runtimeZeroValid = false;
  dualZero.effectiveZeroUsesRuntime = false;
  dualZero.runtimeZeroCapturedAtMs = 0;
  dualZero.lastRuntimeZeroCandidateDistance = 0.0f;
  dualZero.runtimeZeroWindowStartedAtMs = 0;
  runtimeZeroHead = 0;
  runtimeZeroCount = 0;
  refreshEffectiveZero();

  if (hadRuntimeZero) {
    SW_VERBOSE_SERIAL_PRINTF(
        "[ZERO_RUNTIME] CLEAR reason=%s calibration_zero=%.2f effective_zero=%.2f\n",
        reason ? reason : "unspecified",
        dualZero.calibrationZeroDistance,
        dualZero.effectiveZeroDistance);
  }
}

void LaserModule::observeRuntimeZero(float distance, float weight, uint32_t now) {
  const LaserRuntimeZeroThresholdConfig& runtimeZeroCfg = phase2Thresholds.runtimeZero;
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  RuntimeZeroEligibilityInput eligibilityInput{};
  eligibilityInput.distance = distance;
  eligibilityInput.weight = weight;
  eligibilityInput.topState = currentTopState;
  eligibilityInput.userPresent = stableContract.userPresent;
  eligibilityInput.stableCandidate = stableContract.stableCandidate;
  eligibilityInput.occupiedCycleActive = dualZero.occupiedCycleActive;
  eligibilityInput.effectiveZeroLocked = dualZero.effectiveZeroLocked;
  eligibilityInput.baselineReadyLatched = stableContract.baselineReadyLatched;
  const RuntimeZeroDecision eligibility =
      runtimeZeroObserver.evaluateEligibility(runtimeZeroCfg, eligibilityInput);
  if (!eligibility.eligible) {
    if (eligibility.shouldResetWindow) {
      runtimeZeroHead = 0;
      runtimeZeroCount = 0;
      dualZero.runtimeZeroWindowStartedAtMs = 0;
    }
    runtimeZeroObserver.noteDecision(now, eligibility);
    return;
  }

  if (eligibility.shouldResetWindow) {
    runtimeZeroHead = 0;
    runtimeZeroCount = 0;
    dualZero.runtimeZeroWindowStartedAtMs = 0;
  }

  if (runtimeZeroCount == 0) {
    dualZero.runtimeZeroWindowStartedAtMs = now;
  }

  runtimeZeroBuffer[runtimeZeroHead] = distance;
  runtimeZeroHead = (runtimeZeroHead + 1) % WINDOW_N;
  if (runtimeZeroCount < WINDOW_N) {
    runtimeZeroCount++;
  }

  RuntimeZeroWindowInput windowInput{};
  windowInput.values = runtimeZeroBuffer;
  windowInput.head = runtimeZeroHead;
  windowInput.count = runtimeZeroCount;
  windowInput.capacity = WINDOW_N;
  windowInput.calibrationZero = dualZero.calibrationZeroDistance;
  const RuntimeZeroDecision decision =
      runtimeZeroObserver.evaluateWindow(runtimeZeroCfg, windowInput);
  if (!decision.shouldRefresh) {
    runtimeZeroObserver.noteDecision(now, decision);
    return;
  }

  dualZero.lastRuntimeZeroCandidateDistance = decision.stats.mean;
  const bool changed =
      !dualZero.runtimeZeroValid ||
      fabsf(dualZero.runtimeZeroDistance - decision.stats.mean) > 0.0001f;
  dualZero.runtimeZeroDistance = decision.stats.mean;
  dualZero.runtimeZeroValid = true;
  dualZero.runtimeZeroCapturedAtMs = now;
  dualZero.effectiveZeroUsesRuntime = runtimeZeroCfg.applyToWeightConversion;
  refreshEffectiveZero();
  runtimeZeroObserver.noteDecision(now, decision);

  if (changed) {
    SW_VERBOSE_SERIAL_PRINTF(
        "[ZERO_RUNTIME] REFRESH runtime_zero=%.2f calibration_zero=%.2f effective_zero=%.2f "
        "samples=%u std=%.4f range=%.4f window_ms=%lu apply_to_weight=%d\n",
        dualZero.runtimeZeroDistance,
        dualZero.calibrationZeroDistance,
        dualZero.effectiveZeroDistance,
        static_cast<unsigned>(decision.requiredSamples),
        decision.stats.stddev,
        decision.stats.range,
        static_cast<unsigned long>(
            dualZero.runtimeZeroWindowStartedAtMs > 0 && now >= dualZero.runtimeZeroWindowStartedAtMs
                ? (now - dualZero.runtimeZeroWindowStartedAtMs)
                : 0),
        runtimeZeroCfg.applyToWeightConversion ? 1 : 0);
  }
}

void LaserModule::noteDistanceValidity(
    bool valid,
    uint16_t rawRegister,
    int16_t signedRaw,
    float scaledDistance,
    bool sentinel,
    const char* reason,
    uint32_t now) {
  if (valid) {
    if (!lastMeasurementValid) {
      LaserDiagnostics::logDistanceValid(rawRegister, signedRaw, scaledDistance);
    }
    lastMeasurementValid = true;
    lastInvalidReason = nullptr;
    return;
  }

  if (reason && strcmp(reason, "READ_FAIL") == 0) {
    lastMeasurementValid = false;
    currentSampleValid = false;
    lastInvalidReason = reason;
    return;
  }

  const bool shouldLog =
      lastMeasurementValid ||
      lastInvalidReason != reason ||
      (now - lastValidityLogMs) >= LASER_INVALID_LOG_INTERVAL_MS;

  if (shouldLog) {
    LaserDiagnostics::logDistanceInvalid(
        rawRegister,
        signedRaw,
        scaledDistance,
        sentinel,
        reason);
    lastValidityLogMs = now;
  }

  lastMeasurementValid = false;
  currentSampleValid = false;
  lastInvalidReason = reason;
}

void LaserModule::pushStableSample(float distance, float weight) {
  distanceBuffer[bufHead] = distance;
  weightBuffer[bufHead] = weight;
  bufHead = (bufHead + 1) % WINDOW_N;
  if (bufCount < WINDOW_N) bufCount++;
}

bool LaserModule::updatePresenceState(float weight) {
  const LaserPresenceThresholdConfig& presence = phase2Thresholds.presence;

  PresenceCounterInput input{};
  input.weightKg = weight;
  input.currentUserPresent = stableContract.userPresent;
  input.currentEnterConfirmCount = presenceEnterConfirmCount;
  input.currentExitConfirmCount = presenceExitConfirmCount;
  const PresenceCounterResult result =
      PresenceContractEvaluator::evaluateWithCounters(presence, input);

  presenceEnterConfirmCount = result.nextEnterConfirmCount;
  presenceExitConfirmCount = result.nextExitConfirmCount;
  stableContract.userPresent = result.decision.nextUserPresent;
  if (result.decision.changed && stableContract.userPresent) {
    invalidPresenceSamples = 0;
  }
  return result.decision.changed;
}

void LaserModule::noteInvalidPresenceSample(uint32_t now, const char* reason) {
  const LaserPresenceThresholdConfig& presence = phase2Thresholds.presence;
  if (presence.invalidExitSamples == 0) {
    return;
  }

  if (invalidPresenceSamples < 0xFF) {
    invalidPresenceSamples++;
  }

  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  if (currentTopState == TopState::RUNNING) {
    return;
  }

  if (!stableContract.userPresent || invalidPresenceSamples < presence.invalidExitSamples) {
    return;
  }

  stableContract.userPresent = false;
  presenceEnterConfirmCount = 0;
  presenceExitConfirmCount = 0;
  SW_VERBOSE_SERIAL_PRINTF(
      "[PRESENCE] exit reason=%s invalid_samples=%u now_ms=%lu\n",
      reason ? reason : "measurement_invalid",
      static_cast<unsigned>(invalidPresenceSamples),
      static_cast<unsigned long>(now));
}

void LaserModule::syncStableLiveContract(uint32_t now) {
  stableContract.stableCandidate = (stableState == StableState::STABLE_CANDIDATE);
  stableContract.stableReadyLive = (stableState == StableState::STABLE_LATCHED);

  if (stableContract.stableReadyLive) {
    stableContract.stableReadyWeightKg = stableBaselineWeight;
    stableContract.stableReadyDistance = stableBaselineDistance;
    stableContract.stableReadyAtMs = stableLatchedAtMs ? stableLatchedAtMs : now;
  } else {
    stableContract.stableReadyWeightKg = 0.0f;
    stableContract.stableReadyDistance = 0.0f;
    stableContract.stableReadyAtMs = 0;
  }
}

void LaserModule::latchBaselineReadyFromStable(
    uint32_t now,
    const char* source,
    float distance,
    float weight) {
  stableContract.baselineReadyLatched = true;
  stableContract.baselineReadyWeightKg = weight;
  stableContract.baselineReadyDistance = distance;
  stableContract.baselineReadyAtMs = now;
  logBaselineContractLatch(now, source, distance, weight);
}

void LaserModule::syncStartReadyContract(
    uint32_t now,
    TopState currentTopState,
    const RhythmStateUpdateResult& result) {
  (void)result;

  // baselineReadyLatched is the durable owner-side evidence that a valid
  // pre-start baseline has been established for the current occupied cycle.
  // start_ready stays separate from baseline_ready because it still requires
  // user presence and measurement health, but it no longer collapses back to a
  // short idle live-stable window once that baseline has been accepted.
  StartGateContractInput input{};
  // A single frame after a gap cannot reuse a previously latched baseline
  // to reopen the start gate before the measurement-health confirmation has
  // seen consecutive fresh samples again.
  input.measurementValid = lastMeasurementValid && laserAvailable();
  input.userPresent = stableContract.userPresent;
  input.baselineReadyLatched = stableContract.baselineReadyLatched;
  input.stableReadyLive = stableContract.stableReadyLive;
  input.useDistance = deviceConfig.laserInstalled &&
      (measurementReader.backend() == LaserMeasurementBackend::CYD3S1612_IIC ||
       measurementReader.backend() == LaserMeasurementBackend::LASER_485);
  input.baselineReadyWeightKg = stableContract.baselineReadyWeightKg;
  input.baselineReadyDistance = stableContract.baselineReadyDistance;
  input.topState = currentTopState;
  StartGateContractResult evaluation =
      StartGateContractEvaluator::evaluate(phase2Thresholds.startGate, input);

  // No CYD protected-running algorithm has passed real-device acceptance.
  // The healthy, both-switches-off path is owned separately by the FSM; raw
  // readiness must never derive from these provisional distance thresholds.
  if (deviceConfig.sensorProfile == SensorProfileId::CYD3S1612_IIC) {
    evaluation.startReady = false;
    evaluation.startReadyWeightKg = 0.0f;
    evaluation.reason = "cyd_protected_policy_unvalidated";
  }
  stableContract.startReady = evaluation.startReady;
  stableContract.startReadyWeightKg = evaluation.startReadyWeightKg;
  stableContract.startReadyBridge = evaluation.reason;
  observeStartGateDiagnostics(now, input, evaluation);
}

void LaserModule::resetStartGateDiagnosticsWindow(uint32_t now) {
  startGateDiagWindowStartedAtMs = now;
  startGateDiagEvaluations = 0;
  startGateDiagReady = 0;
  startGateDiagMeasurementInvalid = 0;
  startGateDiagUserNotPresent = 0;
  startGateDiagBaselineNotReady = 0;
  startGateDiagLiveStableNotReady = 0;
  startGateDiagRunningHold = 0;
  startGateDiagIdleReady = 0;
}

void LaserModule::observeStartGateDiagnostics(
    uint32_t now,
    const StartGateContractInput& input,
    const StartGateContractResult& evaluation) {
  if (!START_GATE_DIAG_ENABLED) {
    return;
  }

  if (startGateDiagWindowStartedAtMs == 0) {
    resetStartGateDiagnosticsWindow(now);
  }

  ++startGateDiagEvaluations;
  if (evaluation.startReady) {
    ++startGateDiagReady;
  }

  const char* reason = evaluation.reason ? evaluation.reason : "unknown";
  if (strcmp(reason, "measurement_invalid") == 0) {
    ++startGateDiagMeasurementInvalid;
  } else if (strcmp(reason, "user_not_present") == 0) {
    ++startGateDiagUserNotPresent;
  } else if (strcmp(reason, "baseline_not_ready") == 0) {
    ++startGateDiagBaselineNotReady;
  } else if (strcmp(reason, "live_stable_not_ready") == 0) {
    ++startGateDiagLiveStableNotReady;
  } else if (strcmp(reason, "running_contract_hold") == 0) {
    ++startGateDiagRunningHold;
  } else if (strcmp(reason, "idle_contract_ready") == 0) {
    ++startGateDiagIdleReady;
  }

  if (startGateDiagLastLogMs == 0 ||
      now - startGateDiagLastLogMs >= START_GATE_DIAG_LOG_INTERVAL_MS) {
    logStartGateDiagnostics(now, "periodic", input, evaluation);
    startGateDiagLastLogMs = now;
    resetStartGateDiagnosticsWindow(now);
  }
}

void LaserModule::logStartGateDiagnostics(
    uint32_t now,
    const char* trigger,
    const StartGateContractInput& input,
    const StartGateContractResult& evaluation) {
  const uint32_t windowMs =
      startGateDiagWindowStartedAtMs == 0 ? 0 : (now - startGateDiagWindowStartedAtMs);
  SW_VERBOSE_SERIAL_PRINTF(
      "[START_GATE_DIAG] trigger=%s window_ms=%lu evals=%lu ready=%lu "
      "reason=%s start_ready=%d start_weight=%.2f "
      "measurement_valid=%d user_present=%d baseline_latched=%d stable_live=%d "
      "baseline_weight=%.2f top_state=%s "
      "measurement_invalid=%lu user_not_present=%lu baseline_not_ready=%lu "
      "live_stable_not_ready=%lu running_hold=%lu idle_ready=%lu\n",
      trigger ? trigger : "unknown",
      static_cast<unsigned long>(windowMs),
      static_cast<unsigned long>(startGateDiagEvaluations),
      static_cast<unsigned long>(startGateDiagReady),
      evaluation.reason ? evaluation.reason : "unknown",
      evaluation.startReady ? 1 : 0,
      evaluation.startReadyWeightKg,
      input.measurementValid ? 1 : 0,
      input.userPresent ? 1 : 0,
      input.baselineReadyLatched ? 1 : 0,
      input.stableReadyLive ? 1 : 0,
      input.baselineReadyWeightKg,
      topStateName(input.topState),
      static_cast<unsigned long>(startGateDiagMeasurementInvalid),
      static_cast<unsigned long>(startGateDiagUserNotPresent),
      static_cast<unsigned long>(startGateDiagBaselineNotReady),
      static_cast<unsigned long>(startGateDiagLiveStableNotReady),
      static_cast<unsigned long>(startGateDiagRunningHold),
      static_cast<unsigned long>(startGateDiagIdleReady));
}

void LaserModule::syncStableContractBridge(
    uint32_t now,
    const RhythmStateUpdateResult& result) {
  syncStableLiveContract(now);
  if (!stableContract.baselineReadyLatched && result.evidence.baselineReady) {
    latchBaselineReadyFromStable(
        result.evidence.baselineCapturedAtMs ? result.evidence.baselineCapturedAtMs : now,
        "rhythm_bridge",
        result.evidence.baselineDistance,
        result.evidence.baselineWeightKg);
  } else if (stableContract.baselineReadyLatched &&
             result.evidence.baselineReady &&
             stableContract.baselineReadyWeightKg <= 0.0f) {
    stableContract.baselineReadyWeightKg = result.evidence.baselineWeightKg;
    stableContract.baselineReadyDistance = result.evidence.baselineDistance;
  }
  syncStartReadyContract(now, sm ? sm->state() : TopState::IDLE, result);
}

void LaserModule::clearStableContractBridge(const char* reason) {
  const StableContractState before = stableContract;
  stableContract.stableCandidate = false;
  stableContract.stableReadyLive = false;
  stableContract.baselineReadyLatched = false;
  stableContract.startReady = false;
  stableContract.stableReadyWeightKg = 0.0f;
  stableContract.stableReadyDistance = 0.0f;
  stableContract.baselineReadyWeightKg = 0.0f;
  stableContract.baselineReadyDistance = 0.0f;
  stableContract.startReadyWeightKg = 0.0f;
  stableContract.stableReadyAtMs = 0;
  stableContract.baselineReadyAtMs = 0;
  stableContract.startReadyBridge = "not_ready";
  logBaselineContractClear(millis(), reason, before);
}

BaselineContractStateView LaserModule::captureBaselineContractView(
    const StableContractState& state) const {
  BaselineContractStateView view{};
  view.userPresent = state.userPresent;
  view.stableCandidate = state.stableCandidate;
  view.stableReadyLive = state.stableReadyLive;
  view.baselineReadyLatched = state.baselineReadyLatched;
  view.startReady = state.startReady;
  view.baselineReadyWeightKg = state.baselineReadyWeightKg;
  view.startReadyWeightKg = state.startReadyWeightKg;
  view.startReadyBridge = state.startReadyBridge ? state.startReadyBridge : "unknown";
  return view;
}

void LaserModule::logBaselineContractLatch(
    uint32_t now,
    const char* source,
    float distance,
    float weight) const {
  baselineContractDiagnostics.logLatch(
      now,
      source,
      distance,
      weight,
      captureBaselineContractView(stableContract));
}

void LaserModule::logBaselineContractClear(
    uint32_t now,
    const char* reason,
    const StableContractState& before) const {
  baselineContractDiagnostics.logClear(
      now,
      reason,
      captureBaselineContractView(before));
}

void LaserModule::logStartReadyWriteback(
    uint32_t now,
    const char* source,
    TopState topState,
    bool ready,
    float stableWeightKg,
    const char* reason) {
  BaselineContractWritebackInput input{};
  input.now = now;
  input.source = source ? source : "unknown";
  input.topState = topState;
  input.startReady = ready;
  input.startReadyWeightKg = ready ? stableWeightKg : 0.0f;
  input.reason = reason ? reason : "unknown";
  input.state = captureBaselineContractView(stableContract);
  baselineContractDiagnostics.logStartReadyWriteback(input);
}

void LaserModule::resetStableTracking(const char* reason, bool logIfActive) {
  const bool exitedStableStage = stableState != StableState::UNSTABLE;
  if (logIfActive && stableState != StableState::UNSTABLE) {
    SW_VERBOSE_SERIAL_PRINTF("[STABLE] CLEAR reason=%s\n", reason ? reason : "unspecified");
    HumanReadableLog::baseline("稳定测量已清除，等待重新建立");
  }

  stableState = StableState::UNSTABLE;
  stableBaselineDistance = 0.0f;
  stableBaselineDistanceMm = 0.0f;
  stableBaselineWeight = 0.0f;
  stableLatchedAtMs = 0;
  invalidStableSamples = 0;
  stableConfirmCount = 0;
  stableCandidateStartedAtMs = 0;
  stableEarlyCheckpointLogged = false;
  stableExitConfirmCount = 0;
  stableExitPendingReason = nullptr;
  bufHead = 0;
  bufCount = 0;
  syncStableLiveContract(millis());

  if (exitedStableStage) {
    logLatestMeasurementPlaneSummary("stable_exit");
  }
}

void LaserModule::beginStableCandidate(float distance, float weight) {
  if (stableState != StableState::STABLE_CANDIDATE) {
    // 当前约 3 秒体感优化优先落在 stable build，而不是 baseline_ready/start_ready 后半段。
    // 这里记录 build 入口，便于现场直接看出候选开始到 latch 的真实耗时。
    SW_VERBOSE_SERIAL_PRINTF(
        "[STABLE] CANDIDATE state_eval_interval_ms=%lu early_samples=%u legacy_window=%d\n",
        static_cast<unsigned long>(phase2Thresholds.stable.evalIntervalStableBuildMs),
        static_cast<unsigned int>(phase2Thresholds.stable.earlyAcceptSamples),
        phase2Thresholds.stable.enterWindowSamples);
    HumanReadableLog::baseline("开始判断重量是否稳定");
    bufHead = 0;
    bufCount = 0;
    stableConfirmCount = 0;
    stableCandidateStartedAtMs = millis();
    stableEarlyCheckpointLogged = false;
    logLatestMeasurementPlaneSummary("stable_enter");
  }

  stableState = StableState::STABLE_CANDIDATE;
  invalidStableSamples = 0;
  pushStableSample(distance, weight);
  syncStableLiveContract(millis());
}

bool LaserModule::shouldClearLatchedStable(float distance, float weight, const char*& reason) const {
  if (weight < phase2Thresholds.stable.exitLeaveThresholdKg) {
    reason = "leave_threshold";
    return true;
  }

  if (fabsf(weight - stableBaselineWeight) >= phase2Thresholds.stable.exitWeightDeltaKg) {
    reason = "weight_delta";
    return true;
  }

  if (fabsf(distance - stableBaselineDistance) >= phase2Thresholds.stable.exitDistanceDelta) {
    reason = "distance_delta";
    return true;
  }

  reason = nullptr;
  return false;
}

bool LaserModule::shouldUseFastStableBuildReadInterval() const {
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  const bool baselineReady = stableContract.baselineReadyLatched;
  return currentTopState != TopState::RUNNING &&
      (stableState != StableState::STABLE_LATCHED || !baselineReady);
}

void LaserModule::latchStable(uint32_t now, const char* mode, float stddev) {
  const int sampleCount = min<int>(bufCount, phase2Thresholds.stable.enterWindowSamples);
  const int trimCount = min<int>(phase2Thresholds.stable.trimmedMeanDropSamples, sampleCount / 2);
  float finalWeight = LaserStableWindow::computeTrimmedMean(
      weightBuffer,
      bufHead,
      bufCount,
      WINDOW_N,
      sampleCount,
      trimCount);
  float finalDistance = LaserStableWindow::computeTrimmedMean(
      distanceBuffer,
      bufHead,
      bufCount,
      WINDOW_N,
      sampleCount,
      trimCount);
  if (!isfinite(finalWeight)) {
    finalWeight = getMean(weightBuffer);
  }
  if (!isfinite(finalDistance)) {
    finalDistance = getMean(distanceBuffer);
  }
  const uint32_t buildLatencyMs =
      stableCandidateStartedAtMs > 0 && now >= stableCandidateStartedAtMs
          ? (now - stableCandidateStartedAtMs)
          : 0;

  stableState = StableState::STABLE_LATCHED;
  stableBaselineWeight = finalWeight;
  stableBaselineDistance = finalDistance;
  stableBaselineDistanceMm = finalDistance * LASER_DISTANCE_RUNTIME_DIVISOR;
  stableLatchedAtMs = now;
  invalidStableSamples = 0;
  stableConfirmCount = 0;
  stableEarlyCheckpointLogged = false;
  stableExitConfirmCount = 0;
  stableExitPendingReason = nullptr;
  syncStableLiveContract(now);

  SW_VERBOSE_SERIAL_PRINTF(
      "[STABLE] LATCH mode=%s samples=%d std=%.3f weight=%.2f dist=%.2f build_ms=%lu\n",
      mode ? mode : "unknown",
      bufCount,
      stddev,
      finalWeight,
      finalDistance,
      static_cast<unsigned long>(buildLatencyMs));
  HumanReadableLog::baselineEstablished(finalWeight, finalDistance);
  logLatestMeasurementPlaneSummary("stable_latched");
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  const bool shouldCapturePrimaryBaseline =
      currentTopState != TopState::RUNNING &&
      !stableContract.baselineReadyLatched;

  if (!shouldCapturePrimaryBaseline) {
    return;
  }

  SW_VERBOSE_SERIAL_PRINTF("[STABLE] EMIT weight=%.2f\n", finalWeight);

  Event e{};
  e.type = EventType::STABLE_WEIGHT;
  e.v1 = finalWeight;
  e.ts_ms = now;
  if (bus) bus->publish(e);

  // stable_weight：稳定体重。
  // 只在未律动且稳定站立时锁定一次，直到确认离台后才允许清空。
  latchBaselineReadyFromStable(now, "stable_primary", finalDistance, finalWeight);
  rhythmStateJudge.refreshBaselineFromStable(finalDistance, finalWeight, now);
  emitBaselineReadyLog(now);
}

void LaserModule::emitBaselineReadyLog(uint32_t now) const {
  const RhythmStateEvidence& evidence = rhythmStateJudge.lastResult().evidence;
  SW_VERBOSE_SERIAL_PRINTF(
      "%s [BASELINE_READY] stable_weight_kg=%.2f stable_distance=%.2f captured_ms=%lu\n",
      LogMarker::kBaselineReady,
      evidence.baselineWeightKg,
      evidence.baselineDistance,
      static_cast<unsigned long>(evidence.baselineCapturedAtMs ? evidence.baselineCapturedAtMs : now));
  HumanReadableLog::baselineReady(evidence.baselineWeightKg, evidence.baselineDistance);
}

void LaserModule::publishBaselineMainVerification(
    uint32_t now,
    const RhythmStateUpdateResult& result) const {
  if (!bus) return;

  Event e{};
  e.type = EventType::BASELINE_MAIN;
  e.ts_ms = now;
  e.startReady = stableContract.startReady;
  e.baselineReady = result.evidence.baselineReady;
  e.stableWeightKg = result.evidence.baselineWeightKg;
  e.mainMa12WeightKg = result.evidence.ma12WeightKg;
  e.deviationKg = result.evidence.deviationKg;
  e.ratio = result.evidence.ratio;
  e.abnormalDurationMs = result.evidence.abnormalDurationMs;
  e.dangerDurationMs = result.evidence.dangerDurationMs;
  strlcpy(e.mainState, rhythmStateName(result.status), sizeof(e.mainState));
  strlcpy(
      e.stopReasonText,
      result.stopReason ? result.stopReason : "NONE",
      sizeof(e.stopReasonText));
  strlcpy(
      e.stopSourceText,
      result.shouldStopByDanger ? verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC) : "NONE",
      sizeof(e.stopSourceText));
  bus->publish(e);
}

void LaserModule::startRunSummary(uint32_t now, const RhythmStateUpdateResult& rhythmResult) {
  RunSummaryStartSnapshot snapshot{};
  snapshot.now = now;
  snapshot.baselineReady = rhythmResult.evidence.baselineReady;
  snapshot.baselineWeightKg =
      rhythmResult.evidence.baselineReady ? rhythmResult.evidence.baselineWeightKg : stableBaselineWeight;
  snapshot.baselineDistance =
      rhythmResult.evidence.baselineReady ? rhythmResult.evidence.baselineDistance : stableBaselineDistance;
  snapshot.fallStopEnabled = sm ? sm->fallStopEnabled() : FALL_STOP_ENABLED_DEFAULT;
  if (waveObservation) {
    waveObservation->getSummaryParams(
        snapshot.freqHz, snapshot.intensity, snapshot.intensityNormalized);
  }
  runSummaryCollector.start(snapshot, rhythmResult);
}

void LaserModule::accumulateRunSummary(uint32_t now,
                                       float distance,
                                       float weight,
                                       const RhythmStateUpdateResult& rhythmResult) {
  runSummaryCollector.accumulate(now, distance, weight, rhythmResult);
}

void LaserModule::finishRunSummary(uint32_t now,
                                   FaultCode stopReason,
                                   const RhythmStateUpdateResult& rhythmResult) {
  const bool abnormalStop = (stopReason != FaultCode::NONE);
  RunSummaryStopSnapshot snapshot{};
  snapshot.now = now;
  snapshot.stopReason = stopReason;
  snapshot.stopEffect = sm ? sm->lastStopSafetyEffect() : SafetySignalKind::NONE;
  snapshot.stopReasonText = sm ? sm->lastStopReasonText() : (abnormalStop ? faultCodeName(stopReason) : "NONE");
  snapshot.stopSourceText = sm ? sm->lastStopSourceText() : "NONE";
  snapshot.fallStopEnabled = sm ? sm->fallStopEnabled() : FALL_STOP_ENABLED_DEFAULT;
  runSummaryCollector.finish(snapshot, rhythmResult);
}

void LaserModule::handleRunSummaryState(TopState currentTopState,
                                        uint32_t now,
                                        float distance,
                                        float weight,
                                        const RhythmStateUpdateResult& rhythmResult) {
  const TopState previousTopState = lastObservedTopState;

  if (previousTopState != currentTopState) {
    if (previousTopState == TopState::RUNNING && currentTopState != TopState::RUNNING) {
      finishRunSummary(now, sm ? sm->activeFault() : FaultCode::NONE, rhythmResult);
    }

    if (currentTopState == TopState::RUNNING) {
      startRunSummary(now, rhythmResult);
    }

    lastObservedTopState = currentTopState;
  }

  if (currentTopState == TopState::RUNNING) {
    if (!runSummaryCollector.active()) {
      startRunSummary(now, rhythmResult);
    }
    accumulateRunSummary(now, distance, weight, rhythmResult);
  }
}

void LaserModule::handleInvalidMeasurement(const char* reason) {
  rhythmStateJudge.noteInvalidMeasurement();
  resetStableSignalFilter();
  noteInvalidPresenceSample(millis(), reason);
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;

  if (currentTopState != TopState::RUNNING) {
    stableContract.startReady = false;
    stableContract.startReadyWeightKg = 0.0f;
    stableContract.startReadyBridge = "measurement_invalid";
    if (sm) {
      logStartReadyWriteback(
          millis(),
          "measurement_invalid",
          currentTopState,
          false,
          0.0f,
          stableContract.startReadyBridge);
      sm->setStartReadiness(false, 0.0f);
    }
  }

  if (stableState == StableState::STABLE_CANDIDATE) {
    resetStableTracking(reason, true);
  } else if (stableState == StableState::STABLE_LATCHED) {
    if (invalidStableSamples < 0xFF) invalidStableSamples++;
    if (invalidStableSamples >= phase2Thresholds.stable.invalidGraceSamples) {
      resetStableTracking(reason, true);
    }
  }

  if (!stableContract.userPresent && currentTopState != TopState::RUNNING) {
    if (!stableContract.baselineReadyLatched) {
      releaseOccupiedCycle(reason, millis());
    }
    if (sm) {
      sm->setRuntimeReady(false);
      logStartReadyWriteback(
          millis(),
          "user_not_present_invalid",
          currentTopState,
          false,
          0.0f,
          "user_not_present");
      sm->setStartReadiness(false, 0.0f);
    }
  }
}

void LaserModule::updateStableState(float distance, float weight, uint32_t now) {
  const TopState currentTopState = sm ? sm->state() : TopState::IDLE;
  invalidStableSamples = 0;

  if (currentTopState == TopState::RUNNING && stableState == StableState::STABLE_CANDIDATE) {
    resetStableTracking("running_started", true);
    return;
  }

  if (stableState == StableState::STABLE_LATCHED) {
    const char* clearReason = nullptr;
    if (shouldClearLatchedStable(distance, weight, clearReason)) {
      if (stableExitPendingReason == clearReason) {
        if (stableExitConfirmCount < 0xFF) {
          stableExitConfirmCount++;
        }
      } else {
        stableExitPendingReason = clearReason;
        stableExitConfirmCount = 1;
      }

      if (stableExitConfirmCount >= phase2Thresholds.stable.exitConfirmSamples) {
        resetStableTracking(clearReason, true);
      }
      if (currentTopState != TopState::RUNNING &&
          stableContract.userPresent &&
          weight > phase2Thresholds.presence.enterThresholdKg) {
        beginStableCandidate(distance, weight);
      }
    } else {
      stableExitConfirmCount = 0;
      stableExitPendingReason = nullptr;
    }
    return;
  }

  if (currentTopState == TopState::RUNNING || !stableContract.userPresent) {
    if (stableState == StableState::STABLE_CANDIDATE) {
      resetStableTracking(
          currentTopState == TopState::RUNNING ? "running_started" : "user_not_present",
          true);
    }
    return;
  }

  if (weight <= phase2Thresholds.presence.enterThresholdKg) {
    if (stableState == StableState::STABLE_CANDIDATE) {
      resetStableTracking("below_entry_threshold", true);
    }
    return;
  }

  beginStableCandidate(distance, weight);
  const LaserStableWindowMetrics metrics = LaserStableWindow::computeMetrics(
      weightBuffer,
      bufHead,
      bufCount,
      WINDOW_N,
      phase2Thresholds.stable.enterWindowSamples);
  if (!metrics.valid) {
    return;
  }

  BaselineEvidenceInput baselineInput{};
  baselineInput.metrics.valid = metrics.valid;
  baselineInput.metrics.stddev = metrics.stddev;
  baselineInput.metrics.range = metrics.range;
  baselineInput.metrics.drift = metrics.drift;
  baselineInput.currentStableConfirmCount = stableConfirmCount;
  const BaselineEvidenceResult baselineResult =
      BaselineEvidenceEvaluator::evaluate(phase2Thresholds.stable, baselineInput);
  if (!baselineResult.stableEligible) {
    stableConfirmCount = baselineResult.nextStableConfirmCount;
    if (!stableEarlyCheckpointLogged) {
      stableEarlyCheckpointLogged = true;
      SW_VERBOSE_SERIAL_PRINTF(
          "[STABLE] HOLD mode=combined_window samples=%d std=%.3f range=%.3f drift=%.3f std_ok=%d range_ok=%d drift_ok=%d\n",
          bufCount,
          metrics.stddev,
          metrics.range,
          metrics.drift,
          baselineResult.stddevOk ? 1 : 0,
          baselineResult.rangeOk ? 1 : 0,
          baselineResult.driftOk ? 1 : 0);
    }
    return;
  }

  stableEarlyCheckpointLogged = false;
  stableConfirmCount = baselineResult.nextStableConfirmCount;
  if (!baselineResult.baselineEligible) {
    return;
  }

  latchStable(now, "combined_window", metrics.stddev);
}

void LaserModule::logRhythmStateUpdate(const RhythmStateUpdateResult& result) const {
  if (result.formalEventCandidate) {
    SW_VERBOSE_SERIAL_PRINTF(
        "%s [EVENT_AUX] formal_event_candidate=1 action_owner=baseline_main stable_weight_kg=%.2f ma12_weight_kg=%.2f deviation_kg=%.2f ratio=%.4f\n",
        LogMarker::kResearch,
        result.evidence.baselineWeightKg,
        result.evidence.ma12WeightKg,
        result.evidence.deviationKg,
        result.evidence.ratio);
  }

  if (result.advisory.shouldLog) {
    SW_VERBOSE_SERIAL_PRINTF(
        "%s [RISK_ADVISORY] advisory_state=%s advisory_type=%s advisory_level=%s advisory_reason=%s "
        "stable_weight_kg=%.2f ma12_weight_kg=%.2f deviation_kg=%.2f ratio=%.4f peak_ratio=%.4f "
        "abnormal_duration_ms=%lu danger_duration_ms=%lu event_aux_seen=%d final_stop=0 action=advisory_only\n",
        LogMarker::kSafety,
        result.advisory.active ? "ACTIVE" : "NONE",
        riskAdvisoryTypeName(result.advisory.type),
        riskAdvisoryLevelName(result.advisory.level),
        result.advisory.reason ? result.advisory.reason : "none",
        result.evidence.baselineWeightKg,
        result.evidence.ma12WeightKg,
        result.evidence.deviationKg,
        result.evidence.ratio,
        result.advisory.peakRatio,
        static_cast<unsigned long>(result.advisory.abnormalDurationMs),
        static_cast<unsigned long>(result.advisory.dangerDurationMs),
        result.advisory.eventAuxSeen ? 1 : 0);
  }

  if (!result.logShouldEmit) return;

  const char* previousStatusName =
      result.hasPreviousLoggedStatus ? rhythmStateName(result.previousLoggedStatus) : "UNSET";
  const char* nextStatusName = rhythmStateName(result.status);

  SW_VERBOSE_SERIAL_PRINTF(
      "%s [BASELINE_MAIN_STATE] prev=%s next=%s cause=%s reason=%s stable_weight_kg=%.2f ma12_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu user_present=%d\n",
      LogMarker::kResearch,
      previousStatusName,
      nextStatusName,
      result.logCause ? result.logCause : "transition",
      result.reason ? result.reason : "n/a",
      result.evidence.baselineWeightKg,
      result.evidence.ma12WeightKg,
      result.evidence.deviationKg,
      result.evidence.ratio,
      static_cast<unsigned long>(result.evidence.abnormalDurationMs),
      static_cast<unsigned long>(result.evidence.dangerDurationMs),
      stableContract.userPresent ? 1 : 0);

  if (!result.shouldStopByDanger) {
    return;
  }

  const char* stopSource =
      result.evidence.directDangerBandTriggered
          ? "ratio_above_danger_band"
          : "abnormal_exceeded_recovery_and_danger_hold";
  SW_VERBOSE_SERIAL_PRINTF(
      "%s [AUTO_STOP_BY_DANGER] stop_reason=%s stop_source=%s stop_detail=%s stable_weight_kg=%.2f ma12_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu fall_stop_enabled=%d\n",
      LogMarker::kSafety,
      result.stopReason ? result.stopReason : "danger_stop",
      verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC),
      stopSource,
      result.evidence.baselineWeightKg,
      result.evidence.ma12WeightKg,
      result.evidence.deviationKg,
      result.evidence.ratio,
      static_cast<unsigned long>(result.evidence.abnormalDurationMs),
      static_cast<unsigned long>(result.evidence.dangerDurationMs),
      sm && sm->fallStopEnabled() ? 1 : 0);
}

void LaserModule::logFallStopSuppressed(
    const RhythmStateUpdateResult& rhythmResult,
    const FallStopActionDecision& actionDecision) const {
  const char* stopSource =
      rhythmResult.evidence.directDangerBandTriggered
          ? "ratio_above_danger_band"
          : "abnormal_exceeded_recovery_and_danger_hold";
  SW_VERBOSE_SERIAL_PRINTF(
      "%s [AUTO_STOP_SUPPRESSED] stop_reason=%s stop_source=%s stop_detail=%s stable_weight_kg=%.2f ma12_weight_kg=%.2f "
      "deviation_kg=%.2f ratio=%.4f abnormal_duration_ms=%lu danger_duration_ms=%lu "
      "fall_stop_enabled=%d should_execute_stop=%d stop_suppressed_by_switch=%d detail=%s\n",
      LogMarker::kSafety,
      rhythmResult.stopReason ? rhythmResult.stopReason : "danger_stop",
      verificationStopSourceName(VerificationStopSource::BASELINE_MAIN_LOGIC),
      stopSource,
      rhythmResult.evidence.baselineWeightKg,
      rhythmResult.evidence.ma12WeightKg,
      rhythmResult.evidence.deviationKg,
      rhythmResult.evidence.ratio,
      static_cast<unsigned long>(rhythmResult.evidence.abnormalDurationMs),
      static_cast<unsigned long>(rhythmResult.evidence.dangerDurationMs),
      actionDecision.fallStopEnabled ? 1 : 0,
      actionDecision.shouldExecuteStop ? 1 : 0,
      actionDecision.stopSuppressedBySwitch ? 1 : 0,
      actionDecision.detail ? actionDecision.detail : "n/a");
}

void LaserModule::handleFallStopCandidate(const RhythmStateUpdateResult& rhythmResult) {
  if (!rhythmResult.shouldStopByDanger || !sm) return;

  FallStopActionDecision actionDecision = sm->decideFallSuspectedAction();
  actionDecision.verificationStopSource = VerificationStopSource::BASELINE_MAIN_LOGIC;
  if (actionDecision.stopSuppressedBySwitch) {
    logFallStopSuppressed(rhythmResult, actionDecision);
  }
  sm->applyFallSuspectedAction(actionDecision);
}

void LaserModule::taskLoop() {
  uint32_t lastRead = 0;
  uint32_t lastStateEval = 0;
  uint32_t nextReadEligibleAtMs = 0;
  uint32_t loopReaderEpoch = measurementReader.readerEpoch();
#if DIAG_DISABLE_LASER_SAFETY
  bool diagBannerPrinted = false;
#endif

  while (true) {
    const bool noLaserConfigured = !deviceConfig.laserInstalled;
    const TopState loopTopState = sm ? sm->state() : TopState::IDLE;
    const bool unavailableIdleBackoffActive =
        deviceConfig.laserInstalled &&
        nextReadEligibleAtMs != 0 &&
        loopTopState != TopState::RUNNING;
    const uint32_t loopDelayMs =
        noLaserConfigured
            ? kLaserLoopIntervalNoLaserMs
            : (unavailableIdleBackoffActive ? kLaserLoopIntervalUnavailableIdleMs : 20UL);
    vTaskDelay(pdMS_TO_TICKS(loopDelayMs));
    // The config transaction takes this lock before stopping the old reader.
    // Never hold it across vTaskDelay; a completed old read and all resulting
    // state/stream work finish before the profile SET may be ACKed.
    ScopedSemaphore iterationLock(profileTransitionMutex);
    if (!iterationLock.held()) continue;
    const uint32_t activeReaderEpoch = measurementReader.readerEpoch();
    if (loopReaderEpoch != activeReaderEpoch) {
      loopReaderEpoch = activeReaderEpoch;
      lastRead = 0;
      lastStateEval = 0;
      nextReadEligibleAtMs = 0;
    }

#if DIAG_DISABLE_LASER_SAFETY
    if (!diagBannerPrinted) {
      SW_VERBOSE_SERIAL_PRINTLN("[DIAG] Laser safety triggers disabled");
      diagBannerPrinted = true;
    }
#endif

    if (needSendParams) {
      // 发布参数事件（由 BLE Transport 发给 App）
      float z, k; getParams(z, k);
      Event e{}; e.type = EventType::PARAMS; e.v1 = z; e.v2 = k; e.ts_ms = millis();
      if (bus) bus->publish(e);
      needSendParams = false;
    }

    uint32_t now = millis();
    const uint32_t readIntervalMs =
        measurementReader.backend() == LaserMeasurementBackend::CYD3S1612_IIC
            ? LASER_IIC_MEASUREMENT_READ_INTERVAL_MS
            : LASER_MEASUREMENT_READ_INTERVAL_MS;
    if (now - lastRead < readIntervalMs) continue;
    if (nextReadEligibleAtMs != 0 &&
        now < nextReadEligibleAtMs &&
        measurementProbePolicy.state() != MeasurementProbeState::OPEN_UNAVAILABLE) {
      continue;
    }
    lastRead = now;

    if (!deviceConfig.laserInstalled) {
      measurementReader.clearFailureBurst(now, true);
      measurementProbePolicy.reset();
      nextReadEligibleAtMs = 0;
      lastMeasurementValid = false;
      lastInvalidReason = "LASER_NOT_INSTALLED";
      updateMeasurementHealth(now, false, false, "LASER_NOT_INSTALLED");
      resetMeasurementPlane("no_laser_config", false);
      if (!hasLoggedMeasurementBypassState || !lastLoggedMeasurementBypassState) {
        logConfigTruth("measurement_bypass_changed", "no_laser_config");
      }
      continue;
    }

    if (!hasLoggedMeasurementBypassState || lastLoggedMeasurementBypassState) {
      logConfigTruth("measurement_bypass_changed");
    }

    const TopState readAttemptTopState = sm ? sm->state() : TopState::IDLE;
    const MeasurementProbeDecision probeDecision =
        measurementProbePolicy.beforeRead(
            now,
            deviceConfig.laserInstalled,
            measurementHealthMachine.faultConfirmed(),
            readAttemptTopState);
    LaserDiagnostics::logMeasurementProbeDecision(probeDecision, readAttemptTopState);
    if (!probeDecision.shouldRead) {
      nextReadEligibleAtMs = measurementProbePolicy.nextProbeAtMs();
      continue;
    }

    const uint32_t nextReadBackoffMs =
        nextReadEligibleAtMs != 0 && nextReadEligibleAtMs > now
            ? (nextReadEligibleAtMs - now)
            : 0;
    const MeasurementReadResult readResult =
        measurementReader.read(readAttemptTopState, deviceConfig.laserInstalled, nextReadBackoffMs);
    if (readResult.readerEpoch != measurementReader.readerEpoch() ||
        readResult.sensorProfile != deviceConfig.sensorProfile) {
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT] discard_stale_reader_frame result_epoch=%lu active_epoch=%lu result_profile=%s active_profile=%s\n",
          static_cast<unsigned long>(readResult.readerEpoch),
          static_cast<unsigned long>(measurementReader.readerEpoch()),
          sensorProfileName(readResult.sensorProfile),
          sensorProfileName(deviceConfig.sensorProfile));
      continue;
    }
    now = readResult.readCompletedAtMs;
    if (!readResult.transportOk) {
      const char* transportReason =
          readResult.invalidReason ? readResult.invalidReason : "READ_FAIL";
      noteDistanceValidity(false, 0, 0, NAN, false, transportReason, now);
      updateMeasurementHealth(now, false, false, transportReason);
      const MeasurementProbeObservation probeObservation =
          measurementProbePolicy.afterRead(
              now,
              deviceConfig.laserInstalled,
              measurementHealthMachine.faultConfirmed(),
              false,
              readResult.modbusResult,
              readAttemptTopState);
      LaserDiagnostics::logMeasurementProbeObservation(probeObservation, readAttemptTopState);
      const bool runtimeFaultPath = measurementHealthMachine.everReady();
      const uint32_t readFailBackoffMs = runtimeFaultPath
          ? kLaserRuntimeReadFailBackoffMs
          : kLaserUnavailableIdleReadBackoffMs;
      if (measurementProbePolicy.state() == MeasurementProbeState::OPEN_UNAVAILABLE) {
        nextReadEligibleAtMs = measurementProbePolicy.nextProbeAtMs();
      } else {
        nextReadEligibleAtMs =
            readAttemptTopState == TopState::RUNNING || readFailBackoffMs == 0
                ? 0
                : (now + readFailBackoffMs);
      }
      resetMeasurementPlane(transportReason, false);
      handleInvalidMeasurement(transportReason);
      publishMeasurementSample(now, false, 0.0f, 0.0f, transportReason);
      continue;
    }

    nextReadEligibleAtMs = 0;

    const char* validityReason = readResult.invalidReason;
    if (!readResult.validDistance || !readResult.sampleFresh ||
        readResult.sampleSequence <= currentSampleSequence) {
      if (readResult.validDistance &&
          (!readResult.sampleFresh || readResult.sampleSequence <= currentSampleSequence)) {
        validityReason = "SAMPLE_NOT_FRESH";
      }
      noteDistanceValidity(
          false,
          readResult.rawRegister,
          readResult.signedRaw,
          readResult.scaledDistance,
          readResult.sentinel,
          validityReason,
          now);
      updateMeasurementHealth(now, true, false, validityReason);
      const MeasurementProbeObservation probeObservation =
          measurementProbePolicy.afterRead(
              now,
              deviceConfig.laserInstalled,
              measurementHealthMachine.faultConfirmed(),
              true,
              readResult.modbusResult,
              readAttemptTopState);
      LaserDiagnostics::logMeasurementProbeObservation(probeObservation, readAttemptTopState);
      resetMeasurementPlane(readResult.sentinel ? "distance_sentinel" : "distance_out_of_range", false);
      handleInvalidMeasurement(readResult.sentinel ? "distance_sentinel" : "distance_out_of_range");
      publishMeasurementSample(now, false, 0.0f, 0.0f, validityReason);
      continue;
    }

    const uint32_t freshLimitMs =
        sampleFreshnessLimitMs(deviceConfig.sensorProfile);
    if (currentSampleValid && currentSampleAtMs != 0 &&
        freshLimitMs != 0 && now - currentSampleAtMs > freshLimitMs) {
      // A valid response after a long silent interval starts a new
      // consecutive-sample run; it must not inherit READY from the old run.
      const uint32_t gapAgeMs = now - currentSampleAtMs;
      noteDistanceValidity(
          false, readResult.rawRegister, readResult.signedRaw,
          readResult.scaledDistance, false, "SAMPLE_GAP", now);
      updateMeasurementHealth(now, false, false, "SAMPLE_GAP");
      handleInvalidMeasurement("SAMPLE_GAP");
      resetMeasurementPlane("sample_gap", false);
      SW_VERBOSE_SERIAL_PRINTF(
          "[MEASUREMENT] continuity_reset reason=SAMPLE_GAP age_ms=%lu limit_ms=%lu profile=%s\n",
          static_cast<unsigned long>(gapAgeMs),
          static_cast<unsigned long>(freshLimitMs),
          sensorProfileName(deviceConfig.sensorProfile));
    }

    noteDistanceValidity(
        true,
        readResult.rawRegister,
        readResult.signedRaw,
        readResult.scaledDistance,
        false,
        nullptr,
        now);
    currentSampleValid = true;
    currentSamplePhysicalMm = readResult.physicalDistanceMm;
    currentSampleAtMs = now;
    currentSampleSequence = readResult.sampleSequence;
    currentSampleReaderEpoch = readResult.readerEpoch;
    currentSampleProfile = readResult.sensorProfile;
    currentSampleConfigGeneration = deviceConfig.generation;

    float dist = readResult.scaledDistance;
    if (!isfinite(dist)) {
      noteDistanceValidity(
          false,
          readResult.rawRegister,
          readResult.signedRaw,
        dist,
        false,
        "DISTANCE_NONFINITE",
        now);
      updateMeasurementHealth(now, true, false, "DISTANCE_NONFINITE");
      const MeasurementProbeObservation probeObservation =
          measurementProbePolicy.afterRead(
              now,
              deviceConfig.laserInstalled,
              measurementHealthMachine.faultConfirmed(),
              true,
              readResult.modbusResult,
              readAttemptTopState);
      LaserDiagnostics::logMeasurementProbeObservation(probeObservation, readAttemptTopState);
      resetMeasurementPlane("distance_invalid", false);
      handleInvalidMeasurement("distance_invalid");
      publishMeasurementSample(now, false, 0.0f, 0.0f, "DISTANCE_NONFINITE");
      continue;
    }

    if (needZero) {
      // CAL:ZERO is a parameter reset. It must not sample the current sensor
      // distance and silently create a new formal 0 kg point.
      calibrationModelStore.clearModel(preferences);
      if (deviceConfig.schema1Managed && !clearModelBinding(preferences)) {
        SW_VERBOSE_SERIAL_PRINTLN(
            "[CAL] RESET binding_clear_failed; model remains unavailable");
      }
      calibrationModelReady = false;
      zeroDistance = 0.0f;
      scaleFactor = 1.0f;
      calibrationModel = CalibrationModel{};
      calibrationModel.referenceDistance = emptyDistanceReady
          ? emptyDistanceRecord.distanceRuntime
          : 0.0f;
      calibrationModel.coefficients[0] = 0.0f;
      calibrationModel.coefficients[1] = 1.0f;
      calibrationModel.coefficients[2] = 0.0f;
      dualZero.calibrationZeroDistance = calibrationModel.referenceDistance;
      distanceStateChain.reset("calibration_reset");
      distanceStateChain.setEmptyReference(
          emptyDistanceRecord.distanceRuntime,
          emptyDistanceReady && isfinite(emptyDistanceRecord.distanceRuntime));
      releaseOccupiedCycle("calibration_reset", now);
      resetRuntimeZero("calibration_reset");
      resetStableTracking("zero", true);
      rhythmStateJudge.reset("zero_reset");
      clearStableContractBridge("zero");
      resetMeasurementPlane("zero_reset", true);
      if (sm) {
        logStartReadyWriteback(now, "zero", sm->state(), false, 0.0f, "zero");
        sm->setStartReadiness(false, 0.0f);
      }
      needZero = false;
      SW_VERBOSE_SERIAL_PRINTLN("[CAL] RESET parameters_cleared formal_zero_unchanged=1");
      lastLogDist = -999.0f;
      needSendParams = true;
    }

    const bool distanceStateEnabled =
        measurementReader.backend() == LaserMeasurementBackend::CYD3S1612_IIC ||
        measurementReader.backend() == LaserMeasurementBackend::LASER_485;
    const DistanceStateChainResult distanceState =
        distanceStateChain.update(dist, now, sm && sm->state() == TopState::RUNNING);
    const bool weightValid = calibrationModelReady && isfinite(evaluateCalibrationWeight(dist));
    float weight = weightValid ? evaluateCalibrationWeight(dist) : NAN;
    // Sensor transport and distance validity determine measurement health.
    // A missing calibration model is a weight-quality problem and must not
    // suppress the independent distance state chain.
    updateMeasurementHealth(now, true, true, weightValid ? nullptr : "WEIGHT_INVALID");
    const MeasurementProbeObservation probeObservation =
        measurementProbePolicy.afterRead(
            now,
            deviceConfig.laserInstalled,
            measurementHealthMachine.faultConfirmed(),
            true,
            readResult.modbusResult,
            readAttemptTopState);
    LaserDiagnostics::logMeasurementProbeObservation(probeObservation, readAttemptTopState);
    latestWeightKg = weightValid ? weight : 0.0f;
    invalidPresenceSamples = 0;
    publishMeasurementSample(
        now,
        true,
        weightValid,
        dist,
        weightValid ? weight : 0.0f,
        nullptr,
        weightValid ? nullptr : "WEIGHT_INVALID");

    // ===== Safety supervisor: user on/off =====
    // Laser paths use distance-only presence. BASE keeps the existing owner.
    const bool presenceChanged = distanceStateEnabled
        ? distanceState.presenceChanged
        : updatePresenceState(weight);
    if (distanceStateEnabled) {
      stableContract.userPresent = distanceState.userPresent;
      stableContract.stableReadyLive = distanceState.stable;
      stableContract.stableReadyDistance = distanceState.stable
          ? distanceState.distance
          : 0.0f;
      if (distanceState.baselineReady) {
        stableContract.baselineReadyLatched = true;
        stableContract.baselineReadyDistance = distanceState.baselineDistance;
        stableBaselineDistance = distanceState.baselineDistance;
        stableBaselineDistanceMm = distanceState.baselineDistance * LASER_DISTANCE_RUNTIME_DIVISOR;
        if (!rhythmStateJudge.lastResult().evidence.baselineReady) {
          rhythmStateJudge.refreshBaselineFromStable(
              distanceState.baselineDistance,
              weightValid ? weight : 0.0f,
              now);
        }
      } else if (stableContract.userPresent &&
                 !distanceState.baselineReady &&
                 sm && sm->state() != TopState::RUNNING) {
        // DistanceStateChain invalidates its baseline when the user shifts
        // beyond baselineExitDelta. Do not leave the owner-side latch alive,
        // otherwise StartGateContractEvaluator could keep start_ready=true
        // from a stale baseline.
        stableContract.baselineReadyLatched = false;
        stableContract.baselineReadyWeightKg = 0.0f;
        stableContract.baselineReadyDistance = 0.0f;
        stableBaselineDistance = 0.0f;
        stableBaselineDistanceMm = 0.0f;
      }
      if (!distanceState.userPresent && !distanceState.baselineReady) {
        stableContract.baselineReadyLatched = false;
        stableContract.baselineReadyWeightKg = 0.0f;
        stableContract.baselineReadyDistance = 0.0f;
      }
    }
    if (presenceChanged && stableContract.userPresent) {
      lockEffectiveZeroForOccupiedCycle(now, "presence_enter");
    }
    if (presenceChanged && !stableContract.userPresent) {
#if !DIAG_DISABLE_LASER_SAFETY
      if (sm) sm->onUserOff();
#endif
    }
    if (sm) sm->setRuntimeReady(stableContract.userPresent);
    if (weightValid) observeRuntimeZero(dist, weight, now);

    // ===== Fault clear feed (Gemini rule) =====
    if (sm && weightValid) sm->onWeightSample(weight);

    const uint32_t stateEvalIntervalMs =
        shouldUseFastStableBuildReadInterval()
            ? phase2Thresholds.stable.evalIntervalStableBuildMs
            : phase2Thresholds.stable.evalIntervalDefaultMs;
    if (now - lastStateEval < stateEvalIntervalMs) {
      continue;
    }
    lastStateEval = now;

    float stableEvalDistance = dist;
    float stableEvalWeight = weight;
    if (weightValid && !updateStableSignalFilter(dist, stableEvalDistance, stableEvalWeight)) {
      handleInvalidMeasurement("stable_filter_invalid");
      continue;
    }

    if (weightValid) {
      updateStableState(stableEvalDistance, stableEvalWeight, now);
    }

    // LaserModule remains the measurement owner. The dedicated rhythm-state
    // judge consumes normalized context only and never owns final actions.
    TopState currentTopState = sm ? sm->state() : TopState::IDLE;

    RhythmStateJudgeInput rhythmInput{};
    rhythmInput.nowMs = now;
    rhythmInput.distance = dist;
    rhythmInput.weightKg = weightValid ? weight : 0.0f;
    rhythmInput.sampleValid = true;
    rhythmInput.useDistance = distanceStateEnabled;
    if (distanceStateEnabled && measurementReader.backend() == LaserMeasurementBackend::CYD3S1612_IIC) {
      rhythmInput.distanceSafeBand = 0.10f;
      rhythmInput.distanceDangerBand = 0.30f;
      rhythmInput.distanceAdvisoryBand = 0.20f;
      rhythmInput.distanceNearDangerBand = 0.25f;
      rhythmInput.distanceFallRateThreshold = 0.30f;
      rhythmInput.distanceMinimumCumulativeDrop = 0.30f;
    } else if (distanceStateEnabled) {
      rhythmInput.distanceSafeBand = 0.01f;
      rhythmInput.distanceDangerBand = 0.03f;
      rhythmInput.distanceAdvisoryBand = 0.02f;
      rhythmInput.distanceNearDangerBand = 0.025f;
      rhythmInput.distanceFallRateThreshold = 0.10f;
      rhythmInput.distanceMinimumCumulativeDrop = 0.10f;
    }
    rhythmInput.userPresent = stableContract.userPresent;
    rhythmInput.topState = currentTopState;
    const RhythmStateUpdateResult& rhythmResult = rhythmStateJudge.update(rhythmInput);
    MotionSafetyShadowInput shadowInput{};
    shadowInput.nowMs = now;
    shadowInput.sampleValid = true;
    shadowInput.topState = currentTopState;
    shadowInput.userPresent = stableContract.userPresent;
    shadowInput.baselineReady = rhythmResult.evidence.baselineReady;
    shadowInput.distance = dist;
    shadowInput.weightKg = weight;
    shadowInput.baselineDistance = rhythmResult.evidence.baselineDistance;
    shadowInput.baselineWeightKg = rhythmResult.evidence.baselineWeightKg;
    motionSafetyShadow.update(shadowInput);
    syncStableContractBridge(now, rhythmResult);
    if (sm) {
      // baseline_ready 才是正式 start/leave gate；
      // runtime_ready 仍仅表示人在平台上，不再单独承担“可开始”语义。
      logStartReadyWriteback(
          now,
          "loop_contract_writeback",
          sm->state(),
          stableContract.startReady,
          stableContract.startReadyWeightKg,
          stableContract.startReadyBridge);
      sm->setStartReadiness(
          stableContract.startReady,
          stableContract.startReadyWeightKg);
    }
    logRhythmStateUpdate(rhythmResult);
    publishBaselineMainVerification(now, rhythmResult);
    if (rhythmResult.shouldStopByDanger) {
#if !DIAG_DISABLE_LASER_SAFETY
      handleFallStopCandidate(rhythmResult);
#endif
    }

    currentTopState = sm ? sm->state() : currentTopState;
    handleRunSummaryState(currentTopState, now, dist, weightValid ? weight : 0.0f, rhythmResult);

    if (!stableContract.userPresent &&
        currentTopState != TopState::RUNNING &&
        stableContract.baselineReadyLatched) {
      // stable_weight：稳定体重。
      // 只有确认离台后，才清空当前主判断基线。
      // 这里同步清掉正式 start readiness，避免只靠首秒屏蔽或阈值调高来掩盖问题。
      releaseOccupiedCycle("user_left_platform_confirmed", now);
      rhythmStateJudge.reset("user_left_platform_confirmed");
      clearStableContractBridge("user_left_platform_confirmed");
      if (sm) {
        logStartReadyWriteback(
            now,
            "user_left_platform_confirmed",
            currentTopState,
            false,
            0.0f,
            "user_left_platform_confirmed");
        sm->setStartReadiness(false, 0.0f);
      }
    } else if (!stableContract.userPresent &&
               currentTopState != TopState::RUNNING &&
               dualZero.occupiedCycleActive &&
               !stableContract.baselineReadyLatched) {
      releaseOccupiedCycle("occupied_cycle_clear_without_baseline", now);
    }

    // ===== Silent log =====
    if (fabsf(dist - lastLogDist) > LOG_TH) {
#if DEBUG_LASER_STREAM
      SW_VERBOSE_SERIAL_PRINTF(">> Dist %6.2f -> %6.2f | W %5.2f\n",
        lastLogDist == -999.0f ? 0 : lastLogDist, dist, weight);
#endif
      lastLogDist = dist;
    }
  }
}
