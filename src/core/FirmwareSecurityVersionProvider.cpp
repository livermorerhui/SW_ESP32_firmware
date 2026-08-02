#include "FirmwareSecurityVersionProvider.h"

#include <esp_efuse.h>
#include <esp_ota_ops.h>

bool FirmwareSecurityVersionProvider::read(FirmwareSecurityVersionFacts& outFacts) {
  const esp_app_desc_t* app = esp_ota_get_app_description();
  if (app == nullptr) return false;

  outFacts.appSecurityVersion = app->secure_version;
  outFacts.efuseSecurityVersion = esp_efuse_read_secure_version();
  outFacts.currentSecurityVersion = outFacts.appSecurityVersion >= outFacts.efuseSecurityVersion
      ? outFacts.appSecurityVersion
      : outFacts.efuseSecurityVersion;
#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
  outFacts.appAntiRollbackGate = true;
#else
  outFacts.appAntiRollbackGate = false;
#endif
  return true;
}
