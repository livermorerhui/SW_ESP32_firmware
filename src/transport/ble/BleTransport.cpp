#include "BleTransport.h"
#include "core/LogMarkers.h"
#include "core/SystemStateMachine.h"
#include <string.h>

static BleTransport* g_self = nullptr;
static constexpr uint8_t kControlQueueLen = 8;
static constexpr uint8_t kTxControlQueueLen = 64;
static constexpr uint8_t kTxStreamQueueLen = 1;
static constexpr uint32_t kAdvRestartMinIntervalMs = 300;
static constexpr uint32_t kAdvRecoveryCheckIntervalMs = 400;
static constexpr uint32_t kControlLatencyWarnMs = 80;
static constexpr uint32_t kInitialProtocolActivityTimeoutMs = 3500;
static constexpr uint32_t kRecoveryDisconnectMinIntervalMs = 1200;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {}

  void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    if (g_self) {
      const uint16_t connId = param ? param->connect.conn_id : (server ? server->getConnId() : 0);
      g_self->updateConnectionTrackingOnConnect(server, connId, true);
      g_self->enqueueConnectEvent();
    }
    Serial.printf("%s BLE connected conn_id=%u\n",
                  LogMarker::kBleConnected,
                  static_cast<unsigned>(param ? param->connect.conn_id : 0));
  }

  void onDisconnect(BLEServer*) override {}

  void onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    if (g_self) {
      g_self->updateConnectionTrackingOnDisconnect();
      g_self->enqueueDisconnectEvent();
    }
    Serial.printf("%s BLE disconnected conn_id=%u connected_count=%lu\n",
                  LogMarker::kBleDisconnected,
                  static_cast<unsigned>(param ? param->disconnect.conn_id : 0),
                  static_cast<unsigned long>(server ? server->getConnectedCount() : 0));
  }
};

class MyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!g_self) return;

    g_self->noteProtocolActivity();

    std::string v = c->getValue();
    if (v.empty()) return;

    if (!g_self->enqueueCommand(v)) {
      g_self->enqueueTxLineRaw("NACK:BUSY");
    }
  }
};

void BleTransport::controlTaskThunk(void* arg) {
  static_cast<BleTransport*>(arg)->controlTaskLoop();
}

void BleTransport::txTaskThunk(void* arg) {
  static_cast<BleTransport*>(arg)->txTaskLoop();
}

void BleTransport::begin(CommandBus* cb, const char* deviceName, const char* advertisedModel) {
  bus = cb;
  g_self = this;

  controlQueue = xQueueCreate(kControlQueueLen, sizeof(ControlMsg));
  txControlQueue = xQueueCreate(kTxControlQueueLen, sizeof(TxMsg));
  txStreamQueue = xQueueCreate(kTxStreamQueueLen, sizeof(TxMsg));
  txQueueSet = xQueueCreateSet(kTxControlQueueLen + kTxStreamQueueLen);
  if (txQueueSet && txControlQueue && txStreamQueue) {
    xQueueAddToSet(txControlQueue, txQueueSet);
    xQueueAddToSet(txStreamQueue, txQueueSet);
  }
  xTaskCreatePinnedToCore(controlTaskThunk, "BleCtrl", 4096, this, 3, &controlTaskHandle, 1);
  xTaskCreatePinnedToCore(txTaskThunk, "BleTx", 4096, this, 3, &txTaskHandle, 1);

  const char* resolvedDeviceName =
      (deviceName != nullptr && deviceName[0] != '\0') ? deviceName : BLE_DEVICE_NAME;
  const char* resolvedAdvertisedModel =
      (advertisedModel != nullptr && advertisedModel[0] != '\0') ? advertisedModel : nullptr;
  advertisedDeviceName = resolvedDeviceName;
  advertisedModelName = resolvedAdvertisedModel ? resolvedAdvertisedModel : "";
  BLEDevice::init(resolvedDeviceName);
  BLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* svc = pServer->createService(SERVICE_UUID);

  // TX notify
  pTx = svc->createCharacteristic(CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTx->addDescriptor(new BLE2902());

  // RX write
  BLECharacteristic* rx = svc->createCharacteristic(CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new MyRxCallbacks());

  svc->start();
  configureAdvertising(
      advertisedDeviceName.c_str(),
      advertisedModelName.empty() ? nullptr : advertisedModelName.c_str());
  startAdvertisingSafe();
  Serial.printf("[OK] BLE advertising name=%s model=%s\n",
      advertisedDeviceName.c_str(),
      advertisedModelName.empty() ? "UNKNOWN" : advertisedModelName.c_str());
}

void BleTransport::updateAdvertisingIdentity(const char* deviceName, const char* advertisedModel) {
  const char* resolvedDeviceName =
      (deviceName != nullptr && deviceName[0] != '\0') ? deviceName : BLE_DEVICE_NAME;
  advertisedDeviceName = resolvedDeviceName;
  advertisedModelName =
      (advertisedModel != nullptr && advertisedModel[0] != '\0') ? advertisedModel : "";

  configureAdvertising(
      advertisedDeviceName.c_str(),
      advertisedModelName.empty() ? nullptr : advertisedModelName.c_str());
  if (!deviceConnected) {
    startAdvertisingSafe();
  }
}

void BleTransport::updateConnectionTrackingOnConnect(BLEServer* server, uint16_t connId, bool connIdKnown) {
  deviceConnected = true;
  protocolActivityObserved = false;
  currentConnIdValid = connIdKnown;
  currentConnId = connId;
  connectedAtMs = millis();
  lastProtocolActivityAtMs = connectedAtMs;
  if (server) {
    Serial.printf(
        "[BLE RECOVERY] state=connected conn_id=%u connected_count=%lu\n",
        static_cast<unsigned>(connId),
        static_cast<unsigned long>(server->getConnectedCount()));
  }
}

void BleTransport::updateConnectionTrackingOnDisconnect() {
  deviceConnected = false;
  protocolActivityObserved = false;
  currentConnIdValid = false;
  currentConnId = 0;
  connectedAtMs = 0;
  lastProtocolActivityAtMs = 0;
}

void BleTransport::noteProtocolActivity() {
  protocolActivityObserved = true;
  lastProtocolActivityAtMs = millis();
}

void BleTransport::recoverStalledConnection(uint32_t nowMs) {
  if (!deviceConnected || protocolActivityObserved || connectedAtMs == 0) {
    return;
  }
  if (nowMs - connectedAtMs < kInitialProtocolActivityTimeoutMs) {
    return;
  }
  forceDisconnectCurrentClient("initial_protocol_idle_timeout", nowMs);
}

void BleTransport::forceDisconnectCurrentClient(const char* reason, uint32_t nowMs) {
  if (!pServer || !deviceConnected) return;
  if (nowMs - lastRecoveryDisconnectMs < kRecoveryDisconnectMinIntervalMs) return;

  uint16_t connId = currentConnId;
  if (!currentConnIdValid) {
    connId = pServer->getConnId();
    currentConnIdValid = true;
    currentConnId = connId;
  }

  lastRecoveryDisconnectMs = nowMs;
  Serial.printf(
      "[BLE RECOVERY] action=force_disconnect reason=%s conn_id=%u connected_for_ms=%lu last_activity_ms=%lu\n",
      reason ? reason : "unknown",
      static_cast<unsigned>(connId),
      static_cast<unsigned long>(connectedAtMs == 0 ? 0 : (nowMs - connectedAtMs)),
      static_cast<unsigned long>(lastProtocolActivityAtMs == 0 ? 0 : (nowMs - lastProtocolActivityAtMs)));
  pServer->disconnect(connId);
}

void BleTransport::configureAdvertising(const char* deviceName, const char* advertisedModel) {
  if (!pServer) return;
  BLEAdvertising* adv = pServer->getAdvertising();
  if (!adv) return;

  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setCompleteServices(BLEUUID(SERVICE_UUID));

  BLEAdvertisementData scanRespData;
  scanRespData.setName(deviceName ? deviceName : BLE_DEVICE_NAME);
  String serviceData = String("proto=") + String(PROTO_VER) + ";fw=" + FW_VER;
  if (advertisedModel != nullptr && advertisedModel[0] != '\0') {
    serviceData += ";model=";
    serviceData += advertisedModel;
  }
  scanRespData.setServiceData(BLEUUID(SERVICE_UUID), std::string(serviceData.c_str()));

  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanRespData);
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
}

void BleTransport::startAdvertisingSafe() {
  if (!pServer) return;
  BLEAdvertising* adv = pServer->getAdvertising();
  if (!adv) return;
  uint32_t now = millis();
  if (now - lastAdvRestartMs < kAdvRestartMinIntervalMs) return;
  adv->start();
  lastAdvRestartMs = now;
}

bool BleTransport::enqueueCommand(const std::string& raw) {
  if (!controlQueue) return false;

  size_t beg = 0;
  size_t end = raw.size();
  while (beg < end && (raw[beg] == ' ' || raw[beg] == '\r' || raw[beg] == '\n' || raw[beg] == '\t')) {
    ++beg;
  }
  while (end > beg && (raw[end - 1] == ' ' || raw[end - 1] == '\r' || raw[end - 1] == '\n' || raw[end - 1] == '\t')) {
    --end;
  }

  size_t len = end - beg;
  if (len == 0 || len >= sizeof(ControlMsg::line)) {
    return false;
  }

  ControlMsg msg{};
  msg.type = ControlMsg::Type::RX_COMMAND;
  memcpy(msg.line, raw.data() + beg, len);
  msg.line[len] = '\0';
  return xQueueSend(controlQueue, &msg, 0) == pdTRUE;
}

bool BleTransport::enqueueConnectEvent() {
  if (!controlQueue) return false;

  ControlMsg msg{};
  msg.type = ControlMsg::Type::BLE_CONNECTED;
  return xQueueSend(controlQueue, &msg, 0) == pdTRUE;
}

bool BleTransport::enqueueDisconnectEvent() {
  if (!controlQueue) return false;

  ControlMsg msg{};
  msg.type = ControlMsg::Type::BLE_DISCONNECTED;
  return xQueueSend(controlQueue, &msg, 0) == pdTRUE;
}

bool BleTransport::enqueueTxLine(const String& s) {
  return enqueueTxLineRaw(s.c_str());
}

bool BleTransport::enqueueTxLineRaw(const char* s) {
  if (!txControlQueue || !s) return false;

  TxMsg msg{};
  msg.priority = TxMsg::Priority::CONTROL;
  strlcpy(msg.line, s, sizeof(msg.line));
  msg.enqueuedAtMs = millis();
  const bool ok = xQueueSend(txControlQueue, &msg, pdMS_TO_TICKS(10)) == pdTRUE;
  if (!ok) {
    txControlDropCount += 1;
    Serial.printf(
        "[BLE TX] queue=control overflow=1 drops=%lu depth=%u dropped_prefix=%s\n",
        static_cast<unsigned long>(txControlDropCount),
        static_cast<unsigned>(uxQueueMessagesWaiting(txControlQueue)),
        s);
    return false;
  }
  noteQueueWatermark("control", uxQueueMessagesWaiting(txControlQueue), txControlHighWatermark);
  return ok;
}

bool BleTransport::enqueueStreamTxLine(const String& s) {
  return enqueueStreamTxLineRaw(s.c_str());
}

bool BleTransport::enqueueStreamTxLineRaw(const char* s) {
  if (!txStreamQueue || !s) return false;

  TxMsg msg{};
  msg.priority = TxMsg::Priority::STREAM;
  strlcpy(msg.line, s, sizeof(msg.line));
  msg.enqueuedAtMs = millis();

  if (uxQueueMessagesWaiting(txStreamQueue) > 0) {
    txStreamReplaceCount += 1;
  }
  xQueueOverwrite(txStreamQueue, &msg);
  noteQueueWatermark("stream", uxQueueMessagesWaiting(txStreamQueue), txStreamHighWatermark);
  return true;
}

void BleTransport::noteQueueWatermark(const char* queueName, UBaseType_t depth, UBaseType_t& highWatermark) {
  if (depth <= highWatermark) return;
  highWatermark = depth;
  Serial.printf("[BLE TX] queue=%s high_watermark=%u\n", queueName, static_cast<unsigned>(highWatermark));
}

bool BleTransport::tryHandleDirectQuery(const String& s) {
  if (!ProtocolCodec::isSnapshotQuery(s)) {
    return false;
  }

  const SystemStateMachine* snapshotOwner = SystemStateMachine::activeInstance();
  if (!snapshotOwner) {
    enqueueTxLineRaw("NACK:BUSY");
    return true;
  }

  if (!enqueueTxLine(ProtocolCodec::encodeSnapshot(snapshotOwner->snapshot()))) {
    enqueueTxLineRaw("NACK:BUSY");
  }
  return true;
}

void BleTransport::sendLineNow(const char* s) {
  if (!s) return;

  String framed = s;
  if (!framed.endsWith("\n")) {
    framed += "\n";
  }

  String framedEscaped = framed;
  framedEscaped.replace("\r", "\\r");
  framedEscaped.replace("\n", "\\n");

#if DEBUG_BLE_TX_VERBOSE
  Serial.printf("[BLE TX] deviceConnected=%d pTx=%p msg=%s\n",
                deviceConnected ? 1 : 0,
                (void*)pTx,
                s);
  Serial.printf("[BLE TX] framed_len=%u framed=%s\n",
                (unsigned int)framed.length(),
                framedEscaped.c_str());
#endif

  if (!deviceConnected || !pTx) {
#if DEBUG_BLE_TX_VERBOSE
    Serial.println("[BLE TX] skipped");
#endif
    return;
  }

  pTx->setValue((uint8_t*)framed.c_str(), framed.length());
  pTx->notify();
#if DEBUG_BLE_TX_VERBOSE
  Serial.println("[BLE TX] notify() called");
#endif
}

void BleTransport::controlTaskLoop() {
  ControlMsg msg{};

  while (true) {
    if (xQueueReceive(controlQueue, &msg, pdMS_TO_TICKS(kAdvRecoveryCheckIntervalMs)) != pdTRUE) {
      const uint32_t nowMs = millis();
      if (!deviceConnected) {
        startAdvertisingSafe();
      } else {
        recoverStalledConnection(nowMs);
      }
      continue;
    }

    if (msg.type == ControlMsg::Type::BLE_CONNECTED) {
      if (disconnectSink) {
        disconnectSink->onBleConnected();
      }
      continue;
    }

    if (msg.type == ControlMsg::Type::BLE_DISCONNECTED) {
      startAdvertisingSafe();
      if (disconnectSink) {
        disconnectSink->onBleDisconnect();
      }
      continue;
    }

    String in = msg.line;
    if (tryHandleDirectQuery(in)) {
      continue;
    }

    Command cmd{};
    String err;
    if (!ProtocolCodec::parseCommand(in, cmd, err)) {
      enqueueTxLine("NACK:" + err);
      continue;
    }

    String ack;
    if (!bus) {
      enqueueTxLineRaw("NACK:BUSY");
      continue;
    }

    bus->dispatch(cmd, ack);
    enqueueTxLine(ack);
  }
}

void BleTransport::txTaskLoop() {
  TxMsg msg{};

  while (true) {
    if (!txQueueSet) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    QueueSetMemberHandle_t activated = xQueueSelectFromSet(txQueueSet, portMAX_DELAY);
    if (!activated) {
      continue;
    }

    while (xQueueReceive(txControlQueue, &msg, 0) == pdTRUE) {
      const uint32_t latencyMs = millis() - msg.enqueuedAtMs;
      if (latencyMs > kControlLatencyWarnMs) {
        Serial.printf("[BLE TX] queue=control latency_ms=%lu line=%s\n",
                      static_cast<unsigned long>(latencyMs),
                      msg.line);
      }
      sendLineNow(msg.line);
    }

    if (xQueueReceive(txStreamQueue, &msg, 0) == pdTRUE) {
      sendLineNow(msg.line);
    }
  }
}

void BleTransport::onEvent(const Event& e) {
  const String encoded = ProtocolCodec::encodeEvent(e);
  if (e.type == EventType::STREAM) {
    enqueueStreamTxLine(encoded);
    return;
  }
  enqueueTxLine(encoded);
}
