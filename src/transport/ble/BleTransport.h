#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/CommandBus.h"
#include "core/ProtocolCodec.h"

class BleDisconnectSink {
public:
  virtual void onBleConnected() {}
  virtual void onBleDisconnect() = 0;
  virtual ~BleDisconnectSink() = default;
};

class BleTransport : public EventSink {
public:
  void begin(CommandBus* cb);
  bool isConnected() const { return deviceConnected; }
  void setDisconnectSink(BleDisconnectSink* s) { disconnectSink = s; }

  // EventSink
  void onEvent(const Event& e) override;

private:
  struct ControlMsg {
    enum class Type : uint8_t { RX_COMMAND, BLE_CONNECTED, BLE_DISCONNECTED };
    Type type = Type::RX_COMMAND;
    char line[128]{};
  };

  struct TxMsg {
    enum class Type : uint8_t { LINE, STREAM_FLUSH };
    Type type = Type::LINE;
    char line[512]{};
  };

  static void controlTaskThunk(void* arg);
  static void txTaskThunk(void* arg);

  void controlTaskLoop();
  void txTaskLoop();
  void sendLineNow(const char* s);
  void startAdvertisingSafe();
  bool enqueueCommand(const std::string& raw);
  bool enqueueConnectEvent();
  bool enqueueDisconnectEvent();
  bool enqueueTxLine(const String& s);
  bool enqueueTxLineRaw(const char* s);
  bool tryHandleDirectQuery(const String& s);

  friend class MyServerCallbacks;
  friend class MyRxCallbacks;

private:
  CommandBus* bus = nullptr;
  BleDisconnectSink* disconnectSink = nullptr;
  BLEServer* pServer = nullptr;
  BLECharacteristic* pTx = nullptr;

  volatile bool deviceConnected = false;
  QueueHandle_t controlQueue = nullptr;
  QueueHandle_t txQueue = nullptr;
  TaskHandle_t controlTaskHandle = nullptr;
  TaskHandle_t txTaskHandle = nullptr;
  uint32_t lastAdvRestartMs = 0;
};
