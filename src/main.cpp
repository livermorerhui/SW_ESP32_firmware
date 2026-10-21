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
#include "core/BleLifecycleCoordinator.h"
#include "core/VerboseSerial.h"
#include "core/SystemStateMachine.h"
#include "HubAckBuilder.h"
#include "modules/wave/WaveModule.h"
#include "modules/laser/LaserModule.h"
#include "ota/FirmwareRollbackConfirmation.h"
#include "transport/ble/BleTransport.h"
#include "services/WaveCommandService.h"
#include "services/CapabilityCommandService.h"
#include "services/CalibrationCommandService.h"

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

class HubHandler : public CommandHandler {
public:
  HubHandler(SystemStateMachine* fsm,
             WaveModule* wave,
             LaserModule* laser,
             WaveCommandService* waveCommands,
             CapabilityCommandService* capabilityCommands,
             CalibrationCommandService* calibrationCommands)
    : sm(fsm),
      w(wave),
      l(laser),
      waveCommands_(waveCommands),
      capabilityCommands_(capabilityCommands),
      calibrationCommands_(calibrationCommands) {}

  bool handle(const Command& c, String& outAck) override {
    if (waveCommands_ && waveCommands_->handles(c.type)) {
      return waveCommands_->handle(c, outAck);
    }
    if (capabilityCommands_ && capabilityCommands_->handles(c.type)) {
      return capabilityCommands_->handle(c, outAck);
    }
    if (calibrationCommands_ && calibrationCommands_->handles(c.type)) {
      return calibrationCommands_->handle(c, outAck);
    }
    switch (c.type) {
      case CmdType::WAVE_SET:
      case CmdType::WAVE_START:
      case CmdType::WAVE_STOP:
      case CmdType::AUDIO_CUE:
      case CmdType::LEGACY_FIE:
        return waveCommands_->handle(c, outAck);

      case CmdType::CAP_QUERY:
      case CmdType::DEVICE_IDENTITY_QUERY:
      case CmdType::DEVICE_SECURITY_QUERY:
      case CmdType::DEGRADED_START_SET:
        return capabilityCommands_->handle(c, outAck);

      case CmdType::SCALE_ZERO:
      case CmdType::CAL_ZERO:
      case CmdType::SCALE_CAL:
      case CmdType::CAL_CAPTURE:
      case CmdType::CAL_GET_MODEL:
      case CmdType::CAL_SET_MODEL:
      case CmdType::EMPTY_DISTANCE_GET:
      case CmdType::EMPTY_DISTANCE_SET_CURRENT:
      case CmdType::EMPTY_DISTANCE_SET:
      case CmdType::EMPTY_DISTANCE_CLEAR:
      case CmdType::CAL_CLEAR_WEIGHT_MODEL:
        return calibrationCommands_->handle(c, outAck);

      case CmdType::DEVICE_GET_CONFIG: {
        DeviceConfigSnapshot config{};
        String reason;
        if (!l->getProfileConfig(config, reason)) {
          g_ble.invalidatePendingStreamFrames();
          SW_VERBOSE_SERIAL_PRINTF(
              "[DEVICE_CONFIG] op=GET result=failure reason=%s\n",
              reason.c_str());
          outAck = HubAckBuilder::deviceProfileConfigNack(
              "GET", reason == "UNSUPPORTED_MODEL" ? "UNSUPPORTED_MODEL" : "CONFIG_ERROR");
          return false;
        }
        outAck = HubAckBuilder::deviceProfileConfig(
            "GET", config.platformModel, config.sensorProfile, config.generation);
        SW_VERBOSE_SERIAL_PRINTF(
            "[DEVICE_CONFIG] op=GET result=ok model=%s profile=%s generation=%lu\n",
            platformModelName(config.platformModel),
            sensorProfileName(config.sensorProfile),
            static_cast<unsigned long>(config.generation));
        return true;
      }

      case CmdType::DEVICE_SET_PROFILE_CONFIG: {
        const DeviceProfileConfigCommand& requested = c.deviceProfileConfig;
        SW_VERBOSE_SERIAL_PRINTF(
            "[DEVICE_CONFIG] op=SET requested_model=%s requested_profile=%s\n",
            platformModelName(requested.platformModel),
            sensorProfileName(requested.sensorProfile));
        String reason;
        if (!l->setProfileConfig(
                requested.platformModel, requested.sensorProfile, reason)) {
          g_ble.invalidatePendingStreamFrames();
          outAck = HubAckBuilder::deviceProfileConfigNack("SET", reason.c_str());
          SW_VERBOSE_SERIAL_PRINTF(
              "[DEVICE_CONFIG] op=SET result=failure reason=%s\n",
              reason.c_str());
          return false;
        }
        g_ble.invalidatePendingStreamFrames();
        DeviceConfigSnapshot applied{};
        if (!l->getProfileConfig(applied, reason)) {
          g_ble.invalidatePendingStreamFrames();
          outAck = HubAckBuilder::deviceProfileConfigNack("SET", "VERIFY_FAILED");
          SW_VERBOSE_SERIAL_PRINTF(
              "[DEVICE_CONFIG] op=SET result=verify_failed reason=%s\n",
              reason.c_str());
          return false;
        }
        outAck = HubAckBuilder::deviceProfileConfig(
            "SET", applied.platformModel, applied.sensorProfile, applied.generation);
        g_bleDeviceName = buildBleDeviceName(applied.platformModel);
        g_ble.updateAdvertisingIdentity(
            g_bleDeviceName.c_str(),
            platformModelName(applied.platformModel));
        SW_VERBOSE_SERIAL_PRINTF(
            "[DEVICE_CONFIG] op=SET result=ok model=%s profile=%s generation=%lu\n",
            platformModelName(applied.platformModel),
            sensorProfileName(applied.sensorProfile),
            static_cast<unsigned long>(applied.generation));
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
          g_ble.invalidatePendingStreamFrames();
          outAck = HubAckBuilder::simpleNack(reason);
          return false;
        }

        g_ble.invalidatePendingStreamFrames();

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

    }
    outAck = HubAckBuilder::unsupported();
    return false;
  }

private:
  SystemStateMachine* sm;
  WaveModule* w;
  LaserModule* l;
  WaveCommandService* waveCommands_;
  CapabilityCommandService* capabilityCommands_;
  CalibrationCommandService* calibrationCommands_;
};

static WaveCommandService g_waveCommands(&g_fsm, &g_wave);
static CapabilityCommandService g_capabilityCommands(&g_fsm, &g_laser);
static CalibrationCommandService g_calibrationCommands(&g_laser);
static HubHandler g_handler(
    &g_fsm, &g_wave, &g_laser,
    &g_waveCommands, &g_capabilityCommands, &g_calibrationCommands);
static BleLifecycleCoordinator g_bleLifecycle(
    &g_ble, &g_fsm, &g_wave, &g_laser);

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
  FirmwareDiagnostics::begin();
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
  FirmwareDiagnostics::bootDiagnostic(
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

  g_ble.setDisconnectSink(&g_bleLifecycle);
  g_ble.setPlatformSnapshotOwner(&g_fsm);
  waitForMeasurementStartupResolved();
  g_bleDeviceName = g_laser.configIdentityValid()
      ? buildBleDeviceName(g_laser.platformModel())
      : String("SonicWave_Service");
  g_ble.begin(&g_cmdBus,
      g_bleDeviceName.c_str(),
      g_laser.configIdentityValid()
          ? platformModelName(g_laser.platformModel())
          : "SERVICE");
  g_eventBus.setSink(&g_ble);

  HumanReadableLog::ready();
#if defined(SW_ESP32_ROLLBACK_USB_DIAGNOSTICS)
  FirmwareDiagnostics::record(FirmwareDiagnostics::EventKind::RollbackReady);
#endif
}

void loop() {
  // Keep loop lightweight so FreeRTOS workers (I2S/Laser/BLE) run predictably.
  // Safety stop is enforced by SystemStateMachine + BLE disconnect callback.
  delay(20);
}
