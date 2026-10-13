#include "modules/laser/ProfileEmptyDistanceStore.h"

#include <math.h>
#include <string.h>
#include <limits.h>

namespace {

// Schema 1 uses an explicit 32-byte little-endian image:
//   0..3 magic, 4 schema, 5 profile, 6 backend, 7 SET flag,
//   8..11 config generation, 12..15 empty revision,
//   16..19 distance_mm float bits, 20..23 tolerance_mm float bits,
//   24..27 commit marker, 28..31 CRC32 of preceding bytes.
// The existing emp_* record and its CRC layout are intentionally untouched.
constexpr size_t kRecordLength = 32;
constexpr size_t kMaximumReadableRecordLength = 64;
constexpr uint8_t kMagic[4] = {'S', 'W', 'E', 'D'};
constexpr uint8_t kSchema = 1;
constexpr uint32_t kPendingMarker = 0;
constexpr uint32_t kCommittedMarker = 0x31504D45UL;  // bytes "EMP1"
constexpr const char* kSlot0Key = "pe_slot0";
constexpr const char* kSlot1Key = "pe_slot1";

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

uint32_t floatBits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float32 required for empty distance");
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float floatFromBits(uint32_t bits) {
  float value = 0.0f;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

uint32_t crc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }
  return ~crc;
}

bool backendMatchesProfile(
    SensorProfileId profile, ProfileEmptyBackendId backend) {
  switch (profile) {
    case SensorProfileId::J100_485:
      return backend == ProfileEmptyBackendId::UART485;
    case SensorProfileId::CYD3S1612_IIC:
      return backend == ProfileEmptyBackendId::IIC;
    case SensorProfileId::NONE:
      return false;
  }
  return false;
}

bool validSetValue(const ProfileEmptyDistanceRecord& record) {
  return backendMatchesProfile(record.sensorProfile, record.backend) &&
      isfinite(record.distanceMm) && record.distanceMm > 0.0f &&
      isfinite(record.toleranceMm) && record.toleranceMm >= 0.0f;
}

bool validUnsetValue(const ProfileEmptyDistanceRecord& record) {
  return record.backend == ProfileEmptyBackendId::NONE &&
      floatBits(record.distanceMm) == floatBits(0.0f) &&
      floatBits(record.toleranceMm) == floatBits(0.0f);
}

void encodeRecord(
    const ProfileEmptyDistanceRecord& record,
    uint32_t marker,
    uint8_t (&bytes)[kRecordLength]) {
  memset(bytes, 0, sizeof(bytes));
  memcpy(bytes, kMagic, sizeof(kMagic));
  bytes[4] = kSchema;
  bytes[5] = static_cast<uint8_t>(record.sensorProfile);
  bytes[6] = static_cast<uint8_t>(record.backend);
  bytes[7] = record.isSet ? 1 : 0;
  writeU32Le(bytes + 8, record.configGeneration);
  writeU32Le(bytes + 12, record.emptyRevision);
  writeU32Le(bytes + 16, floatBits(record.distanceMm));
  writeU32Le(bytes + 20, floatBits(record.toleranceMm));
  writeU32Le(bytes + 24, marker);
  writeU32Le(bytes + 28, crc32(bytes, kRecordLength - 4));
}

struct SlotRecord {
  bool exists = false;
  bool unreadableFormat = false;
  bool validEnvelope = false;
  bool committed = false;
  size_t length = 0;
  uint8_t bytes[kMaximumReadableRecordLength]{};
  uint8_t schema = 0;
  uint8_t profile = 0;
  uint32_t revision = 0;
};

SlotRecord readSlot(Preferences& preferences, const char* key) {
  SlotRecord slot{};
  slot.exists = preferences.isKey(key);
  if (!slot.exists) return slot;
  if (preferences.getType(key) != PT_BLOB) {
    slot.unreadableFormat = true;
    return slot;
  }
  slot.length = preferences.getBytesLength(key);
  if (slot.length == 0 || slot.length > kMaximumReadableRecordLength) {
    // An existing blob with an unreadable length may be the latest anchor.
    // Treat it as an I/O/format error, not a damaged older record.
    slot.unreadableFormat = true;
    return slot;
  }
  if (preferences.getBytes(key, slot.bytes, slot.length) != slot.length) {
    // Preferences returns zero on read failure. Do not silently reselect a
    // previous D_e if a newer committed slot cannot be read right now.
    slot.unreadableFormat = true;
    return slot;
  }
  // A fully read but truncated/CRC-invalid record may be discarded in favor
  // of the previous committed record. This differs from an unreadable record.
  if (slot.length < kRecordLength) {
    return slot;
  }
  if (memcmp(slot.bytes, kMagic, sizeof(kMagic)) != 0 ||
      readU32Le(slot.bytes + slot.length - 4) !=
          crc32(slot.bytes, slot.length - 4)) {
    return slot;
  }
  slot.schema = slot.bytes[4];
  slot.profile = slot.bytes[5];
  slot.revision = readU32Le(slot.bytes + 12);
  slot.validEnvelope = true;
  slot.committed = readU32Le(slot.bytes + 24) == kCommittedMarker;
  return slot;
}

const SlotRecord* selectLatestCommitted(
    const SlotRecord& slot0,
    const SlotRecord& slot1,
    unsigned& selectedIndex,
    bool& conflict) {
  conflict = false;
  if (!slot0.committed && !slot1.committed) return nullptr;
  if (!slot0.committed) {
    selectedIndex = 1;
    return &slot1;
  }
  if (!slot1.committed) {
    selectedIndex = 0;
    return &slot0;
  }
  if (slot0.revision == slot1.revision) {
    conflict = slot0.length != slot1.length ||
        memcmp(slot0.bytes, slot1.bytes, slot0.length) != 0;
    selectedIndex = 0;
    return &slot0;
  }
  selectedIndex = slot0.revision > slot1.revision ? 0 : 1;
  return selectedIndex == 0 ? &slot0 : &slot1;
}

ProfileEmptyDistanceRecord decodeRecord(const SlotRecord& slot) {
  ProfileEmptyDistanceRecord record{};
  record.sensorProfile = static_cast<SensorProfileId>(slot.bytes[5]);
  record.backend = static_cast<ProfileEmptyBackendId>(slot.bytes[6]);
  record.isSet = slot.bytes[7] == 1;
  record.configGeneration = readU32Le(slot.bytes + 8);
  record.emptyRevision = slot.revision;
  record.distanceMm = floatFromBits(readU32Le(slot.bytes + 16));
  record.toleranceMm = floatFromBits(readU32Le(slot.bytes + 20));
  return record;
}

ProfileEmptyStoreError writeRecord(
    Preferences& preferences,
    const char* key,
    const ProfileEmptyDistanceRecord& record,
    uint32_t marker) {
  uint8_t bytes[kRecordLength]{};
  encodeRecord(record, marker, bytes);
  if (preferences.putBytes(key, bytes, sizeof(bytes)) != sizeof(bytes)) {
    return ProfileEmptyStoreError::STORAGE_FAILED;
  }
  if (preferences.getType(key) != PT_BLOB ||
      preferences.getBytesLength(key) != sizeof(bytes)) {
    return ProfileEmptyStoreError::VERIFY_FAILED;
  }
  uint8_t actual[kRecordLength]{};
  if (preferences.getBytes(key, actual, sizeof(actual)) != sizeof(actual) ||
      memcmp(actual, bytes, sizeof(bytes)) != 0) {
    return ProfileEmptyStoreError::VERIFY_FAILED;
  }
  return ProfileEmptyStoreError::NONE;
}

bool sameRecord(
    const ProfileEmptyDistanceRecord& left,
    const ProfileEmptyDistanceRecord& right) {
  return left.sensorProfile == right.sensorProfile &&
      left.configGeneration == right.configGeneration &&
      left.emptyRevision == right.emptyRevision &&
      left.isSet == right.isSet &&
      left.backend == right.backend &&
      floatBits(left.distanceMm) == floatBits(right.distanceMm) &&
      floatBits(left.toleranceMm) == floatBits(right.toleranceMm);
}

bool bothSlotsMatchCommitted(
    Preferences& preferences,
    const ProfileEmptyDistanceRecord& expected) {
  uint8_t expectedBytes[kRecordLength]{};
  encodeRecord(expected, kCommittedMarker, expectedBytes);
  const SlotRecord slot0 = readSlot(preferences, kSlot0Key);
  const SlotRecord slot1 = readSlot(preferences, kSlot1Key);
  return slot0.validEnvelope && slot0.committed &&
      slot1.validEnvelope && slot1.committed &&
      slot0.length == sizeof(expectedBytes) &&
      slot1.length == sizeof(expectedBytes) &&
      memcmp(slot0.bytes, expectedBytes, sizeof(expectedBytes)) == 0 &&
      memcmp(slot1.bytes, expectedBytes, sizeof(expectedBytes)) == 0;
}

}  // namespace

ProfileEmptyLoadResult ProfileEmptyDistanceStore::load(
    Preferences& preferences,
    bool storageOpened,
    SensorProfileId activeProfile,
    uint32_t activeGeneration) const {
  ProfileEmptyLoadResult result{};
  if (!storageOpened) {
    result.status = ProfileEmptyStoreStatus::STORAGE_UNAVAILABLE;
    return result;
  }

  const SlotRecord slot0 = readSlot(preferences, kSlot0Key);
  const SlotRecord slot1 = readSlot(preferences, kSlot1Key);
  if (slot0.unreadableFormat || slot1.unreadableFormat) {
    result.status = ProfileEmptyStoreStatus::CONFIG_ERROR;
    return result;
  }
  unsigned selectedIndex = 0;
  bool conflict = false;
  const SlotRecord* active = selectLatestCommitted(
      slot0, slot1, selectedIndex, conflict);
  if (conflict) {
    result.status = ProfileEmptyStoreStatus::CONFIG_ERROR;
    return result;
  }
  if (!active) {
    result.status = (!slot0.exists && !slot1.exists)
        ? ProfileEmptyStoreStatus::UNSET
        : ProfileEmptyStoreStatus::CONFIG_ERROR;
    return result;
  }

  // A fully committed future schema or unknown profile is authoritative even
  // when the other slot contains a familiar previous anchor.
  if (active->schema != kSchema || active->length != kRecordLength) {
    result.status = ProfileEmptyStoreStatus::UNSUPPORTED_SCHEMA;
    return result;
  }
  if (!isKnownSensorProfile(static_cast<SensorProfileId>(active->profile))) {
    result.status = ProfileEmptyStoreStatus::UNSUPPORTED_PROFILE;
    return result;
  }

  result.record = decodeRecord(*active);
  if (active->bytes[7] > 1 ||
      result.record.sensorProfile == SensorProfileId::NONE ||
      result.record.emptyRevision == 0 ||
      (result.record.isSet && !validSetValue(result.record)) ||
      (!result.record.isSet && !validUnsetValue(result.record))) {
    result.status = ProfileEmptyStoreStatus::CONFIG_ERROR;
    return result;
  }

  if (result.record.sensorProfile != activeProfile ||
      result.record.configGeneration != activeGeneration) {
    result.status = ProfileEmptyStoreStatus::STALE;
    return result;
  }
  result.status = result.record.isSet
      ? ProfileEmptyStoreStatus::SET
      : ProfileEmptyStoreStatus::UNSET;
  return result;
}

ProfileEmptySaveResult ProfileEmptyDistanceStore::set(
    Preferences& preferences,
    bool storageOpened,
    SensorProfileId activeProfile,
    uint32_t activeGeneration,
    float distanceMm,
    float toleranceMm,
    ProfileEmptyBackendId backend) const {
  ProfileEmptyDistanceRecord next{};
  next.sensorProfile = activeProfile;
  next.configGeneration = activeGeneration;
  next.isSet = true;
  next.distanceMm = distanceMm;
  next.toleranceMm = toleranceMm;
  next.backend = backend;
  return save(preferences, storageOpened, next);
}

ProfileEmptySaveResult ProfileEmptyDistanceStore::clear(
    Preferences& preferences,
    bool storageOpened,
    SensorProfileId activeProfile,
    uint32_t activeGeneration) const {
  ProfileEmptyDistanceRecord next{};
  next.sensorProfile = activeProfile;
  next.configGeneration = activeGeneration;
  next.isSet = false;
  return save(preferences, storageOpened, next);
}

ProfileEmptySaveResult ProfileEmptyDistanceStore::save(
    Preferences& preferences,
    bool storageOpened,
    const ProfileEmptyDistanceRecord& nextWithoutRevision) const {
  ProfileEmptySaveResult result{};
  if (!storageOpened) {
    result.error = ProfileEmptyStoreError::STORAGE_FAILED;
    return result;
  }
  if (!isKnownSensorProfile(nextWithoutRevision.sensorProfile) ||
      nextWithoutRevision.sensorProfile == SensorProfileId::NONE ||
      (nextWithoutRevision.isSet && !validSetValue(nextWithoutRevision)) ||
      (!nextWithoutRevision.isSet && !validUnsetValue(nextWithoutRevision))) {
    result.error = ProfileEmptyStoreError::INVALID_RECORD;
    return result;
  }

  const ProfileEmptyLoadResult before = load(
      preferences,
      storageOpened,
      nextWithoutRevision.sensorProfile,
      nextWithoutRevision.configGeneration);
  if (before.status != ProfileEmptyStoreStatus::SET &&
      before.status != ProfileEmptyStoreStatus::UNSET &&
      before.status != ProfileEmptyStoreStatus::STALE) {
    result.error = ProfileEmptyStoreError::CONFIG_ERROR;
    return result;
  }
  if (before.record.emptyRevision == UINT32_MAX) {
    result.error = ProfileEmptyStoreError::REVISION_OVERFLOW;
    return result;
  }

  const SlotRecord slot0 = readSlot(preferences, kSlot0Key);
  const SlotRecord slot1 = readSlot(preferences, kSlot1Key);
  unsigned activeIndex = 1;  // blank store writes slot 0
  bool conflict = false;
  selectLatestCommitted(slot0, slot1, activeIndex, conflict);
  if (conflict) {
    result.error = ProfileEmptyStoreError::CONFIG_ERROR;
    return result;
  }

  ProfileEmptyDistanceRecord next = nextWithoutRevision;
  next.emptyRevision = before.record.emptyRevision + 1;
  const char* targetKey = activeIndex == 0 ? kSlot1Key : kSlot0Key;
  ProfileEmptyStoreError writeError = writeRecord(
      preferences, targetKey, next, kPendingMarker);
  if (writeError == ProfileEmptyStoreError::NONE) {
    writeError = writeRecord(
        preferences, targetKey, next, kCommittedMarker);
  }
  if (writeError != ProfileEmptyStoreError::NONE) {
    result.error = writeError;
    return result;
  }

  const ProfileEmptyLoadResult verified = load(
      preferences,
      true,
      next.sensorProfile,
      next.configGeneration);
  if (verified.status != (next.isSet
          ? ProfileEmptyStoreStatus::SET : ProfileEmptyStoreStatus::UNSET) ||
      !sameRecord(verified.record, next)) {
    result.error = ProfileEmptyStoreError::VERIFY_FAILED;
    return result;
  }

  if (!next.isSet) {
    // An acknowledged clear must remain UNSET even if one NVS slot later
    // becomes CRC-invalid. The first committed tombstone is authoritative if
    // writing the second copy fails; the caller then fails closed in RAM.
    const char* secondKey = activeIndex == 0 ? kSlot0Key : kSlot1Key;
    writeError = writeRecord(preferences, secondKey, next, kPendingMarker);
    if (writeError == ProfileEmptyStoreError::NONE) {
      writeError = writeRecord(
          preferences, secondKey, next, kCommittedMarker);
    }
    if (writeError != ProfileEmptyStoreError::NONE) {
      result.error = writeError;
      return result;
    }
    if (!bothSlotsMatchCommitted(preferences, next)) {
      result.error = ProfileEmptyStoreError::VERIFY_FAILED;
      return result;
    }
  }
  result.error = ProfileEmptyStoreError::NONE;
  result.record = verified.record;
  return result;
}

const char* ProfileEmptyDistanceStore::statusName(ProfileEmptyStoreStatus status) {
  switch (status) {
    case ProfileEmptyStoreStatus::UNSET: return "UNSET";
    case ProfileEmptyStoreStatus::SET: return "SET";
    case ProfileEmptyStoreStatus::STALE: return "STALE";
    case ProfileEmptyStoreStatus::STORAGE_UNAVAILABLE: return "STORAGE_UNAVAILABLE";
    case ProfileEmptyStoreStatus::CONFIG_ERROR: return "CONFIG_ERROR";
    case ProfileEmptyStoreStatus::UNSUPPORTED_SCHEMA: return "UNSUPPORTED_SCHEMA";
    case ProfileEmptyStoreStatus::UNSUPPORTED_PROFILE: return "UNSUPPORTED_PROFILE";
  }
  return "UNKNOWN";
}

const char* ProfileEmptyDistanceStore::errorName(ProfileEmptyStoreError error) {
  switch (error) {
    case ProfileEmptyStoreError::NONE: return "NONE";
    case ProfileEmptyStoreError::INVALID_RECORD: return "INVALID_RECORD";
    case ProfileEmptyStoreError::REVISION_OVERFLOW: return "REVISION_OVERFLOW";
    case ProfileEmptyStoreError::CONFIG_ERROR: return "CONFIG_ERROR";
    case ProfileEmptyStoreError::STORAGE_FAILED: return "STORAGE_FAILED";
    case ProfileEmptyStoreError::VERIFY_FAILED: return "VERIFY_FAILED";
  }
  return "UNKNOWN";
}
