#pragma once
#include "Types.h"

enum class CmdType : uint8_t {
  CAP_QUERY,
  WAVE_SET,      // set freq/intensity (not necessarily start)
  WAVE_START,
  WAVE_STOP,
  SCALE_ZERO,
  SCALE_CAL,
  LEGACY_FIE     // 兼容 F/I/E 组合命令
};

struct Command {
  CmdType type = CmdType::CAP_QUERY;
  WaveParams wave{};
  float p1 = 0; // zero
  float p2 = 0; // factor
};

class CommandHandler {
public:
  virtual bool handle(const Command& c, String& outAck) = 0;
  virtual ~CommandHandler() = default;
};

class CommandBus {
public:
  void setHandler(CommandHandler* h) { handler = h; }
  bool dispatch(const Command& c, String& outAck) {
    if (!handler) { outAck = "NACK:BUSY"; return false; }
    return handler->handle(c, outAck);
  }
private:
  CommandHandler* handler = nullptr;
};