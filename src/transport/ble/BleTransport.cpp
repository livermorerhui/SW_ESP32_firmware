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
static constexpr uint32_t kAdvRecoveryCheckIntervalIdleLowPowerMs = 1500;
static constexpr uint32_t kControlLatencyWarnMs = 80;
static constexpr uint32_t kRecoveryObservationWindowMs = 5000;
static constexpr uint8_t kRecoveryObservationMinChecks = 3;
static constexpr uint32_t kRecoveryDisconnectMinIntervalMs = 1200;
static constexpr uint16_t kPreferredMtu = 247;
static constexpr uint16_t kConnParamMinInterval = 12;
static constexpr uint16_t kConnParamMaxInterval = 24;
static constexpr uint16_t kConnParamLatency = 0;
static constexpr uint16_t kConnParamTimeout = 400;
static constexpr uint32_t kStreamControlHoldoffMs = 35;
static constexpr esp_power_level_t kBleDefaultTxPowerLevel = ESP_PWR_LVL_P3;
static constexpr esp_power_level_t kBleAdvertisingFastPowerLevel = ESP_PWR_LVL_P3;
static constexpr esp_power_level_t kBleAdvertisingIdlePowerLevel = ESP_PWR_LVL_N0;
static constexpr uint16_t kAdvFastIntervalMinUnits = 0x0100;  // 160 ms
static constexpr uint16_t kAdvFastIntervalMaxUnits = 0x0180;  // 240 ms
static constexpr uint16_t kAdvIdleIntervalMinUnits = 0x0280;  // 400 ms
static constexpr uint16_t kAdvIdleIntervalMaxUnits = 0x0400;  // 640 ms
static constexpr uint32_t kAdvFastDiscoveryWindowMs = 15000UL;

const char* BleTransport::disconnectReasonCodeName(DisconnectReasonCode code) {
  switch (code) {
    case DisconnectReasonCode::PEER_DISCONNECT:
      return "PEER_DISCONNECT";
    case DisconnectReasonCode::LOCAL_FORCE_DISCONNECT:
      return "LOCAL_FORCE_DISCONNECT";
    case DisconnectReasonCode::RECOVERY_FORCE_DISCONNECT:
      return "RECOVERY_FORCE_DISCONNECT";
    case DisconnectReasonCode::UNKNOWN:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* BleTransport::negotiationStatusCodeName(NegotiationStatusCode code) {
  switch (code) {
    case NegotiationStatusCode::NOT_ATTEMPTED:
      return "NOT_ATTEMPTED";
    case NegotiationStatusCode::REQUESTED:
      return "REQUESTED";
    case NegotiationStatusCode::APPLIED:
      return "APPLIED";
    case NegotiationStatusCode::FAILED:
      return "FAILED";
    case NegotiationStatusCode::UNKNOWN_RESULT:
      return "UNKNOWN_RESULT";
  }
  return "UNKNOWN_RESULT";
}

const char* BleTransport::recoveryAnomalyCodeName(RecoveryAnomalyCode code) {
  switch (code) {
    case RecoveryAnomalyCode::NONE:
      return "NONE";
    case RecoveryAnomalyCode::LOCAL_CONNECTED_BUT_SERVER_COUNT_ZERO:
      return "LOCAL_CONNECTED_BUT_SERVER_COUNT_ZERO";
    case RecoveryAnomalyCode::LOCAL_SERVER_CONN_ID_MISMATCH:
      return "LOCAL_SERVER_CONN_ID_MISMATCH";
  }
  return "NONE";
}

const char* BleTransport::recoverySkipReasonCodeName(RecoverySkipReasonCode code) {
  switch (code) {
    case RecoverySkipReasonCode::NONE:
      return "NONE";
    case RecoverySkipReasonCode::NO_ANOMALY:
      return "NO_ANOMALY";
    case RecoverySkipReasonCode::PROHIBITED_SCENARIO:
      return "PROHIBITED_SCENARIO";
    case RecoverySkipReasonCode::WINDOW_NOT_REACHED:
      return "WINDOW_NOT_REACHED";
    case RecoverySkipReasonCode::SESSION_CHANGED:
      return "SESSION_CHANGED";
    case RecoverySkipReasonCode::RATE_LIMITED:
      return "RATE_LIMITED";
  }
  return "NONE";
}

bool BleTransport::recoveryAnomalyAllowedInPhase1(RecoveryAnomalyCode code) {
  return code == RecoveryAnomalyCode::LOCAL_CONNECTED_BUT_SERVER_COUNT_ZERO;
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {}

  void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    if (g_self) {
      const uint16_t connId = param ? param->connect.conn_id : (server ? server->getConnId() : 0);
      const esp_bd_addr_t* remoteBda = param ? &param->connect.remote_bda : nullptr;
      g_self->resetSessionOnConnect(server, connId, true, remoteBda, millis());
      g_self->enqueueConnectEvent();
    }
  }

  void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    if (!g_self || !param) return;
    g_self->noteMtuNegotiationResult(
        g_self->sessionId,
        BleTransport::NegotiationStatusCode::APPLIED,
        param->mtu.mtu);
  }

  void onDisconnect(BLEServer*) override {}

  void onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    if (g_self) {
      const uint16_t connId = param ? param->disconnect.conn_id : 0;
      const uint16_t rawReason = param ? param->disconnect.reason : 0;
      const BleTransport::DisconnectReasonCode reasonCode =
          (g_self->lastDisconnectReasonCode == BleTransport::DisconnectReasonCode::RECOVERY_FORCE_DISCONNECT)
              ? BleTransport::DisconnectReasonCode::RECOVERY_FORCE_DISCONNECT
              : BleTransport::DisconnectReasonCode::PEER_DISCONNECT;
      g_self->resetSessionOnDisconnect(
          connId,
          reasonCode,
          rawReason,
          millis());
      g_self->enqueueDisconnectEvent();
    }
  }
};

class MyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!g_self) return;

    std::string v = c->getValue();
    if (v.empty()) return;
    g_self->noteRxActivity(millis(), v.size());

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
  sessionId = 0;
  clearRecoveryState();
  rxActivityObserved = false;
  lastRxActivityAtMs = 0;
  txNotifyIssuedObserved = false;
  lastTxNotifyIssuedAtMs = 0;
  sessionProgressAtMs = 0;
  notifySubscriptionState = NotifySubscriptionState::UNKNOWN;
  mtuNegotiationState = NegotiationStatusCode::NOT_ATTEMPTED;
  negotiatedMtu = 23;
  connParamUpdateState = NegotiationStatusCode::NOT_ATTEMPTED;
  lastDisconnectReasonCode = DisconnectReasonCode::UNKNOWN;
  lastRecoveryReasonCode = RecoveryAnomalyCode::NONE;
  remoteBdaValid = false;
  advertisingProfile = AdvertisingProfile::FAST_DISCOVERY;
  advertisingProfileStartedAtMs = millis();
  memset(remoteBda, 0, sizeof(remoteBda));
  lastDisconnectRawReason = 0;

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
  BLEDevice::setPower(kBleDefaultTxPowerLevel);
  applyAdvertisingPowerProfile();
  const esp_err_t mtuErr = BLEDevice::setMTU(kPreferredMtu);
  if (mtuErr == ESP_OK) {
    negotiatedMtu = BLEDevice::getMTU();
  }
  logNegotiationEvent(
      "mtu",
      "local_preference",
      sessionId,
      mtuErr == ESP_OK ? NegotiationStatusCode::APPLIED : NegotiationStatusCode::FAILED,
      negotiatedMtu);

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

void BleTransport::logSessionEvent(const char* eventName,
                                   uint32_t sessionIdValue,
                                   uint16_t connIdValue,
                                   DisconnectReasonCode reasonCode,
                                   uint16_t detailValue) const {
  Serial.printf(
      "[BLE SESSION] event=%s session_id=%lu conn_id=%u reason_code=%s detail=%u\n",
      eventName ? eventName : "unknown",
      static_cast<unsigned long>(sessionIdValue),
      static_cast<unsigned>(connIdValue),
      disconnectReasonCodeName(reasonCode),
      static_cast<unsigned>(detailValue));
}

void BleTransport::logNegotiationEvent(const char* kind,
                                       const char* action,
                                       uint32_t sessionIdValue,
                                       NegotiationStatusCode status,
                                       uint16_t value) const {
  Serial.printf(
      "[BLE NEGOTIATION] kind=%s action=%s session_id=%lu status=%s value=%u\n",
      kind ? kind : "unknown",
      action ? action : "unknown",
      static_cast<unsigned long>(sessionIdValue),
      negotiationStatusCodeName(status),
      static_cast<unsigned>(value));
}

void BleTransport::logRecoveryEvent(const char* action,
                                    uint32_t sessionIdValue,
                                    uint16_t connIdValue,
                                    RecoveryAnomalyCode anomalyCode,
                                    RecoverySkipReasonCode skipReason,
                                    uint32_t observedForMs,
                                    uint8_t checks) const {
  Serial.printf(
      "[BLE RECOVERY] action=%s session_id=%lu conn_id=%u anomaly=%s skip_reason=%s observed_for_ms=%lu checks=%u\n",
      action ? action : "unknown",
      static_cast<unsigned long>(sessionIdValue),
      static_cast<unsigned>(connIdValue),
      recoveryAnomalyCodeName(anomalyCode),
      recoverySkipReasonCodeName(skipReason),
      static_cast<unsigned long>(observedForMs),
      static_cast<unsigned>(checks));
}

void BleTransport::resetSessionOnConnect(BLEServer* server,
                                         uint16_t connId,
                                         bool connIdKnown,
                                         const esp_bd_addr_t* remoteBdaIn,
                                         uint32_t nowMs) {
  flushStreamSuppressionSummaryIfNeeded(nowMs);
  deviceConnected = true;
  protocolActivityObserved = false;
  currentConnIdValid = connIdKnown;
  currentConnId = connId;
  connectedAtMs = nowMs;
  lastProtocolActivityAtMs = connectedAtMs;
  sessionId += 1;
  rxActivityObserved = false;
  lastRxActivityAtMs = 0;
  txNotifyIssuedObserved = false;
  lastTxNotifyIssuedAtMs = 0;
  sessionProgressAtMs = nowMs;
  notifySubscriptionState = NotifySubscriptionState::UNKNOWN;
  mtuNegotiationState = NegotiationStatusCode::NOT_ATTEMPTED;
  negotiatedMtu = BLEDevice::getMTU();
  connParamUpdateState = NegotiationStatusCode::NOT_ATTEMPTED;
  remoteBdaValid = remoteBdaIn != nullptr;
  if (remoteBdaValid) {
    memcpy(remoteBda, *remoteBdaIn, sizeof(remoteBda));
  } else {
    memset(remoteBda, 0, sizeof(remoteBda));
  }
  clearRecoveryState();
  lastDisconnectReasonCode = DisconnectReasonCode::UNKNOWN;
  lastDisconnectRawReason = 0;
  (void)server;
  logSessionEvent("connect", sessionId, connId, DisconnectReasonCode::UNKNOWN);
}

void BleTransport::resetSessionOnDisconnect(uint16_t connId,
                                            DisconnectReasonCode reasonCode,
                                            uint16_t rawReason,
                                            uint32_t nowMs) {
  flushStreamSuppressionSummaryIfNeeded(nowMs);
  deviceConnected = false;
  protocolActivityObserved = false;
  currentConnIdValid = false;
  currentConnId = 0;
  connectedAtMs = 0;
  lastProtocolActivityAtMs = 0;
  rxActivityObserved = false;
  lastRxActivityAtMs = 0;
  txNotifyIssuedObserved = false;
  lastTxNotifyIssuedAtMs = 0;
  sessionProgressAtMs = nowMs;
  notifySubscriptionState = NotifySubscriptionState::UNKNOWN;
  mtuNegotiationState = NegotiationStatusCode::NOT_ATTEMPTED;
  negotiatedMtu = BLEDevice::getMTU();
  connParamUpdateState = NegotiationStatusCode::NOT_ATTEMPTED;
  remoteBdaValid = false;
  memset(remoteBda, 0, sizeof(remoteBda));
  clearRecoveryState();
  lastDisconnectReasonCode = reasonCode;
  lastDisconnectRawReason = rawReason;
  logSessionEvent("disconnect", sessionId, connId, reasonCode, rawReason);
}

void BleTransport::noteRxActivity(uint32_t nowMs, size_t rxBytes) {
  protocolActivityObserved = true;
  lastProtocolActivityAtMs = nowMs;
  lastRxActivityAtMs = nowMs;
  sessionProgressAtMs = nowMs;
  if (!rxActivityObserved) {
    rxActivityObserved = true;
    Serial.printf(
        "[BLE SESSION] event=rx_first session_id=%lu conn_id=%u bytes=%u\n",
        static_cast<unsigned long>(sessionId),
        static_cast<unsigned>(currentConnId),
        static_cast<unsigned>(rxBytes));
  }
}

void BleTransport::noteTxNotifyIssued(uint32_t nowMs, size_t txBytes, bool isStreamFrame) {
  protocolActivityObserved = true;
  lastProtocolActivityAtMs = nowMs;
  lastTxNotifyIssuedAtMs = nowMs;
  sessionProgressAtMs = nowMs;
  if (!txNotifyIssuedObserved) {
    txNotifyIssuedObserved = true;
    Serial.printf(
        "[BLE SESSION] event=tx_notify_first session_id=%lu conn_id=%u bytes=%u stream=%d\n",
        static_cast<unsigned long>(sessionId),
        static_cast<unsigned>(currentConnId),
        static_cast<unsigned>(txBytes),
        isStreamFrame ? 1 : 0);
  }
}

void BleTransport::requestMtuNegotiation(uint32_t expectedSessionId) {
  if (!deviceConnected || expectedSessionId != sessionId) {
    return;
  }

  const esp_err_t err = BLEDevice::setMTU(kPreferredMtu);
  if (err != ESP_OK) {
    noteMtuNegotiationResult(expectedSessionId, NegotiationStatusCode::FAILED, negotiatedMtu);
    return;
  }

  mtuNegotiationState = NegotiationStatusCode::REQUESTED;
  sessionProgressAtMs = millis();
  logNegotiationEvent("mtu", "request", expectedSessionId, mtuNegotiationState, kPreferredMtu);
}

void BleTransport::noteMtuNegotiationResult(uint32_t observedSessionId,
                                            NegotiationStatusCode status,
                                            uint16_t negotiatedMtuValue) {
  if (observedSessionId != sessionId) {
    return;
  }

  mtuNegotiationState = status;
  if (negotiatedMtuValue >= 23) {
    negotiatedMtu = negotiatedMtuValue;
  }
  sessionProgressAtMs = millis();
  logNegotiationEvent("mtu", "result", observedSessionId, status, negotiatedMtu);
}

void BleTransport::requestConnectionParamUpdate(uint32_t expectedSessionId) {
  if (!deviceConnected || expectedSessionId != sessionId) {
    return;
  }
  if (!pServer || !remoteBdaValid) {
    noteConnectionParamUpdateResult(expectedSessionId, NegotiationStatusCode::UNKNOWN_RESULT);
    return;
  }

  connParamUpdateState = NegotiationStatusCode::REQUESTED;
  sessionProgressAtMs = millis();
  logNegotiationEvent("conn_params", "request", expectedSessionId, connParamUpdateState, 0);
  pServer->updateConnParams(
      remoteBda,
      kConnParamMinInterval,
      kConnParamMaxInterval,
      kConnParamLatency,
      kConnParamTimeout);
  noteConnectionParamUpdateResult(expectedSessionId, NegotiationStatusCode::UNKNOWN_RESULT);
}

void BleTransport::noteConnectionParamUpdateResult(uint32_t observedSessionId,
                                                   NegotiationStatusCode status) {
  if (observedSessionId != sessionId) {
    return;
  }

  connParamUpdateState = status;
  sessionProgressAtMs = millis();
  logNegotiationEvent("conn_params", "result", observedSessionId, status, 0);
}

BleTransport::RecoveryAnomalyCode BleTransport::detectRecoveryAnomaly(uint16_t serverConnectedCount,
                                                                      bool serverConnIdAvailable,
                                                                      uint16_t serverConnId) const {
  if (!deviceConnected) {
    return RecoveryAnomalyCode::NONE;
  }
  if (serverConnectedCount == 0) {
    return RecoveryAnomalyCode::LOCAL_CONNECTED_BUT_SERVER_COUNT_ZERO;
  }
  if (currentConnIdValid && serverConnIdAvailable && currentConnId != serverConnId) {
    return RecoveryAnomalyCode::LOCAL_SERVER_CONN_ID_MISMATCH;
  }
  return RecoveryAnomalyCode::NONE;
}

bool BleTransport::evaluateRecoveryWindow(uint32_t nowMs,
                                          uint32_t expectedSessionId,
                                          RecoveryAnomalyCode anomalyCode,
                                          RecoverySkipReasonCode& outSkipReason) {
  if (expectedSessionId != sessionId || !deviceConnected) {
    outSkipReason = RecoverySkipReasonCode::SESSION_CHANGED;
    return false;
  }
  if (!recoveryAnomalyAllowedInPhase1(anomalyCode)) {
    outSkipReason = RecoverySkipReasonCode::PROHIBITED_SCENARIO;
    return false;
  }
  if (nowMs - lastRecoveryDisconnectMs < kRecoveryDisconnectMinIntervalMs) {
    outSkipReason = RecoverySkipReasonCode::RATE_LIMITED;
    return false;
  }
  if (recoveryAnomalySinceMs == 0 ||
      nowMs - recoveryAnomalySinceMs < kRecoveryObservationWindowMs ||
      recoveryAnomalyChecks < kRecoveryObservationMinChecks) {
    outSkipReason = RecoverySkipReasonCode::WINDOW_NOT_REACHED;
    return false;
  }
  outSkipReason = RecoverySkipReasonCode::NONE;
  return true;
}

void BleTransport::clearRecoveryState() {
  recoveryAnomalyCode = RecoveryAnomalyCode::NONE;
  recoveryAnomalySinceMs = 0;
  recoveryAnomalyChecks = 0;
}

void BleTransport::recoverStalledConnection(uint32_t nowMs) {
  if (!pServer) {
    clearRecoveryState();
    lastRecoverySkipReason = RecoverySkipReasonCode::PROHIBITED_SCENARIO;
    logRecoveryEvent(
        "skip",
        sessionId,
        currentConnId,
        RecoveryAnomalyCode::NONE,
        lastRecoverySkipReason,
        0,
        0);
    return;
  }

  if (!deviceConnected) {
    clearRecoveryState();
    return;
  }

  const uint32_t expectedSessionId = sessionId;
  const uint16_t serverConnectedCount = static_cast<uint16_t>(pServer->getConnectedCount());
  const bool serverConnIdAvailable = serverConnectedCount > 0;
  const uint16_t serverConnId = serverConnIdAvailable ? pServer->getConnId() : 0;
  const RecoveryAnomalyCode anomalyCode =
      detectRecoveryAnomaly(serverConnectedCount, serverConnIdAvailable, serverConnId);

  if (anomalyCode == RecoveryAnomalyCode::NONE) {
    clearRecoveryState();
    lastRecoverySkipReason = RecoverySkipReasonCode::NO_ANOMALY;
    return;
  }

  if (!recoveryAnomalyAllowedInPhase1(anomalyCode)) {
    clearRecoveryState();
    lastRecoverySkipReason = RecoverySkipReasonCode::PROHIBITED_SCENARIO;
    logRecoveryEvent(
        "skip",
        expectedSessionId,
        currentConnId,
        anomalyCode,
        lastRecoverySkipReason,
        0,
        0);
    return;
  }

  if (recoveryAnomalyCode != anomalyCode) {
    recoveryAnomalyCode = anomalyCode;
    recoveryAnomalySinceMs = nowMs;
    recoveryAnomalyChecks = 1;
  } else if (recoveryAnomalyChecks < 0xFF) {
    recoveryAnomalyChecks += 1;
  }

  RecoverySkipReasonCode skipReason = RecoverySkipReasonCode::NONE;
  if (!evaluateRecoveryWindow(nowMs, expectedSessionId, anomalyCode, skipReason)) {
    lastRecoverySkipReason = skipReason;
    logRecoveryEvent(
        "skip",
        expectedSessionId,
        currentConnId,
        anomalyCode,
        skipReason,
        recoveryAnomalySinceMs == 0 ? 0 : (nowMs - recoveryAnomalySinceMs),
        recoveryAnomalyChecks);
    return;
  }

  if (!forceDisconnectCurrentClient(anomalyCode, expectedSessionId, nowMs)) {
    lastRecoverySkipReason = RecoverySkipReasonCode::RATE_LIMITED;
    logRecoveryEvent(
        "skip",
        expectedSessionId,
        currentConnId,
        anomalyCode,
        lastRecoverySkipReason,
        recoveryAnomalySinceMs == 0 ? 0 : (nowMs - recoveryAnomalySinceMs),
        recoveryAnomalyChecks);
    return;
  }

  clearRecoveryState();
}

bool BleTransport::forceDisconnectCurrentClient(RecoveryAnomalyCode reasonCode,
                                                uint32_t expectedSessionId,
                                                uint32_t nowMs) {
  if (!pServer || !deviceConnected) return false;
  if (expectedSessionId != sessionId) return false;
  if (nowMs - lastRecoveryDisconnectMs < kRecoveryDisconnectMinIntervalMs) return false;

  uint16_t connId = currentConnId;
  if (!currentConnIdValid) {
    return false;
  }

  lastRecoveryDisconnectMs = nowMs;
  lastRecoveryReasonCode = reasonCode;
  lastDisconnectReasonCode = DisconnectReasonCode::RECOVERY_FORCE_DISCONNECT;
  logRecoveryEvent(
      "force_disconnect",
      expectedSessionId,
      connId,
      reasonCode,
      RecoverySkipReasonCode::NONE,
      recoveryAnomalySinceMs == 0 ? 0 : (nowMs - recoveryAnomalySinceMs),
      recoveryAnomalyChecks);
  logSessionEvent("local_disconnect", expectedSessionId, connId, DisconnectReasonCode::RECOVERY_FORCE_DISCONNECT);
  pServer->disconnect(connId);
  return true;
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
  if (advertisingProfile == AdvertisingProfile::IDLE_LOW_POWER) {
    adv->setMinInterval(kAdvIdleIntervalMinUnits);
    adv->setMaxInterval(kAdvIdleIntervalMaxUnits);
  } else {
    adv->setMinInterval(kAdvFastIntervalMinUnits);
    adv->setMaxInterval(kAdvFastIntervalMaxUnits);
  }
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
}

void BleTransport::applyAdvertisingPowerProfile() const {
  const esp_power_level_t advPowerLevel =
      advertisingProfile == AdvertisingProfile::IDLE_LOW_POWER
          ? kBleAdvertisingIdlePowerLevel
          : kBleAdvertisingFastPowerLevel;
  BLEDevice::setPower(advPowerLevel, ESP_BLE_PWR_TYPE_ADV);
}

void BleTransport::setAdvertisingProfile(AdvertisingProfile profile, bool restartIfNeeded) {
  if (advertisingProfile == profile) {
    return;
  }
  advertisingProfile = profile;
  advertisingProfileStartedAtMs = millis();
  applyAdvertisingPowerProfile();
  Serial.printf(
      "[BLE ADV] profile=%s restart=%d tx_power=%s\n",
      profile == AdvertisingProfile::IDLE_LOW_POWER ? "IDLE_LOW_POWER" : "FAST_DISCOVERY",
      restartIfNeeded ? 1 : 0,
      profile == AdvertisingProfile::IDLE_LOW_POWER ? "N0" : "P3");
  configureAdvertising(
      advertisedDeviceName.c_str(),
      advertisedModelName.empty() ? nullptr : advertisedModelName.c_str());
  if (!deviceConnected && restartIfNeeded && pServer) {
    BLEAdvertising* adv = pServer->getAdvertising();
    if (adv) {
      adv->stop();
    }
    startAdvertisingSafe();
  }
}

void BleTransport::maybeRelaxAdvertisingProfile(uint32_t nowMs) {
  if (deviceConnected || advertisingProfile != AdvertisingProfile::FAST_DISCOVERY) {
    return;
  }
  if (advertisingProfileStartedAtMs == 0) {
    advertisingProfileStartedAtMs = nowMs;
    return;
  }
  if (nowMs - advertisingProfileStartedAtMs < kAdvFastDiscoveryWindowMs) {
    return;
  }
  setAdvertisingProfile(AdvertisingProfile::IDLE_LOW_POWER, true);
}

TickType_t BleTransport::controlTaskIdleWaitTicks() const {
  const uint32_t waitMs =
      (!deviceConnected && advertisingProfile == AdvertisingProfile::IDLE_LOW_POWER)
          ? kAdvRecoveryCheckIntervalIdleLowPowerMs
          : kAdvRecoveryCheckIntervalMs;
  return pdMS_TO_TICKS(waitMs);
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

  if (shouldDeferStreamForControl()) {
    noteStreamSuppressedForControl(
        txControlQueue ? uxQueueMessagesWaiting(txControlQueue) : 0U,
        millis());
    return true;
  }

  flushStreamSuppressionSummaryIfNeeded(millis());
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

void BleTransport::noteStreamSuppressedForControl(UBaseType_t controlDepth, uint32_t nowMs) {
  txStreamSuppressedForControlCount += 1;
  if (txStreamSuppressionBurstCount == 0) {
    txStreamSuppressionBurstStartedAtMs = nowMs;
    txStreamSuppressionBurstMaxControlDepth = controlDepth;
  } else if (controlDepth > txStreamSuppressionBurstMaxControlDepth) {
    txStreamSuppressionBurstMaxControlDepth = controlDepth;
  }
  txStreamSuppressionBurstCount += 1;
}

void BleTransport::flushStreamSuppressionSummaryIfNeeded(uint32_t nowMs) {
  if (txStreamSuppressionBurstCount == 0) return;
  Serial.printf(
      "[BLE TX] queue=stream suppressed_for_control burst_count=%lu total=%lu max_control_depth=%u window_ms=%lu\n",
      static_cast<unsigned long>(txStreamSuppressionBurstCount),
      static_cast<unsigned long>(txStreamSuppressedForControlCount),
      static_cast<unsigned>(txStreamSuppressionBurstMaxControlDepth),
      static_cast<unsigned long>(nowMs - txStreamSuppressionBurstStartedAtMs));
  txStreamSuppressionBurstCount = 0;
  txStreamSuppressionBurstStartedAtMs = 0;
  txStreamSuppressionBurstMaxControlDepth = 0;
}

void BleTransport::logTruthPayloadBudgetWarningIfNeeded(const char* s, size_t framedLen) const {
  if (!s) return;

  if (strncmp(s, "ACK:CAP ", 8) == 0 && framedLen > ProtocolCodec::kCapTruthPayloadBudgetBytes) {
    Serial.printf(
        "[BLE TX] warn=bootstrap_truth_budget_exceeded framed_len=%u budget=%u prefix=%s\n",
        static_cast<unsigned>(framedLen),
        static_cast<unsigned>(ProtocolCodec::kCapTruthPayloadBudgetBytes),
        s);
    return;
  }

  if (strncmp(s, "SNAPSHOT:", 9) == 0 && framedLen > ProtocolCodec::kConnectSnapshotPayloadBudgetBytes) {
    Serial.printf(
        "[BLE TX] warn=runtime_truth_budget_exceeded framed_len=%u budget=%u prefix=%s\n",
        static_cast<unsigned>(framedLen),
        static_cast<unsigned>(ProtocolCodec::kConnectSnapshotPayloadBudgetBytes),
        s);
  }
}

bool BleTransport::shouldDeferStreamForControl() const {
  if (txControlQueue && uxQueueMessagesWaiting(txControlQueue) > 0) {
    return true;
  }
  const uint32_t nowMs = millis();
  return lastControlTxAtMs != 0 && (nowMs - lastControlTxAtMs) < kStreamControlHoldoffMs;
}

bool BleTransport::isStreamFrame(const char* s) const {
  return s && strncmp(s, "EVT:STREAM ", 11) == 0;
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

  logTruthPayloadBudgetWarningIfNeeded(s, framed.length());

  if (negotiatedMtu >= 23) {
    const uint16_t notifyPayloadLimit = negotiatedMtu - 3;
    if (framed.length() > notifyPayloadLimit) {
      Serial.printf(
          "[BLE TX] warn=payload_exceeds_mtu framed_len=%u mtu=%u payload_limit=%u prefix=%s\n",
          static_cast<unsigned>(framed.length()),
          static_cast<unsigned>(negotiatedMtu),
          static_cast<unsigned>(notifyPayloadLimit),
          s);
    }
  }

  pTx->setValue((uint8_t*)framed.c_str(), framed.length());
  pTx->notify();
  const bool streamFrame = isStreamFrame(s);
  if (!streamFrame) {
    lastControlTxAtMs = millis();
  }
  noteTxNotifyIssued(millis(), framed.length(), streamFrame);
#if DEBUG_BLE_TX_VERBOSE
  Serial.println("[BLE TX] notify() called");
#endif
}

void BleTransport::controlTaskLoop() {
  ControlMsg msg{};

  while (true) {
    if (xQueueReceive(controlQueue, &msg, controlTaskIdleWaitTicks()) != pdTRUE) {
      const uint32_t nowMs = millis();
      if (!deviceConnected) {
        maybeRelaxAdvertisingProfile(nowMs);
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
      requestMtuNegotiation(sessionId);
      requestConnectionParamUpdate(sessionId);
      continue;
    }

    if (msg.type == ControlMsg::Type::BLE_DISCONNECTED) {
      setAdvertisingProfile(AdvertisingProfile::FAST_DISCOVERY, false);
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
      if (shouldDeferStreamForControl()) {
        xQueueOverwrite(txStreamQueue, &msg);
        continue;
      }
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
