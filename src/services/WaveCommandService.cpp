#include "WaveCommandService.h"

#include "HubAckBuilder.h"
#include "core/HumanReadableLog.h"
#include "core/SystemStateMachine.h"
#include "core/VerboseSerial.h"
#include "modules/wave/WaveModule.h"

WaveCommandService::WaveCommandService(SystemStateMachine* stateMachine,
                                       WaveModule* waveModule)
    : stateMachine_(stateMachine), waveModule_(waveModule) {}

bool WaveCommandService::handles(CmdType type) const {
  switch (type) {
    case CmdType::WAVE_SET:
    case CmdType::WAVE_START:
    case CmdType::WAVE_STOP:
    case CmdType::AUDIO_CUE:
    case CmdType::LEGACY_FIE:
      return true;
    default:
      return false;
  }
}

bool WaveCommandService::handle(const Command& command, String& outAck) const {
  if (!stateMachine_ || !waveModule_) {
    outAck = HubAckBuilder::simpleNack("WAVE_SERVICE_UNAVAILABLE");
    return false;
  }

  switch (command.type) {
    case CmdType::WAVE_SET: {
      HumanReadableLog::command("设置波形参数");
      SW_VERBOSE_SERIAL_PRINTF(
          "[LAYER:COMMAND_DISPATCH] cmd=WAVE:SET freq_hz=%.2f intensity=%d owner=WaveModule::setParams\n",
          command.wave.freqHz,
          command.wave.intensity);
      waveModule_->setParams(command.wave.freqHz, command.wave.intensity);
      WaveModule::DebugState debug{};
      waveModule_->getDebugState(debug);
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
      const PlatformSnapshot snapshot = stateMachine_->snapshot();
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
      if (!stateMachine_->requestStart(reason)) {
        outAck = HubAckBuilder::startRejected(reason);
        return false;
      }
      outAck = HubAckBuilder::ok();
      return true;
    }

    case CmdType::WAVE_STOP:
      HumanReadableLog::command("停止波形输出");
      SW_VERBOSE_SERIAL_PRINTLN("[CMD] WAVE_STOP received");
      SW_VERBOSE_SERIAL_PRINTLN(
          "[LAYER:COMMAND_DISPATCH] cmd=WAVE:STOP owner=SystemStateMachine::requestStop");
      stateMachine_->requestStop();
      outAck = HubAckBuilder::ok();
      return true;

    case CmdType::AUDIO_CUE:
      return handleAudioCue(command, outAck);

    case CmdType::LEGACY_FIE:
      return handleLegacyFie(command, outAck);

    default:
      outAck = HubAckBuilder::unsupported();
      return false;
  }
}

bool WaveCommandService::handleAudioCue(const Command& command,
                                        String& outAck) const {
  if (command.audioCue.id != AudioCueId::CONNECT) {
    outAck = HubAckBuilder::audioCueNack(command.audioCue.id, "LOCAL_ONLY");
    return false;
  }
  const WaveModule::CueRequestResult result = waveModule_->requestCue(
      command.audioCue.id,
      command.transportSessionId,
      command.connectionId,
      command.connectionIdKnown);
  switch (result) {
    case WaveModule::CueRequestResult::ACCEPTED:
      outAck = HubAckBuilder::audioCue(command.audioCue.id, "ACCEPTED");
      return true;
    case WaveModule::CueRequestResult::DUPLICATE:
      outAck = HubAckBuilder::audioCue(command.audioCue.id, "DUPLICATE");
      return true;
    case WaveModule::CueRequestResult::BUSY_WAVE:
      outAck = HubAckBuilder::audioCueNack(command.audioCue.id, "BUSY_WAVE");
      return false;
    case WaveModule::CueRequestResult::BUSY_CUE:
      outAck = HubAckBuilder::audioCueNack(command.audioCue.id, "BUSY_CUE");
      return false;
    case WaveModule::CueRequestResult::INVALID:
    default:
      outAck = HubAckBuilder::audioCueNack(command.audioCue.id, "INVALID");
      return false;
  }
}

bool WaveCommandService::handleLegacyFie(const Command& command,
                                         String& outAck) const {
  if (command.wave.freqHz >= 0) waveModule_->setFreq(command.wave.freqHz);
  if (command.wave.intensity >= 0) waveModule_->setIntensity(command.wave.intensity);

  if (command.wave.hasEnable) {
    if (command.wave.enable) {
      FaultCode reason = FaultCode::NONE;
      if (!stateMachine_->requestStart(reason)) {
        outAck = HubAckBuilder::startRejected(reason);
        return false;
      }
    } else {
      SW_VERBOSE_SERIAL_PRINTLN("[CMD] WAVE_STOP received");
      stateMachine_->requestStop();
    }
  }

  outAck = HubAckBuilder::ok();
  return true;
}
