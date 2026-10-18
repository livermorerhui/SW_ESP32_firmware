#!/usr/bin/env python3
"""Host checks for the profile-bound empty-distance NVS record.

This script has its own Preferences fake so torn writes, stale profiles and
unknown complete records can be exercised without an ESP32 or Android build.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

from run_evaluator_unit_tests import ARDUINO_STUB


ROOT = Path(__file__).resolve().parents[1]

PREFERENCES_STUB = r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

enum PreferenceType { PT_BLOB = 9, PT_INVALID = 10 };

class Preferences {
public:
  std::map<std::string, std::vector<uint8_t>> blobs;
  int putCalls = 0;
  int failPutCall = -1;
  mutable int failGetAfterPutCall = -1;
  mutable std::string failGetKey;
  std::string failLengthKey;

  bool isKey(const char* key) const { return blobs.count(key) != 0; }
  PreferenceType getType(const char* key) const {
    return isKey(key) ? PT_BLOB : PT_INVALID;
  }
  size_t getBytesLength(const char* key) const {
    if (failLengthKey == key) return 0;
    auto it = blobs.find(key);
    return it == blobs.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char* key, void* output, size_t maximum) const {
    if (putCalls == failGetAfterPutCall && failGetKey == key) {
      failGetAfterPutCall = -1;
      return 0;
    }
    auto it = blobs.find(key);
    if (it == blobs.end() || maximum < it->second.size()) return 0;
    std::memcpy(output, it->second.data(), it->second.size());
    return it->second.size();
  }
  size_t putBytes(const char* key, const void* input, size_t length) {
    ++putCalls;
    if (putCalls == failPutCall) return 0;
    const auto* bytes = static_cast<const uint8_t*>(input);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + length);
    return length;
  }
};
"""

TEST_MAIN = r"""
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "modules/laser/ProfileEmptyDistanceStore.h"

namespace {

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

void putU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
  }
}

void repairCrc(std::vector<uint8_t>& bytes) {
  putU32(bytes, bytes.size() - 4, crc32(bytes.data(), bytes.size() - 4));
}

ProfileEmptySaveResult setJ100(
    const ProfileEmptyDistanceStore& store, Preferences& prefs, float distance) {
  return store.set(
      prefs, true, SensorProfileId::J100_485, 3, distance, 0.10f,
      ProfileEmptyBackendId::UART485);
}

void test_blank_set_clear_and_stale_profile() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  prefs.blobs["emp_mm"] = {0xA5, 0x5A};
  auto read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::UNSET);
  assert(read.record.emptyRevision == 0);
  assert(store.load(prefs, false, SensorProfileId::J100_485, 3).status ==
      ProfileEmptyStoreStatus::STORAGE_UNAVAILABLE);

  auto saved = setJ100(store, prefs, 112.55f);
  assert(saved.ok());
  assert(saved.record.emptyRevision == 1);
  assert(saved.record.isSet);
  assert(saved.record.sensorProfile == SensorProfileId::J100_485);
  assert(saved.record.configGeneration == 3);
  assert(saved.record.backend == ProfileEmptyBackendId::UART485);
  assert(std::fabs(saved.record.distanceMm - 112.55f) < 0.0001f);

  // A new store object simulates a cold boot over the same NVS contents.
  const ProfileEmptyDistanceStore afterBoot;
  read = afterBoot.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::SET);
  assert(read.record.emptyRevision == 1);

  // A profile switch or reconfiguration invalidates the old anchor without
  // deleting the old bytes or changing the device config generation.
  read = afterBoot.load(prefs, true, SensorProfileId::CYD3S1612_IIC, 4);
  assert(read.status == ProfileEmptyStoreStatus::STALE);
  assert(read.record.emptyRevision == 1);
  assert(read.record.sensorProfile == SensorProfileId::J100_485);

  saved = store.set(
      prefs, true, SensorProfileId::CYD3S1612_IIC, 4, 110.0f, 1.0f,
      ProfileEmptyBackendId::IIC);
  assert(saved.ok());
  assert(saved.record.emptyRevision == 2);
  assert(store.load(prefs, true, SensorProfileId::J100_485, 3).status ==
      ProfileEmptyStoreStatus::STALE);
  assert(store.load(prefs, true, SensorProfileId::CYD3S1612_IIC, 4).status ==
      ProfileEmptyStoreStatus::SET);

  saved = store.clear(prefs, true, SensorProfileId::CYD3S1612_IIC, 4);
  assert(saved.ok());
  assert(saved.record.emptyRevision == 3);
  assert(!saved.record.isSet);
  read = store.load(prefs, true, SensorProfileId::CYD3S1612_IIC, 4);
  assert(read.status == ProfileEmptyStoreStatus::UNSET);
  assert(read.record.emptyRevision == 3);
  assert(prefs.blobs["pe_slot0"] == prefs.blobs["pe_slot1"]);
  assert(prefs.blobs["emp_mm"] == std::vector<uint8_t>({0xA5, 0x5A}));
}

void test_clear_survives_one_bad_slot_after_ack() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  const auto cleared = store.clear(prefs, true, SensorProfileId::J100_485, 3);
  assert(cleared.ok());
  assert(cleared.record.emptyRevision == 2);
  assert(prefs.blobs["pe_slot0"] == prefs.blobs["pe_slot1"]);

  for (const char* key : {"pe_slot0", "pe_slot1"}) {
    Preferences rebooted = prefs;
    rebooted.blobs[key].back() ^= 0xFF;  // one committed tombstone has bad CRC
    const ProfileEmptyDistanceStore afterBoot;
    const auto read = afterBoot.load(
        rebooted, true, SensorProfileId::J100_485, 3);
    assert(read.status == ProfileEmptyStoreStatus::UNSET);
    assert(read.record.emptyRevision == 2);
    assert(!read.record.isSet);
  }
}

void test_clear_second_slot_failure_never_exposes_old_set() {
  for (int failedWriteOffset : {3, 4}) {
    Preferences prefs;
    const ProfileEmptyDistanceStore store;
    assert(setJ100(store, prefs, 112.55f).ok());
    prefs.failPutCall = prefs.putCalls + failedWriteOffset;
    const auto cleared = store.clear(
        prefs, true, SensorProfileId::J100_485, 3);
    assert(cleared.error == ProfileEmptyStoreError::STORAGE_FAILED);
    prefs.failPutCall = -1;
    const ProfileEmptyDistanceStore afterBoot;
    const auto read = afterBoot.load(
        prefs, true, SensorProfileId::J100_485, 3);
    assert(read.status == ProfileEmptyStoreStatus::UNSET);
    assert(read.record.emptyRevision == 2);
  }

  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  prefs.failGetAfterPutCall = prefs.putCalls + 4;
  prefs.failGetKey = "pe_slot0";  // second slot's committed readback
  const auto cleared = store.clear(
      prefs, true, SensorProfileId::J100_485, 3);
  assert(cleared.error == ProfileEmptyStoreError::VERIFY_FAILED);
  const ProfileEmptyDistanceStore afterBoot;
  const auto read = afterBoot.load(
      prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::UNSET);
  assert(read.record.emptyRevision == 2);
}

void test_reject_bad_values_and_preserve_old_anchor() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  const auto first = prefs.blobs["pe_slot0"];

  auto bad = store.set(
      prefs, true, SensorProfileId::J100_485, 3, NAN, 0.1f,
      ProfileEmptyBackendId::UART485);
  assert(bad.error == ProfileEmptyStoreError::INVALID_RECORD);
  bad = store.set(
      prefs, true, SensorProfileId::J100_485, 3, 112.56f, 0.1f,
      ProfileEmptyBackendId::IIC);
  assert(bad.error == ProfileEmptyStoreError::INVALID_RECORD);
  bad = store.clear(prefs, true, SensorProfileId::NONE, 3);
  assert(bad.error == ProfileEmptyStoreError::INVALID_RECORD);
  bad = store.clear(prefs, false, SensorProfileId::J100_485, 3);
  assert(bad.error == ProfileEmptyStoreError::STORAGE_FAILED);
  assert(prefs.blobs["pe_slot0"] == first);
  assert(store.load(prefs, true, SensorProfileId::J100_485, 3).record.emptyRevision == 1);
}

void test_torn_write_and_crc_recovery() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  prefs.failPutCall = prefs.putCalls + 2;  // pending write succeeds, commit fails
  auto saved = setJ100(store, prefs, 112.56f);
  assert(saved.error == ProfileEmptyStoreError::STORAGE_FAILED);
  auto read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::SET);
  assert(read.record.emptyRevision == 1);
  prefs.failPutCall = -1;

  saved = setJ100(store, prefs, 112.57f);
  assert(saved.ok());
  assert(saved.record.emptyRevision == 2);
  prefs.blobs["pe_slot1"].back() ^= 0xFF;  // damage latest CRC
  read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::SET);
  assert(read.record.emptyRevision == 1);
}

void test_unreadable_latest_slot_does_not_restore_previous_anchor() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  assert(setJ100(store, prefs, 112.56f).ok());
  assert(store.load(prefs, true, SensorProfileId::J100_485, 3).record.emptyRevision == 2);

  // A transient NVS read failure is not proof of damaged CRC. Both records
  // still exist, so accepting revision 1 here would silently restore D_e.
  prefs.failGetAfterPutCall = prefs.putCalls;
  prefs.failGetKey = "pe_slot1";
  auto read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::CONFIG_ERROR);
  read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::SET);
  assert(read.record.emptyRevision == 2);
  assert(std::fabs(read.record.distanceMm - 112.56f) < 0.0001f);

  // getBytesLength also returns zero on failure, even though isKey is true.
  prefs.failLengthKey = "pe_slot1";
  read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::CONFIG_ERROR);
  prefs.failLengthKey.clear();
  assert(store.load(prefs, true, SensorProfileId::J100_485, 3).record.emptyRevision == 2);
}

void test_complete_unknown_records_block_fallback() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  auto newer = prefs.blobs["pe_slot0"];
  putU32(newer, 12, 2);  // newer revision
  newer[4] = 2;  // complete future schema
  repairCrc(newer);
  prefs.blobs["pe_slot1"] = newer;
  auto read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::UNSUPPORTED_SCHEMA);
  assert(store.clear(prefs, true, SensorProfileId::J100_485, 3).error ==
      ProfileEmptyStoreError::CONFIG_ERROR);

  newer[4] = 1;
  newer[5] = 99;  // complete unknown sensor profile
  repairCrc(newer);
  prefs.blobs["pe_slot1"] = newer;
  read = store.load(prefs, true, SensorProfileId::J100_485, 3);
  assert(read.status == ProfileEmptyStoreStatus::UNSUPPORTED_PROFILE);
}

void test_revision_overflow() {
  Preferences prefs;
  const ProfileEmptyDistanceStore store;
  assert(setJ100(store, prefs, 112.55f).ok());
  auto& active = prefs.blobs["pe_slot0"];
  putU32(active, 12, UINT32_MAX);
  repairCrc(active);
  const auto cleared = store.clear(prefs, true, SensorProfileId::J100_485, 3);
  assert(cleared.error == ProfileEmptyStoreError::REVISION_OVERFLOW);
}

}  // namespace

int main() {
  test_blank_set_clear_and_stale_profile();
  test_clear_survives_one_bad_slot_after_ack();
  test_clear_second_slot_failure_never_exposes_old_set();
  test_reject_bad_values_and_preserve_old_anchor();
  test_torn_write_and_crc_recovery();
  test_unreadable_latest_slot_does_not_restore_previous_anchor();
  test_complete_unknown_records_block_fallback();
  test_revision_overflow();
  std::cout << "profile empty-distance store tests passed\n";
}
"""


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sw_profile_empty_store_") as directory:
        temp = Path(directory)
        (temp / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
        (temp / "Preferences.h").write_text(PREFERENCES_STUB, encoding="utf-8")
        source = temp / "profile_empty_store_tests.cpp"
        source.write_text(TEST_MAIN, encoding="utf-8")
        binary = temp / "profile_empty_store_tests"
        subprocess.run(
            [
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-variable", "-I", str(temp), "-I", str(ROOT / "src"),
                str(source),
                str(ROOT / "src/modules/laser/ProfileEmptyDistanceStore.cpp"),
                "-o", str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
