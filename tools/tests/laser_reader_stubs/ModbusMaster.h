#pragma once

#include <cstdint>
#include <functional>

#include <Arduino.h>

extern std::function<uint8_t()> g_modbusRead;
extern uint16_t g_modbusRaw;
extern int g_modbusBeginCount;
extern int g_modbusReadCount;

class ModbusMaster {
 public:
  static constexpr uint8_t ku8MBSuccess = 0;
  void begin(uint8_t, FakeSerial&) { ++g_modbusBeginCount; }
  uint8_t readInputRegisters(uint16_t, uint8_t) {
    ++g_modbusReadCount;
    return g_modbusRead ? g_modbusRead() : ku8MBSuccess;
  }
  uint8_t readHoldingRegisters(uint16_t address, uint8_t count) {
    return readInputRegisters(address, count);
  }
  uint16_t getResponseBuffer(uint8_t) const { return g_modbusRaw; }
};
