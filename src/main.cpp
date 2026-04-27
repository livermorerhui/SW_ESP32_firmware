#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
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

static const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

static void appendKeyValue(String& out, const char* key, const char* value) {
  out += key;
  out += value ? value : "";
}

static void appendKeyIntValue(String& out, const char* key, int value) {
  out += key;
  out += value;
}

static void appendKeyUnsignedLongValue(String& out, const char* key, unsigned long value) {
  out += key;
  out += value;
}

static void appendKeyFloatValue(String& out, const char* key, float value, unsigned char decimals) {
  out += key;
  out += String(value, static_cast<unsigned int>(decimals));
}

static void buildSimpleNack(String& out, const String& reason) {
  out = "NACK:";
  out += reason;
}

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
        outAck = "";
        outAck.reserve(96);
        appendKeyValue(outAck, "ACK:CAP fw=", FW_VER);
        appendKeyIntValue(outAck, " proto=", PROTO_VER);
        appendKeyValue(outAck, " platform_model=", platformModelName(l->platformModel()));
        appendKeyIntValue(outAck, " laser_installed=", l->laserInstalled() ? 1 : 0);
        ProtocolCodec::logTruthPayloadBudgetWarningIfNeeded(
            "bootstrap_truth",
            outAck.length() + 1,
            ProtocolCodec::kCapTruthPayloadBudgetBytes,
            outAck);
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
          buildSimpleNack(outAck, reason);
          return false;
        }

        outAck = "";
        outAck.reserve(80);
        appendKeyValue(outAck, "ACK:DEVICE_CONFIG platform_model=", platformModelName(l->platformModel()));
        appendKeyIntValue(outAck, " laser_installed=", l->laserInstalled() ? 1 : 0);
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
        outAck = "";
        outAck.reserve(56);
        appendKeyIntValue(outAck, "ACK:DEGRADED_START enabled=", snapshot.degradedStartEnabled ? 1 : 0);
        appendKeyIntValue(outAck, " available=", snapshot.degradedStartAvailable ? 1 : 0);
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
          buildSimpleNack(outAck, reason);
          return false;
        }
        outAck = "";
        outAck.reserve(128);
        appendKeyUnsignedLongValue(outAck, "ACK:CAL_POINT idx=", static_cast<unsigned long>(capture.index));
        appendKeyUnsignedLongValue(outAck, " ts=", static_cast<unsigned long>(capture.ts_ms));
        appendKeyFloatValue(outAck, " d_mm=", capture.distanceMm, 2);
        appendKeyFloatValue(outAck, " ref_kg=", capture.referenceWeightKg, 2);
        appendKeyFloatValue(outAck, " pred_kg=", capture.predictedWeightKg, 2);
        appendKeyIntValue(outAck, " stable=", capture.stableFlag ? 1 : 0);
        appendKeyIntValue(outAck, " valid=", capture.validFlag ? 1 : 0);
        return true;
      }

      case CmdType::CAL_GET_MODEL: {
        CalibrationModel model{};
        l->getCalibrationModel(model);
        outAck = "";
        outAck.reserve(104);
        appendKeyValue(outAck, "ACK:CAL_MODEL type=", calibrationModelTypeName(static_cast<uint8_t>(model.type)));
        appendKeyFloatValue(outAck, " ref=", model.referenceDistance, 4);
        appendKeyFloatValue(outAck, " c0=", model.coefficients[0], 6);
        appendKeyFloatValue(outAck, " c1=", model.coefficients[1], 6);
        appendKeyFloatValue(outAck, " c2=", model.coefficients[2], 6);
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
          outAck = "";
          outAck.reserve(96);
          appendKeyValue(outAck, "NACK:CAL_SET_MODEL type=", calibrationModelTypeName(static_cast<uint8_t>(model.type)));
          appendKeyValue(outAck, " reason=", reason.c_str());
          return false;
        }

        Serial.printf("[CAL] SET_MODEL result=success type=%s ref=%.4f c0=%.6f c1=%.6f c2=%.6f\n",
            calibrationModelTypeName(static_cast<uint8_t>(model.type)),
            model.referenceDistance,
            model.coefficients[0],
            model.coefficients[1],
            model.coefficients[2]);
        outAck = "";
        outAck.reserve(112);
        appendKeyValue(outAck, "ACK:CAL_SET_MODEL type=", calibrationModelTypeName(static_cast<uint8_t>(model.type)));
        appendKeyFloatValue(outAck, " ref=", model.referenceDistance, 4);
        appendKeyFloatValue(outAck, " c0=", model.coefficients[0], 6);
        appendKeyFloatValue(outAck, " c1=", model.coefficients[1], 6);
        appendKeyFloatValue(outAck, " c2=", model.coefficients[2], 6);
        return true;
      }

      case CmdType::FALL_STOP_SET:
        sm->setFallStopEnabled(c.fallStop.enabled);
        outAck = "";
        outAck.reserve(56);
        appendKeyIntValue(outAck, "ACK:FALL_STOP enabled=", c.fallStop.enabled ? 1 : 0);
        appendKeyValue(outAck, " mode=", sm->fallStopModeName());
        return true;

      case CmdType::MOTION_SAMPLING_MODE_SET:
        sm->setMotionSamplingMode(c.motionSamplingMode.enabled);
        outAck = "";
        outAck.reserve(72);
        appendKeyIntValue(outAck, "ACK:MOTION_SAMPLING enabled=", c.motionSamplingMode.enabled ? 1 : 0);
        appendKeyIntValue(outAck, " fall_action_suppressed=", sm->fallStopEnabled() ? 0 : 1);
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
  const esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf(
      "[BOOT_DIAG] fw=%s proto=%d reset_reason=%s(%d) heap_free=%u heap_min_free=%u psram_free=%u board=sonicwave_esp32s3_n16r8 psram_enabled=%d\n",
      FW_VER,
      PROTO_VER,
      resetReasonName(resetReason),
      static_cast<int>(resetReason),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(esp_get_minimum_free_heap_size()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
#ifdef BOARD_HAS_PSRAM
      1
#else
      0
#endif
  );

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
