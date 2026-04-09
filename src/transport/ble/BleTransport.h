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

  enum class NotifySubscriptionState : uint8_t {
    UNKNOWN = 0,
    ENABLED,
    OBSERVED_DISABLED
  };

  enum class NegotiationStatusCode : uint8_t {
    NOT_ATTEMPTED = 0,
    REQUESTED,
    APPLIED,
    FAILED,
    UNKNOWN_RESULT
  };

  enum class RecoveryAnomalyCode : uint8_t {
    NONE = 0,
    LOCAL_CONNECTED_BUT_SERVER_COUNT_ZERO,
    LOCAL_SERVER_CONN_ID_MISMATCH
  };

  enum class RecoverySkipReasonCode : uint8_t {
    NONE = 0,
    NO_ANOMALY,
    PROHIBITED_SCENARIO,
    WINDOW_NOT_REACHED,
    SESSION_CHANGED,
    RATE_LIMITED
  };

  enum class DisconnectReasonCode : uint8_t {
    UNKNOWN = 0,
    PEER_DISCONNECT,
    LOCAL_FORCE_DISCONNECT,
    RECOVERY_FORCE_DISCONNECT
  };

  enum class AdvertisingProfile : uint8_t {
    FAST_DISCOVERY = 0,
    IDLE_LOW_POWER
  };

  static void controlTaskThunk(void* arg);
  static void txTaskThunk(void* arg);
  static const char* disconnectReasonCodeName(DisconnectReasonCode code);
  static const char* negotiationStatusCodeName(NegotiationStatusCode code);
  static const char* recoveryAnomalyCodeName(RecoveryAnomalyCode code);
  static const char* recoverySkipReasonCodeName(RecoverySkipReasonCode code);
  static bool recoveryAnomalyAllowedInPhase1(RecoveryAnomalyCode code);
  static void gapEventThunk(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);

  void controlTaskLoop();
  void txTaskLoop();
  void logAdvertisingAction(const char* action, const char* reason) const;
  void handleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
  void logSessionEvent(const char* eventName,
                       uint32_t sessionIdValue,
                       uint16_t connIdValue,
                       DisconnectReasonCode reasonCode,
                       uint16_t detailValue = 0) const;
  void logNegotiationEvent(const char* kind,
                           const char* action,
                           uint32_t sessionIdValue,
                           NegotiationStatusCode status,
                           uint16_t value = 0) const;
  void logRecoveryEvent(const char* action,
                        uint32_t sessionIdValue,
                        uint16_t connIdValue,
                        RecoveryAnomalyCode anomalyCode,
                        RecoverySkipReasonCode skipReason,
                        uint32_t observedForMs,
                        uint8_t checks) const;
  void resetSessionOnConnect(BLEServer* server,
                             uint16_t connId,
                             bool connIdKnown,
                             const esp_bd_addr_t* remoteBda,
                             uint32_t nowMs);
  void resetSessionOnDisconnect(uint16_t connId,
                                DisconnectReasonCode reasonCode,
                                uint16_t rawReason,
                                uint32_t nowMs);
  void noteRxActivity(uint32_t nowMs, size_t rxBytes);
  void noteTxNotifyIssued(uint32_t nowMs, size_t txBytes, bool isStreamFrame);
  void requestMtuNegotiation(uint32_t expectedSessionId);
  void noteMtuNegotiationResult(uint32_t observedSessionId,
                                NegotiationStatusCode status,
                                uint16_t negotiatedMtu);
  void requestConnectionParamUpdate(uint32_t expectedSessionId);
  void noteConnectionParamUpdateResult(uint32_t observedSessionId,
                                       NegotiationStatusCode status);
  RecoveryAnomalyCode detectRecoveryAnomaly(uint16_t serverConnectedCount,
                                            bool serverConnIdAvailable,
                                            uint16_t serverConnId) const;
  bool evaluateRecoveryWindow(uint32_t nowMs,
                              uint32_t expectedSessionId,
                              RecoveryAnomalyCode anomalyCode,
                              RecoverySkipReasonCode& outSkipReason);
  void clearRecoveryState();
  void recoverStalledConnection(uint32_t nowMs);
  bool forceDisconnectCurrentClient(RecoveryAnomalyCode reasonCode,
                                    uint32_t expectedSessionId,
                                    uint32_t nowMs);
  void sendLineNow(const char* s);
  void startAdvertisingSafe(const char* reason = "unspecified");
  void stopAdvertisingSafe(const char* reason = "unspecified");
  void configureAdvertising(const char* deviceName, const char* advertisedModel);
  bool enqueueCommand(const std::string& raw);
  bool enqueueConnectEvent();
  bool enqueueDisconnectEvent();
  bool enqueueTxLine(const String& s);
  bool enqueueTxLineRaw(const char* s);
  bool enqueueStreamTxLine(const String& s);
  bool enqueueStreamTxLineRaw(const char* s);
  bool tryHandleDirectQuery(const String& s);
  void applyAdvertisingPowerProfile() const;
  void setAdvertisingProfile(AdvertisingProfile profile, bool restartIfNeeded);
  void maybeRelaxAdvertisingProfile(uint32_t nowMs);
  TickType_t controlTaskIdleWaitTicks() const;
  void noteQueueWatermark(const char* queueName, UBaseType_t depth, UBaseType_t& highWatermark);
  void noteStreamSuppressedForControl(UBaseType_t controlDepth, uint32_t nowMs);
  void flushStreamSuppressionSummaryIfNeeded(uint32_t nowMs);
  void logTruthPayloadBudgetWarningIfNeeded(const char* s, size_t framedLen) const;
  bool isStreamFrame(const char* s) const;
  bool shouldDeferStreamForControl() const;

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
  volatile uint32_t sessionId = 0;
  bool rxActivityObserved = false;
  uint32_t lastRxActivityAtMs = 0;
  bool txNotifyIssuedObserved = false;
  uint32_t lastTxNotifyIssuedAtMs = 0;
  uint32_t sessionProgressAtMs = 0;
  NotifySubscriptionState notifySubscriptionState = NotifySubscriptionState::UNKNOWN;
  NegotiationStatusCode mtuNegotiationState = NegotiationStatusCode::NOT_ATTEMPTED;
  uint16_t negotiatedMtu = 23;
  NegotiationStatusCode connParamUpdateState = NegotiationStatusCode::NOT_ATTEMPTED;
  RecoveryAnomalyCode recoveryAnomalyCode = RecoveryAnomalyCode::NONE;
  uint32_t recoveryAnomalySinceMs = 0;
  uint8_t recoveryAnomalyChecks = 0;
  RecoverySkipReasonCode lastRecoverySkipReason = RecoverySkipReasonCode::NONE;
  DisconnectReasonCode lastDisconnectReasonCode = DisconnectReasonCode::UNKNOWN;
  RecoveryAnomalyCode lastRecoveryReasonCode = RecoveryAnomalyCode::NONE;
  esp_bd_addr_t remoteBda{};
  bool remoteBdaValid = false;
  uint16_t lastDisconnectRawReason = 0;
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
  uint32_t txStreamSuppressedForControlCount = 0;
  uint32_t txStreamSuppressionBurstCount = 0;
  uint32_t txStreamSuppressionBurstStartedAtMs = 0;
  UBaseType_t txStreamSuppressionBurstMaxControlDepth = 0;
  uint32_t lastControlTxAtMs = 0;
  uint32_t lastRecoveryDisconnectMs = 0;
  AdvertisingProfile advertisingProfile = AdvertisingProfile::FAST_DISCOVERY;
  uint32_t advertisingProfileStartedAtMs = 0;
  std::string advertisedDeviceName;
  std::string advertisedModelName;
  bool advertisingActive = false;
  uint32_t advertisingStartRequests = 0;
  uint32_t advertisingStopRequests = 0;
};
