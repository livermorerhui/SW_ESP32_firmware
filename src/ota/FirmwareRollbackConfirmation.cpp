#include "FirmwareRollbackConfirmation.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

namespace {

struct RollbackEvidence {
  bool hasEvidence = false;
  String event;
  String result;
  String reason;
  String partition;
  uint32_t subtype = 0;
  uint32_t address = 0;
  uint32_t freeHeap = 0;
};

RollbackEvidence& evidence() {
  static RollbackEvidence value;
  return value;
}

void setEvidence(const String& event, const String& result, const String& reason, const esp_partition_t* running, uint32_t freeHeap = 0) {
  auto& slot = evidence();
  slot.hasEvidence = true;
  slot.event = event;
  slot.result = result;
  slot.reason = reason;
  slot.partition = running ? running->label : "";
  slot.subtype = running ? static_cast<uint32_t>(running->subtype) : 0;
  slot.address = running ? static_cast<uint32_t>(running->address) : 0;
  slot.freeHeap = freeHeap;
}

bool startupSelfCheckPasses(const esp_partition_t* running) {
  if (!running) {
    setEvidence("self_check", "failure", "running_partition_unavailable", running);
    Serial.println("[OTA_ROLLBACK] event=self_check result=failure reason=running_partition_unavailable");
    return false;
  }
  const size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (freeHeap < 32U * 1024U) {
    setEvidence("self_check", "failure", "low_heap", running, static_cast<uint32_t>(freeHeap));
    Serial.printf("[OTA_ROLLBACK] event=self_check result=failure reason=low_heap free_heap=%u partition=%s\n",
        static_cast<unsigned>(freeHeap),
        running->label);
    return false;
  }
  setEvidence("self_check", "pass", "", running, static_cast<uint32_t>(freeHeap));
  Serial.printf("[OTA_ROLLBACK] event=self_check result=pass partition=%s subtype=%u addr=0x%lx free_heap=%u\n",
      running->label,
      static_cast<unsigned>(running->subtype),
      static_cast<unsigned long>(running->address),
      static_cast<unsigned>(freeHeap));
  return true;
}

}  // namespace

void FirmwareRollbackConfirmation::resetEvidence() {
  evidence() = RollbackEvidence{};
}

bool FirmwareRollbackConfirmation::hasEvidence() {
  return evidence().hasEvidence;
}

String FirmwareRollbackConfirmation::evidenceJson() {
  const auto& slot = evidence();
  if (!slot.hasEvidence) {
    return "";
  }
  String json = "\"rb\":{\"e\":\"";
  json += slot.event;
  json += "\",\"r\":\"";
  json += slot.result;
  json += "\"";
  if (slot.reason.length() > 0) {
    json += ",\"rs\":\"";
    json += slot.reason;
    json += "\"";
  }
  if (slot.partition.length() > 0) {
    json += ",\"p\":\"";
    json += slot.partition;
    json += "\"";
  }
  if (slot.subtype > 0) {
    json += ",\"s\":";
    json += static_cast<unsigned long>(slot.subtype);
  }
  if (slot.address > 0) {
    json += ",\"a\":";
    json += static_cast<unsigned long>(slot.address);
  }
  if (slot.freeHeap > 0) {
    json += ",\"h\":";
    json += static_cast<unsigned long>(slot.freeHeap);
  }
  json += "}";
  return json;
}

void FirmwareRollbackConfirmation::confirmIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    setEvidence("skip", "failure", "running_partition_unavailable", running);
    Serial.println("[OTA_ROLLBACK] event=skip reason=running_partition_unavailable");
    return;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateErr = esp_ota_get_state_partition(running, &state);
  if (stateErr != ESP_OK) {
    setEvidence("skip", "failure", "get_state_failed", running);
    Serial.printf("[OTA_ROLLBACK] event=skip reason=get_state_failed err=%d\n", static_cast<int>(stateErr));
    return;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    setEvidence("skip", "not_pending", "", running);
    Serial.printf("[OTA_ROLLBACK] event=skip reason=not_pending state=%d\n", static_cast<int>(state));
    return;
  }

  if (!startupSelfCheckPasses(running)) {
    setEvidence("mark_valid", "skipped", "self_check_failed", running);
    Serial.println("[OTA_ROLLBACK] event=mark_valid result=skipped reason=self_check_failed code=ROLLBACK_CONFIRM_FAILED");
    return;
  }

  const esp_err_t markErr = esp_ota_mark_app_valid_cancel_rollback();
  if (markErr == ESP_OK) {
    setEvidence("mark_valid", "success", "", running);
    Serial.println("[OTA_ROLLBACK] event=mark_valid result=success");
  } else {
    setEvidence("mark_valid", "failure", "mark_valid_failed", running);
    Serial.printf("[OTA_ROLLBACK] event=mark_valid result=failure err=%d\n", static_cast<int>(markErr));
  }
}
