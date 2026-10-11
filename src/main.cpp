#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_log.h>
#include <string.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/CommandBus.h"
#include "core/FirmwareDeviceIdentityProvider.h"
#include "core/FirmwareSecurityVersionProvider.h"
#include "core/HumanReadableLog.h"
#include "core/VerboseSerial.h"
#include "core/SystemStateMachine.h"
#include "HubAckBuilder.h"
#include "modules/wave/WaveModule.h"
#include "modules/laser/LaserModule.h"
#include "ota/FirmwareRollbackConfirmation.h"
#include "transport/ble/BleTransport.h"

static EventBus g_eventBus;
static CommandBus g_cmdBus;
static SystemStateMachine g_fsm;

static WaveModule g_wave;
static LaserModule g_laser;
static BleTransport g_ble;
static String g_bleDeviceName;
static String buildBleDeviceName(PlatformModel model);

static void configureHumanSerialLog() {
  // 保留警告和错误，隐藏 ESP-IDF 正常初始化时的英文信息。
  esp_log_level_set("BLE_INIT", ESP_LOG_WARN);
  esp_log_level_set("system_api", ESP_LOG_WARN);
  esp_log_level_set("phy_init", ESP_LOG_WARN);
}

static MeasurementHealthState waitForMeasurementStartupResolved() {
  const uint32_t startedAt = millis();
  MeasurementHealthState health = g_laser.measurementHealth();
  while (!g_laser.measurementStartupResolved() &&
      millis() - startedAt < BLE_STARTUP_MEASUREMENT_READY_WAIT_MS) {
    delay(BLE_STARTUP_MEASUREMENT_READY_POLL_MS);
    health = g_laser.measurementHealth();
  }
  SW_VERBOSE_SERIAL_PRINTF(
      "[BLE_STARTUP_GATE] result=%s health=%s wait_ms=%lu timeout_ms=%lu\n",
      g_laser.measurementStartupResolved() ? "resolved" : "timeout",
      measurementHealthStateName(health),
      static_cast<unsigned long>(millis() - startedAt),
      static_cast<unsigned long>(BLE_STARTUP_MEASUREMENT_READY_WAIT_MS));
  return health;
}

static void clearBoardRgbLed() {
  neopixelWrite(BOARD_RGB_LED_PIN, 0, 0, 0);
}

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

class HubHandler : public CommandHandler, public BleDisconnectSink {
public:
  HubHandler(SystemStateMachine* fsm, WaveModule* wave, LaserModule* laser)
    : sm(fsm), w(wave), l(laser) {}

  bool handle(const Command& c, String& outAck) override {
    switch (c.type) {
      case CmdType::CAP_QUERY: {
        const PlatformSnapshot snapshot = sm->snapshot();
        SW_VERBOSE_SERIAL_PRINTF(
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
        outAck = HubAckBuilder::cap(
            FW_VER,
            FW_BUILD_ID,
            FW_BOARD_ID,
            PROTO_VER,
            l->platformModel(),
            l->laserInstalled(),
            true);
        return true;
      }

      case CmdType::DEVICE_IDENTITY_QUERY: {
        String deviceId;
        if (!FirmwareDeviceIdentityProvider::readFromEfuse(deviceId) ||
            !FirmwareDeviceIdentityProvider::isValid(deviceId)) {
          SW_VERBOSE_SERIAL_PRINTLN("[DEVICE_IDENTITY] result=unavailable");
          outAck = "NACK:DEVICE_ID_UNAVAILABLE";
          return false;
        }
        SW_VERBOSE_SERIAL_PRINTF(
            "[DEVICE_IDENTITY] result=ok version=%d id_suffix=%s\n",
            FirmwareDeviceIdentityProvider::kIdentityVersion,
            FirmwareDeviceIdentityProvider::logSuffix(deviceId).c_str());
        outAck = HubAckBuilder::deviceIdentity(
            deviceId,
            FirmwareDeviceIdentityProvider::kIdentityVersion);
        return true;
      }

      case CmdType::DEVICE_SECURITY_QUERY: {
        FirmwareSecurityVersionFacts facts{};
        if (!FirmwareSecurityVersionProvider::read(facts)) {
          SW_VERBOSE_SERIAL_PRINTLN("[DEVICE_SECURITY] result=unavailable");
          outAck = "NACK:DEVICE_SECURITY_UNAVAILABLE";
          return false;
        }
        SW_VERBOSE_SERIAL_PRINTF(
            "[DEVICE_SECURITY] result=ok app_sv=%lu efuse_sv=%lu floor_sv=%lu app_gate=%d\n",
            static_cast<unsigned long>(facts.appSecurityVersion),
            static_cast<unsigned long>(facts.efuseSecurityVersion),
            static_cast<unsigned long>(facts.currentSecurityVersion),
            facts.appAntiRollbackGate ? 1 : 0);
        outAck = HubAckBuilder::deviceSecurity(
            facts.appSecurityVersion,
            facts.efuseSecurityVersion,
            facts.currentSecurityVersion,
            facts.appAntiRollbackGate);
        return true;
      }

      case CmdType::DEVICE_SET_CONFIG: {
        SW_VERBOSE_SERIAL_PRINTF(
            "[LAYER:COMMAND_DISPATCH] cmd=DEVICE_SET_CONFIG platform_model=%s laser_installed=%d owner=LaserModule::setDeviceConfig\n",
            platformModelName(c.deviceConfig.platformModel),
            c.deviceConfig.laserInstalled ? 1 : 0);
        String reason;
        if (!l->setDeviceConfig(
                c.deviceConfig.platformModel,
                c.deviceConfig.laserInstalled,
                reason)) {
          outAck = HubAckBuilder::simpleNack(reason);
          return false;
        }

        outAck = HubAckBuilder::deviceConfig(l->platformModel(), l->laserInstalled());
        g_bleDeviceName = buildBleDeviceName(l->platformModel());
        g_ble.updateAdvertisingIdentity(
            g_bleDeviceName.c_str(),
            platformModelName(l->platformModel()));
        const PlatformSnapshot snapshot = sm->snapshot();
        SW_VERBOSE_SERIAL_PRINTF(
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
        outAck = HubAckBuilder::degradedStart(snapshot);
        return true;
      }

      case CmdType::STREAM_SET: {
        String reason;
        if (!l->setStreamSubscription(
                c.streamSubscription.enabled,
                c.streamSubscription.rateHz,
                reason)) {
          outAck = HubAckBuilder::simpleNack(reason);
          return false;
        }
        outAck = HubAckBuilder::streamSubscription(
            l->streamSubscriptionEnabled(),
            l->streamSubscriptionRateHz());
        return true;
      }

      case CmdType::WAVE_SET: {
        HumanReadableLog::command("设置波形参数");
        SW_VERBOSE_SERIAL_PRINTF(
            "[LAYER:COMMAND_DISPATCH] cmd=WAVE:SET freq_hz=%.2f intensity=%d owner=WaveModule::setParams\n",
            c.wave.freqHz,
            c.wave.intensity);
        w->setParams(c.wave.freqHz, c.wave.intensity);
        WaveModule::DebugState debug{};
        w->getDebugState(debug);
        SW_VERBOSE_SERIAL_PRINTF(
            "[LAYER:COMMAND_DISPATCH] cmd=WAVE:SET applied display_freq_hz=%.2f target_phase_inc=%lu target_intensity=%d run_requested=%d run_state=%d\n",
            debug.displayFreqHz,
            static_cast<unsigned long>(debug.targetPhaseInc),
            debug.targetIntensity,
            debug.runRequested ? 1 : 0,
            debug.runState ? 1 : 0);
        outAck = HubAckBuilder::ok();
        return true;
      }

      case CmdType::WAVE_START: {
        HumanReadableLog::command("开始波形输出");
        const PlatformSnapshot snapshot = sm->snapshot();
        SW_VERBOSE_SERIAL_PRINTF(
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
          outAck = HubAckBuilder::startRejected(reason);
          return false;
        }
        outAck = HubAckBuilder::ok();
        return true;
      }

      case CmdType::WAVE_STOP:
        HumanReadableLog::command("停止波形输出");
        SW_VERBOSE_SERIAL_PRINTLN("[CMD] WAVE_STOP received");
        SW_VERBOSE_SERIAL_PRINTLN("[LAYER:COMMAND_DISPATCH] cmd=WAVE:STOP owner=SystemStateMachine::requestStop");
        sm->requestStop();
        outAck = HubAckBuilder::ok();
        return true;

      case CmdType::AUDIO_CUE: {
        if (c.audioCue.id != AudioCueId::CONNECT) {
          // DISCONNECT is an internal BLE lifecycle cue. Keeping it out of
          // the APP command surface prevents an arbitrary RX write from
          // fabricating a disconnect sound while the link is still alive.
          outAck = HubAckBuilder::audioCueNack(c.audioCue.id, "LOCAL_ONLY");
          return false;
        }
        const WaveModule::CueRequestResult result = w->requestCue(
            c.audioCue.id,
            c.transportSessionId,
            c.connectionId,
            c.connectionIdKnown);
        switch (result) {
          case WaveModule::CueRequestResult::ACCEPTED:
            outAck = HubAckBuilder::audioCue(c.audioCue.id, "ACCEPTED");
            return true;
          case WaveModule::CueRequestResult::DUPLICATE:
            outAck = HubAckBuilder::audioCue(c.audioCue.id, "DUPLICATE");
            return true;
          case WaveModule::CueRequestResult::BUSY_WAVE:
            outAck = HubAckBuilder::audioCueNack(c.audioCue.id, "BUSY_WAVE");
            return false;
          case WaveModule::CueRequestResult::BUSY_CUE:
            outAck = HubAckBuilder::audioCueNack(c.audioCue.id, "BUSY_CUE");
            return false;
          case WaveModule::CueRequestResult::INVALID:
          default:
            outAck = HubAckBuilder::audioCueNack(c.audioCue.id, "INVALID");
            return false;
        }
      }

      case CmdType::SCALE_ZERO:
        l->triggerZero();
        outAck = HubAckBuilder::ok();
        return true;

      case CmdType::CAL_ZERO:
        l->triggerZero();
        outAck = HubAckBuilder::calibrationZero();
        return true;

      case CmdType::SCALE_CAL:
        l->setParams(c.p1, c.p2);
        outAck = HubAckBuilder::ok();
        return true;

      case CmdType::CAL_CAPTURE: {
        CalibrationCapture capture{};
        String reason;
        if (!l->captureCalibrationPoint(c.capture.referenceWeightKg, capture, reason)) {
          outAck = HubAckBuilder::simpleNack(reason);
          return false;
        }
        outAck = HubAckBuilder::calibrationPoint(
            capture.index,
            capture.ts_ms,
            capture.distanceMm,
            capture.referenceWeightKg,
            capture.predictedWeightKg,
            capture.stableFlag,
            capture.validFlag);
        return true;
      }

      case CmdType::CAL_GET_MODEL: {
        CalibrationModel model{};
        const bool ready = l->getCalibrationModel(model);
        outAck = HubAckBuilder::calibrationModel(model, ready);
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
          SW_VERBOSE_SERIAL_PRINTF("[CAL] SET_MODEL result=failure type=%s reason=%s\n",
              HubAckBuilder::calibrationModelTypeName(model.type),
              reason.c_str());
          outAck = HubAckBuilder::calibrationSetModelRejected(model.type, reason);
          return false;
        }

        SW_VERBOSE_SERIAL_PRINTF("[CAL] SET_MODEL result=success type=%s ref=%.4f c0=%.6f c1=%.6f c2=%.6f\n",
            HubAckBuilder::calibrationModelTypeName(model.type),
            model.referenceDistance,
            model.coefficients[0],
            model.coefficients[1],
            model.coefficients[2]);
        outAck = HubAckBuilder::calibrationSetModel(model, true);
        return true;
      }

      case CmdType::EMPTY_DISTANCE_GET: {
        EmptyDistanceRecord record{};
        const bool supported = l->laserInstalled() &&
            (strcmp(l->sensorBackendName(), "LASER_485") == 0 ||
             strcmp(l->sensorBackendName(), "CYD3S1612_IIC") == 0);
        const bool ready = supported && l->getEmptyDistance(record);
        outAck = HubAckBuilder::emptyDistance(
            "GET",
            supported,
            ready ? "SET" : (supported ? "UNSET" : "UNSUPPORTED"),
            ready ? record.distanceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR : NAN,
            ready ? record.toleranceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR : NAN,
            l->sensorBackendName());
        return true;
      }

      case CmdType::EMPTY_DISTANCE_SET: {
        String reason;
        if (!l->setEmptyDistance(c.emptyDistance.distanceMm, reason)) {
          const bool supported = l->laserInstalled() &&
              (strcmp(l->sensorBackendName(), "LASER_485") == 0 ||
               strcmp(l->sensorBackendName(), "CYD3S1612_IIC") == 0);
          outAck = HubAckBuilder::emptyDistanceRejected("SET", reason, supported);
          return false;
        }
        EmptyDistanceRecord record{};
        l->getEmptyDistance(record);
        outAck = HubAckBuilder::emptyDistance(
            "SET",
            true,
            "SET",
            record.distanceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
            record.toleranceRuntime * LASER_DISTANCE_RUNTIME_DIVISOR,
            l->sensorBackendName());
        return true;
      }

      case CmdType::EMPTY_DISTANCE_CLEAR: {
        String reason;
        if (!l->clearEmptyDistance(reason)) {
          const bool supported = l->laserInstalled() &&
              (strcmp(l->sensorBackendName(), "LASER_485") == 0 ||
               strcmp(l->sensorBackendName(), "CYD3S1612_IIC") == 0);
          outAck = HubAckBuilder::emptyDistanceRejected("CLEAR", reason, supported);
          return false;
        }
        outAck = HubAckBuilder::emptyDistance(
            "CLEAR", true, "UNSET", NAN, NAN, l->sensorBackendName());
        return true;
      }

      case CmdType::CAL_CLEAR_WEIGHT_MODEL: {
        String reason;
        if (!l->clearWeightModel(reason)) {
          outAck = HubAckBuilder::weightModelClearRejected(reason, reason != "UNSUPPORTED");
          return false;
        }
        outAck = HubAckBuilder::weightModelClear(false);
        return true;
      }

      case CmdType::FALL_STOP_SET:
        sm->setFallStopEnabled(c.fallStop.enabled);
        outAck = HubAckBuilder::fallStop(c.fallStop.enabled, sm->fallStopModeName());
        return true;

      case CmdType::LEAVE_PROTECTION_SET:
        sm->setLeaveStopEnabled(c.leaveProtection.enabled);
        outAck = HubAckBuilder::leaveProtection(sm->leaveStopEnabled(), sm->leaveStopModeName());
        return true;

      case CmdType::MOTION_SAMPLING_MODE_SET:
        sm->setMotionSamplingMode(c.motionSamplingMode.enabled);
        outAck = HubAckBuilder::motionSampling(
            c.motionSamplingMode.enabled,
            !sm->fallStopEnabled());
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
              outAck = HubAckBuilder::startRejected(reason);
              return false;
            }
          } else {
          SW_VERBOSE_SERIAL_PRINTLN("[CMD] WAVE_STOP received");
            sm->requestStop();
          }
        }

        outAck = HubAckBuilder::ok();
        return true;
      }
    }
    outAck = HubAckBuilder::unsupported();
    return false;
  }

  void onBleDisconnect(uint32_t sessionId, uint16_t connId, bool connIdKnown) override {
    if (l) l->resetStreamSubscription("ble_disconnect");
    if (sm) sm->onBleDisconnected();
    if (w) {
      const WaveModule::CueRequestResult result = w->requestCue(
          AudioCueId::DISCONNECT,
          sessionId,
          connId,
          connIdKnown);
      SW_VERBOSE_SERIAL_PRINTF(
          "[CUE] source=ble_disconnect id=DISCONNECT result=%d session_id=%lu conn_id=%u\n",
          static_cast<int>(result),
          static_cast<unsigned long>(sessionId),
          static_cast<unsigned>(connId));
    }
  }

  void onBleConnected() override {
    if (l) l->resetStreamSubscription("ble_connected");
    const uint32_t sessionId = g_ble.currentSessionId();
    if (w) {
      w->resetCuesForNewSession(sessionId);
      if (g_ble.isConnected()) {
        const WaveModule::CueRequestResult result = w->requestCue(
            AudioCueId::CONNECT,
            sessionId,
            0,
            false);
        SW_VERBOSE_SERIAL_PRINTF(
            "[CUE] source=ble_connected id=CONNECT result=%d session_id=%lu\n",
            static_cast<int>(result),
            static_cast<unsigned long>(sessionId));
      }
    }
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
  clearBoardRgbLed();
  Serial.begin(115200);
  configureHumanSerialLog();
  delay(1500);
  clearBoardRgbLed();
  SW_VERBOSE_SERIAL_PRINTLN("\n=== SonicWave Hub FW (Integrated) ===");
  const esp_reset_reason_t resetReason = esp_reset_reason();
  SW_VERBOSE_SERIAL_PRINTF(
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
  HumanReadableLog::boot(FW_VER, PROTO_VER, resetReasonName(resetReason));
#if defined(SW_ESP32_ROLLBACK_USB_DIAGNOSTICS)
  ESP_LOGI(
      "BOOT_DIAG",
      "[BOOT_DIAG] fw=%s proto=%d reset_reason=%s(%d) heap_free=%u heap_min_free=%u psram_free=%u board=sonicwave_esp32s3_n16r8 psram_enabled=%d",
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
#endif

  // Rollback confirmation must precede sensor readiness; transient external
  // sensor state must not decide whether a new app is bootable.
  // Init order: state machine -> rollback confirmation -> modules -> startup measurement gate -> BLE transport.
  // EventBus is connected to BLE only after BLE queues are initialized; startup samples are recovered by SNAPSHOT.
  g_fsm.begin(&g_eventBus, &g_wave);
  FirmwareRollbackConfirmation::confirmIfPending();

  g_wave.begin(&g_eventBus);
  g_laser.begin(&g_eventBus, &g_fsm, &g_wave);
  g_laser.startTask();
  g_cmdBus.setHandler(&g_handler);

  g_ble.setDisconnectSink(&g_handler);
  g_ble.setPlatformSnapshotOwner(&g_fsm);
  waitForMeasurementStartupResolved();
  g_bleDeviceName = buildBleDeviceName(g_laser.platformModel());
  g_ble.begin(&g_cmdBus,
      g_bleDeviceName.c_str(),
      platformModelName(g_laser.platformModel()));
  g_eventBus.setSink(&g_ble);

  HumanReadableLog::ready();
#if defined(SW_ESP32_ROLLBACK_USB_DIAGNOSTICS)
  ESP_LOGI("BOOT_DIAG", "Ready");
#endif
}

void loop() {
  // Keep loop lightweight so FreeRTOS workers (I2S/Laser/BLE) run predictably.
  // Safety stop is enforced by SystemStateMachine + BLE disconnect callback.
  delay(20);
}
