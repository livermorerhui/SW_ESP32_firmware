#pragma once
#include "CommandBus.h"
#include "EventBus.h"
#include "config/GlobalConfig.h"

class ProtocolCodec {
public:
  // 返回 true = 成功解析
  static bool parseCommand(const String& in, Command& out, String& err) {
    String s = in; s.trim();
    if (s.length() == 0) { err = "EMPTY"; return false; }

    // CAP
    if (s.equalsIgnoreCase("CAP?")) { out.type = CmdType::CAP_QUERY; return true; }

    // New protocol
    if (s.startsWith("WAVE:SET")) {
      out.type = CmdType::WAVE_SET;
      int fi = s.indexOf("freq=");
      int ai = s.indexOf("amp=");
      if (fi < 0 || ai < 0) { err = "INVALID_PARAM"; return false; }
      float f = s.substring(fi + 5).toFloat();
      int a = s.substring(ai + 4).toInt();
      if (f < 0 || f > 50.0f || a < 0 || a > 120) { err = "INVALID_PARAM"; return false; }
      out.wave.freqHz = f; out.wave.intensity = a;
      return true;
    }
    if (s.equalsIgnoreCase("WAVE:START")) { out.type = CmdType::WAVE_START; return true; }
    if (s.equalsIgnoreCase("WAVE:STOP"))  { out.type = CmdType::WAVE_STOP;  return true; }
    if (s.equalsIgnoreCase("SCALE:ZERO")) { out.type = CmdType::SCALE_ZERO; return true; }
    if (s.startsWith("SCALE:CAL")) {
      out.type = CmdType::SCALE_CAL;
      int zi = s.indexOf("z=");
      int ki = s.indexOf("k=");
      if (zi < 0 || ki < 0) { err = "INVALID_PARAM"; return false; }
      out.p1 = s.substring(zi + 2).toFloat();
      out.p2 = s.substring(ki + 2).toFloat();
      return true;
    }

    // Legacy scale
    if (s.equalsIgnoreCase("ZERO")) { out.type = CmdType::SCALE_ZERO; return true; }
    if (s.startsWith("SET_PS:")) {
      out.type = CmdType::SCALE_CAL;
      String p = s.substring(7);
      int idx = p.indexOf(',');
      if (idx <= 0) { err = "INVALID_PARAM"; return false; }
      out.p1 = p.substring(0, idx).toFloat();
      out.p2 = p.substring(idx + 1).toFloat();
      return true;
    }

    // Legacy wave: F/I/E
    if (s.indexOf("F:") != -1 || s.indexOf("I:") != -1 || s.indexOf("E:") != -1) {
      out.type = CmdType::LEGACY_FIE;
      // 默认沿用当前值由 handler 自己处理；这里先解析可能出现的字段
      // freq
      int fIndex = s.indexOf("F:");
      if (fIndex != -1) {
        int comma = s.indexOf(",", fIndex);
        String sub = (comma == -1) ? s.substring(fIndex + 2) : s.substring(fIndex + 2, comma);
        out.wave.freqHz = sub.toFloat();
      } else out.wave.freqHz = -1; // 表示“未触碰”
      // intensity
      int iIndex = s.indexOf("I:");
      if (iIndex != -1) {
        int comma = s.indexOf(",", iIndex);
        String sub = (comma == -1) ? s.substring(iIndex + 2) : s.substring(iIndex + 2, comma);
        out.wave.intensity = sub.toInt();
      } else out.wave.intensity = -1;
      // enable
      int eIndex = s.indexOf("E:");
      if (eIndex != -1) {
        int comma = s.indexOf(",", eIndex);
        String sub = (comma == -1) ? s.substring(eIndex + 2) : s.substring(eIndex + 2, comma);
        out.wave.enable = (sub.toInt() != 0);
        out.wave.hasEnable = true;
      } else {
        out.wave.hasEnable = false;
      }
      return true;
    }

    err = "UNKNOWN_CMD";
    return false;
  }

  static String encodeEvent(const Event& e) {
    String s;
    switch (e.type) {
      case EventType::STATE:
        s = "EVT:STATE ";
        s += (e.state == TopState::IDLE ? "IDLE" :
              e.state == TopState::ARMED ? "ARMED" :
              e.state == TopState::RUNNING ? "RUNNING" : "FAULT_STOP");
        return s;
      case EventType::FAULT:
        s = "EVT:FAULT ";
        s += String((uint16_t)e.fault);
        return s;
      case EventType::STABLE_WEIGHT:
        s = "EVT:STABLE:";
        s += String(e.v1, 2);
        return s;
      case EventType::PARAMS:
        s = "EVT:PARAM:";
        s += String(e.v1, 2);
        s += ",";
        s += String(e.v2, 4);
        return s;
      case EventType::STREAM:
        s = "EVT:STREAM:";
        s += String(e.v1, 2);
        s += ",";
        s += String(e.v2, 2);
        return s;
    }
    return "EVT:UNKNOWN";
  }
};
