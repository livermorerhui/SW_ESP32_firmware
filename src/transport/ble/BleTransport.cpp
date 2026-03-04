#include "BleTransport.h"

static BleTransport* g_self = nullptr;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    if (g_self) g_self->deviceConnected = true;
    Serial.println("🔗 BLE connected");
  }
  void onDisconnect(BLEServer*) override {
    if (g_self) g_self->deviceConnected = false;
    Serial.println("❌ BLE disconnected");
    // 广播恢复
    if (g_self) g_self->startAdvertisingSafe();
  }
};

class MyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!g_self) return;
    std::string v = c->getValue();
    if (v.empty()) return;

    String s; s.reserve(v.size());
    for (char ch : v) s += ch;
    s.trim();

    Command cmd{};
    String err;
    if (!ProtocolCodec::parseCommand(s, cmd, err)) {
      g_self->sendLine("NACK:" + err);
      return;
    }

    String ack;
    g_self->bus->dispatch(cmd, ack);
    g_self->sendLine(ack);
  }
};

void BleTransport::begin(CommandBus* cb) {
  bus = cb;
  g_self = this;

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
  adv->start();
}

void BleTransport::sendLine(const String& s) {
  if (!deviceConnected || !pTx) return;
  pTx->setValue((uint8_t*)s.c_str(), s.length());
  pTx->notify();
}

void BleTransport::onEvent(const Event& e) {
  // 统一编码并发送
  sendLine(ProtocolCodec::encodeEvent(e));
}