#include "BleLifecycleCoordinator.h"

#include "HumanReadableLog.h"
#include "SystemStateMachine.h"
#include "modules/laser/LaserModule.h"
#include "modules/wave/WaveModule.h"

BleLifecycleCoordinator::BleLifecycleCoordinator(
    BleTransport* transport,
    SystemStateMachine* stateMachine,
    WaveModule* waveModule,
    LaserModule* laserModule)
    : transport_(transport),
      stateMachine_(stateMachine),
      waveModule_(waveModule),
      laserModule_(laserModule) {}

void BleLifecycleCoordinator::onBleDisconnect(uint32_t sessionId,
                                              uint16_t connId,
                                              bool connIdKnown) {
  if (laserModule_) laserModule_->resetStreamSubscription("ble_disconnect");
  if (stateMachine_) stateMachine_->onBleDisconnected();
  if (!waveModule_) return;

  const WaveModule::CueRequestResult result = waveModule_->requestCue(
      AudioCueId::DISCONNECT, sessionId, connId, connIdKnown);
  SW_VERBOSE_SERIAL_PRINTF(
      "[CUE] source=ble_disconnect id=DISCONNECT result=%d session_id=%lu conn_id=%u\n",
      static_cast<int>(result),
      static_cast<unsigned long>(sessionId),
      static_cast<unsigned>(connId));
}

void BleLifecycleCoordinator::onBleConnected() {
  if (laserModule_) laserModule_->resetStreamSubscription("ble_connected");
  if (!transport_ || !waveModule_) return;

  const uint32_t sessionId = transport_->currentSessionId();
  waveModule_->resetCuesForNewSession(sessionId);
  if (transport_->isConnected()) {
    const WaveModule::CueRequestResult result = waveModule_->requestCue(
        AudioCueId::CONNECT, sessionId, 0, false);
    SW_VERBOSE_SERIAL_PRINTF(
        "[CUE] source=ble_connected id=CONNECT result=%d session_id=%lu\n",
        static_cast<int>(result),
        static_cast<unsigned long>(sessionId));
  }
  if (stateMachine_) stateMachine_->onBleConnected();
}
