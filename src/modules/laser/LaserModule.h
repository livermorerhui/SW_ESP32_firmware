#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/semphr.h>
#include "config/LaserPhase2Config.h"
#include "core/DeviceConfig.h"
#include "core/DeviceConfigPorts.h"
#include "core/MeasurementFacts.h"
#include "core/LaserStatePort.h"
#include "core/EventBus.h"
#include "core/ProtocolCodec.h"
#include "core/WaveObservation.h"
#include "modules/laser/BaselineContractDiagnostics.h"
#include "modules/laser/BaselineEvidenceEvaluator.h"
#include "modules/laser/CalibrationRuntime.h"
#include "modules/laser/CalibrationModelStore.h"
#include "modules/laser/DeviceConfigStore.h"
#include "modules/laser/DistanceStateChain.h"
#include "modules/laser/LaserDiagnostics.h"
#include "modules/laser/LaserMeasurementReader.h"
#include "modules/laser/LaserMeasurementRuntime.h"
#include "modules/laser/LaserStabilityRuntime.h"
#include "modules/laser/LaserStableWindow.h"
#include "modules/laser/MeasurementAvailabilityProbePolicy.h"
#include "modules/laser/MeasurementHealthStateMachine.h"
#include "modules/laser/MeasurementPlane.h"
#include "modules/laser/MotionSafetyShadowEvaluator.h"
#include "modules/laser/PresenceContractEvaluator.h"
#include "modules/laser/ProfileEmptyDistanceStore.h"
#include "modules/laser/RhythmStateJudge.h"
#include "modules/laser/RuntimeZeroObserver.h"
#include "modules/laser/RunSummaryCollector.h"
#include "modules/laser/StartGateContractEvaluator.h"

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

class LaserModule : public IDeviceConfigOwner, public MeasurementFacts {
public:
  void begin(EventBus* eb, LaserStatePort* statePort, WaveObservation* waveObservation);
  void startTask();

  // Command hooks（由统一命令处理器调用）
  void triggerZero();
  bool setParams(float zero, float factor, String& reason);
  void getParams(float &zero, float &factor) const;
  float getWeightKg() const;
  bool isUserPresent() const override;
  bool baselineReady() const override;
  float stableWeightKg() const override;
  PlatformModel platformModel() const override;
  bool laserInstalled() const override;
  bool laserAvailable() const override;
  MeasurementHealthState measurementHealth() const override;
  bool measurementFaultConfirmed() const override;
  bool measurementStartupResolved() const;
  bool protectionDegraded() const;
  void getDeviceConfig(DeviceConfigSnapshot& out) const;
  bool getProfileConfig(DeviceConfigSnapshot& out, String& reason) override;
  bool setProfileConfig(
      PlatformModel platformModel,
      SensorProfileId sensorProfile,
      String& reason) override;
  bool setDeviceConfig(
      PlatformModel platformModel,
      bool laserInstalled,
      String& reason) override;
  bool configOperational() const;
  bool configIdentityValid() const;
  bool startConfigurationComplete() const override;
  bool protectedStartPolicyAvailable() const override;
  bool getCalibrationModel(CalibrationModel& out) const;
  bool setCalibrationModel(const CalibrationModel& model, String& reason);
  bool getEmptyDistance(EmptyDistanceRecord& out) const;
  bool getProfileEmptyDistance(ProfileEmptyDistanceRecord& out, String& reason);
  bool setEmptyDistanceFromCurrent(EmptyDistanceRecord& out, String& reason);
  bool setEmptyDistanceFromCurrent(ProfileEmptyDistanceRecord& out, String& reason);
  bool setEmptyDistance(float distanceMm, String& reason);
  bool clearEmptyDistance(String& reason);
  bool clearWeightModel(String& reason);
  bool clearCalibrationParameters(String& reason);
  const char* sensorBackendName() const;
  bool captureCalibrationPoint(float referenceWeightKg, CalibrationCapture& out, String& reason);
  bool setStreamSubscription(bool enabled, uint8_t rateHz, String& reason);
  void resetStreamSubscription(const char* reason);
  bool streamSubscriptionEnabled() const;
  uint8_t streamSubscriptionRateHz() const;
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
  void resetMeasurementHealth(const char* reason, uint32_t now);
  void updateMeasurementHealth(
      uint32_t now,
      bool transportOk,
      bool validDistance,
      const char* reason);
  void logMeasurementHealthChange(
      uint32_t now,
      MeasurementHealthState previous,
      MeasurementHealthState next,
      const char* reason) const;
  void syncSensorHealthToStateMachine();
  bool measurementBypassActive() const;
  void logConfigTruth(const char* source, const char* reason = nullptr);
  void loadDeviceConfig();
  bool applyStoredConfigTransition(
      PlatformModel platformModel,
      SensorProfileId sensorProfile,
      bool legacyReserved,
      const char* source,
      String& reason);
  void applyDeviceConfigRuntimeEffects(const char* source);
  void configureDistanceStateChainForProfile(const char* source);
  bool configurationMutationBlocked(String& reason) const;
  void stopWaveForConfigurationFault(const char* source);
  void invalidateEmptyReferenceAfterStorageFailure(const char* source);
  void loadCalibrationModel();
  void loadEmptyDistance();
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
  BaselineContractStateView captureBaselineContractView(const StableContractState& state) const;
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
  void publishMeasurementSample(
      uint32_t now,
      bool distanceValid,
      bool weightValid,
      float distance,
      float weight,
      const char* distanceReason,
      const char* weightReason);
  bool shouldPublishStreamSample(uint32_t now);
  void logLatestMeasurementPlaneSummary(const char* trigger);

  float getMean(const float* values) const;
  float getStdDev(const float* values) const;

private:
  using StableState = LaserStabilityRuntime::StableState;

  EventBus* bus = nullptr;
  LaserStatePort* statePort = nullptr;
  WaveObservation* waveObservation = nullptr;

  LaserMeasurementRuntime measurementRuntime;
  LaserMeasurementReader& measurementReader = measurementRuntime.reader();
  DistanceStateChain distanceStateChain;
  MeasurementPlane& measurementPlane = measurementRuntime.plane();
  DeviceConfigStore deviceConfigStore;
  CalibrationModelStore calibrationModelStore;
  ProfileEmptyDistanceStore profileEmptyDistanceStore;
  Preferences preferences;
  DeviceConfigSnapshot deviceConfig{};
  DeviceConfigStoreStatus configStatus = DeviceConfigStoreStatus::CONFIG_ERROR;
  bool preferencesOpened = false;
  volatile bool configTransitionActive = false;
  SemaphoreHandle_t profileTransitionMutex = nullptr;
  bool currentSampleValid = false;
  float currentSamplePhysicalMm = NAN;
  uint32_t currentSampleAtMs = 0;
  uint32_t currentSampleSequence = 0;
  uint32_t currentSampleReaderEpoch = 0;
  SensorProfileId currentSampleProfile = SensorProfileId::NONE;
  uint32_t currentSampleConfigGeneration = 0;
  LaserPhase2ThresholdConfig phase2Thresholds{};

  // 与你原固件保持一致
  float zeroDistance = 0.0f;
  float scaleFactor = 1.0f;
  CalibrationModel calibrationModel{};
  EmptyDistanceRecord emptyDistanceRecord{};
  // A cleared or never-written model must not be silently treated as a valid
  // weight calibration. Distance state can still run without it.
  bool calibrationModelReady = false;
  bool emptyDistanceReady = false;
  DualZeroState dualZero{};

  LaserStabilityRuntime stabilityRuntime;
  float (&weightBuffer)[WINDOW_N] = stabilityRuntime.weightBuffer;
  float (&distanceBuffer)[WINDOW_N] = stabilityRuntime.distanceBuffer;
  int& bufHead = stabilityRuntime.bufHead;
  int& bufCount = stabilityRuntime.bufCount;
  LaserStabilityRuntime::StableState& stableState = stabilityRuntime.stableState;
  // stable_weight 生命周期入口：
  // 仅在未律动且稳定站立时锁定，确认离台后才允许清空并重建。
  float& stableBaselineDistance = stabilityRuntime.stableBaselineDistance;
  float& stableBaselineDistanceMm = stabilityRuntime.stableBaselineDistanceMm;
  float& stableBaselineWeight = stabilityRuntime.stableBaselineWeight;
  uint32_t& stableLatchedAtMs = stabilityRuntime.stableLatchedAtMs;
  uint8_t& invalidStableSamples = stabilityRuntime.invalidStableSamples;
  uint8_t& stableConfirmCount = stabilityRuntime.stableConfirmCount;
  uint32_t& stableCandidateStartedAtMs = stabilityRuntime.stableCandidateStartedAtMs;
  bool& stableEarlyCheckpointLogged = stabilityRuntime.stableEarlyCheckpointLogged;
  bool& stableFilterValid = stabilityRuntime.stableFilterValid;
  float& stableFilteredDistance = stabilityRuntime.stableFilteredDistance;
  float& stableFilteredWeight = stabilityRuntime.stableFilteredWeight;
  float runtimeZeroBuffer[WINDOW_N]{};
  uint8_t runtimeZeroHead = 0;
  uint8_t runtimeZeroCount = 0;
  StableContractState& stableContract = stabilityRuntime.stableContract;
  uint8_t& presenceEnterConfirmCount = stabilityRuntime.presenceEnterConfirmCount;
  uint8_t& presenceExitConfirmCount = stabilityRuntime.presenceExitConfirmCount;
  uint8_t& invalidPresenceSamples = stabilityRuntime.invalidPresenceSamples;
  uint8_t& stableExitConfirmCount = stabilityRuntime.stableExitConfirmCount;
  const char*& stableExitPendingReason = stabilityRuntime.stableExitPendingReason;

  // 静默日志
  float lastLogDist = -999.0f;

  // flags
  volatile bool needZero = false;
  volatile bool needSendParams = false;

  volatile float latestWeightKg = 0.0f;
  bool lastMeasurementValid = false;
  MeasurementAvailabilityProbePolicy& measurementProbePolicy = measurementRuntime.probePolicy();
  MeasurementHealthStateMachine& measurementHealthMachine = measurementRuntime.health();
  uint32_t lastValidityLogMs = 0;
  bool hasLoggedMeasurementBypassState = false;
  bool lastLoggedMeasurementBypassState = false;
  const char* lastInvalidReason = nullptr;
  uint32_t calibrationCaptureCounter = 0;
  volatile bool streamEnabled = false;
  volatile uint8_t streamRateHz = ProtocolCodec::kStreamDefaultRateHz;
  uint32_t lastStreamPublishedAtMs = 0;
  // Primary Judgment Owner：
  // MA12 / deviation / ratio / main_state / duration 统一由 RhythmStateJudge 维护。
  RhythmStateJudge rhythmStateJudge{};
  MotionSafetyShadowEvaluator motionSafetyShadow{};
  RuntimeZeroObserver runtimeZeroObserver{};
  RunSummaryCollector runSummaryCollector{};
  BaselineContractDiagnostics baselineContractDiagnostics{};
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
};
