#include "BleTransport.h"
#include "core/LogMarkers.h"
#include <string.h>

static BleTransport* g_self = nullptr;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    if (g_self) g_self->deviceConnected = true;
    Serial.printf("%s BLE connected\n", LogMarker::kBleConnected);
    if (g_self) {
      g_self->enqueueConnectEvent();
    }
  }
  void onDisconnect(BLEServer*) override {
    if (g_self) g_self->deviceConnected = false;
    Serial.printf("%s BLE disconnected\n", LogMarker::kBleDisconnected);
    if (g_self) {
      g_self->enqueueDisconnectEvent();
    }
  }
};

class MyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!g_self) return;

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

void BleTransport::begin(CommandBus* cb) {
  bus = cb;
  g_self = this;

  controlQueue = xQueueCreate(8, sizeof(ControlMsg));
  txQueue = xQueueCreate(16, sizeof(TxMsg));
  xTaskCreatePinnedToCore(controlTaskThunk, "BleCtrl", 4096, this, 3, &controlTaskHandle, 1);
  xTaskCreatePinnedToCore(txTaskThunk, "BleTx", 4096, this, 3, &txTaskHandle, 1);

  BLEDevice::init(BLE_DEVICE_NAME);
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
  startAdvertisingSafe();
  Serial.println("[OK] BLE advertising");
}

void BleTransport::startAdvertisingSafe() {
  if (!pServer) return;
  BLEAdvertising* adv = pServer->getAdvertising();
  if (!adv) return;
  uint32_t now = millis();
  if (now - lastAdvRestartMs < 300) return;
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
  if (!txQueue || !s) return false;

  TxMsg msg{};
  msg.type = TxMsg::Type::LINE;
  strlcpy(msg.line, s, sizeof(msg.line));
  return xQueueSend(txQueue, &msg, 0) == pdTRUE;
}

bool BleTransport::enqueueStreamFlush() {
  if (!txQueue) return false;

  TxMsg msg{};
  msg.type = TxMsg::Type::STREAM_FLUSH;
  if (xQueueSend(txQueue, &msg, 0) == pdTRUE) {
    return true;
  }

  portENTER_CRITICAL(&streamMux);
  streamFlushQueued = false;
  portEXIT_CRITICAL(&streamMux);
  return false;
}

bool BleTransport::loadPendingStream(char* out, size_t outSize, uint32_t& version) {
  if (!out || outSize == 0) return false;

  bool hasPayload = false;
  portENTER_CRITICAL(&streamMux);
  hasPayload = latestStreamPayload[0] != '\0';
  if (hasPayload) {
    strlcpy(out, latestStreamPayload, outSize);
    version = latestStreamVersion;
  }
  portEXIT_CRITICAL(&streamMux);
  return hasPayload;
}

bool BleTransport::completePendingStream(uint32_t version) {
  bool done = false;
  portENTER_CRITICAL(&streamMux);
  if (latestStreamVersion == version) {
    streamFlushQueued = false;
    done = true;
  }
  portEXIT_CRITICAL(&streamMux);
  return done;
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
    if (xQueueReceive(controlQueue, &msg, portMAX_DELAY) != pdTRUE) {
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
  char streamPayload[sizeof(TxMsg::line)];

  while (true) {
    if (xQueueReceive(txQueue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (msg.type == TxMsg::Type::LINE) {
      sendLineNow(msg.line);
      continue;
    }

    while (true) {
      uint32_t version = 0;
      if (!loadPendingStream(streamPayload, sizeof(streamPayload), version)) {
        portENTER_CRITICAL(&streamMux);
        streamFlushQueued = false;
        portEXIT_CRITICAL(&streamMux);
        break;
      }

      sendLineNow(streamPayload);
      if (completePendingStream(version)) {
        break;
      }
    }
  }
}

void BleTransport::onEvent(const Event& e) {
  if (e.type == EventType::STREAM) {
    String encoded = ProtocolCodec::encodeEvent(e);
    bool shouldEnqueue = false;

    portENTER_CRITICAL(&streamMux);
    strlcpy(latestStreamPayload, encoded.c_str(), sizeof(latestStreamPayload));
    ++latestStreamVersion;
    if (!streamFlushQueued) {
      streamFlushQueued = true;
      shouldEnqueue = true;
    }
    portEXIT_CRITICAL(&streamMux);

    if (shouldEnqueue) {
      enqueueStreamFlush();
    }
    return;
  }

  // Unified encoding path for non-stream events.
  enqueueTxLine(ProtocolCodec::encodeEvent(e));
}
