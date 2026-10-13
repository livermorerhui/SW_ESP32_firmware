#include "modules/laser/DeviceConfigStore.h"

#include <limits.h>
#include <string.h>

namespace {

// Wire/storage IDs are deliberately independent of LaserSensorProfileId.
// Version 1 is exactly 20 bytes, with all multi-byte fields little-endian:
//   0..3 magic, 4 schema, 5 model, 6 sensor, 7 reserved,
//   8..11 generation, 12..15 commit marker, 16..19 CRC32(0..15).
// A future schema can append data before its trailing CRC while retaining the
// fixed envelope.  An intact unknown schema always blocks fallback.
constexpr size_t kRecordLength = 20;
constexpr size_t kMaximumReadableRecordLength = 64;
constexpr uint8_t kMagic[4] = {'S', 'W', 'C', 'F'};
constexpr uint8_t kSchema = 1;
constexpr uint32_t kPendingMarker = 0;
constexpr uint32_t kCommittedMarker = 0x31544D43UL;  // bytes "CMT1"

uint32_t readU32Le(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
      (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24);
}

void writeU32Le(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

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

struct SlotRecord {
  bool exists = false;
  bool readError = false;
  bool validEnvelope = false;
  bool committed = false;
  uint8_t bytes[kMaximumReadableRecordLength]{};
  size_t length = 0;
  uint8_t schema = 0;
  uint8_t model = 0;
  uint8_t profile = 0;
  uint32_t generation = 0;
};

SlotRecord readSlot(Preferences& preferences, const char* key) {
  SlotRecord slot{};
  slot.exists = preferences.isKey(key);
  if (!slot.exists) {
    return slot;
  }
  if (preferences.getType(key) != PT_BLOB) {
    slot.readError = true;
    return slot;
  }
  slot.length = preferences.getBytesLength(key);
  // isKey/getType established that a blob exists. A zero/oversize length or
  // incomplete read is an I/O/format error, not proof that its CRC is bad.
  // In particular, the other slot may contain an older sensor profile whose
  // legacy model/laser mirror is indistinguishable from this slot's mirror.
  if (slot.length == 0 || slot.length > kMaximumReadableRecordLength ||
      preferences.getBytes(key, slot.bytes, slot.length) != slot.length) {
    slot.readError = true;
    return slot;
  }
  // A fully read but truncated or CRC-damaged record may be discarded in
  // favor of a previous committed record, subject to the legacy mirror.
  if (slot.length < kRecordLength) {
    return slot;
  }
  if (memcmp(slot.bytes, kMagic, sizeof(kMagic)) != 0 ||
      readU32Le(slot.bytes + slot.length - 4) !=
          crc32(slot.bytes, slot.length - 4)) {
    return slot;
  }

  slot.schema = slot.bytes[4];
  slot.model = slot.bytes[5];
  slot.profile = slot.bytes[6];
  slot.generation = readU32Le(slot.bytes + 8);
  slot.validEnvelope = true;
  slot.committed = readU32Le(slot.bytes + 12) == kCommittedMarker;
  return slot;
}

void encodeRecord(
    const DeviceConfigSnapshot& config,
    uint32_t marker,
    uint8_t (&bytes)[kRecordLength]) {
  memset(bytes, 0, sizeof(bytes));
  memcpy(bytes, kMagic, sizeof(kMagic));
  bytes[4] = kSchema;
  bytes[5] = static_cast<uint8_t>(config.platformModel);
  bytes[6] = static_cast<uint8_t>(config.sensorProfile);
  writeU32Le(bytes + 8, config.generation);
  writeU32Le(bytes + 12, marker);
  writeU32Le(bytes + 16, crc32(bytes, kRecordLength - 4));
}

DeviceConfigStoreError writeRecord(
    Preferences& preferences,
    const char* key,
    const DeviceConfigSnapshot& config,
    uint32_t marker) {
  uint8_t bytes[kRecordLength]{};
  encodeRecord(config, marker, bytes);
  if (preferences.putBytes(key, bytes, sizeof(bytes)) != sizeof(bytes)) {
    return DeviceConfigStoreError::STORAGE_FAILED;
  }
  if (preferences.getType(key) != PT_BLOB ||
      preferences.getBytesLength(key) != sizeof(bytes)) {
    return DeviceConfigStoreError::VERIFY_FAILED;
  }
  uint8_t persisted[kRecordLength]{};
  if (preferences.getBytes(key, persisted, sizeof(persisted)) != sizeof(persisted) ||
      memcmp(bytes, persisted, sizeof(bytes)) != 0) {
    return DeviceConfigStoreError::VERIFY_FAILED;
  }
  return DeviceConfigStoreError::NONE;
}

enum class MirrorState : uint8_t { BLANK, VALID, ERROR };

struct LegacyMirror {
  MirrorState state = MirrorState::ERROR;
  PlatformModel model = PlatformModel::BASE;
  bool hasLaser = false;
  uint8_t laser = 0xFF;
};

LegacyMirror readLegacyMirror(Preferences& preferences) {
  LegacyMirror mirror{};
  const bool hasModel = preferences.isKey("cfg_model");
  const bool hasLaser = preferences.isKey("cfg_laser");
  if (!hasModel) {
    mirror.state = hasLaser ? MirrorState::ERROR : MirrorState::BLANK;
    return mirror;
  }
  if (preferences.getType("cfg_model") != PT_U8) {
    return mirror;
  }
  mirror.model = static_cast<PlatformModel>(preferences.getUChar("cfg_model", 0xFF));
  if (!isKnownPlatformModel(mirror.model)) {
    return mirror;
  }
  mirror.hasLaser = hasLaser;
  if (hasLaser) {
    if (preferences.getType("cfg_laser") != PT_U8) {
      return mirror;
    }
    mirror.laser = preferences.getUChar("cfg_laser", 0xFF);
    if (mirror.laser > 1) {
      return mirror;
    }
  }
  mirror.state = MirrorState::VALID;
  return mirror;
}

bool mirrorMatches(const LegacyMirror& mirror, PlatformModel model) {
  return mirror.state == MirrorState::VALID && mirror.hasLaser &&
      mirror.model == model &&
      mirror.laser == static_cast<uint8_t>(platformModelImpliesLaserInstalled(model));
}

DeviceConfigStoreError writeMirror(
    Preferences& preferences,
    const DeviceConfigSnapshot& config) {
  const uint8_t model = static_cast<uint8_t>(config.platformModel);
  const uint8_t laser = static_cast<uint8_t>(config.laserInstalled);
  if (preferences.getType("cfg_model") != PT_U8 ||
      preferences.getUChar("cfg_model", 0xFF) != model) {
    if (preferences.putUChar("cfg_model", model) != 1) {
      return DeviceConfigStoreError::STORAGE_FAILED;
    }
  }
  if (preferences.getType("cfg_model") != PT_U8 ||
      preferences.getUChar("cfg_model", 0xFF) != model) {
    return DeviceConfigStoreError::VERIFY_FAILED;
  }
  if (preferences.getType("cfg_laser") != PT_U8 ||
      preferences.getUChar("cfg_laser", 0xFF) != laser) {
    if (preferences.putUChar("cfg_laser", laser) != 1) {
      return DeviceConfigStoreError::STORAGE_FAILED;
    }
  }
  if (preferences.getType("cfg_laser") != PT_U8 ||
      preferences.getUChar("cfg_laser", 0xFF) != laser) {
    return DeviceConfigStoreError::VERIFY_FAILED;
  }
  return DeviceConfigStoreError::NONE;
}

const SlotRecord* selectHighestCommitted(
    const SlotRecord& slot0,
    const SlotRecord& slot1,
    bool& conflict,
    unsigned& selectedIndex) {
  conflict = false;
  if (!slot0.committed && !slot1.committed) {
    return nullptr;
  }
  if (!slot0.committed) {
    selectedIndex = 1;
    return &slot1;
  }
  if (!slot1.committed) {
    selectedIndex = 0;
    return &slot0;
  }
  if (slot0.generation == slot1.generation) {
    conflict = slot0.length != slot1.length ||
        memcmp(slot0.bytes, slot1.bytes, slot0.length) != 0;
    selectedIndex = 0;
    return &slot0;
  }
  selectedIndex = slot0.generation > slot1.generation ? 0 : 1;
  return selectedIndex == 0 ? &slot0 : &slot1;
}

bool sameConfig(const DeviceConfigSnapshot& left, const DeviceConfigSnapshot& right) {
  return left.platformModel == right.platformModel &&
      left.sensorProfile == right.sensorProfile &&
      left.generation == right.generation &&
      left.schema1Managed == right.schema1Managed;
}

}  // namespace

DeviceConfigLoadResult DeviceConfigStore::load(
    Preferences& preferences,
    bool storageOpened) const {
  DeviceConfigLoadResult result{};
  if (!storageOpened) {
    result.status = DeviceConfigStoreStatus::STORAGE_UNAVAILABLE;
    return result;
  }

  const SlotRecord slot0 = readSlot(preferences, kSlot0Key);
  const SlotRecord slot1 = readSlot(preferences, kSlot1Key);
  const LegacyMirror mirror = readLegacyMirror(preferences);
  if (slot0.readError || slot1.readError) {
    result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
    return result;
  }
  bool conflict = false;
  unsigned selectedIndex = 0;
  const SlotRecord* active = selectHighestCommitted(slot0, slot1, conflict, selectedIndex);
  if (conflict) {
    result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
    return result;
  }

  if (active != nullptr) {
    result.config.platformModel = static_cast<PlatformModel>(active->model);
    result.config.sensorProfile = static_cast<SensorProfileId>(active->profile);
    result.config.generation = active->generation;
    result.config.schema1Managed = true;
    result.storedLaserInstalled = mirror.laser;

    // A complete future record is authoritative even if the other slot and
    // the old mirror look usable.  Never turn it into an older profile.
    if (active->schema != kSchema || active->length != kRecordLength) {
      result.status = DeviceConfigStoreStatus::UNSUPPORTED_SCHEMA;
      return result;
    }
    if (active->bytes[7] != 0) {
      result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
      return result;
    }
    if (!isKnownSensorProfile(result.config.sensorProfile)) {
      result.status = DeviceConfigStoreStatus::UNSUPPORTED_PROFILE;
      return result;
    }
    if (!isKnownPlatformModel(result.config.platformModel)) {
      result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
      return result;
    }
    result.config.laserInstalled =
        platformModelImpliesLaserInstalled(result.config.platformModel);
    const bool validCombination =
        (result.config.platformModel == PlatformModel::BASE &&
            result.config.sensorProfile == SensorProfileId::NONE) ||
        (result.config.platformModel == PlatformModel::PLUS) ||
        ((result.config.platformModel == PlatformModel::PRO ||
            result.config.platformModel == PlatformModel::ULTRA) &&
            result.config.sensorProfile == SensorProfileId::NONE);
    if (!validCombination ||
        !mirrorMatches(mirror, result.config.platformModel)) {
      result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
      return result;
    }
    result.status = (result.config.platformModel == PlatformModel::PRO ||
                     result.config.platformModel == PlatformModel::ULTRA)
        ? DeviceConfigStoreStatus::LEGACY_RESERVED
        : DeviceConfigStoreStatus::READY;
    return result;
  }

  // A damaged or interrupted slot is not an empty new board.  The previous
  // valid committed slot (if any) was selected above; otherwise service must
  // inspect the raw NVS instead of silently accepting an old-looking mirror.
  if (slot0.exists || slot1.exists) {
    result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
    return result;
  }

  if (mirror.state == MirrorState::BLANK) {
    result.status = DeviceConfigStoreStatus::BLANK;
    return result;
  }
  if (mirror.state != MirrorState::VALID) {
    result.status = DeviceConfigStoreStatus::CONFIG_ERROR;
    return result;
  }

  result.config.platformModel = mirror.model;
  result.config.sensorProfile = SensorProfileId::NONE;
  result.config.generation = 0;
  result.config.schema1Managed = false;
  result.config.laserInstalled = platformModelImpliesLaserInstalled(mirror.model);
  result.storedLaserInstalled = mirror.laser;
  result.laserInstalledNormalized =
      mirror.hasLaser &&
      mirror.laser != static_cast<uint8_t>(result.config.laserInstalled);
  result.status = (mirror.model == PlatformModel::PRO ||
                   mirror.model == PlatformModel::ULTRA)
      ? DeviceConfigStoreStatus::LEGACY_RESERVED
      : DeviceConfigStoreStatus::LEGACY;
  return result;
}

DeviceConfigSaveResult DeviceConfigStore::save(
    Preferences& preferences,
    bool storageOpened,
    const DeviceConfigSnapshot& current,
    PlatformModel nextModel,
    SensorProfileId nextProfile) const {
  return saveInternal(
      preferences, storageOpened, current, nextModel, nextProfile, false);
}

DeviceConfigSaveResult DeviceConfigStore::saveLegacyReserved(
    Preferences& preferences,
    bool storageOpened,
    const DeviceConfigSnapshot& current,
    PlatformModel nextModel) const {
  return saveInternal(
      preferences, storageOpened, current, nextModel, SensorProfileId::NONE, true);
}

DeviceConfigSaveResult DeviceConfigStore::saveInternal(
    Preferences& preferences,
    bool storageOpened,
    const DeviceConfigSnapshot& current,
    PlatformModel nextModel,
    SensorProfileId nextProfile,
    bool allowReserved) const {
  DeviceConfigSaveResult result{};
  result.config = current;
  if (!storageOpened) {
    result.error = DeviceConfigStoreError::STORAGE_FAILED;
    return result;
  }
  if (!isKnownSensorProfile(nextProfile) ||
      (nextModel == PlatformModel::BASE && nextProfile != SensorProfileId::NONE)) {
    result.error = DeviceConfigStoreError::INVALID_CONFIG;
    return result;
  }
  const bool supported = nextModel == PlatformModel::BASE ||
      nextModel == PlatformModel::PLUS ||
      (allowReserved && nextProfile == SensorProfileId::NONE &&
       (nextModel == PlatformModel::PRO || nextModel == PlatformModel::ULTRA));
  if (!supported) {
    result.error = DeviceConfigStoreError::UNSUPPORTED_MODEL;
    return result;
  }

  const DeviceConfigLoadResult persisted = load(preferences, true);
  const bool readable = persisted.status == DeviceConfigStoreStatus::BLANK ||
      persisted.status == DeviceConfigStoreStatus::LEGACY ||
      persisted.status == DeviceConfigStoreStatus::LEGACY_RESERVED ||
      persisted.status == DeviceConfigStoreStatus::READY;
  if (!readable || !sameConfig(persisted.config, current)) {
    result.error = DeviceConfigStoreError::CONFIG_ERROR;
    return result;
  }

  const bool sameIdentity =
      current.platformModel == nextModel && current.sensorProfile == nextProfile;
  // A blank factory board is only a runtime default. Its first explicit SET
  // must create durable evidence even when the requested value is BASE+NONE.
  // Likewise, an old Base/Plus with only legacy keys is materialized by its
  // first explicit SET; later same-value SETs remain write-free.
  const bool materializeSameIdentity = sameIdentity &&
      (persisted.status == DeviceConfigStoreStatus::BLANK ||
       persisted.status == DeviceConfigStoreStatus::LEGACY);
  if (sameIdentity && !materializeSameIdentity) {
    result.error = DeviceConfigStoreError::NONE;
    return result;
  }
  if (!sameIdentity && current.generation == UINT32_MAX) {
    result.error = DeviceConfigStoreError::GENERATION_OVERFLOW;
    return result;
  }

  DeviceConfigSnapshot next{};
  next.platformModel = nextModel;
  next.sensorProfile = nextProfile;
  next.laserInstalled = platformModelImpliesLaserInstalled(nextModel);
  next.generation = current.generation + (sameIdentity ? 0 : 1);
  next.schema1Managed = true;

  SlotRecord slot0 = readSlot(preferences, kSlot0Key);
  SlotRecord slot1 = readSlot(preferences, kSlot1Key);
  bool conflict = false;
  unsigned activeIndex = 0;
  const SlotRecord* active = selectHighestCommitted(slot0, slot1, conflict, activeIndex);
  if (conflict) {
    result.error = DeviceConfigStoreError::CONFIG_ERROR;
    return result;
  }

  // Migration materializes the old/blank value as the first committed slot.
  // A later interrupted write then has a known previous value to recover.
  if (active == nullptr) {
    DeviceConfigSnapshot baseline = current;
    baseline.schema1Managed = true;
    DeviceConfigStoreError writeError = writeMirror(preferences, baseline);
    if (writeError == DeviceConfigStoreError::NONE) {
      writeError = writeRecord(preferences, kSlot0Key, baseline, kPendingMarker);
    }
    if (writeError == DeviceConfigStoreError::NONE) {
      writeError = writeRecord(preferences, kSlot0Key, baseline, kCommittedMarker);
    }
    if (writeError != DeviceConfigStoreError::NONE) {
      result.error = writeError;
      return result;
    }
    slot0 = readSlot(preferences, kSlot0Key);
    if (!slot0.committed || slot0.generation != current.generation) {
      result.error = DeviceConfigStoreError::VERIFY_FAILED;
      return result;
    }
    activeIndex = 0;
  }

  if (materializeSameIdentity) {
    const DeviceConfigLoadResult verified = load(preferences, true);
    if (verified.status != DeviceConfigStoreStatus::READY ||
        !sameConfig(verified.config, next) ||
        verified.config.laserInstalled != next.laserInstalled) {
      result.error = DeviceConfigStoreError::VERIFY_FAILED;
      return result;
    }
    result.config = verified.config;
    result.error = DeviceConfigStoreError::NONE;
    return result;
  }

  const char* inactiveKey = activeIndex == 0 ? kSlot1Key : kSlot0Key;
  DeviceConfigStoreError writeError =
      writeRecord(preferences, inactiveKey, next, kPendingMarker);
  if (writeError == DeviceConfigStoreError::NONE) {
    writeError = writeMirror(preferences, next);
  }
  if (writeError == DeviceConfigStoreError::NONE) {
    writeError = writeRecord(preferences, inactiveKey, next, kCommittedMarker);
  }
  if (writeError != DeviceConfigStoreError::NONE) {
    result.error = writeError;
    return result;
  }

  const DeviceConfigLoadResult verified = load(preferences, true);
  if ((verified.status != DeviceConfigStoreStatus::READY &&
       verified.status != DeviceConfigStoreStatus::LEGACY_RESERVED) ||
      !sameConfig(verified.config, next) ||
      verified.config.laserInstalled != next.laserInstalled) {
    result.error = DeviceConfigStoreError::VERIFY_FAILED;
    return result;
  }
  result.config = verified.config;
  result.error = DeviceConfigStoreError::NONE;
  result.changed = true;
  return result;
}

const char* DeviceConfigStore::statusName(DeviceConfigStoreStatus status) {
  switch (status) {
    case DeviceConfigStoreStatus::BLANK: return "BLANK";
    case DeviceConfigStoreStatus::LEGACY: return "LEGACY";
    case DeviceConfigStoreStatus::LEGACY_RESERVED: return "LEGACY_RESERVED";
    case DeviceConfigStoreStatus::READY: return "READY";
    case DeviceConfigStoreStatus::STORAGE_UNAVAILABLE: return "STORAGE_UNAVAILABLE";
    case DeviceConfigStoreStatus::CONFIG_ERROR: return "CONFIG_ERROR";
    case DeviceConfigStoreStatus::UNSUPPORTED_SCHEMA: return "UNSUPPORTED_SCHEMA";
    case DeviceConfigStoreStatus::UNSUPPORTED_PROFILE: return "UNSUPPORTED_PROFILE";
  }
  return "UNKNOWN";
}

const char* DeviceConfigStore::errorName(DeviceConfigStoreError error) {
  switch (error) {
    case DeviceConfigStoreError::NONE: return "NONE";
    case DeviceConfigStoreError::INVALID_CONFIG: return "INVALID_CONFIG";
    case DeviceConfigStoreError::UNSUPPORTED_MODEL: return "UNSUPPORTED_MODEL";
    case DeviceConfigStoreError::GENERATION_OVERFLOW: return "GENERATION_OVERFLOW";
    case DeviceConfigStoreError::CONFIG_ERROR: return "CONFIG_ERROR";
    case DeviceConfigStoreError::STORAGE_FAILED: return "STORAGE_FAILED";
    case DeviceConfigStoreError::VERIFY_FAILED: return "VERIFY_FAILED";
  }
  return "UNKNOWN";
}
