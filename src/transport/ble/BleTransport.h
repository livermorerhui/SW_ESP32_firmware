#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/CommandBus.h"
#include "core/ProtocolCodec.h"

class BleTransport : public EventSink {
public:
  void begin(CommandBus* cb);
  bool isConnected() const { return deviceConnected; }

  // EventSink
  void onEvent(const Event& e) override;

private:
  void sendLine(const String& s);
  void startAdvertisingSafe();

  friend class MyServerCallbacks;
  friend class MyRxCallbacks;

private:
  CommandBus* bus = nullptr;
  BLEServer* pServer = nullptr;
  BLECharacteristic* pTx = nullptr;

  volatile bool deviceConnected = false;
};
