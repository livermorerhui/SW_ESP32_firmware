#include "FirmwareOtaManager.h"
#include "ota/FirmwareRollbackConfirmation.h"
#include <ctype.h>

static String escapeJson(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.c_str()[i];
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

void FirmwareOtaManager::begin(const PlatformSnapshotOwner* snapshotOwner, FirmwareOtaStatusSink* statusSink) {
  platformSnapshotOwner = snapshotOwner;
  sink = statusSink;
  resetSession();
}

bool FirmwareOtaManager::active() const {
  return otaState == FirmwareOtaState::PREPARED ||
      otaState == FirmwareOtaState::RECEIVING ||
      otaState == FirmwareOtaState::VERIFYING ||
      otaState == FirmwareOtaState::READY_TO_REBOOT ||
      otaState == FirmwareOtaState::REBOOTING;
}

bool FirmwareOtaManager::blocksBusinessCommands() const {
  return active();
}

FirmwareOtaError FirmwareOtaManager::handleControlJson(const String& rawJson) {
  const String type = readJsonString(rawJson, "type");
  if (type == "ota_begin") {
    FirmwareOtaBeginRequest request{};
    FirmwareOtaError error = FirmwareOtaError::NONE;
    if (!parseBeginRequest(rawJson, request, error)) {
      fail(error, "invalid_begin");
      return error;
    }
    return handleBegin(request);
  }
  if (type == "ota_end") {
    const long size = readJsonLong(rawJson, "size", -1);
    const String sha256 = readJsonString(rawJson, "sha256");
    return handleEnd(size > 0 ? static_cast<size_t>(size) : 0, sha256);
  }
  if (type == "ota_abort") {
    return handleAbort(parseAbortReason(rawJson));
  }
  if (type == "ota_reboot") {
    return handleReboot();
  }
  if (type == "ota_query") {
    return handleQuery();
  }
  fail(FirmwareOtaError::UNSUPPORTED_COMMAND, "unsupported_command");
  return FirmwareOtaError::UNSUPPORTED_COMMAND;
}

FirmwareOtaError FirmwareOtaManager::handleDataFrame(const uint8_t* data, size_t length) {
  FirmwareOtaDataFrame frame{};
  if (!parseDataFrame(data, length, frame)) {
    fail(FirmwareOtaError::INVALID_METADATA, "invalid_data_frame");
    return FirmwareOtaError::INVALID_METADATA;
  }
  return acceptDataFrame(frame);
}

void FirmwareOtaManager::onBleDisconnected() {
  if (!active()) return;
  fail(FirmwareOtaError::BLE_DISCONNECTED, "ble_disconnected");
}

void FirmwareOtaManager::onRxQueueFull() {
  if (!active()) return;
  fail(FirmwareOtaError::RX_QUEUE_FULL, "rx_queue_full");
}

FirmwareOtaError FirmwareOtaManager::handleBegin(const FirmwareOtaBeginRequest& request) {
  if (otaState != FirmwareOtaState::IDLE && otaState != FirmwareOtaState::FAILED) {
    fail(FirmwareOtaError::INVALID_STATE, "begin_invalid_state");
    return FirmwareOtaError::INVALID_STATE;
  }
  const FirmwareOtaError precheck = validatePreconditions(request);
  if (precheck != FirmwareOtaError::NONE) {
    fail(precheck, "begin_precheck");
    return precheck;
  }

  resetSession();
  targetVersion = request.version;
  targetBuildId = request.buildId;
  targetSha256 = request.sha256;
  targetSize = request.size;
  negotiatedChunkSize = request.chunkSize;
  transferMode = request.transferMode;
  negotiatedWindowSize = request.windowSize;
  negotiatedAckIntervalChunks = request.ackIntervalChunks;
  negotiatedMaxInflightChunks = request.maxInflightChunks;
  negotiatedAckPolicy = request.ackPolicy;
  negotiatedBusyPolicy = request.busyPolicy;

  runningPartition = esp_ota_get_running_partition();
  updatePartition = esp_ota_get_next_update_partition(nullptr);
  bootPartition = esp_ota_get_boot_partition();
  if (!runningPartition || !updatePartition || updatePartition == runningPartition) {
    fail(FirmwareOtaError::INTERNAL_ERROR, "partition_select_failed");
    return FirmwareOtaError::INTERNAL_ERROR;
  }
  if (targetSize > updatePartition->size) {
    fail(FirmwareOtaError::IMAGE_TOO_LARGE, "image_exceeds_update_partition");
    return FirmwareOtaError::IMAGE_TOO_LARGE;
  }
  Serial.printf("[OTA] event=partition_select running=%s running_subtype=%u running_addr=0x%lx boot=%s update=%s update_subtype=%u update_addr=0x%lx update_size=%u image_size=%u\n",
      runningPartition->label,
      static_cast<unsigned>(runningPartition->subtype),
      static_cast<unsigned long>(runningPartition->address),
      bootPartition ? bootPartition->label : "unknown",
      updatePartition->label,
      static_cast<unsigned>(updatePartition->subtype),
      static_cast<unsigned long>(updatePartition->address),
      static_cast<unsigned>(updatePartition->size),
      static_cast<unsigned>(targetSize));

  const esp_err_t beginErr = esp_ota_begin(updatePartition, targetSize, &otaHandle);
  if (beginErr != ESP_OK) {
    Serial.printf("[OTA] event=ota_begin_failed err=%d\n", static_cast<int>(beginErr));
    fail(FirmwareOtaError::INTERNAL_ERROR, "esp_ota_begin_failed");
    return FirmwareOtaError::INTERNAL_ERROR;
  }
  otaHandleActive = true;
  resetSha256();

  otaState = FirmwareOtaState::PREPARED;
  Serial.printf("[OTA] event=begin version=%s build=%s size=%u chunk_size=%u transfer_mode=%s window_size=%u max_inflight_chunks=%u ack_interval_chunks=%u ack_policy=%s busy_policy=%s\n",
      targetVersion.c_str(),
      targetBuildId.c_str(),
      static_cast<unsigned>(targetSize),
      static_cast<unsigned>(negotiatedChunkSize),
      transferModeName(transferMode),
      static_cast<unsigned>(negotiatedWindowSize),
      static_cast<unsigned>(negotiatedMaxInflightChunks),
      static_cast<unsigned>(negotiatedAckIntervalChunks),
      negotiatedAckPolicy.c_str(),
      negotiatedBusyPolicy.c_str());
  emitStateStatus();
  return FirmwareOtaError::NONE;
}

FirmwareOtaError FirmwareOtaManager::acceptDataFrame(const FirmwareOtaDataFrame& frame) {
  if (otaState != FirmwareOtaState::PREPARED && otaState != FirmwareOtaState::RECEIVING) {
    fail(FirmwareOtaError::INVALID_STATE, "data_invalid_state");
    return FirmwareOtaError::INVALID_STATE;
  }
  if (frame.seq != expectedSeq) {
    fail(FirmwareOtaError::SEQ_MISMATCH, "seq_mismatch");
    return FirmwareOtaError::SEQ_MISMATCH;
  }
  if (frame.offset != receivedBytes) {
    fail(FirmwareOtaError::OFFSET_MISMATCH, "offset_mismatch");
    return FirmwareOtaError::OFFSET_MISMATCH;
  }
  if (frame.length == 0 || receivedBytes + frame.length > targetSize) {
    fail(FirmwareOtaError::INVALID_METADATA, "data_length_invalid");
    return FirmwareOtaError::INVALID_METADATA;
  }

  otaState = FirmwareOtaState::RECEIVING;
  if (!otaHandleActive || !updatePartition) {
    fail(FirmwareOtaError::WRITE_FAILED, "ota_handle_missing");
    return FirmwareOtaError::WRITE_FAILED;
  }
  const esp_err_t writeErr = esp_ota_write(otaHandle, frame.payload, frame.length);
  if (writeErr != ESP_OK) {
    Serial.printf("[OTA] event=ota_write_failed err=%d seq=%u offset=%u len=%u\n",
        static_cast<int>(writeErr),
        static_cast<unsigned>(frame.seq),
        static_cast<unsigned>(frame.offset),
        static_cast<unsigned>(frame.length));
    fail(FirmwareOtaError::WRITE_FAILED, "esp_ota_write_failed");
    return FirmwareOtaError::WRITE_FAILED;
  }
  updateSha256(frame.payload, frame.length);
  receivedBytes += frame.length;
  expectedSeq += 1;
  emitProgressIfNeeded(false);
  emitWindowAckIfNeeded(false);
  return FirmwareOtaError::NONE;
}

FirmwareOtaError FirmwareOtaManager::handleEnd(size_t expectedSize, const String& expectedSha256) {
  if (otaState != FirmwareOtaState::RECEIVING) {
    fail(FirmwareOtaError::INVALID_STATE, "end_invalid_state");
    return FirmwareOtaError::INVALID_STATE;
  }
  if (expectedSize != targetSize || expectedSha256 != targetSha256 || receivedBytes != targetSize) {
    fail(FirmwareOtaError::VERIFY_FAILED, "metadata_mismatch");
    return FirmwareOtaError::VERIFY_FAILED;
  }

  otaState = FirmwareOtaState::VERIFYING;
  emitStateStatus();
  if (!finishSha256Matches(targetSha256)) {
    fail(FirmwareOtaError::VERIFY_FAILED, "sha256_mismatch");
    return FirmwareOtaError::VERIFY_FAILED;
  }
  if (!otaHandleActive || !updatePartition) {
    fail(FirmwareOtaError::VERIFY_FAILED, "ota_handle_missing");
    return FirmwareOtaError::VERIFY_FAILED;
  }
  const esp_err_t endErr = esp_ota_end(otaHandle);
  otaHandleActive = false;
  otaHandle = 0;
  if (endErr != ESP_OK) {
    Serial.printf("[OTA] event=ota_end_failed err=%d\n", static_cast<int>(endErr));
    fail(FirmwareOtaError::VERIFY_FAILED, "esp_ota_end_failed");
    return FirmwareOtaError::VERIFY_FAILED;
  }
  const esp_err_t bootErr = esp_ota_set_boot_partition(updatePartition);
  if (bootErr != ESP_OK) {
    Serial.printf("[OTA] event=boot_partition_set result=failure err=%d target=%s\n",
        static_cast<int>(bootErr),
        updatePartition->label);
    fail(FirmwareOtaError::BOOT_SWITCH_FAILED, "esp_ota_set_boot_partition_failed");
    return FirmwareOtaError::BOOT_SWITCH_FAILED;
  }
  bootPartition = esp_ota_get_boot_partition();
  Serial.printf("[OTA] event=boot_partition_set result=success target=%s subtype=%u addr=0x%lx\n",
      updatePartition->label,
      static_cast<unsigned>(updatePartition->subtype),
      static_cast<unsigned long>(updatePartition->address));

  otaState = FirmwareOtaState::READY_TO_REBOOT;
  Serial.printf("[OTA] event=ready_to_reboot version=%s build=%s size=%u\n",
      targetVersion.c_str(),
      targetBuildId.c_str(),
      static_cast<unsigned>(targetSize));
  emitStateStatus();
  return FirmwareOtaError::NONE;
}

FirmwareOtaError FirmwareOtaManager::handleAbort(FirmwareOtaError reason) {
  if (otaState == FirmwareOtaState::IDLE) {
    emitStateStatus();
    return FirmwareOtaError::NONE;
  }
  fail(reason == FirmwareOtaError::NONE ? FirmwareOtaError::USER_CANCEL : reason, "abort");
  return reason;
}

FirmwareOtaError FirmwareOtaManager::handleReboot() {
  if (otaState != FirmwareOtaState::READY_TO_REBOOT) {
    fail(FirmwareOtaError::INVALID_STATE, "reboot_invalid_state");
    return FirmwareOtaError::INVALID_STATE;
  }
  otaState = FirmwareOtaState::REBOOTING;
  emitStateStatus();
  Serial.println("[OTA] event=reboot_requested");
  delay(150);
  ESP.restart();
  return FirmwareOtaError::NONE;
}

FirmwareOtaError FirmwareOtaManager::handleQuery() {
  emitQueryStatus();
  return FirmwareOtaError::NONE;
}

FirmwareOtaError FirmwareOtaManager::validatePreconditions(const FirmwareOtaBeginRequest& request) const {
  if (request.protocol != OTA_PROTOCOL_VERSION ||
      request.version.length() == 0 ||
      request.buildId.length() == 0 ||
      request.board.length() == 0 ||
      request.size == 0 ||
      request.sha256.length() == 0 ||
      !isHexSha256(request.sha256)) {
    return FirmwareOtaError::INVALID_METADATA;
  }
  if (request.board != FW_BOARD_ID) {
    return FirmwareOtaError::UNSUPPORTED_BOARD;
  }
  if (request.transferMode != FirmwareOtaTransferMode::WRITE_REQUEST &&
      request.transferMode != FirmwareOtaTransferMode::WRITE_COMMAND) {
    return FirmwareOtaError::UNSUPPORTED_TRANSFER_MODE;
  }
  if (request.windowSize == 0 || request.windowSize > 16 ||
      request.maxInflightChunks == 0 || request.maxInflightChunks > request.windowSize ||
      request.ackIntervalChunks == 0 || request.ackIntervalChunks > 128) {
    return FirmwareOtaError::INVALID_METADATA;
  }
  const PlatformSnapshot snapshot = platformSnapshotOwner ? platformSnapshotOwner->snapshot() : PlatformSnapshot{};
  if (snapshot.topState == TopState::RUNNING || snapshot.waveOutputActive) {
    return FirmwareOtaError::BUSY_RUNNING;
  }
  if (snapshot.topState == TopState::FAULT_STOP) {
    return FirmwareOtaError::INVALID_STATE;
  }
  return FirmwareOtaError::NONE;
}

void FirmwareOtaManager::fail(FirmwareOtaError error, const char* detail) {
  const FirmwareOtaState previousState = otaState;
  abortUpdateIfNeeded(previousState, detail);
  lastError = error;
  otaState = FirmwareOtaState::FAILED;
  Serial.printf("[OTA] event=error code=%s detail=%s previous_state=%s received=%u size=%u\n",
      errorCodeName(error),
      detail ? detail : "unknown",
      stateName(previousState),
      static_cast<unsigned>(receivedBytes),
      static_cast<unsigned>(targetSize));
  String json = "{\"type\":\"ota_error\",\"code\":\"";
  json += errorCodeName(error);
  json += "\",\"message\":\"";
  json += detail ? detail : "unknown";
  json += "\",\"state\":\"FAILED\"}";
  emitStatus(json);
}

void FirmwareOtaManager::abortUpdateIfNeeded(FirmwareOtaState previousState, const char* detail) {
  if (!shouldAbortUpdateOnFailure(previousState)) {
    return;
  }
  if (otaHandleActive) {
    const esp_err_t abortErr = esp_ota_abort(otaHandle);
    Serial.printf("[OTA] event=esp_ota_abort result=%s err=%d\n",
        abortErr == ESP_OK ? "success" : "failure",
        static_cast<int>(abortErr));
    otaHandleActive = false;
    otaHandle = 0;
  }
  Serial.printf("[OTA] event=abort_update reason=%s previous_state=%s received=%u size=%u\n",
      detail ? detail : "unknown",
      stateName(previousState),
      static_cast<unsigned>(receivedBytes),
      static_cast<unsigned>(targetSize));
}

bool FirmwareOtaManager::shouldAbortUpdateOnFailure(FirmwareOtaState state) {
  return state == FirmwareOtaState::PREPARED ||
      state == FirmwareOtaState::RECEIVING ||
      state == FirmwareOtaState::VERIFYING;
}

void FirmwareOtaManager::emitStatus(const String& json) {
  if (sink) {
    sink->notifyOtaStatus(json);
  }
}

String FirmwareOtaManager::baseStatusJson() const {
  String json = "{\"type\":\"ota_status\",\"st\":\"";
  json += stateName(otaState);
  json += "\",\"bd\":\"";
  json += FW_BOARD_ID;
  json += "\",\"v\":\"";
  json += escapeJson(targetVersion);
  json += "\",\"b\":\"";
  json += escapeJson(targetBuildId);
  json += "\",\"r\":";
  json += static_cast<unsigned long>(receivedBytes);
  json += ",\"s\":";
  json += static_cast<unsigned long>(targetSize);
  json += ",\"c\":";
  json += static_cast<unsigned long>(negotiatedChunkSize);
  json += ",\"m\":\"";
  json += transferModeName(transferMode);
  json += "\",\"sm\":[\"wr\",\"wc\"]";
  json += ",\"w\":";
  json += static_cast<unsigned long>(negotiatedWindowSize);
  json += ",\"i\":";
  json += static_cast<unsigned long>(negotiatedMaxInflightChunks);
  json += ",\"ap\":\"";
  json += escapeJson(negotiatedAckPolicy);
  json += "\",\"bp\":\"";
  json += escapeJson(negotiatedBusyPolicy);
  json += "\"";
  json += ",\"a\":";
  json += static_cast<unsigned long>(negotiatedAckIntervalChunks);
  json += ",\"n\":";
  json += static_cast<unsigned long>(expectedSeq);
  json += ",\"e\":";
  json += static_cast<unsigned long>(receivedBytes);
  json += "}";
  return json;
}

String FirmwareOtaManager::compactStateStatusJson() const {
  String json = "{\"type\":\"ota_status\",\"st\":\"";
  json += stateName(otaState);
  json += "\",\"r\":";
  json += static_cast<unsigned long>(receivedBytes);
  json += ",\"s\":";
  json += static_cast<unsigned long>(targetSize);
  json += "}";
  return json;
}

String FirmwareOtaManager::evidenceStatusJson() const {
  String json = "{\"type\":\"ota_status\",\"st\":\"";
  json += stateName(otaState);
  json += "\"";
  json += compactPartitionJson("run", runningPartition ? runningPartition : esp_ota_get_running_partition());
  json += compactPartitionJson("boot", bootPartition ? bootPartition : esp_ota_get_boot_partition());
  json += compactPartitionJson("upd", updatePartition);
  const bool includeRollback = FirmwareRollbackConfirmation::hasEvidence() &&
      (otaState == FirmwareOtaState::IDLE || otaState == FirmwareOtaState::FAILED);
  if (includeRollback) {
    const String rollbackEvidence = FirmwareRollbackConfirmation::evidenceJson();
    json += ",";
    json += rollbackEvidence;
  }
  json += "}";
  return json;
}

void FirmwareOtaManager::emitStateStatus() {
  emitStatus(compactStateStatusJson());
}

void FirmwareOtaManager::emitQueryStatus() {
  emitStatus(evidenceStatusJson());
}

void FirmwareOtaManager::emitProgressIfNeeded(bool force) {
  const uint32_t percent = targetSize == 0 ? 0 : (receivedBytes * 100UL / targetSize);
  if (!force && percent == lastProgressPercent && receivedBytes < targetSize) {
    return;
  }
  lastProgressPercent = percent;
  String json = "{\"type\":\"ota_progress\",\"st\":\"RECEIVING\",\"r\":";
  json += static_cast<unsigned long>(receivedBytes);
  json += ",\"s\":";
  json += static_cast<unsigned long>(targetSize);
  json += ",\"p\":";
  json += static_cast<unsigned long>(percent);
  json += "}";
  emitStatus(json);
}

void FirmwareOtaManager::emitWindowAckIfNeeded(bool force) {
  if (otaState != FirmwareOtaState::RECEIVING || expectedSeq == 0) return;
  const bool intervalReached =
      negotiatedAckIntervalChunks > 0 && (expectedSeq % negotiatedAckIntervalChunks == 0);
  const bool windowReached =
      negotiatedWindowSize > 0 && (expectedSeq % negotiatedWindowSize == 0);
  if (!force && !intervalReached && !windowReached && receivedBytes < targetSize) {
    return;
  }
  if (!force && lastAckSeq == expectedSeq && receivedBytes < targetSize) {
    return;
  }
  lastAckSeq = expectedSeq;
  String json = "{\"type\":\"ota_window_ack\",\"m\":\"";
  json += transferModeName(transferMode);
  json += "\",\"as\":";
  json += static_cast<unsigned long>(expectedSeq == 0 ? 0 : expectedSeq - 1);
  json += ",\"n\":";
  json += static_cast<unsigned long>(expectedSeq);
  json += ",\"r\":";
  json += static_cast<unsigned long>(receivedBytes);
  json += ",\"e\":";
  json += static_cast<unsigned long>(receivedBytes);
  json += ",\"w\":";
  json += static_cast<unsigned long>(negotiatedWindowSize);
  json += ",\"i\":";
  json += static_cast<unsigned long>(negotiatedMaxInflightChunks);
  json += ",\"ap\":\"";
  json += escapeJson(negotiatedAckPolicy);
  json += "\",\"bp\":\"";
  json += escapeJson(negotiatedBusyPolicy);
  json += "\"";
  json += ",\"a\":";
  json += static_cast<unsigned long>(negotiatedAckIntervalChunks);
  json += "}";
  emitStatus(json);
}

void FirmwareOtaManager::resetSession() {
  otaState = FirmwareOtaState::IDLE;
  lastError = FirmwareOtaError::NONE;
  targetVersion = "";
  targetBuildId = "";
  targetSha256 = "";
  targetSize = 0;
  negotiatedChunkSize = OTA_DEFAULT_CHUNK_SIZE;
  transferMode = FirmwareOtaTransferMode::WRITE_REQUEST;
  negotiatedWindowSize = OTA_DEFAULT_WINDOW_SIZE;
  negotiatedAckIntervalChunks = OTA_DEFAULT_ACK_INTERVAL_CHUNKS;
  negotiatedMaxInflightChunks = OTA_DEFAULT_WINDOW_SIZE;
  negotiatedAckPolicy = "window_ack";
  negotiatedBusyPolicy = "retry_same_chunk";
  expectedSeq = 0;
  receivedBytes = 0;
  lastProgressPercent = 0;
  lastAckSeq = 0;
  otaHandle = 0;
  runningPartition = nullptr;
  updatePartition = nullptr;
  bootPartition = nullptr;
  otaHandleActive = false;
  resetSha256();
}

void FirmwareOtaManager::resetSha256() {
  if (shaStarted) {
    mbedtls_sha256_free(&shaContext);
    shaStarted = false;
  }
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts(&shaContext, 0);
  shaStarted = true;
}

void FirmwareOtaManager::updateSha256(const uint8_t* data, size_t length) {
  if (!shaStarted) {
    resetSha256();
  }
  mbedtls_sha256_update(&shaContext, data, length);
}

bool FirmwareOtaManager::finishSha256Matches(const String& expected) {
  if (!shaStarted || !isHexSha256(expected)) return false;
  uint8_t digest[32]{};
  mbedtls_sha256_finish(&shaContext, digest);
  mbedtls_sha256_free(&shaContext);
  shaStarted = false;
  char actual[65]{};
  for (size_t i = 0; i < sizeof(digest); ++i) {
    snprintf(actual + (i * 2), 3, "%02x", digest[i]);
  }
  return expected.equalsIgnoreCase(actual);
}

const char* FirmwareOtaManager::stateName(FirmwareOtaState state) {
  switch (state) {
    case FirmwareOtaState::IDLE: return "IDLE";
    case FirmwareOtaState::PREPARED: return "PREPARED";
    case FirmwareOtaState::RECEIVING: return "RECEIVING";
    case FirmwareOtaState::VERIFYING: return "VERIFYING";
    case FirmwareOtaState::READY_TO_REBOOT: return "READY_TO_REBOOT";
    case FirmwareOtaState::REBOOTING: return "REBOOTING";
    case FirmwareOtaState::FAILED: return "FAILED";
  }
  return "FAILED";
}

const char* FirmwareOtaManager::errorCodeName(FirmwareOtaError error) {
  switch (error) {
    case FirmwareOtaError::NONE: return "NONE";
    case FirmwareOtaError::BUSY_RUNNING: return "BUSY_RUNNING";
    case FirmwareOtaError::UNSUPPORTED_BOARD: return "UNSUPPORTED_BOARD";
    case FirmwareOtaError::IMAGE_TOO_LARGE: return "IMAGE_TOO_LARGE";
    case FirmwareOtaError::INVALID_METADATA: return "INVALID_METADATA";
    case FirmwareOtaError::INVALID_STATE: return "INVALID_STATE";
    case FirmwareOtaError::SEQ_MISMATCH: return "SEQ_MISMATCH";
    case FirmwareOtaError::OFFSET_MISMATCH: return "OFFSET_MISMATCH";
    case FirmwareOtaError::WRITE_FAILED: return "WRITE_FAILED";
    case FirmwareOtaError::VERIFY_FAILED: return "VERIFY_FAILED";
    case FirmwareOtaError::BOOT_SWITCH_FAILED: return "BOOT_SWITCH_FAILED";
    case FirmwareOtaError::BLE_DISCONNECTED: return "BLE_DISCONNECTED";
    case FirmwareOtaError::USER_CANCEL: return "USER_CANCEL";
    case FirmwareOtaError::UNSUPPORTED_COMMAND: return "UNSUPPORTED_COMMAND";
    case FirmwareOtaError::UNSUPPORTED_TRANSFER_MODE: return "UNSUPPORTED_TRANSFER_MODE";
    case FirmwareOtaError::GATT_BUSY: return "GATT_BUSY";
    case FirmwareOtaError::RX_QUEUE_FULL: return "RX_QUEUE_FULL";
    case FirmwareOtaError::SEQ_DUPLICATE_UNSUPPORTED: return "SEQ_DUPLICATE_UNSUPPORTED";
    case FirmwareOtaError::WINDOW_TIMEOUT: return "WINDOW_TIMEOUT";
    case FirmwareOtaError::WINDOW_OVERFLOW: return "WINDOW_OVERFLOW";
    case FirmwareOtaError::VERSION_CONFIRM_FAILED: return "VERSION_CONFIRM_FAILED";
    case FirmwareOtaError::MANIFEST_SIGNATURE_INVALID: return "MANIFEST_SIGNATURE_INVALID";
    case FirmwareOtaError::MANIFEST_EXPIRED: return "MANIFEST_EXPIRED";
    case FirmwareOtaError::ROLLBACK_CONFIRM_FAILED: return "ROLLBACK_CONFIRM_FAILED";
    case FirmwareOtaError::INTERNAL_ERROR: return "INTERNAL_ERROR";
  }
  return "INTERNAL_ERROR";
}

bool FirmwareOtaManager::parseBeginRequest(const String& rawJson, FirmwareOtaBeginRequest& out, FirmwareOtaError& error) {
  out.protocol = static_cast<uint16_t>(readJsonLong(rawJson, "protocol", readJsonLong(rawJson, "p", -1)));
  out.version = readJsonString(rawJson, "version");
  if (out.version.length() == 0) out.version = readJsonString(rawJson, "v");
  out.buildId = readJsonString(rawJson, "build_id");
  if (out.buildId.length() == 0) out.buildId = readJsonString(rawJson, "b");
  out.board = readJsonString(rawJson, "board");
  if (out.board.length() == 0) out.board = readJsonString(rawJson, "bd");
  out.size = static_cast<size_t>(readJsonLong(rawJson, "size", readJsonLong(rawJson, "s", -1)));
  out.sha256 = readJsonString(rawJson, "sha256");
  if (out.sha256.length() == 0) out.sha256 = readJsonString(rawJson, "h");
  out.chunkSize = static_cast<uint16_t>(readJsonLong(rawJson, "chunk_size", readJsonLong(rawJson, "c", OTA_DEFAULT_CHUNK_SIZE)));
  bool transferModeSupported = true;
  String transferModeRaw = readJsonString(rawJson, "transfer_mode");
  if (transferModeRaw.length() == 0) transferModeRaw = readJsonString(rawJson, "m");
  out.transferMode = parseTransferMode(transferModeRaw, transferModeSupported);
  out.windowSize = static_cast<uint8_t>(readJsonLong(rawJson, "window_size", readJsonLong(rawJson, "w", OTA_DEFAULT_WINDOW_SIZE)));
  out.ackIntervalChunks = static_cast<uint16_t>(readJsonLong(rawJson, "ack_interval_chunks", readJsonLong(rawJson, "a", OTA_DEFAULT_ACK_INTERVAL_CHUNKS)));
  out.maxInflightChunks = static_cast<uint8_t>(readJsonLong(rawJson, "max_inflight_chunks", readJsonLong(rawJson, "i", out.windowSize)));
  out.ackPolicy = readJsonString(rawJson, "ack_policy");
  if (out.ackPolicy.length() == 0) out.ackPolicy = readJsonString(rawJson, "ap");
  if (out.ackPolicy == "wa") out.ackPolicy = "window_ack";
  if (out.ackPolicy.length() == 0) out.ackPolicy = "window_ack";
  out.busyPolicy = readJsonString(rawJson, "busy_policy");
  if (out.busyPolicy.length() == 0) out.busyPolicy = readJsonString(rawJson, "bp");
  if (out.busyPolicy == "rsc") out.busyPolicy = "retry_same_chunk";
  if (out.busyPolicy.length() == 0) out.busyPolicy = "retry_same_chunk";
  if (!transferModeSupported) {
    error = FirmwareOtaError::UNSUPPORTED_TRANSFER_MODE;
    return false;
  }
  if (out.protocol == 0 || out.version.length() == 0 || out.buildId.length() == 0 ||
      out.board.length() == 0 || out.size == 0 || !isHexSha256(out.sha256) || out.chunkSize == 0 ||
      out.windowSize == 0 || out.maxInflightChunks == 0 || out.ackIntervalChunks == 0 ||
      out.ackPolicy != "window_ack" || out.busyPolicy != "retry_same_chunk") {
    error = FirmwareOtaError::INVALID_METADATA;
    return false;
  }
  error = FirmwareOtaError::NONE;
  return true;
}

bool FirmwareOtaManager::parseDataFrame(const uint8_t* data, size_t length, FirmwareOtaDataFrame& out) {
  if (!data || length < 10) return false;
  const uint32_t seq =
      static_cast<uint32_t>(data[0]) |
      (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24);
  const uint32_t offset =
      static_cast<uint32_t>(data[4]) |
      (static_cast<uint32_t>(data[5]) << 8) |
      (static_cast<uint32_t>(data[6]) << 16) |
      (static_cast<uint32_t>(data[7]) << 24);
  const uint16_t payloadLen =
      static_cast<uint16_t>(data[8]) |
      (static_cast<uint16_t>(data[9]) << 8);
  if (payloadLen == 0 || static_cast<size_t>(payloadLen) + 10 != length) return false;
  out.seq = seq;
  out.offset = offset;
  out.length = payloadLen;
  out.payload = data + 10;
  return true;
}

bool FirmwareOtaManager::isHexSha256(const String& value) {
  if (value.length() != 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(value.c_str()[i]))) return false;
  }
  return true;
}

String FirmwareOtaManager::readJsonString(const String& rawJson, const char* key) {
  String needle = "\"";
  needle += key;
  needle += "\"";
  int keyIndex = rawJson.indexOf(needle);
  if (keyIndex < 0) return "";
  int colon = rawJson.indexOf(':', keyIndex + needle.length());
  if (colon < 0) return "";
  int firstQuote = rawJson.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";
  int secondQuote = rawJson.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";
  return rawJson.substring(firstQuote + 1, secondQuote);
}

long FirmwareOtaManager::readJsonLong(const String& rawJson, const char* key, long fallback) {
  String needle = "\"";
  needle += key;
  needle += "\"";
  int keyIndex = rawJson.indexOf(needle);
  if (keyIndex < 0) return fallback;
  int colon = rawJson.indexOf(':', keyIndex + needle.length());
  if (colon < 0) return fallback;
  int start = colon + 1;
  while (start < static_cast<int>(rawJson.length()) && rawJson.c_str()[start] == ' ') start += 1;
  int end = start;
  while (end < static_cast<int>(rawJson.length()) && isdigit(static_cast<unsigned char>(rawJson.c_str()[end]))) {
    end += 1;
  }
  if (end == start) return fallback;
  return strtol(rawJson.substring(start, end).c_str(), nullptr, 10);
}

FirmwareOtaError FirmwareOtaManager::parseAbortReason(const String& rawJson) {
  const String reason = readJsonString(rawJson, "reason");
  if (reason == "BLE_DISCONNECTED") return FirmwareOtaError::BLE_DISCONNECTED;
  if (reason == "USER_CANCEL" || reason.length() == 0) return FirmwareOtaError::USER_CANCEL;
  return FirmwareOtaError::USER_CANCEL;
}

FirmwareOtaTransferMode FirmwareOtaManager::parseTransferMode(const String& value, bool& supported) {
  if (value.length() == 0 || value == "write_request" || value == "wr") {
    supported = true;
    return FirmwareOtaTransferMode::WRITE_REQUEST;
  }
  if (value == "write_command" || value == "wc") {
    supported = true;
    return FirmwareOtaTransferMode::WRITE_COMMAND;
  }
  supported = false;
  return FirmwareOtaTransferMode::WRITE_REQUEST;
}

const char* FirmwareOtaManager::transferModeName(FirmwareOtaTransferMode mode) {
  switch (mode) {
    case FirmwareOtaTransferMode::WRITE_REQUEST:
      return "wr";
    case FirmwareOtaTransferMode::WRITE_COMMAND:
      return "wc";
  }
  return "wr";
}

String FirmwareOtaManager::partitionJson(const char* key, const esp_partition_t* partition) {
  if (!key || !partition) return "";
  String json = ",\"";
  json += key;
  json += "\":{\"label\":\"";
  json += escapeJson(partition->label);
  json += "\",\"subtype\":";
  json += static_cast<unsigned long>(partition->subtype);
  json += ",\"addr\":";
  json += static_cast<unsigned long>(partition->address);
  json += ",\"size\":";
  json += static_cast<unsigned long>(partition->size);
  json += "}";
  return json;
}

String FirmwareOtaManager::compactPartitionJson(const char* key, const esp_partition_t* partition) {
  if (!key || !partition) return "";
  String json = ",\"";
  json += key;
  json += "\":{\"l\":\"";
  json += escapeJson(partition->label);
  json += "\",\"s\":";
  json += static_cast<unsigned long>(partition->subtype);
  json += ",\"a\":";
  json += static_cast<unsigned long>(partition->address);
  json += "}";
  return json;
}
