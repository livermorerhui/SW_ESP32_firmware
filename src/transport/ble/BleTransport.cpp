#include "BleTransport.h"
#include "core/FirmwareLogPolicy.h"
#include "core/LogMarkers.h"
#include "core/SystemStateMachine.h"
#include <esp_gap_ble_api.h>
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

static const char* addrTypeName(uint8_t addrType) {
  switch (addrType) {
    case BLE_ADDR_TYPE_PUBLIC:
      return "PUBLIC";
    case BLE_ADDR_TYPE_RANDOM:
      return "RANDOM";
    case BLE_ADDR_TYPE_RPA_PUBLIC:
      return "RPA_PUBLIC";
    case BLE_ADDR_TYPE_RPA_RANDOM:
      return "RPA_RANDOM";
    default:
      return "UNKNOWN";
  }
}

static bool readUsedBleAddress(char* out, size_t outLen, uint8_t& outAddrType) {
  if (!out || outLen < 18) return false;
  esp_bd_addr_t usedAddr{};
  outAddrType = 0xFF;
  const esp_err_t err = esp_ble_gap_get_local_used_addr(usedAddr, &outAddrType);
  if (err != ESP_OK) {
    snprintf(out, outLen, "ERR:%d", static_cast<int>(err));
    return false;
  }
  snprintf(out, outLen, "%02x:%02x:%02x:%02x:%02x:%02x",
           usedAddr[0], usedAddr[1], usedAddr[2], usedAddr[3], usedAddr[4], usedAddr[5]);
  return true;
}

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

const char* BleTransport::txFrameClassName(TxFrameClass frameClass) {
  switch (frameClass) {
    case TxFrameClass::CRITICAL_EVENT:
      return "CRITICAL_EVENT";
    case TxFrameClass::STATUS_EVENT:
      return "STATUS_EVENT";
    case TxFrameClass::STREAM_EVENT:
      return "STREAM_EVENT";
    case TxFrameClass::SNAPSHOT:
      return "SNAPSHOT";
    case TxFrameClass::CAPABILITY:
      return "CAPABILITY";
    case TxFrameClass::ACK:
      return "ACK";
    case TxFrameClass::NACK:
      return "NACK";
    case TxFrameClass::OTHER_CONTROL:
      return "OTHER_CONTROL";
  }
  return "OTHER_CONTROL";
}

const char* BleTransport::eventTypeName(EventType type) {
  switch (type) {
    case EventType::STATE:
      return "STATE";
    case EventType::WAVE_OUTPUT:
      return "WAVE_OUTPUT";
    case EventType::FAULT:
      return "FAULT";
    case EventType::SAFETY:
      return "SAFETY";
    case EventType::STABLE_WEIGHT:
      return "STABLE_WEIGHT";
    case EventType::PARAMS:
      return "PARAMS";
    case EventType::STREAM:
      return "STREAM";
    case EventType::BASELINE_MAIN:
      return "BASELINE_MAIN";
    case EventType::STOP:
      return "STOP";
  }
  return "UNKNOWN";
}

bool BleTransport::isCriticalEvent(EventType type) {
  switch (type) {
    case EventType::STATE:
    case EventType::WAVE_OUTPUT:
    case EventType::FAULT:
    case EventType::SAFETY:
    case EventType::STOP:
      return true;
    case EventType::BASELINE_MAIN:
    case EventType::STABLE_WEIGHT:
    case EventType::PARAMS:
    case EventType::STREAM:
      return false;
  }
  return false;
}

void BleTransport::gapEventThunk(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  if (g_self) {
    g_self->handleGapEvent(event, param);
  }
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {}

  void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    if (g_self) {
      const uint16_t connId = param ? param->connect.conn_id : (server ? server->getConnId() : 0);
      const esp_bd_addr_t* remoteBda = param ? &param->connect.remote_bda : nullptr;
      g_self->resetSessionOnConnect(server, connId, true, remoteBda, millis());
      if (!g_self->enqueueConnectEvent()) {
        g_self->noteLifecycleControlEnqueueFailure("BLE_CONNECTED");
      }
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
      if (!g_self->enqueueDisconnectEvent()) {
        g_self->noteLifecycleControlEnqueueFailure("BLE_DISCONNECTED");
      }
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
      if (!g_self->enqueueTxLineRaw("NACK:BUSY")) {
        g_self->noteTxEnqueueFailure(
            BleTransport::TxFrameClass::NACK,
            "rx_busy_nack",
            "NACK:BUSY");
      }
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
  advertisingActive = false;
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
  BLEDevice::setCustomGapHandler(BleTransport::gapEventThunk);
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
  startAdvertisingSafe("begin");
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
    if (advertisingActive) {
      stopAdvertisingSafe("update_identity");
    }
    startAdvertisingSafe("update_identity");
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

void BleTransport::logAdvertisingAction(const char* action, const char* reason) const {
  BLEAddress localAddress = BLEDevice::getAddress();
  char usedAddr[24]{};
  uint8_t usedAddrType = 0xFF;
  const bool usedAddrOk = readUsedBleAddress(usedAddr, sizeof(usedAddr), usedAddrType);
  Serial.printf(
      "[BLE ADV TRACE] action=%s reason=%s active=%d connected=%d starts=%lu stops=%lu identity_addr=%s used_addr=%s used_addr_type=%s name=%s model=%s session_id=%lu\n",
      action ? action : "unknown",
      reason ? reason : "unknown",
      advertisingActive ? 1 : 0,
      deviceConnected ? 1 : 0,
      static_cast<unsigned long>(advertisingStartRequests),
      static_cast<unsigned long>(advertisingStopRequests),
      localAddress.toString().c_str(),
      usedAddrOk ? usedAddr : "unavailable",
      usedAddrOk ? addrTypeName(usedAddrType) : "unknown",
      advertisedDeviceName.c_str(),
      advertisedModelName.empty() ? "UNKNOWN" : advertisedModelName.c_str(),
      static_cast<unsigned long>(sessionId));
}

void BleTransport::handleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  switch (event) {
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      {
        char usedAddr[24]{};
        uint8_t usedAddrType = 0xFF;
        const bool usedAddrOk = readUsedBleAddress(usedAddr, sizeof(usedAddr), usedAddrType);
        Serial.printf(
            "[BLE GAP TRACE] event=ADV_START_COMPLETE status=%d identity_addr=%s used_addr=%s used_addr_type=%s\n",
            param ? static_cast<int>(param->adv_start_cmpl.status) : -1,
            BLEDevice::getAddress().toString().c_str(),
            usedAddrOk ? usedAddr : "unavailable",
            usedAddrOk ? addrTypeName(usedAddrType) : "unknown");
      }
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      {
        char usedAddr[24]{};
        uint8_t usedAddrType = 0xFF;
        const bool usedAddrOk = readUsedBleAddress(usedAddr, sizeof(usedAddr), usedAddrType);
        Serial.printf(
            "[BLE GAP TRACE] event=ADV_STOP_COMPLETE status=%d identity_addr=%s used_addr=%s used_addr_type=%s\n",
            param ? static_cast<int>(param->adv_stop_cmpl.status) : -1,
            BLEDevice::getAddress().toString().c_str(),
            usedAddrOk ? usedAddr : "unavailable",
            usedAddrOk ? addrTypeName(usedAddrType) : "unknown");
      }
      break;
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
      {
        char usedAddr[24]{};
        uint8_t usedAddrType = 0xFF;
        const bool usedAddrOk = readUsedBleAddress(usedAddr, sizeof(usedAddr), usedAddrType);
        Serial.printf(
            "[BLE GAP TRACE] event=SET_LOCAL_PRIVACY_COMPLETE status=%d identity_addr=%s used_addr=%s used_addr_type=%s\n",
            param ? static_cast<int>(param->local_privacy_cmpl.status) : -1,
            BLEDevice::getAddress().toString().c_str(),
            usedAddrOk ? usedAddr : "unavailable",
            usedAddrOk ? addrTypeName(usedAddrType) : "unknown");
      }
      break;
    default:
      break;
  }
}

void BleTransport::resetSessionOnConnect(BLEServer* server,
                                         uint16_t connId,
                                         bool connIdKnown,
                                         const esp_bd_addr_t* remoteBdaIn,
                                         uint32_t nowMs) {
  flushStreamSuppressionSummaryIfNeeded(nowMs);
  deviceConnected = true;
  advertisingActive = false;
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
  Serial.printf(
      "[BLE_LIFECYCLE] event=connect session_id=%lu conn_id=%u connected_count=%u mtu=%u remote_bda_valid=%d\n",
      static_cast<unsigned long>(sessionId),
      static_cast<unsigned>(connId),
      static_cast<unsigned>(pServer ? pServer->getConnectedCount() : 0),
      static_cast<unsigned>(negotiatedMtu),
      remoteBdaValid ? 1 : 0);
}

void BleTransport::resetSessionOnDisconnect(uint16_t connId,
                                            DisconnectReasonCode reasonCode,
                                            uint16_t rawReason,
                                            uint32_t nowMs) {
  flushStreamSuppressionSummaryIfNeeded(nowMs);
  deviceConnected = false;
  advertisingActive = false;
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
  Serial.printf(
      "[BLE_LIFECYCLE] event=disconnect session_id=%lu conn_id=%u reason_code=%s raw_reason=%u connected_count=%u tx_skips=%lu control_drops=%lu critical_drops=%lu stream_replaced=%lu\n",
      static_cast<unsigned long>(sessionId),
      static_cast<unsigned>(connId),
      disconnectReasonCodeName(reasonCode),
      static_cast<unsigned>(rawReason),
      static_cast<unsigned>(pServer ? pServer->getConnectedCount() : 0),
      static_cast<unsigned long>(txSendSkipCount),
      static_cast<unsigned long>(txControlDropCount),
      static_cast<unsigned long>(txCriticalEventDropCount),
      static_cast<unsigned long>(txStreamReplaceCount));
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
    stopAdvertisingSafe("set_profile_restart");
    startAdvertisingSafe("set_profile_restart");
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

void BleTransport::startAdvertisingSafe(const char* reason) {
  if (!pServer) {
    Serial.printf("[BLE_ADV] action=skip reason=%s skip_reason=no_server\n", reason ? reason : "unspecified");
    return;
  }
  if (deviceConnected || advertisingActive) {
    Serial.printf(
        "[BLE_ADV] action=skip reason=%s skip_reason=%s active=%d connected=%d session_id=%lu\n",
        reason ? reason : "unspecified",
        deviceConnected ? "device_connected" : "already_active",
        advertisingActive ? 1 : 0,
        deviceConnected ? 1 : 0,
        static_cast<unsigned long>(sessionId));
    return;
  }
  BLEAdvertising* adv = pServer->getAdvertising();
  if (!adv) {
    Serial.printf("[BLE_ADV] action=skip reason=%s skip_reason=no_advertising\n", reason ? reason : "unspecified");
    return;
  }
  uint32_t now = millis();
  const uint32_t sinceRestartMs = now - lastAdvRestartMs;
  if (sinceRestartMs < kAdvRestartMinIntervalMs) {
    Serial.printf(
        "[BLE_ADV] action=skip reason=%s skip_reason=restart_rate_limited since_last_restart_ms=%lu min_ms=%lu active=%d connected=%d session_id=%lu\n",
        reason ? reason : "unspecified",
        static_cast<unsigned long>(sinceRestartMs),
        static_cast<unsigned long>(kAdvRestartMinIntervalMs),
        advertisingActive ? 1 : 0,
        deviceConnected ? 1 : 0,
        static_cast<unsigned long>(sessionId));
    return;
  }
  advertisingStartRequests += 1;
  logAdvertisingAction("start_request", reason);
  Serial.printf(
      "[BLE_ADV] action=start reason=%s since_last_restart_ms=%lu active=%d connected=%d starts=%lu session_id=%lu\n",
      reason ? reason : "unspecified",
      static_cast<unsigned long>(sinceRestartMs),
      advertisingActive ? 1 : 0,
      deviceConnected ? 1 : 0,
      static_cast<unsigned long>(advertisingStartRequests),
      static_cast<unsigned long>(sessionId));
  adv->start();
  lastAdvRestartMs = now;
  advertisingActive = true;
}

void BleTransport::stopAdvertisingSafe(const char* reason) {
  if (!pServer || !advertisingActive) return;
  BLEAdvertising* adv = pServer->getAdvertising();
  if (!adv) return;
  advertisingStopRequests += 1;
  logAdvertisingAction("stop_request", reason);
  Serial.printf(
      "[BLE_ADV] action=stop reason=%s starts=%lu stops=%lu session_id=%lu\n",
      reason ? reason : "unspecified",
      static_cast<unsigned long>(advertisingStartRequests),
      static_cast<unsigned long>(advertisingStopRequests),
      static_cast<unsigned long>(sessionId));
  adv->stop();
  advertisingActive = false;
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

BleTransport::TxFrameClass BleTransport::classifyTxLine(const char* s) const {
  if (!s) return TxFrameClass::OTHER_CONTROL;
  if (strncmp(s, "EVT:STATE ", 10) == 0 ||
      strncmp(s, "EVT:WAVE_OUTPUT ", 16) == 0 ||
      strncmp(s, "EVT:FAULT ", 10) == 0 ||
      strncmp(s, "EVT:SAFETY ", 11) == 0 ||
      strncmp(s, "EVT:STOP ", 9) == 0) {
    return TxFrameClass::CRITICAL_EVENT;
  }
  if (strncmp(s, "EVT:STREAM ", 11) == 0) {
    return TxFrameClass::STREAM_EVENT;
  }
  if (strncmp(s, "EVT:", 4) == 0) {
    return TxFrameClass::STATUS_EVENT;
  }
  if (strncmp(s, "SNAPSHOT:", 9) == 0) {
    return TxFrameClass::SNAPSHOT;
  }
  if (strncmp(s, "ACK:CAP ", 8) == 0) {
    return TxFrameClass::CAPABILITY;
  }
  if (strncmp(s, "ACK:", 4) == 0) {
    return TxFrameClass::ACK;
  }
  if (strncmp(s, "NACK:", 5) == 0) {
    return TxFrameClass::NACK;
  }
  return TxFrameClass::OTHER_CONTROL;
}

void BleTransport::noteTxEnqueueFailure(TxFrameClass frameClass, const char* origin, const char* line) {
  txClassifiedDropCount += 1;
  if (frameClass == TxFrameClass::CRITICAL_EVENT) {
    txCriticalEventDropCount += 1;
    markReconnectSnapshotDirty(frameClass, origin, line);
  }
  Serial.printf(
      "[BLE TX] event=enqueue_failed class=%s origin=%s classified_drops=%lu critical_drops=%lu control_drops=%lu depth=%u line=%s\n",
      txFrameClassName(frameClass),
      origin ? origin : "unknown",
      static_cast<unsigned long>(txClassifiedDropCount),
      static_cast<unsigned long>(txCriticalEventDropCount),
      static_cast<unsigned long>(txControlDropCount),
      static_cast<unsigned>(txControlQueue ? uxQueueMessagesWaiting(txControlQueue) : 0U),
      line ? line : "");
  logTxPressureSnapshot("enqueue_failed", millis(), true);
}

void BleTransport::noteEventEnqueueFailure(EventType type, const char* line) {
  noteTxEnqueueFailure(
      isCriticalEvent(type) ? TxFrameClass::CRITICAL_EVENT : classifyTxLine(line),
      eventTypeName(type),
      line);
}

void BleTransport::noteLifecycleControlEnqueueFailure(const char* lifecycleEvent) {
  lifecycleControlDropCount += 1;
  Serial.printf(
      "[BLE CTRL] event=lifecycle_enqueue_failed lifecycle=%s drops=%lu depth=%u\n",
      lifecycleEvent ? lifecycleEvent : "UNKNOWN",
      static_cast<unsigned long>(lifecycleControlDropCount),
      static_cast<unsigned>(controlQueue ? uxQueueMessagesWaiting(controlQueue) : 0U));
}

void BleTransport::noteTxSendSkipped(TxFrameClass frameClass, const char* reason, const char* line) {
  txSendSkipCount += 1;
  const uint32_t nowMs = millis();
  if (frameClass == TxFrameClass::CRITICAL_EVENT) {
    markReconnectSnapshotDirty(frameClass, reason, line);
  }

  const bool criticalFrame = frameClass == TxFrameClass::CRITICAL_EVENT;
  if (!FirmwareLogPolicy::shouldLogNow(
          nowMs,
          lastTxSendSkipLogMs,
          FirmwareLogPolicy::kBleTxSendSkipLogIntervalMs,
          criticalFrame)) {
    return;
  }
  Serial.printf(
      "[BLE TX] event=send_skipped class=%s reason=%s skips=%lu connected=%d pTx=%p line=%s\n",
      txFrameClassName(frameClass),
      reason ? reason : "unknown",
      static_cast<unsigned long>(txSendSkipCount),
      deviceConnected ? 1 : 0,
      static_cast<void*>(pTx),
      line ? line : "");
  logTxPressureSnapshot("send_skipped", nowMs, criticalFrame);
}

void BleTransport::markReconnectSnapshotDirty(TxFrameClass frameClass, const char* origin, const char* line) {
  reconnectSnapshotDirtyCount += 1;
  if (!reconnectSnapshotDirty) {
    reconnectSnapshotDirty = true;
    reconnectSnapshotDirtySinceMs = millis();
  }
  Serial.printf(
      "[BLE TX] event=reconnect_snapshot_dirty class=%s origin=%s dirty_count=%lu dirty_for_ms=%lu line=%s\n",
      txFrameClassName(frameClass),
      origin ? origin : "unknown",
      static_cast<unsigned long>(reconnectSnapshotDirtyCount),
      static_cast<unsigned long>(millis() - reconnectSnapshotDirtySinceMs),
      line ? line : "");
}

void BleTransport::noteReconnectSnapshotPending(const char* origin) const {
  if (!reconnectSnapshotDirty) return;
  Serial.printf(
      "[BLE TX] event=reconnect_snapshot_pending origin=%s dirty_count=%lu dirty_for_ms=%lu\n",
      origin ? origin : "unknown",
      static_cast<unsigned long>(reconnectSnapshotDirtyCount),
      static_cast<unsigned long>(millis() - reconnectSnapshotDirtySinceMs));
}

void BleTransport::noteReconnectSnapshotDelivered(const char* origin, const char* line) {
  if (!reconnectSnapshotDirty) return;
  reconnectSnapshotCompensationCount += 1;
  Serial.printf(
      "[BLE TX] event=reconnect_snapshot_compensated origin=%s compensations=%lu dirty_count=%lu dirty_for_ms=%lu line=%s\n",
      origin ? origin : "unknown",
      static_cast<unsigned long>(reconnectSnapshotCompensationCount),
      static_cast<unsigned long>(reconnectSnapshotDirtyCount),
      static_cast<unsigned long>(millis() - reconnectSnapshotDirtySinceMs),
      line ? line : "");
  reconnectSnapshotDirty = false;
  reconnectSnapshotDirtySinceMs = 0;
  reconnectSnapshotDirtyCount = 0;
}

void BleTransport::logTxPressureSnapshot(const char* reason, uint32_t nowMs, bool force) {
  if (!FirmwareLogPolicy::shouldLogNow(
          nowMs,
          lastTxPressureLogMs,
          FirmwareLogPolicy::kBleTxPressureSnapshotIntervalMs,
          force)) {
    return;
  }
  Serial.printf(
      "[BLE_TX_PRESSURE] reason=%s session_id=%lu connected=%d control_depth=%u stream_depth=%u control_high=%u stream_high=%u control_drops=%lu critical_drops=%lu classified_drops=%lu send_skips=%lu stream_replaced=%lu stream_suppressed=%lu lifecycle_drops=%lu\n",
      reason ? reason : "periodic",
      static_cast<unsigned long>(sessionId),
      deviceConnected ? 1 : 0,
      static_cast<unsigned>(txControlQueue ? uxQueueMessagesWaiting(txControlQueue) : 0U),
      static_cast<unsigned>(txStreamQueue ? uxQueueMessagesWaiting(txStreamQueue) : 0U),
      static_cast<unsigned>(txControlHighWatermark),
      static_cast<unsigned>(txStreamHighWatermark),
      static_cast<unsigned long>(txControlDropCount),
      static_cast<unsigned long>(txCriticalEventDropCount),
      static_cast<unsigned long>(txClassifiedDropCount),
      static_cast<unsigned long>(txSendSkipCount),
      static_cast<unsigned long>(txStreamReplaceCount),
      static_cast<unsigned long>(txStreamSuppressedForControlCount),
      static_cast<unsigned long>(lifecycleControlDropCount));
}

bool BleTransport::tryHandleDirectQuery(const String& s) {
  if (!ProtocolCodec::isSnapshotQuery(s)) {
    return false;
  }

  const SystemStateMachine* snapshotOwner = SystemStateMachine::activeInstance();
  if (!snapshotOwner) {
    if (!enqueueTxLineRaw("NACK:BUSY")) {
      noteTxEnqueueFailure(TxFrameClass::NACK, "snapshot_busy_nack", "NACK:BUSY");
    }
    return true;
  }

  const String snapshot = ProtocolCodec::encodeSnapshot(snapshotOwner->snapshot());
  if (!enqueueTxLine(snapshot)) {
    noteTxEnqueueFailure(TxFrameClass::SNAPSHOT, "snapshot_query", "SNAPSHOT");
    if (!enqueueTxLineRaw("NACK:BUSY")) {
      noteTxEnqueueFailure(TxFrameClass::NACK, "snapshot_fallback_nack", "NACK:BUSY");
    }
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
    noteTxSendSkipped(classifyTxLine(s), !deviceConnected ? "not_connected" : "missing_tx_characteristic", s);
#if DEBUG_BLE_TX_VERBOSE
    Serial.println("[BLE TX] skipped");
#endif
    return;
  }

  logTruthPayloadBudgetWarningIfNeeded(s, framed.length());

  const TxFrameClass frameClass = classifyTxLine(s);
  const bool streamFrame = frameClass == TxFrameClass::STREAM_EVENT;
  const uint16_t notifyPayloadLimit = (negotiatedMtu >= 23) ? (negotiatedMtu - 3) : 20;
  const size_t framedLen = framed.length();
  const bool fragmented = framedLen > notifyPayloadLimit;

  if (fragmented) {
    txFragmentedFrameCount += 1;
    const size_t chunkCount = (framedLen + notifyPayloadLimit - 1) / notifyPayloadLimit;
    if (txFragmentedFrameCount <= 5 || (txFragmentedFrameCount % 50) == 0) {
      Serial.printf(
          "[BLE TX] event=notify_fragmented class=%s framed_len=%u mtu=%u payload_limit=%u chunks=%u fragmented_frames=%lu prefix=%s\n",
          txFrameClassName(frameClass),
          static_cast<unsigned>(framedLen),
          static_cast<unsigned>(negotiatedMtu),
          static_cast<unsigned>(notifyPayloadLimit),
          static_cast<unsigned>(chunkCount),
          static_cast<unsigned long>(txFragmentedFrameCount),
          s);
    }
  }

  size_t offset = 0;
  while (offset < framedLen) {
    if (!deviceConnected || !pTx) {
      noteTxSendSkipped(frameClass, "notify_fragment_interrupted", s);
      return;
    }

    const size_t remaining = framedLen - offset;
    const size_t chunkLen = remaining > notifyPayloadLimit ? notifyPayloadLimit : remaining;
    String chunk = framed.substring(offset, offset + chunkLen);
    pTx->setValue((uint8_t*)chunk.c_str(), chunk.length());
    pTx->notify();
    offset += chunkLen;

    if (fragmented && offset < framedLen) {
      vTaskDelay(1);
    }
  }

  if (!streamFrame) {
    lastControlTxAtMs = millis();
  }
  noteTxNotifyIssued(millis(), framedLen, streamFrame);
  if (frameClass == TxFrameClass::SNAPSHOT) {
    noteReconnectSnapshotDelivered("snapshot_notify", s);
  }
  logTxPressureSnapshot(streamFrame ? "stream_notify" : "control_notify", millis(), false);
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
      noteReconnectSnapshotPending("ble_connected");
      continue;
    }

    if (msg.type == ControlMsg::Type::BLE_DISCONNECTED) {
      setAdvertisingProfile(AdvertisingProfile::FAST_DISCOVERY, false);
      startAdvertisingSafe("disconnect_event");
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
      const String nack = "NACK:" + err;
      if (!enqueueTxLine(nack)) {
        noteTxEnqueueFailure(TxFrameClass::NACK, "parse_nack", nack.c_str());
      }
      continue;
    }

    String ack;
    if (!bus) {
      if (!enqueueTxLineRaw("NACK:BUSY")) {
        noteTxEnqueueFailure(TxFrameClass::NACK, "bus_busy_nack", "NACK:BUSY");
      }
      continue;
    }

    bus->dispatch(cmd, ack);
    if (!enqueueTxLine(ack)) {
      noteTxEnqueueFailure(classifyTxLine(ack.c_str()), "command_ack", ack.c_str());
    }
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
    if (!enqueueStreamTxLine(encoded)) {
      noteEventEnqueueFailure(e.type, encoded.c_str());
    }
    return;
  }
  if (!enqueueTxLine(encoded)) {
    noteEventEnqueueFailure(e.type, encoded.c_str());
  }
}
