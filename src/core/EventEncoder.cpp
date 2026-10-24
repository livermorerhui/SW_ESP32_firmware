#include "EventEncoder.h"

#include "core/ProtocolSupport.h"
#include "core/VerboseSerial.h"
#include "config/GlobalConfig.h"

String EventEncoder::encodeSnapshot(const PlatformSnapshot& snapshot) {
    String s;
    // Keep connect-time snapshot within a single BLE notify frame for the
    // common MTU=185 path. Startup/start-button truth must not depend on a
    // multi-fragment SNAPSHOT.
    s.reserve(160);
    s = "SNAPSHOT:";
    s += "top_state=";
    s += topStateName(snapshot.topState);
    s += " start_ready=";
    s += snapshot.startReady ? "1" : "0";
    s += " laser_available=";
    s += snapshot.laserAvailable ? "1" : "0";
    s += " measurement_health=";
    s += measurementHealthStateName(snapshot.measurementHealth);
    s += " degraded_start_available=";
    s += snapshot.degradedStartAvailable ? "1" : "0";
    s += " degraded_start_enabled=";
    s += snapshot.degradedStartEnabled ? "1" : "0";
    s += " leave_stop_enabled=";
    s += snapshot.leaveStopEnabled ? "1" : "0";
    ProtocolSupport::logTruthPayloadBudgetWarningIfNeeded(
        "runtime_truth",
        s.length() + 1,
        ProtocolSupport::kConnectSnapshotPayloadBudgetBytes,
        s);
    return s;
  }

String EventEncoder::encode(const Event& e) {
    String s;
    switch (e.type) {
      case EventType::STATE:
        s = "EVT:STATE ";
        s += (e.state == TopState::IDLE ? "IDLE" :
              e.state == TopState::ARMED ? "ARMED" :
              e.state == TopState::RUNNING ? "RUNNING" : "FAULT_STOP");
        return s;
      case EventType::WAVE_OUTPUT:
        s = "EVT:WAVE_OUTPUT active=";
        s += e.waveOutputActive ? "1" : "0";
        return s;
      case EventType::FAULT:
        s = "EVT:FAULT ";
        s += String((uint16_t)e.fault);
        if (const char* compatReason = ProtocolSupport::minimalCompatFaultReason(e.fault)) {
          s += " reason=";
          s += compatReason;
          SW_VERBOSE_SERIAL_PRINTF("[FAULT_COMPAT] export code=%u reason=%s route=EVT:FAULT\n",
                        static_cast<unsigned>(e.fault),
                        compatReason);
        }
        return s;
      case EventType::SAFETY:
        s = "EVT:SAFETY reason=";
        s += faultCodeName(e.fault);
        s += " code=";
        s += String((uint16_t)e.fault);
        s += " effect=";
        s += safetySignalName(e.safety);
        s += " state=";
        s += topStateName(e.state);
        s += " wave=";
        s += e.waveStopped ? "STOPPED" : "RUNNING";
        return s;
      case EventType::STABLE_WEIGHT:
        s = "EVT:STABLE:";
        s += String(e.v1, 2);
        return s;
      case EventType::PARAMS:
        s = "EVT:PARAM:";
        s += String(e.v1, 2);
        s += ",";
        s += String(e.v2, 4);
        return s;
      case EventType::STREAM:
        // Older producers only populated measurementValid. Infer a complete
        // pair for that legacy shape; new producers set the two explicit
        // quality bits and can therefore represent distance-only samples.
        {
        const bool legacyComplete = e.measurementValid &&
            !e.distanceValid && !e.weightValid;
        const bool encodedDistanceValid = e.distanceValid || legacyComplete;
        const bool encodedWeightValid = e.weightValid || legacyComplete;
        s = "EVT:STREAM ";
        s += "seq=";
        s += String(e.sampleSeq);
        s += " ts_ms=";
        s += String(e.ts_ms);
        s += " valid=";
        s += e.measurementValid ? "1" : "0";
        s += " distance_valid=";
        s += encodedDistanceValid ? "1" : "0";
        s += " weight_valid=";
        s += encodedWeightValid ? "1" : "0";
        s += " ma12_ready=";
        s += e.ma12Ready ? "1" : "0";
        if (encodedDistanceValid) {
          s += " distance_mm=";
          s += String(e.distance * LASER_DISTANCE_RUNTIME_DIVISOR, 2);
        }
        if (encodedWeightValid) {
          s += " weight=";
          s += String(e.weightKg, 2);
          if (e.ma12Ready) {
            s += " ma12=";
            s += String(e.ma12WeightKg, 2);
          }
        }
        // Keep the normal complete frame compact. A reason is emitted for
        // transport/quality failures and partial samples only.
        if ((!e.measurementValid || !encodedDistanceValid || !encodedWeightValid) &&
            e.measurementReason[0] != '\0') {
          s += " reason=";
          s += e.measurementReason;
        }
        if (e.distanceReason[0] != '\0') {
          s += " distance_reason=";
          s += e.distanceReason;
        }
        if (e.weightReason[0] != '\0') {
          s += " weight_reason=";
          s += e.weightReason;
        }
        if (e.sensorBackend[0] != '\0') {
          s += " backend=";
          s += e.sensorBackend;
        }
        return s;
        }
      case EventType::BASELINE_MAIN:
        s = "EVT:BASELINE ";
        s += "start_ready=";
        s += e.startReady ? "1" : "0";
        s += " ";
        s += "baseline_ready=";
        s += e.baselineReady ? "1" : "0";
        s += " stable_weight=";
        s += String(e.stableWeightKg, 2);
        s += " ma12=";
        s += String(e.mainMa12WeightKg, 2);
        s += " deviation=";
        s += String(e.deviationKg, 2);
        s += " ratio=";
        s += String(e.ratio, 4);
        s += " main_state=";
        s += (e.mainState[0] != '\0') ? e.mainState : "BASELINE_PENDING";
        s += " abnormal_duration_ms=";
        s += String(e.abnormalDurationMs);
        s += " danger_duration_ms=";
        s += String(e.dangerDurationMs);
        s += " stop_reason=";
        s += (e.stopReasonText[0] != '\0') ? e.stopReasonText : "NONE";
        s += " stop_source=";
        s += (e.stopSourceText[0] != '\0') ? e.stopSourceText : "NONE";
        return s;
      case EventType::STOP:
        s = "EVT:STOP ";
        s += "stop_reason=";
        s += (e.stopReasonText[0] != '\0') ? e.stopReasonText : "NONE";
        s += " stop_source=";
        s += (e.stopSourceText[0] != '\0') ? e.stopSourceText : "NONE";
        s += " code=";
        s += String((uint16_t)e.fault);
        s += " effect=";
        s += safetySignalName(e.safety);
        s += " state=";
        s += topStateName(e.state);
        return s;
      case EventType::SNAPSHOT:
        return "EVT:UNKNOWN";
    }
    return "EVT:UNKNOWN";
  }

