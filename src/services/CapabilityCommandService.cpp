#include "CapabilityCommandService.h"

#include "HubAckBuilder.h"
#include "core/FirmwareDeviceIdentityProvider.h"
#include "core/FirmwareSecurityVersionProvider.h"
#include "core/SystemStateMachine.h"
#include "core/VerboseSerial.h"
#include "modules/laser/LaserModule.h"

CapabilityCommandService::CapabilityCommandService(
    SystemStateMachine* stateMachine,
    LaserModule* laserModule)
    : stateMachine_(stateMachine), laserModule_(laserModule) {}

bool CapabilityCommandService::handles(CmdType type) const {
  switch (type) {
    case CmdType::CAP_QUERY:
    case CmdType::DEVICE_IDENTITY_QUERY:
    case CmdType::DEVICE_SECURITY_QUERY:
    case CmdType::DEGRADED_START_SET:
      return true;
    default:
      return false;
  }
}

bool CapabilityCommandService::handle(const Command& command,
                                      String& outAck) const {
  if (!stateMachine_ || !laserModule_) {
    outAck = HubAckBuilder::simpleNack("CAPABILITY_SERVICE_UNAVAILABLE");
    return false;
  }

  switch (command.type) {
    case CmdType::CAP_QUERY: {
      if (!laserModule_->configIdentityValid()) {
        SW_VERBOSE_SERIAL_PRINTLN(
            "[DEVICE_CONFIG] op=CAP result=reject reason=CONFIG_ERROR");
        outAck = "NACK:CONFIG_ERROR";
        return false;
      }
      const PlatformSnapshot snapshot = stateMachine_->snapshot();
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
          laserModule_->platformModel(),
          laserModule_->laserInstalled(),
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

    case CmdType::DEGRADED_START_SET:
      // Legacy clients may still send this command. Readiness is now an ESP32
      // local decision, so acknowledge the current projection without letting
      // an APP command mutate it.
      outAck = HubAckBuilder::degradedStart(stateMachine_->snapshot());
      return true;

    default:
      outAck = HubAckBuilder::unsupported();
      return false;
  }
}
