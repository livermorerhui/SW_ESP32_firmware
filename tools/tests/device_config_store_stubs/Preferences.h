#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

enum PreferenceType { PT_U8 = 1, PT_BLOB = 9, PT_INVALID = 10 };

class Preferences {
public:
  struct Value {
    PreferenceType type = PT_INVALID;
    std::vector<uint8_t> bytes;
  };

  std::map<std::string, Value> values;
  std::string failNextPutKey;
  std::string failLengthKey;
  std::string failReadKey;
  std::string corruptNextReadKey;
  std::string corruptReadAfterPutKey;
  int putCalls = 0;
  int failPutAtCall = -1;

  bool isKey(const char* key) const { return values.count(key) != 0; }
  PreferenceType getType(const char* key) const {
    auto value = values.find(key);
    return value == values.end() ? PT_INVALID : value->second.type;
  }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) const {
    auto value = values.find(key);
    return value == values.end() || value->second.type != PT_U8 ||
            value->second.bytes.size() != 1
        ? fallback
        : value->second.bytes[0];
  }
  size_t putUChar(const char* key, uint8_t value) {
    if (shouldFail(key)) return 0;
    values[key] = {PT_U8, {value}};
    return 1;
  }
  size_t putBytes(const char* key, const void* source, size_t length) {
    if (shouldFail(key)) return 0;
    const uint8_t* bytes = static_cast<const uint8_t*>(source);
    values[key] = {PT_BLOB, std::vector<uint8_t>(bytes, bytes + length)};
    if (corruptReadAfterPutKey == key) {
      corruptNextReadKey = key;
      corruptReadAfterPutKey.clear();
    }
    return length;
  }
  size_t getBytesLength(const char* key) const {
    if (failLengthKey == key) return 0;
    auto value = values.find(key);
    return value == values.end() || value->second.type != PT_BLOB
        ? 0 : value->second.bytes.size();
  }
  size_t getBytes(const char* key, void* target, size_t capacity) {
    if (failReadKey == key) return 0;
    auto value = values.find(key);
    if (value == values.end() || value->second.type != PT_BLOB ||
        value->second.bytes.size() > capacity) return 0;
    std::memcpy(target, value->second.bytes.data(), value->second.bytes.size());
    if (corruptNextReadKey == key && !value->second.bytes.empty()) {
      static_cast<uint8_t*>(target)[0] ^= 0xFF;
      corruptNextReadKey.clear();
    }
    return value->second.bytes.size();
  }

private:
  bool shouldFail(const char* key) {
    ++putCalls;
    if (failPutAtCall == putCalls || failNextPutKey == key) {
      failNextPutKey.clear();
      return true;
    }
    return false;
  }
};
