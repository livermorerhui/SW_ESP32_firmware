#pragma once

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include "core/PlatformSnapshotOwner.h"
#include "config/GlobalConfig.h"

enum class FirmwareOtaState : uint8_t {
  IDLE,
  PREPARED,
  RECEIVING,
  VERIFYING,
  READY_TO_REBOOT,
  REBOOTING,
  FAILED,
};

enum class FirmwareOtaError : uint8_t {
  NONE,
  BUSY_RUNNING,
  UNSUPPORTED_BOARD,
  IMAGE_TOO_LARGE,
  INVALID_METADATA,
  INVALID_STATE,
  SEQ_MISMATCH,
  OFFSET_MISMATCH,
  WRITE_FAILED,
  VERIFY_FAILED,
  BOOT_SWITCH_FAILED,
  BLE_DISCONNECTED,
  USER_CANCEL,
  UNSUPPORTED_COMMAND,
  UNSUPPORTED_TRANSFER_MODE,
  GATT_BUSY,
  RX_QUEUE_FULL,
  SEQ_DUPLICATE_UNSUPPORTED,
  WINDOW_TIMEOUT,
  WINDOW_OVERFLOW,
  VERSION_CONFIRM_FAILED,
  MANIFEST_SIGNATURE_INVALID,
  MANIFEST_EXPIRED,
  ROLLBACK_CONFIRM_FAILED,
  INTERNAL_ERROR,
};

enum class FirmwareOtaTransferMode : uint8_t {
  WRITE_REQUEST,
  WRITE_COMMAND,
};

struct FirmwareOtaBeginRequest {
  uint16_t protocol = 0;
  String version;
  String buildId;
  String board;
  size_t size = 0;
  String sha256;
  uint16_t chunkSize = OTA_DEFAULT_CHUNK_SIZE;
  FirmwareOtaTransferMode transferMode = FirmwareOtaTransferMode::WRITE_REQUEST;
  uint8_t windowSize = OTA_DEFAULT_WINDOW_SIZE;
  uint16_t ackIntervalChunks = OTA_DEFAULT_ACK_INTERVAL_CHUNKS;
  uint8_t maxInflightChunks = OTA_DEFAULT_WINDOW_SIZE;
  String ackPolicy;
  String busyPolicy;
};

struct FirmwareOtaDataFrame {
  uint32_t seq = 0;
  uint32_t offset = 0;
  uint16_t length = 0;
  const uint8_t* payload = nullptr;
};

class FirmwareOtaStatusSink {
public:
  virtual void notifyOtaStatus(const String& json) = 0;
  virtual ~FirmwareOtaStatusSink() = default;
};

class FirmwareOtaManager {
public:
  void begin(const PlatformSnapshotOwner* snapshotOwner, FirmwareOtaStatusSink* statusSink);
  FirmwareOtaState state() const { return otaState; }
  bool active() const;
  bool blocksBusinessCommands() const;

  FirmwareOtaError handleControlJson(const String& rawJson);
  FirmwareOtaError handleDataFrame(const uint8_t* data, size_t length);
  void onBleDisconnected();
  void onRxQueueFull();

  static const char* stateName(FirmwareOtaState state);
  static const char* errorCodeName(FirmwareOtaError error);
  static bool parseBeginRequest(const String& rawJson, FirmwareOtaBeginRequest& out, FirmwareOtaError& error);
  static bool parseDataFrame(const uint8_t* data, size_t length, FirmwareOtaDataFrame& out);

private:
  FirmwareOtaError handleBegin(const FirmwareOtaBeginRequest& request);
  FirmwareOtaError handleEnd(size_t expectedSize, const String& expectedSha256);
  FirmwareOtaError handleAbort(FirmwareOtaError reason);
  FirmwareOtaError handleReboot();
  FirmwareOtaError handleQuery();
  FirmwareOtaError acceptDataFrame(const FirmwareOtaDataFrame& frame);
  FirmwareOtaError validatePreconditions(const FirmwareOtaBeginRequest& request) const;
  void fail(FirmwareOtaError error, const char* detail);
  void abortUpdateIfNeeded(FirmwareOtaState previousState, const char* detail);
  static bool shouldAbortUpdateOnFailure(FirmwareOtaState state);
  void emitStatus(const String& json);
  void emitStateStatus();
  void emitQueryStatus();
  void emitProgressIfNeeded(bool force);
  void emitWindowAckIfNeeded(bool force);
  void resetSession();
  void resetSha256();
  void updateSha256(const uint8_t* data, size_t length);
  bool finishSha256Matches(const String& expected);
  String baseStatusJson() const;
  String compactStateStatusJson() const;
  String evidenceStatusJson() const;
  static bool isHexSha256(const String& value);
  static String readJsonString(const String& rawJson, const char* key);
  static long readJsonLong(const String& rawJson, const char* key, long fallback);
  static FirmwareOtaError parseAbortReason(const String& rawJson);
  static FirmwareOtaTransferMode parseTransferMode(const String& value, bool& supported);
  static const char* transferModeName(FirmwareOtaTransferMode mode);
  static String partitionJson(const char* key, const esp_partition_t* partition);
  static String compactPartitionJson(const char* key, const esp_partition_t* partition);

  const PlatformSnapshotOwner* platformSnapshotOwner = nullptr;
  FirmwareOtaStatusSink* sink = nullptr;
  FirmwareOtaState otaState = FirmwareOtaState::IDLE;
  FirmwareOtaError lastError = FirmwareOtaError::NONE;
  String targetVersion;
  String targetBuildId;
  String targetSha256;
  size_t targetSize = 0;
  uint16_t negotiatedChunkSize = OTA_DEFAULT_CHUNK_SIZE;
  FirmwareOtaTransferMode transferMode = FirmwareOtaTransferMode::WRITE_REQUEST;
  uint8_t negotiatedWindowSize = OTA_DEFAULT_WINDOW_SIZE;
  uint16_t negotiatedAckIntervalChunks = OTA_DEFAULT_ACK_INTERVAL_CHUNKS;
  uint8_t negotiatedMaxInflightChunks = OTA_DEFAULT_WINDOW_SIZE;
  String negotiatedAckPolicy;
  String negotiatedBusyPolicy;
  uint32_t expectedSeq = 0;
  uint32_t receivedBytes = 0;
  uint32_t lastProgressPercent = 0;
  uint32_t lastAckSeq = 0;
  esp_ota_handle_t otaHandle = 0;
  const esp_partition_t* runningPartition = nullptr;
  const esp_partition_t* updatePartition = nullptr;
  const esp_partition_t* bootPartition = nullptr;
  bool otaHandleActive = false;
  mbedtls_sha256_context shaContext{};
  bool shaStarted = false;
};
