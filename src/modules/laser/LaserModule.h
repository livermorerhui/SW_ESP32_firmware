#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config/LaserPhase2Config.h"
#include "core/DeviceConfig.h"
#include "core/EventBus.h"
#include "core/SystemStateMachine.h"
#include "modules/laser/BaselineEvidenceEvaluator.h"
#include "modules/laser/CalibrationModelStore.h"
#include "modules/laser/DeviceConfigStore.h"
#include "modules/laser/LaserMeasurementReader.h"
#include "modules/laser/MeasurementPlane.h"
#include "modules/laser/MotionSafetyShadowEvaluator.h"
#include "modules/laser/PresenceContractEvaluator.h"
#include "modules/laser/RhythmStateJudge.h"
#include "modules/laser/RuntimeZeroObserver.h"
#include "modules/laser/RunSummaryCollector.h"
#include "modules/laser/StartGateContractEvaluator.h"

class WaveModule;

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
  float evaluateCalibrationWeight(float distance) const;
  float evaluateCalibrationWeight(const CalibrationModel& model, float distance) const;
  float evaluateCalibrationWeight(
      const CalibrationModel& model,
      float distance,
      float zeroReferenceDistance) const;
  float computeUnlockedEffectiveZeroDistance() const;
  void lockEffectiveZeroForOccupiedCycle(uint32_t now, const char* reason);
  void releaseOccupiedCycle(const char* reason, uint32_t now);
  void resetRuntimeZero(const char* reason);
  void observeRuntimeZero(float distance, float weight, uint32_t now);
  float computeEffectiveZeroDistance() const;
  void refreshEffectiveZero();
  void resetStableSignalFilter();
  bool updateStableSignalFilter(float distance, float& filteredDistance, float& filteredWeight);
  void pushStableSample(float distance, float weight);
  void beginStableCandidate(float distance, float weight);
  void updateStableState(float distance, float weight, uint32_t now);
  bool updatePresenceState(float weight);
  void noteInvalidPresenceSample(uint32_t now, const char* reason);
  void syncStableLiveContract(uint32_t now);
  void latchBaselineReadyFromStable(uint32_t now, const char* source, float distance, float weight);
  void syncStartReadyContract(uint32_t now, TopState currentTopState, const RhythmStateUpdateResult& result);
  void observeStartGateDiagnostics(
      uint32_t now,
      const StartGateContractInput& input,
      const StartGateContractResult& evaluation);
  void logStartGateDiagnostics(
      uint32_t now,
      const char* trigger,
      const StartGateContractInput& input,
      const StartGateContractResult& evaluation);
  void resetStartGateDiagnosticsWindow(uint32_t now);
  void syncStableContractBridge(uint32_t now, const RhythmStateUpdateResult& result);
  void clearStableContractBridge(const char* reason);
  void logBaselineContractLatch(uint32_t now, const char* source, float distance, float weight) const;
  void logBaselineContractClear(
      uint32_t now,
      const char* reason,
      const StableContractState& before) const;
  void logStartReadyWriteback(
      uint32_t now,
      const char* source,
      TopState topState,
      bool ready,
      float stableWeightKg,
      const char* reason);
  void handleInvalidMeasurement(const char* reason);
  void latchStable(uint32_t now, const char* mode, float stddev);
  void resetStableTracking(const char* reason, bool logIfActive);
  bool shouldClearLatchedStable(float distance, float weight, const char*& reason) const;
  bool shouldUseFastStableBuildReadInterval() const;
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
  void resetMeasurementPlane(const char* reason, bool logReset);
  void publishMeasurementSample(
      uint32_t now,
      bool valid,
      float distance,
      float weight,
      const char* reason);
  void logLatestMeasurementPlaneSummary(const char* trigger);

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
  WaveModule* wave = nullptr;

  LaserMeasurementReader measurementReader;
  MeasurementPlane measurementPlane;
  DeviceConfigStore deviceConfigStore;
  CalibrationModelStore calibrationModelStore;
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
  uint8_t stableConfirmCount = 0;
  uint32_t stableCandidateStartedAtMs = 0;
  bool stableEarlyCheckpointLogged = false;
  bool stableFilterValid = false;
  float stableFilteredDistance = 0.0f;
  float stableFilteredWeight = 0.0f;
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
  // Primary Judgment Owner：
  // MA12 / deviation / ratio / main_state / duration 统一由 RhythmStateJudge 维护。
  RhythmStateJudge rhythmStateJudge{};
  MotionSafetyShadowEvaluator motionSafetyShadow{};
  RuntimeZeroObserver runtimeZeroObserver{};
  RunSummaryCollector runSummaryCollector{};
  TopState lastObservedTopState = TopState::IDLE;
  uint32_t startGateDiagWindowStartedAtMs = 0;
  uint32_t startGateDiagLastLogMs = 0;
  uint32_t startGateDiagEvaluations = 0;
  uint32_t startGateDiagReady = 0;
  uint32_t startGateDiagMeasurementInvalid = 0;
  uint32_t startGateDiagUserNotPresent = 0;
  uint32_t startGateDiagBaselineNotReady = 0;
  uint32_t startGateDiagLiveStableNotReady = 0;
  uint32_t startGateDiagRunningHold = 0;
  uint32_t startGateDiagIdleReady = 0;
  bool hasLoggedStartReadyWriteback = false;
  bool lastLoggedStartReadyWritebackReady = false;
  TopState lastLoggedStartReadyWritebackTopState = TopState::IDLE;
  float lastLoggedStartReadyWritebackWeightKg = 0.0f;
  const char* lastLoggedStartReadyWritebackSource = nullptr;
  const char* lastLoggedStartReadyWritebackReason = nullptr;
};
