#pragma once

#include "transport/ble/BleTransport.h"

class LaserModule;
class SystemStateMachine;
class WaveModule;

class BleLifecycleCoordinator final : public BleDisconnectSink {
public:
  BleLifecycleCoordinator(BleTransport* transport,
                          SystemStateMachine* stateMachine,
                          WaveModule* waveModule,
                          LaserModule* laserModule);

  void onBleConnected() override;
  void onBleDisconnect(uint32_t sessionId,
                       uint16_t connId,
                       bool connIdKnown) override;

private:
  BleTransport* transport_;
  SystemStateMachine* stateMachine_;
  WaveModule* waveModule_;
  LaserModule* laserModule_;
};
