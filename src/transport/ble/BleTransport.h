#pragma once
#include <BLEAdvertising.h>
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
  void begin(CommandBus* cb, const char* deviceName = nullptr, const char* advertisedModel = nullptr);
  void updateAdvertisingIdentity(const char* deviceName, const char* advertisedModel = nullptr);
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
    enum class Priority : uint8_t { CONTROL, STREAM };
    Priority priority = Priority::CONTROL;
    char line[512]{};
    uint32_t enqueuedAtMs = 0;
  };

  static void controlTaskThunk(void* arg);
  static void txTaskThunk(void* arg);

  void controlTaskLoop();
  void txTaskLoop();
  void updateConnectionTrackingOnConnect(BLEServer* server, uint16_t connId, bool connIdKnown);
  void updateConnectionTrackingOnDisconnect();
  void noteProtocolActivity();
  void recoverStalledConnection(uint32_t nowMs);
  void forceDisconnectCurrentClient(const char* reason, uint32_t nowMs);
  void sendLineNow(const char* s);
  void startAdvertisingSafe();
  void configureAdvertising(const char* deviceName, const char* advertisedModel);
  bool enqueueCommand(const std::string& raw);
  bool enqueueConnectEvent();
  bool enqueueDisconnectEvent();
  bool enqueueTxLine(const String& s);
  bool enqueueTxLineRaw(const char* s);
  bool enqueueStreamTxLine(const String& s);
  bool enqueueStreamTxLineRaw(const char* s);
  bool tryHandleDirectQuery(const String& s);
  void noteQueueWatermark(const char* queueName, UBaseType_t depth, UBaseType_t& highWatermark);

  friend class MyServerCallbacks;
  friend class MyRxCallbacks;

private:
  CommandBus* bus = nullptr;
  BleDisconnectSink* disconnectSink = nullptr;
  BLEServer* pServer = nullptr;
  BLECharacteristic* pTx = nullptr;

  volatile bool deviceConnected = false;
  volatile bool protocolActivityObserved = false;
  volatile bool currentConnIdValid = false;
  volatile uint16_t currentConnId = 0;
  volatile uint32_t connectedAtMs = 0;
  volatile uint32_t lastProtocolActivityAtMs = 0;
  QueueHandle_t controlQueue = nullptr;
  QueueHandle_t txControlQueue = nullptr;
  QueueHandle_t txStreamQueue = nullptr;
  QueueSetHandle_t txQueueSet = nullptr;
  TaskHandle_t controlTaskHandle = nullptr;
  TaskHandle_t txTaskHandle = nullptr;
  uint32_t lastAdvRestartMs = 0;
  UBaseType_t txControlHighWatermark = 0;
  UBaseType_t txStreamHighWatermark = 0;
  uint32_t txControlDropCount = 0;
  uint32_t txStreamReplaceCount = 0;
  uint32_t lastRecoveryDisconnectMs = 0;
  std::string advertisedDeviceName;
  std::string advertisedModelName;
};
