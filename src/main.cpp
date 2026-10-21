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
#include "services/DeviceConfigCommandService.h"
#include "services/MeasurementStreamCommandService.h"
#include "services/ProtectionCommandService.h"
#include "services/LegacyCommandAdapter.h"
#include "transport/ble/BleDeviceConfigEffects.h"

static EventBus g_eventBus;
static CommandBus g_cmdBus;
static SystemStateMachine g_fsm;

static WaveModule g_wave;
static LaserModule g_laser;
static BleTransport g_ble;
static BleDeviceConfigEffects g_bleConfigEffects(&g_ble);

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

static WaveCommandService g_waveCommands(&g_fsm, &g_wave);
static CapabilityCommandService g_capabilityCommands(&g_fsm, &g_laser);
static CalibrationCommandService g_calibrationCommands(&g_laser);
static DeviceConfigCommandService g_deviceConfigCommands(
    &g_laser,
    &g_bleConfigEffects,
    &g_bleConfigEffects,
    &g_fsm);
static MeasurementStreamCommandService g_streamCommands(&g_laser);
static ProtectionCommandService g_protectionCommands(&g_fsm);
static LegacyCommandAdapter g_handler(
    &g_waveCommands, &g_capabilityCommands, &g_calibrationCommands,
    &g_deviceConfigCommands, &g_streamCommands, &g_protectionCommands);
static BleLifecycleCoordinator g_bleLifecycle(
    &g_ble, &g_fsm, &g_wave, &g_laser);

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
  g_bleConfigEffects.setInitialDeviceName(
      g_laser.configIdentityValid()
          ? BleDeviceConfigEffects::deviceNameForModel(g_laser.platformModel())
          : String("SonicWave_Service"));
  g_ble.begin(&g_cmdBus,
      g_bleConfigEffects.currentDeviceName().c_str(),
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
