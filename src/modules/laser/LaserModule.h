#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include "config/LaserPhase2Config.h"
#include "core/DeviceConfig.h"
#include "core/EventBus.h"
#include "core/SystemStateMachine.h"
#include "modules/laser/RhythmStateJudge.h"

class WaveModule;

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

struct DualZeroState {
  float calibrationZeroDistance = 0.0f;
  float runtimeZeroDistance = 0.0f;
  bool runtimeZeroValid = false;
  bool occupiedCycleActive = false;
  bool effectiveZeroLocked = false;
  bool effectiveZeroUsesRuntime = false;
  float effectiveZeroDistance = 0.0f;
  uint32_t effectiveZeroLockedAtMs = 0;
  uint32_t runtimeZeroCapturedAtMs = 0;
  float lastRuntimeZeroCandidateDistance = 0.0f;
  uint32_t runtimeZeroWindowStartedAtMs = 0;
};

struct StableContractState {
  bool userPresent = false;
  bool stableCandidate = false;
  bool stableReadyLive = false;
  bool baselineReadyLatched = false;
  bool startReady = false;
  float stableReadyWeightKg = 0.0f;
  float stableReadyDistance = 0.0f;
  float baselineReadyWeightKg = 0.0f;
  float baselineReadyDistance = 0.0f;
  float startReadyWeightKg = 0.0f;
  uint32_t stableReadyAtMs = 0;
  uint32_t baselineReadyAtMs = 0;
  const char* startReadyBridge = "not_ready";
};

class LaserModule {
public:
  void begin(EventBus* eb, SystemStateMachine* fsm, WaveModule* waveModule);
  void startTask();

  // Command hooks（由统一命令处理器调用）
  void triggerZero();
  void setParams(float zero, float factor);
  void getParams(float &zero, float &factor) const;
  float getWeightKg() const;
  bool isUserPresent() const;
  bool baselineReady() const;
  float stableWeightKg() const;
  PlatformModel platformModel() const;
  bool laserInstalled() const;
  bool laserAvailable() const;
  bool protectionDegraded() const;
  void getDeviceConfig(DeviceConfigSnapshot& out) const;
  bool setDeviceConfig(PlatformModel platformModel, bool laserInstalled, String& reason);
  bool getCalibrationModel(CalibrationModel& out) const;
  bool setCalibrationModel(const CalibrationModel& model, String& reason);
  bool captureCalibrationPoint(float referenceWeightKg, CalibrationCapture& out, String& reason);
  static const char* calibrationModelTypeName(CalibrationModelType type);

private:
  static void taskThunk(void* arg);
  void taskLoop();
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
  bool measurementBypassActive() const;
  void logConfigTruth(const char* source, const char* reason = nullptr);
  void loadDeviceConfig();
  void saveDeviceConfig();
  void applyDeviceConfigRuntimeEffects(const char* source);
  void loadCalibrationModel();
  void saveCalibrationModel();
  void syncLegacyParamsFromModel();
  bool applyCalibrationModel(const CalibrationModel& model, bool persist, const char* source, String& reason);
  bool isCalibrationModelFinite(const CalibrationModel& model) const;
  bool isCalibrationModelMonotonic(const CalibrationModel& model) const;
  float evaluateCalibrationWeight(float distance) const;
  float evaluateCalibrationWeight(const CalibrationModel& model, float distance) const;
  float evaluateCalibrationWeight(
      const CalibrationModel& model,
      float distance,
      float zeroReferenceDistance) const;
  float computeUnlockedEffectiveZeroDistance() const;
  bool runtimeZeroRefreshFrozen(TopState currentTopState) const;
  void lockEffectiveZeroForOccupiedCycle(uint32_t now, const char* reason);
  void releaseOccupiedCycle(const char* reason, uint32_t now);
  void resetRuntimeZero(const char* reason);
  void observeRuntimeZero(float distance, float weight, uint32_t now);
  float computeEffectiveZeroDistance() const;
  void refreshEffectiveZero();
  void pushStableSample(float distance, float weight);
  void beginStableCandidate(float distance, float weight);
  void updateStableState(float distance, float weight, uint32_t now);
  bool updatePresenceState(float weight);
  void noteInvalidPresenceSample(uint32_t now, const char* reason);
  void syncStableLiveContract(uint32_t now);
  void latchBaselineReadyFromStable(uint32_t now, float distance, float weight);
  void syncStartReadyContract(uint32_t now, TopState currentTopState, const RhythmStateUpdateResult& result);
  void syncStableContractBridge(uint32_t now, const RhythmStateUpdateResult& result);
  void clearStableContractBridge();
  void handleInvalidMeasurement(const char* reason);
  void latchStable(uint32_t now, const char* mode, float stddev);
  void resetStableTracking(const char* reason, bool logIfActive);
  bool shouldClearLatchedStable(float distance, float weight, const char*& reason) const;
  bool shouldUseFastStableBuildReadInterval() const;
  bool shouldLatchStableEarly(float latestWeight, float& stddev, float& latestDelta) const;
  void logRhythmStateUpdate(const RhythmStateUpdateResult& result) const;
  void emitBaselineReadyLog(uint32_t now) const;
  void publishBaselineMainVerification(uint32_t now, const RhythmStateUpdateResult& result) const;
  void handleRunSummaryState(TopState currentTopState,
                             uint32_t now,
                             float distance,
                             float weight,
                             const RhythmStateUpdateResult& rhythmResult);
  void handleFallStopCandidate(const RhythmStateUpdateResult& rhythmResult);
  void logFallStopSuppressed(const RhythmStateUpdateResult& rhythmResult,
                             const FallStopActionDecision& actionDecision) const;
  void startRunSummary(uint32_t now, const RhythmStateUpdateResult& rhythmResult);
  void accumulateRunSummary(uint32_t now,
                            float distance,
                            float weight,
                            const RhythmStateUpdateResult& rhythmResult);
  void finishRunSummary(uint32_t now,
                        FaultCode stopReason,
                        const RhythmStateUpdateResult& rhythmResult);
  bool computeRunAverage(uint8_t window, float& avgWeight, float& avgDistance) const;
  void resetMeasurementPlane(const char* reason, bool logReset);
  void pushMeasurementWeightSample(float weight);
  bool currentMa12(float& out) const;
  void publishMeasurementSample(
      uint32_t now,
      bool valid,
      float distance,
      float weight,
      const char* reason);
  void logMeasurementPlaneSummary(
      uint32_t now,
      bool valid,
      float distance,
      float weight,
      bool ma12Ready,
      float ma12,
      const char* reason,
      const char* trigger);
  void logLatestMeasurementPlaneSummary(const char* trigger);

  float getMean(const float* values) const;
  float getStdDev(const float* values) const;

private:
  enum class StableState : uint8_t {
    UNSTABLE,
    STABLE_CANDIDATE,
    STABLE_LATCHED
  };

  struct RangeTracker {
    bool valid = false;
    float min = 0.0f;
    float max = 0.0f;
  };

  // Per-run evidence stays local so stop/abort summaries can stay concise.
  struct RunSummaryState {
    bool active = false;
    uint32_t nextTestId = 1;
    uint32_t testId = 0;
    uint32_t startedAtMs = 0;
    uint32_t samples = 0;
    bool baselineReady = false;
    float freqHz = 0.0f;
    int intensity = 0;
    float intensityNormalized = 0.0f;
    float baselineWeightKg = 0.0f;
    float baselineDistance = 0.0f;
    bool fallStopEnabled = FALL_STOP_ENABLED_DEFAULT;
    RhythmStateStatus lastRhythmStatus = RhythmStateStatus::BASELINE_PENDING;
    const char* lastRhythmReason = "baseline_pending";
    RangeTracker weightKgRange{};
    RangeTracker distanceRange{};
    RangeTracker ma3WeightKgRange{};
    RangeTracker ma3DistanceRange{};
    RangeTracker ma5WeightKgRange{};
    RangeTracker ma5DistanceRange{};
    RangeTracker ma7WeightKgRange{};
    RangeTracker ma7DistanceRange{};
    uint16_t advisoryCount = 0;
    RiskAdvisoryType lastAdvisoryType = RiskAdvisoryType::NONE;
    RiskAdvisoryLevel lastAdvisoryLevel = RiskAdvisoryLevel::NONE;
    const char* lastAdvisoryReason = "none";
    float recentWeightKg[7]{};
    float recentDistance[7]{};
    uint8_t recentHead = 0;
    uint8_t recentCount = 0;
  };

  EventBus* bus = nullptr;
  SystemStateMachine* sm = nullptr;
  WaveModule* wave = nullptr;

  ModbusMaster node;
  Preferences preferences;
  DeviceConfigSnapshot deviceConfig{};
  LaserPhase2ThresholdConfig phase2Thresholds{};

  // 与你原固件保持一致
  float zeroDistance = 0.0f;
  float scaleFactor = 1.0f;
  CalibrationModel calibrationModel{};
  DualZeroState dualZero{};

  float weightBuffer[WINDOW_N]{};
  float distanceBuffer[WINDOW_N]{};
  int bufHead = 0, bufCount = 0;
  StableState stableState = StableState::UNSTABLE;
  // stable_weight 生命周期入口：
  // 仅在未律动且稳定站立时锁定，确认离台后才允许清空并重建。
  float stableBaselineDistance = 0.0f;
  float stableBaselineDistanceMm = 0.0f;
  float stableBaselineWeight = 0.0f;
  uint32_t stableLatchedAtMs = 0;
  uint8_t invalidStableSamples = 0;
  uint32_t stableCandidateStartedAtMs = 0;
  bool stableEarlyCheckpointLogged = false;
  float runtimeZeroBuffer[WINDOW_N]{};
  uint8_t runtimeZeroHead = 0;
  uint8_t runtimeZeroCount = 0;
  StableContractState stableContract{};
  uint8_t presenceEnterConfirmCount = 0;
  uint8_t presenceExitConfirmCount = 0;
  uint8_t invalidPresenceSamples = 0;
  uint8_t stableExitConfirmCount = 0;
  const char* stableExitPendingReason = nullptr;

  // 静默日志
  float lastLogDist = -999.0f;

  // flags
  volatile bool needZero = false;
  volatile bool needSendParams = false;

  volatile float latestWeightKg = 0.0f;
  bool lastMeasurementValid = false;
  uint32_t lastValidityLogMs = 0;
  bool hasLoggedMeasurementBypassState = false;
  bool lastLoggedMeasurementBypassState = false;
  const char* lastInvalidReason = nullptr;
  uint32_t calibrationCaptureCounter = 0;
  float ma12WeightBuffer[MEASUREMENT_MA12_WINDOW]{};
  uint8_t ma12Head = 0;
  uint8_t ma12Count = 0;
  uint32_t measurementSequence = 0;
  uint32_t measurementPlaneLogStartedAtMs = 0;
  uint32_t measurementPlaneLogSamples = 0;
  uint32_t lastInvalidMeasurementEventMs = 0;
  const char* lastInvalidMeasurementEventReason = nullptr;
  bool hasLatestMeasurementSample = false;
  bool latestMeasurementSampleValid = false;
  float latestMeasurementSampleDistance = 0.0f;
  float latestMeasurementSampleWeight = 0.0f;
  bool latestMeasurementSampleMa12Ready = false;
  float latestMeasurementSampleMa12 = 0.0f;
  const char* latestMeasurementSampleReason = nullptr;
  bool hasLoggedMeasurementSummary = false;
  bool lastLoggedMeasurementSummaryValid = false;
  const char* lastLoggedMeasurementSummaryReason = nullptr;
  // Primary Judgment Owner：
  // MA7 / deviation / ratio / main_state / duration 统一由 RhythmStateJudge 维护。
  RhythmStateJudge rhythmStateJudge{};
  RunSummaryState runSummary{};
  TopState lastObservedTopState = TopState::IDLE;
};
