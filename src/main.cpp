#include <Arduino.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/CommandBus.h"
#include "core/SystemStateMachine.h"
#include "modules/wave/WaveModule.h"
#include "modules/laser/LaserModule.h"
#include "transport/ble/BleTransport.h"

static EventBus g_eventBus;
static CommandBus g_cmdBus;
static SystemStateMachine g_fsm;

static WaveModule g_wave;
static LaserModule g_laser;
static BleTransport g_ble;
static String g_bleDeviceName;
static String buildBleDeviceName(PlatformModel model);

static const char* calibrationModelTypeName(uint8_t type) {
  switch (type) {
    case static_cast<uint8_t>(CalibrationModelType::LINEAR):
      return "LINEAR";
    case static_cast<uint8_t>(CalibrationModelType::QUADRATIC):
      return "QUADRATIC";
  }
  return "UNKNOWN";
}

class HubHandler : public CommandHandler, public BleDisconnectSink {
public:
  HubHandler(SystemStateMachine* fsm, WaveModule* wave, LaserModule* laser)
    : sm(fsm), w(wave), l(laser) {}

  bool handle(const Command& c, String& outAck) override {
    switch (c.type) {
      case CmdType::CAP_QUERY: {
        const PlatformSnapshot snapshot = sm->snapshot();
        Serial.printf(
            "[LAYER:CONFIG_TRUTH] source=CAP_QUERY platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d degraded_start_available=%d degraded_start_enabled=%d runtime_ready=%d start_ready=%d baseline_ready=%d top_state=%s\n",
            platformModelName(snapshot.platformModel),
            snapshot.laserInstalled ? 1 : 0,
            snapshot.laserAvailable ? 1 : 0,
            snapshot.protectionDegraded ? 1 : 0,
            snapshot.degradedStartAvailable ? 1 : 0,
            snapshot.degradedStartEnabled ? 1 : 0,
            snapshot.runtimeReady ? 1 : 0,
            snapshot.startReady ? 1 : 0,
            snapshot.baselineReady ? 1 : 0,
            topStateName(snapshot.topState));
        outAck = String("ACK:CAP fw=") + FW_VER +
            " proto=" + String(PROTO_VER) +
            " platform_model=" + String(platformModelName(l->platformModel())) +
            " laser_installed=" + String(l->laserInstalled() ? 1 : 0) +
            " fall_stop_enabled=" + String(sm->fallStopEnabled() ? 1 : 0) +
            " fall_stop_mode=" + String(sm->fallStopModeName());
        return true;
      }

      case CmdType::DEVICE_SET_CONFIG: {
        Serial.printf(
            "[LAYER:COMMAND_DISPATCH] cmd=DEVICE_SET_CONFIG platform_model=%s laser_installed=%d owner=LaserModule::setDeviceConfig\n",
            platformModelName(c.deviceConfig.platformModel),
            c.deviceConfig.laserInstalled ? 1 : 0);
        String reason;
        if (!l->setDeviceConfig(
                c.deviceConfig.platformModel,
                c.deviceConfig.laserInstalled,
                reason)) {
          outAck = String("NACK:") + reason;
          return false;
        }

        outAck = String("ACK:DEVICE_CONFIG platform_model=") +
            platformModelName(l->platformModel()) +
            " laser_installed=" + String(l->laserInstalled() ? 1 : 0);
        g_bleDeviceName = buildBleDeviceName(l->platformModel());
        g_ble.updateAdvertisingIdentity(
            g_bleDeviceName.c_str(),
            platformModelName(l->platformModel()));
        const PlatformSnapshot snapshot = sm->snapshot();
        Serial.printf(
            "[LAYER:CONFIG_TRUTH] source=DEVICE_SET_CONFIG platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d degraded_start_available=%d degraded_start_enabled=%d runtime_ready=%d start_ready=%d baseline_ready=%d top_state=%s\n",
            platformModelName(snapshot.platformModel),
            snapshot.laserInstalled ? 1 : 0,
            snapshot.laserAvailable ? 1 : 0,
            snapshot.protectionDegraded ? 1 : 0,
            snapshot.degradedStartAvailable ? 1 : 0,
            snapshot.degradedStartEnabled ? 1 : 0,
            snapshot.runtimeReady ? 1 : 0,
            snapshot.startReady ? 1 : 0,
            snapshot.baselineReady ? 1 : 0,
            topStateName(snapshot.topState));
        return true;
      }

      case CmdType::DEGRADED_START_SET: {
        sm->setDegradedStartAuthorized(c.degradedStart.enabled);
        const PlatformSnapshot snapshot = sm->snapshot();
        outAck = String("ACK:DEGRADED_START enabled=") +
            String(snapshot.degradedStartEnabled ? 1 : 0) +
            " available=" + String(snapshot.degradedStartAvailable ? 1 : 0);
        return true;
      }

      case CmdType::WAVE_SET: {
        Serial.printf(
            "[LAYER:COMMAND_DISPATCH] cmd=WAVE:SET freq_hz=%.2f intensity=%d owner=WaveModule::setParams\n",
            c.wave.freqHz,
            c.wave.intensity);
        w->setParams(c.wave.freqHz, c.wave.intensity);
        WaveModule::DebugState debug{};
        w->getDebugState(debug);
        Serial.printf(
            "[LAYER:COMMAND_DISPATCH] cmd=WAVE:SET applied display_freq_hz=%.2f target_phase_inc=%lu target_intensity=%d run_requested=%d run_state=%d\n",
            debug.displayFreqHz,
            static_cast<unsigned long>(debug.targetPhaseInc),
            debug.targetIntensity,
            debug.runRequested ? 1 : 0,
            debug.runState ? 1 : 0);
        outAck = "ACK:OK";
        return true;
      }

      case CmdType::WAVE_START: {
        const PlatformSnapshot snapshot = sm->snapshot();
        Serial.printf(
            "[LAYER:COMMAND_DISPATCH] cmd=WAVE:START owner=SystemStateMachine::requestStart top_state=%s runtime_ready=%d start_ready=%d baseline_ready=%d platform_model=%s laser_installed=%d laser_available=%d protection_degraded=%d degraded_start_available=%d degraded_start_enabled=%d\n",
            topStateName(snapshot.topState),
            snapshot.runtimeReady ? 1 : 0,
            snapshot.startReady ? 1 : 0,
            snapshot.baselineReady ? 1 : 0,
            platformModelName(snapshot.platformModel),
            snapshot.laserInstalled ? 1 : 0,
            snapshot.laserAvailable ? 1 : 0,
            snapshot.protectionDegraded ? 1 : 0,
            snapshot.degradedStartAvailable ? 1 : 0,
            snapshot.degradedStartEnabled ? 1 : 0);
        FaultCode reason = FaultCode::NONE;
        if (!sm->requestStart(reason)) {
          outAck = (reason == FaultCode::FAULT_LOCKED) ? "NACK:FAULT_LOCKED" : "NACK:NOT_ARMED";
          return false;
        }
        outAck = "ACK:OK";
        return true;
      }

      case CmdType::WAVE_STOP:
        Serial.println("[CMD] WAVE_STOP received");
        Serial.println("[LAYER:COMMAND_DISPATCH] cmd=WAVE:STOP owner=SystemStateMachine::requestStop");
        sm->requestStop();
        outAck = "ACK:OK";
        return true;

      case CmdType::SCALE_ZERO:
        l->triggerZero();
        outAck = "ACK:OK";
        return true;

      case CmdType::SCALE_CAL:
        l->setParams(c.p1, c.p2);
        outAck = "ACK:OK";
        return true;

      case CmdType::CAL_CAPTURE: {
        CalibrationCapture capture{};
        String reason;
        if (!l->captureCalibrationPoint(c.capture.referenceWeightKg, capture, reason)) {
          outAck = String("NACK:") + reason;
          return false;
        }
        outAck = String("ACK:CAL_POINT idx=") + String(capture.index) +
            " ts=" + String(capture.ts_ms) +
            " d_mm=" + String(capture.distanceMm, 2) +
            " ref_kg=" + String(capture.referenceWeightKg, 2) +
            " pred_kg=" + String(capture.predictedWeightKg, 2) +
            " stable=" + String(capture.stableFlag ? 1 : 0) +
            " valid=" + String(capture.validFlag ? 1 : 0);
        return true;
      }

      case CmdType::CAL_GET_MODEL: {
        CalibrationModel model{};
        l->getCalibrationModel(model);
        outAck = String("ACK:CAL_MODEL type=") +
            calibrationModelTypeName(static_cast<uint8_t>(model.type)) +
            " ref=" + String(model.referenceDistance, 4) +
            " c0=" + String(model.coefficients[0], 6) +
            " c1=" + String(model.coefficients[1], 6) +
            " c2=" + String(model.coefficients[2], 6);
        return true;
      }

      case CmdType::CAL_SET_MODEL: {
        CalibrationModel model{};
        model.type = static_cast<CalibrationModelType>(c.model.type);
        model.referenceDistance = c.model.referenceDistance;
        model.coefficients[0] = c.model.coefficients[0];
        model.coefficients[1] = c.model.coefficients[1];
        model.coefficients[2] = c.model.coefficients[2];

        String reason;
        if (!l->setCalibrationModel(model, reason)) {
          Serial.printf("[CAL] SET_MODEL result=failure type=%s reason=%s\n",
              calibrationModelTypeName(static_cast<uint8_t>(model.type)),
              reason.c_str());
          outAck = String("NACK:CAL_SET_MODEL type=") +
              calibrationModelTypeName(static_cast<uint8_t>(model.type)) +
              " reason=" + reason;
          return false;
        }

        Serial.printf("[CAL] SET_MODEL result=success type=%s ref=%.4f c0=%.6f c1=%.6f c2=%.6f\n",
            calibrationModelTypeName(static_cast<uint8_t>(model.type)),
            model.referenceDistance,
            model.coefficients[0],
            model.coefficients[1],
            model.coefficients[2]);
        outAck = String("ACK:CAL_SET_MODEL type=") +
            calibrationModelTypeName(static_cast<uint8_t>(model.type)) +
            " ref=" + String(model.referenceDistance, 4) +
            " c0=" + String(model.coefficients[0], 6) +
            " c1=" + String(model.coefficients[1], 6) +
            " c2=" + String(model.coefficients[2], 6);
        return true;
      }

      case CmdType::FALL_STOP_SET:
        sm->setFallStopEnabled(c.fallStop.enabled);
        outAck = String("ACK:FALL_STOP enabled=") +
            String(c.fallStop.enabled ? 1 : 0) +
            " mode=" + String(sm->fallStopModeName());
        return true;

      case CmdType::MOTION_SAMPLING_MODE_SET:
        sm->setMotionSamplingMode(c.motionSamplingMode.enabled);
        outAck = String("ACK:MOTION_SAMPLING enabled=") +
            String(c.motionSamplingMode.enabled ? 1 : 0) +
            " fall_action_suppressed=" + String(sm->fallStopEnabled() ? 0 : 1);
        return true;

      case CmdType::LEGACY_FIE: {
        // 兼容旧命令：只改触碰到的字段
        // freqHz = -1 表示没提供
        if (c.wave.freqHz >= 0) w->setFreq(c.wave.freqHz);
        if (c.wave.intensity >= 0) w->setIntensity(c.wave.intensity);

        // E 字段触碰时复用状态机联锁，避免旧协议绕过安全逻辑直接启停
        if (c.wave.hasEnable) {
          if (c.wave.enable) {
            FaultCode reason = FaultCode::NONE;
            if (!sm->requestStart(reason)) {
              outAck = (reason == FaultCode::FAULT_LOCKED) ? "NACK:FAULT_LOCKED" : "NACK:NOT_ARMED";
              return false;
            }
          } else {
            Serial.println("[CMD] WAVE_STOP received");
            sm->requestStop();
          }
        }

        outAck = "ACK:OK";
        return true;
      }
    }
    outAck = "NACK:UNSUPPORTED";
    return false;
  }

  void onBleDisconnect() override {
    if (sm) sm->onBleDisconnected();
  }

  void onBleConnected() override {
    if (sm) sm->onBleConnected();
  }

private:
  SystemStateMachine* sm;
  WaveModule* w;
  LaserModule* l;
};

static HubHandler g_handler(&g_fsm, &g_wave, &g_laser);

static String buildBleDeviceName(PlatformModel model) {
  switch (model) {
    case PlatformModel::BASE:
      return "SonicWave_Base";
    case PlatformModel::PLUS:
      return "SonicWave_Plus";
    case PlatformModel::PRO:
      return "SonicWave_Pro";
    case PlatformModel::ULTRA:
      return "SonicWave_Ultra";
  }
  return BLE_DEVICE_NAME;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== SonicWave Hub FW (Integrated) ===");

  // Init order: bus -> state machine -> modules -> BLE transport.
  g_eventBus.setSink(&g_ble);
  g_fsm.begin(&g_eventBus, &g_wave);

  g_wave.begin(&g_eventBus);
  g_laser.begin(&g_eventBus, &g_fsm, &g_wave);
  g_laser.startTask();
  g_cmdBus.setHandler(&g_handler);

  g_ble.setDisconnectSink(&g_handler);
  g_bleDeviceName = buildBleDeviceName(g_laser.platformModel());
  g_ble.begin(&g_cmdBus,
      g_bleDeviceName.c_str(),
      platformModelName(g_laser.platformModel()));

  Serial.println("Ready ✅");
  Serial.println("Try: CAP?");
  Serial.println("Try: WAVE:SET f=40,i=80");
  Serial.println("Try: WAVE:START");
  Serial.println("Try: WAVE:STOP");
}

void loop() {
  // Keep loop lightweight so FreeRTOS workers (I2S/Laser/BLE) run predictably.
  // Safety stop is enforced by SystemStateMachine + BLE disconnect callback.
  delay(20);
}
