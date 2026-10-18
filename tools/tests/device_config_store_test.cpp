#include <cassert>
#include <cstdint>
#include <cstdio>

#include "modules/laser/DeviceConfigStore.h"

namespace {

uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }
  return ~crc;
}

void refreshCrc(std::vector<uint8_t>& record) {
  assert(record.size() == 20);
  const uint32_t crc = crc32(record.data(), 16);
  for (unsigned index = 0; index < 4; ++index) {
    record[16 + index] = static_cast<uint8_t>(crc >> (8 * index));
  }
}

DeviceConfigSaveResult writeJ100(Preferences& preferences, DeviceConfigStore& store) {
  const DeviceConfigLoadResult blank = store.load(preferences, true);
  assert(blank.status == DeviceConfigStoreStatus::BLANK);
  const DeviceConfigSaveResult saved = store.save(
      preferences, true, blank.config,
      PlatformModel::PLUS, SensorProfileId::J100_485);
  assert(saved.ok());
  assert(saved.changed);
  assert(saved.config.generation == 1);
  return saved;
}

void testBlankDefaultAndExplicitProfile() {
  Preferences preferences;
  DeviceConfigStore store;
  const DeviceConfigLoadResult blank = store.load(preferences, true);
  assert(blank.status == DeviceConfigStoreStatus::BLANK);
  assert(blank.config.platformModel == PlatformModel::BASE);
  assert(blank.config.sensorProfile == SensorProfileId::NONE);
  assert(blank.config.generation == 0);
  assert(preferences.values.empty());  // Boot must not erase old no-key evidence.

  const DeviceConfigSaveResult j100 = writeJ100(preferences, store);
  assert(preferences.getUChar("cfg_model", 0xFF) == 1);
  assert(preferences.getUChar("cfg_laser", 0xFF) == 1);
  assert(preferences.getBytesLength("cfg_slot0") == 20);
  assert(preferences.getBytesLength("cfg_slot1") == 20);
  assert(preferences.values["cfg_slot1"].bytes[6] == 1);  // Not old internal J100=0.

  const DeviceConfigLoadResult restart = store.load(preferences, true);
  assert(restart.status == DeviceConfigStoreStatus::READY);
  assert(restart.config.platformModel == PlatformModel::PLUS);
  assert(restart.config.sensorProfile == SensorProfileId::J100_485);
  assert(restart.config.generation == j100.config.generation);
  assert(restart.config.schema1Managed);

  const int writesBeforeIdempotent = preferences.putCalls;
  const DeviceConfigSaveResult same = store.save(
      preferences, true, restart.config,
      PlatformModel::PLUS, SensorProfileId::J100_485);
  assert(same.ok() && !same.changed);
  assert(preferences.putCalls == writesBeforeIdempotent);
}

void testExplicitSameValueMaterializesFactoryBase() {
  DeviceConfigStore store;
  Preferences preferences;
  const DeviceConfigLoadResult initial = store.load(preferences, true);
  assert(initial.status == DeviceConfigStoreStatus::BLANK);
  assert(preferences.values.empty());

  const DeviceConfigSaveResult saved = store.save(
      preferences, true, initial.config,
      PlatformModel::BASE, SensorProfileId::NONE);
  assert(saved.ok() && !saved.changed);
  assert(saved.config.schema1Managed);
  assert(saved.config.generation == 0);
  assert(preferences.getUChar("cfg_model", 0xFF) == 0);
  assert(preferences.getUChar("cfg_laser", 0xFF) == 0);
  assert(preferences.getBytesLength("cfg_slot0") == 20);
  assert(!preferences.isKey("cfg_slot1"));

  const DeviceConfigLoadResult reboot = store.load(preferences, true);
  assert(reboot.status == DeviceConfigStoreStatus::READY);
  assert(reboot.config.platformModel == PlatformModel::BASE);
  assert(reboot.config.sensorProfile == SensorProfileId::NONE);
  assert(reboot.config.generation == 0);
  assert(reboot.config.schema1Managed);

  const int writesBeforeRepeat = preferences.putCalls;
  const DeviceConfigSaveResult repeat = store.save(
      preferences, true, reboot.config,
      PlatformModel::BASE, SensorProfileId::NONE);
  assert(repeat.ok() && !repeat.changed);
  assert(preferences.putCalls == writesBeforeRepeat);
}

void testExplicitSameValueMaterializesLegacyPlus() {
  DeviceConfigStore store;
  Preferences preferences;
  preferences.putUChar("cfg_model", 1);
  const DeviceConfigLoadResult old = store.load(preferences, true);
  assert(old.status == DeviceConfigStoreStatus::LEGACY);
  assert(!old.config.schema1Managed);

  const DeviceConfigSaveResult saved = store.save(
      preferences, true, old.config,
      PlatformModel::PLUS, SensorProfileId::NONE);
  assert(saved.ok() && !saved.changed);
  assert(saved.config.schema1Managed);
  assert(saved.config.generation == 0);
  assert(preferences.getUChar("cfg_model", 0xFF) == 1);
  assert(preferences.getUChar("cfg_laser", 0xFF) == 1);
  const DeviceConfigLoadResult reboot = store.load(preferences, true);
  assert(reboot.status == DeviceConfigStoreStatus::READY);
  assert(reboot.config.platformModel == PlatformModel::PLUS);
  assert(reboot.config.sensorProfile == SensorProfileId::NONE);
  assert(reboot.config.generation == 0);

  const int writesBeforeRepeat = preferences.putCalls;
  assert(store.save(preferences, true, reboot.config,
                    PlatformModel::PLUS, SensorProfileId::NONE).ok());
  assert(preferences.putCalls == writesBeforeRepeat);
}

void testExplicitSameValueMaterializesLegacyBaseAndNormalizesMirror() {
  DeviceConfigStore store;
  Preferences preferences;
  preferences.putUChar("cfg_model", 0);
  preferences.putUChar("cfg_laser", 1);  // Old, inconsistent compatibility bit.
  const DeviceConfigLoadResult old = store.load(preferences, true);
  assert(old.status == DeviceConfigStoreStatus::LEGACY);
  assert(old.laserInstalledNormalized);

  const DeviceConfigSaveResult saved = store.save(
      preferences, true, old.config,
      PlatformModel::BASE, SensorProfileId::NONE);
  assert(saved.ok() && !saved.changed);
  assert(saved.config.generation == 0 && saved.config.schema1Managed);
  assert(preferences.getUChar("cfg_model", 0xFF) == 0);
  assert(preferences.getUChar("cfg_laser", 0xFF) == 0);
  const DeviceConfigLoadResult reboot = store.load(preferences, true);
  assert(reboot.status == DeviceConfigStoreStatus::READY);
  assert(reboot.config.platformModel == PlatformModel::BASE);
  assert(reboot.config.sensorProfile == SensorProfileId::NONE);
}

void testSameValueMaterializationCutpointsAndBaselineRecovery() {
  DeviceConfigStore store;
  for (int cut = 1; cut <= 4; ++cut) {
    Preferences preferences;
    const DeviceConfigLoadResult initial = store.load(preferences, true);
    preferences.failPutAtCall = preferences.putCalls + cut;
    const DeviceConfigSaveResult failed = store.save(
        preferences, true, initial.config,
        PlatformModel::BASE, SensorProfileId::NONE);
    assert(!failed.ok());
    const DeviceConfigLoadResult reboot = store.load(preferences, true);
    assert(reboot.status == DeviceConfigStoreStatus::BLANK ||
           reboot.status == DeviceConfigStoreStatus::LEGACY ||
           reboot.status == DeviceConfigStoreStatus::CONFIG_ERROR);
    if (reboot.status != DeviceConfigStoreStatus::CONFIG_ERROR) {
      assert(reboot.config.platformModel == PlatformModel::BASE);
    }
  }

  for (int cut = 1; cut <= 3; ++cut) {
    Preferences preferences;
    preferences.putUChar("cfg_model", 1);
    const DeviceConfigLoadResult initial = store.load(preferences, true);
    assert(initial.status == DeviceConfigStoreStatus::LEGACY);
    preferences.failPutAtCall = preferences.putCalls + cut;
    const DeviceConfigSaveResult failed = store.save(
        preferences, true, initial.config,
        PlatformModel::PLUS, SensorProfileId::NONE);
    assert(!failed.ok());
    const DeviceConfigLoadResult reboot = store.load(preferences, true);
    assert(reboot.status == DeviceConfigStoreStatus::LEGACY ||
           reboot.status == DeviceConfigStoreStatus::CONFIG_ERROR);
  }

  // A changed SET can commit the old identity's baseline before failing to
  // write its target. The caller must receive NACK and can reload that safe
  // baseline, including its schema1Managed=true flag.
  Preferences blankToPlus;
  const DeviceConfigLoadResult blank = store.load(blankToPlus, true);
  blankToPlus.failPutAtCall = blankToPlus.putCalls + 5;
  const DeviceConfigSaveResult failedPlus = store.save(
      blankToPlus, true, blank.config,
      PlatformModel::PLUS, SensorProfileId::J100_485);
  assert(!failedPlus.ok());
  const DeviceConfigLoadResult recoveredBase = store.load(blankToPlus, true);
  assert(recoveredBase.status == DeviceConfigStoreStatus::READY);
  assert(recoveredBase.config.platformModel == PlatformModel::BASE);
  assert(recoveredBase.config.sensorProfile == SensorProfileId::NONE);
  assert(recoveredBase.config.generation == 0);
  assert(recoveredBase.config.schema1Managed);

  Preferences legacyToCyd;
  legacyToCyd.putUChar("cfg_model", 1);
  const DeviceConfigLoadResult legacy = store.load(legacyToCyd, true);
  legacyToCyd.failPutAtCall = legacyToCyd.putCalls + 4;
  const DeviceConfigSaveResult failedCyd = store.save(
      legacyToCyd, true, legacy.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(!failedCyd.ok());
  const DeviceConfigLoadResult recoveredPlus = store.load(legacyToCyd, true);
  assert(recoveredPlus.status == DeviceConfigStoreStatus::READY);
  assert(recoveredPlus.config.platformModel == PlatformModel::PLUS);
  assert(recoveredPlus.config.sensorProfile == SensorProfileId::NONE);
  assert(recoveredPlus.config.generation == 0);
  assert(recoveredPlus.config.schema1Managed);
}

void testLegacyAndOldEvidence() {
  DeviceConfigStore store;
  Preferences oldPlus;
  oldPlus.putUChar("cfg_model", 1);
  const DeviceConfigLoadResult loaded = store.load(oldPlus, true);
  assert(loaded.status == DeviceConfigStoreStatus::LEGACY);
  assert(loaded.config.platformModel == PlatformModel::PLUS);
  assert(loaded.config.sensorProfile == SensorProfileId::NONE);
  assert(!loaded.config.schema1Managed);
  assert(!oldPlus.isKey("cfg_laser"));
  const DeviceConfigSaveResult cyd = store.save(
      oldPlus, true, loaded.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(cyd.ok() && cyd.config.generation == 1);
  assert(oldPlus.getUChar("cfg_model", 0xFF) == 1);
  assert(oldPlus.getUChar("cfg_laser", 0xFF) == 1);
  assert(store.load(oldPlus, true).config.sensorProfile ==
      SensorProfileId::CYD3S1612_IIC);
  assert(oldPlus.values["cfg_slot1"].bytes[6] == 2);

  Preferences missingModel;
  missingModel.putUChar("cfg_laser", 1);
  assert(store.load(missingModel, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);

  Preferences wrongType;
  wrongType.values["cfg_model"] = {PT_BLOB, {1}};
  assert(store.load(wrongType, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);

  Preferences unavailable;
  assert(store.load(unavailable, false).status ==
      DeviceConfigStoreStatus::STORAGE_UNAVAILABLE);
  assert(!store.save(unavailable, false, DeviceConfigSnapshot{},
                     PlatformModel::PLUS, SensorProfileId::J100_485).ok());
  assert(unavailable.values.empty());
}

void testReservedModelPreservedButNewProfileRejected() {
  DeviceConfigStore store;
  Preferences preferences;
  preferences.putUChar("cfg_model", 2);
  preferences.putUChar("cfg_laser", 1);
  const DeviceConfigLoadResult pro = store.load(preferences, true);
  assert(pro.status == DeviceConfigStoreStatus::LEGACY_RESERVED);
  assert(pro.config.platformModel == PlatformModel::PRO);
  assert(store.save(preferences, true, pro.config,
                    PlatformModel::ULTRA, SensorProfileId::NONE).error ==
      DeviceConfigStoreError::UNSUPPORTED_MODEL);
  const DeviceConfigSaveResult ultra = store.saveLegacyReserved(
      preferences, true, pro.config, PlatformModel::ULTRA);
  assert(ultra.ok());
  assert(preferences.getUChar("cfg_model", 0xFF) == 3);
  assert(store.load(preferences, true).status ==
      DeviceConfigStoreStatus::LEGACY_RESERVED);
}

void testValidUnknownSchemaAndProfileBlockFallback() {
  DeviceConfigStore store;
  Preferences futureSchema;
  writeJ100(futureSchema, store);
  auto& newerSchema = futureSchema.values["cfg_slot1"].bytes;
  newerSchema[4] = 2;
  refreshCrc(newerSchema);
  assert(store.load(futureSchema, true).status ==
      DeviceConfigStoreStatus::UNSUPPORTED_SCHEMA);

  Preferences futureProfile;
  writeJ100(futureProfile, store);
  auto& newerProfile = futureProfile.values["cfg_slot1"].bytes;
  newerProfile[6] = 99;
  refreshCrc(newerProfile);
  assert(store.load(futureProfile, true).status ==
      DeviceConfigStoreStatus::UNSUPPORTED_PROFILE);

  Preferences unsupportedFlags;
  writeJ100(unsupportedFlags, store);
  auto& reserved = unsupportedFlags.values["cfg_slot1"].bytes;
  reserved[7] = 1;
  refreshCrc(reserved);
  assert(store.load(unsupportedFlags, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);
}

void testCorruptNewestRecoversOnlyMatchingPrevious() {
  DeviceConfigStore store;
  Preferences preferences;
  const DeviceConfigSaveResult j100 = writeJ100(preferences, store);
  const DeviceConfigSaveResult cyd = store.save(
      preferences, true, j100.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(cyd.ok() && cyd.config.generation == 2);
  preferences.values["cfg_slot0"].bytes[6] ^= 0x01;  // Bad CRC in latest slot.
  const DeviceConfigLoadResult recovered = store.load(preferences, true);
  assert(recovered.status == DeviceConfigStoreStatus::READY);
  assert(recovered.config.sensorProfile == SensorProfileId::J100_485);
  assert(recovered.config.generation == 1);

  preferences.putUChar("cfg_model", 0);  // Cross-key mismatch blocks fallback.
  assert(store.load(preferences, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);
}

void testUnreadableNewestNeverSelectsOldSensor() {
  DeviceConfigStore store;
  Preferences preferences;
  const DeviceConfigSaveResult j100 = writeJ100(preferences, store);
  const DeviceConfigSaveResult cyd = store.save(
      preferences, true, j100.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(cyd.ok() && cyd.config.generation == 2);
  // cfg_slot0 now contains CYD; cfg_slot1 still contains J100. Both share the
  // same legacy model/laser mirror, so read errors must not look like CRC
  // damage and silently reselect J100.
  preferences.failReadKey = "cfg_slot0";
  assert(store.load(preferences, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);
  preferences.failReadKey.clear();

  preferences.failLengthKey = "cfg_slot0";
  assert(store.load(preferences, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);
  preferences.failLengthKey.clear();

  preferences.values["cfg_slot0"].type = PT_U8;
  assert(store.load(preferences, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);
  preferences.values["cfg_slot0"].type = PT_BLOB;

  const DeviceConfigLoadResult intact = store.load(preferences, true);
  assert(intact.status == DeviceConfigStoreStatus::READY);
  assert(intact.config.sensorProfile == SensorProfileId::CYD3S1612_IIC);
}

void testWriteFailuresAndUncertainCrossKeyStateFailClosed() {
  DeviceConfigStore store;
  Preferences pendingWriteFails;
  const DeviceConfigSaveResult j100 = writeJ100(pendingWriteFails, store);
  pendingWriteFails.failNextPutKey = "cfg_slot0";
  const DeviceConfigSaveResult storageFailed = store.save(
      pendingWriteFails, true, j100.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(storageFailed.error == DeviceConfigStoreError::STORAGE_FAILED);
  assert(store.load(pendingWriteFails, true).config.sensorProfile ==
      SensorProfileId::J100_485);

  Preferences verifyFails;
  const DeviceConfigSaveResult j100b = writeJ100(verifyFails, store);
  verifyFails.corruptReadAfterPutKey = "cfg_slot0";
  const DeviceConfigSaveResult verifyFailed = store.save(
      verifyFails, true, j100b.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(verifyFailed.error == DeviceConfigStoreError::VERIFY_FAILED);
  assert(store.load(verifyFails, true).config.sensorProfile ==
      SensorProfileId::J100_485);

  Preferences crossKey;
  const DeviceConfigSaveResult j100c = writeJ100(crossKey, store);
  crossKey.failNextPutKey = "cfg_laser";
  const DeviceConfigSaveResult mirrorFailed = store.save(
      crossKey, true, j100c.config,
      PlatformModel::BASE, SensorProfileId::NONE);
  assert(mirrorFailed.error == DeviceConfigStoreError::STORAGE_FAILED);
  assert(store.load(crossKey, true).status ==
      DeviceConfigStoreStatus::CONFIG_ERROR);
}

void testGenerationOverflowAndInvalidCombinations() {
  DeviceConfigStore store;
  Preferences preferences;
  writeJ100(preferences, store);
  auto& record = preferences.values["cfg_slot1"].bytes;
  record[8] = record[9] = record[10] = record[11] = 0xFF;
  refreshCrc(record);
  const DeviceConfigLoadResult max = store.load(preferences, true);
  assert(max.status == DeviceConfigStoreStatus::READY);
  assert(max.config.generation == UINT32_MAX);
  assert(store.save(preferences, true, max.config,
                    PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC).error ==
      DeviceConfigStoreError::GENERATION_OVERFLOW);
  assert(store.save(preferences, true, max.config,
                    PlatformModel::BASE, SensorProfileId::J100_485).error ==
      DeviceConfigStoreError::INVALID_CONFIG);
}

void testPowerLossCutpoints() {
  DeviceConfigStore store;
  for (int cut = 1; cut <= 4; ++cut) {
    Preferences preferences;
    const DeviceConfigSaveResult j100 = writeJ100(preferences, store);
    preferences.failPutAtCall = preferences.putCalls + cut;
    const DeviceConfigSaveResult failed = store.save(
        preferences, true, j100.config,
        PlatformModel::BASE, SensorProfileId::NONE);
    assert(!failed.ok());
    const DeviceConfigLoadResult reboot = store.load(preferences, true);
    // An interrupted cross-key transition can remain on the old profile or
    // enter CONFIG_ERROR, but it must never silently claim new Base success.
    assert(reboot.status == DeviceConfigStoreStatus::READY ||
           reboot.status == DeviceConfigStoreStatus::CONFIG_ERROR);
    if (reboot.status == DeviceConfigStoreStatus::READY) {
      assert(reboot.config.platformModel == PlatformModel::PLUS);
      assert(reboot.config.sensorProfile == SensorProfileId::J100_485);
    }
  }
}

void testStaleExpectedConfigCannotOverwrite() {
  DeviceConfigStore store;
  Preferences preferences;
  const DeviceConfigSaveResult j100 = writeJ100(preferences, store);
  const DeviceConfigSaveResult cyd = store.save(
      preferences, true, j100.config,
      PlatformModel::PLUS, SensorProfileId::CYD3S1612_IIC);
  assert(cyd.ok());
  const int writesBefore = preferences.putCalls;
  assert(store.save(preferences, true, j100.config,
                    PlatformModel::BASE, SensorProfileId::NONE).error ==
      DeviceConfigStoreError::CONFIG_ERROR);
  assert(preferences.putCalls == writesBefore);
}

}  // namespace

int main() {
  testBlankDefaultAndExplicitProfile();
  testExplicitSameValueMaterializesFactoryBase();
  testExplicitSameValueMaterializesLegacyPlus();
  testExplicitSameValueMaterializesLegacyBaseAndNormalizesMirror();
  testSameValueMaterializationCutpointsAndBaselineRecovery();
  testLegacyAndOldEvidence();
  testReservedModelPreservedButNewProfileRejected();
  testValidUnknownSchemaAndProfileBlockFallback();
  testCorruptNewestRecoversOnlyMatchingPrevious();
  testUnreadableNewestNeverSelectsOldSensor();
  testWriteFailuresAndUncertainCrossKeyStateFailClosed();
  testGenerationOverflowAndInvalidCombinations();
  testPowerLossCutpoints();
  testStaleExpectedConfigCannotOverwrite();
  std::puts("DeviceConfigStore stage B host tests passed");
}
