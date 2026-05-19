#include "FirmwareRollbackConfirmation.h"
#include <Arduino.h>
#include <esp_ota_ops.h>

void FirmwareRollbackConfirmation::confirmIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    Serial.println("[OTA_ROLLBACK] event=skip reason=running_partition_unavailable");
    return;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateErr = esp_ota_get_state_partition(running, &state);
  if (stateErr != ESP_OK) {
    Serial.printf("[OTA_ROLLBACK] event=skip reason=get_state_failed err=%d\n", static_cast<int>(stateErr));
    return;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    Serial.printf("[OTA_ROLLBACK] event=skip reason=not_pending state=%d\n", static_cast<int>(state));
    return;
  }

  const esp_err_t markErr = esp_ota_mark_app_valid_cancel_rollback();
  if (markErr == ESP_OK) {
    Serial.println("[OTA_ROLLBACK] event=mark_valid result=success");
  } else {
    Serial.printf("[OTA_ROLLBACK] event=mark_valid result=failure err=%d\n", static_cast<int>(markErr));
  }
}
