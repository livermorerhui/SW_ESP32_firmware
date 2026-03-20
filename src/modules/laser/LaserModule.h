#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/SystemStateMachine.h"

enum class CalibrationModelType : uint8_t {
  LINEAR = 1,
  QUADRATIC = 2
};

struct CalibrationModel {
  CalibrationModelType type = CalibrationModelType::LINEAR;
  float referenceDistance = 0.0f;
  float coefficients[3] = {0.0f, 1.0f, 0.0f};
};

struct CalibrationCapture {
  uint32_t index = 0;
  uint32_t ts_ms = 0;
  float distanceMm = 0.0f;
  float referenceWeightKg = 0.0f;
  float predictedWeightKg = 0.0f;
  bool stableFlag = false;
  bool validFlag = false;
};

class LaserModule {
public:
  void begin(EventBus* eb, SystemStateMachine* fsm);
  void startTask();

  // Command hooks（由统一命令处理器调用）
  void triggerZero();
  void setParams(float zero, float factor);
  void getParams(float &zero, float &factor) const;
  float getWeightKg() const;
  bool getCalibrationModel(CalibrationModel& out) const;
  bool setCalibrationModel(const CalibrationModel& model, String& reason);
  bool captureCalibrationPoint(float referenceWeightKg, CalibrationCapture& out, String& reason);
  static const char* calibrationModelTypeName(CalibrationModelType type);

private:
  static void taskThunk(void* arg);
  void taskLoop();
  bool shouldEmitStream(float distance, float weight, uint32_t now) const;
  void noteStreamSent(float distance, float weight, uint32_t now);
  bool isDistanceSentinelRaw(uint16_t rawRegister, int16_t signedRaw, const char*& reason) const;
  bool isDistanceValidRaw(int16_t signedRaw, const char*& reason) const;
  void noteDistanceValidity(
      bool valid,
      uint16_t rawRegister,
      int16_t signedRaw,
      float scaledDistance,
      bool sentinel,
      const char* reason,
      uint32_t now);
  void loadCalibrationModel();
  void saveCalibrationModel();
  void syncLegacyParamsFromModel();
  bool applyCalibrationModel(const CalibrationModel& model, bool persist, const char* source, String& reason);
  bool isCalibrationModelFinite(const CalibrationModel& model) const;
  bool isCalibrationModelMonotonic(const CalibrationModel& model) const;
  float evaluateCalibrationWeight(float distance) const;
  float evaluateCalibrationWeight(const CalibrationModel& model, float distance) const;
  void pushStableSample(float distance, float weight);
  void beginStableCandidate(float distance, float weight);
  void updateStableState(float distance, float weight, uint32_t now);
  void handleInvalidMeasurement(const char* reason);
  void latchStable(uint32_t now);
  void resetStableTracking(const char* reason, bool logIfActive);
  bool shouldClearLatchedStable(float distance, float weight, const char*& reason) const;

  float getMean(const float* values) const;
  float getStdDev(const float* values) const;

private:
  enum class StableState : uint8_t {
    UNSTABLE,
    STABLE_CANDIDATE,
    STABLE_LATCHED
  };

  EventBus* bus = nullptr;
  SystemStateMachine* sm = nullptr;

  ModbusMaster node;
  Preferences preferences;

  // 与你原固件保持一致
  float zeroDistance = 0.0f;
  float scaleFactor = 1.0f;
  CalibrationModel calibrationModel{};

  float weightBuffer[WINDOW_N]{};
  float distanceBuffer[WINDOW_N]{};
  int bufHead = 0, bufCount = 0;
  StableState stableState = StableState::UNSTABLE;
  float stableBaselineDistance = 0.0f;
  float stableBaselineDistanceMm = 0.0f;
  float stableBaselineWeight = 0.0f;
  uint32_t stableLatchedAtMs = 0;
  uint8_t invalidStableSamples = 0;

  // 静默日志
  float lastLogDist = -999.0f;

  // flags
  volatile bool needZero = false;
  volatile bool needSendParams = false;

  // for fall detect
  float lastWeight = 0.0f;
  uint32_t lastMs = 0;
  volatile float latestWeightKg = 0.0f;
  bool userPresent = false;
  bool hasStreamSample = false;
  uint32_t lastStreamTime = 0;
  float lastStreamDistance = 0.0f;
  float lastStreamWeight = 0.0f;
  bool lastMeasurementValid = true;
  uint32_t lastValidityLogMs = 0;
  const char* lastInvalidReason = nullptr;
  uint32_t calibrationCaptureCounter = 0;
};
