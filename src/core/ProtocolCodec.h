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

    auto readParam = [&](const String& key, String& valueOut) -> bool {
      int idx = s.indexOf(key);
      if (idx < 0) return false;
      int beg = idx + key.length();
      int end = s.length();

      int comma = s.indexOf(',', beg);
      if (comma >= 0 && comma < end) end = comma;
      int space = s.indexOf(' ', beg);
      if (space >= 0 && space < end) end = space;

      valueOut = s.substring(beg, end);
      valueOut.trim();
      return valueOut.length() > 0;
    };

    // CAP
    if (s.equalsIgnoreCase("CAP?")) { out.type = CmdType::CAP_QUERY; return true; }

    // New protocol.
    // Accept both:
    //  - WAVE:SET f=<float>,i=<int>   (v1 minimal set)
    //  - WAVE:SET freq=<float> amp=<int> (compat)
    if (s.startsWith("WAVE:SET")) {
      out.type = CmdType::WAVE_SET;

      String fStr, iStr;
      bool hasF = readParam("f=", fStr) || readParam("freq=", fStr);
      bool hasI = readParam("i=", iStr) || readParam("amp=", iStr);
      if (!hasF || !hasI) { err = "INVALID_PARAM"; return false; }

      float f = fStr.toFloat();
      int a = iStr.toInt();
      if (f < 0 || f > 50.0f || a < 0 || a > 120) { err = "INVALID_PARAM"; return false; }
      out.wave.freqHz = f; out.wave.intensity = a;
      return true;
    }
    if (s.equalsIgnoreCase("WAVE:START")) { out.type = CmdType::WAVE_START; return true; }
    if (s.equalsIgnoreCase("WAVE:STOP"))  { out.type = CmdType::WAVE_STOP;  return true; }
    if (s.equalsIgnoreCase("SCALE:ZERO")) { out.type = CmdType::SCALE_ZERO; return true; }
    if (s.equalsIgnoreCase("CAL:ZERO"))   { out.type = CmdType::SCALE_ZERO; return true; }
    if (s.startsWith("SCALE:CAL")) {
      out.type = CmdType::SCALE_CAL;
      String zStr, kStr;
      bool hasZ = readParam("z=", zStr);
      bool hasK = readParam("k=", kStr);
      if (!hasZ || !hasK) { err = "INVALID_PARAM"; return false; }
      out.p1 = zStr.toFloat();
      out.p2 = kStr.toFloat();
      return true;
    }
    if (s.startsWith("CAL:CAPTURE")) {
      out.type = CmdType::CAL_CAPTURE;
      String wStr;
      bool hasW = readParam("w=", wStr) || readParam("ref=", wStr);
      if (!hasW) { err = "INVALID_PARAM"; return false; }
      out.capture.referenceWeightKg = wStr.toFloat();
      return true;
    }
    if (s.equalsIgnoreCase("CAL:GET_MODEL")) {
      out.type = CmdType::CAL_GET_MODEL;
      return true;
    }
    if (s.startsWith("CAL:SET_MODEL")) {
      out.type = CmdType::CAL_SET_MODEL;

      String typeStr, refStr, c0Str, c1Str, c2Str;
      bool hasType = readParam("type=", typeStr) || readParam("t=", typeStr);
      bool hasRef = readParam("ref=", refStr);
      bool hasC0 = readParam("c0=", c0Str) || readParam("a=", c0Str);
      bool hasC1 = readParam("c1=", c1Str) || readParam("b=", c1Str);
      bool hasC2 = readParam("c2=", c2Str) || readParam("c=", c2Str);
      if (!hasType || !hasRef || !hasC0 || !hasC1 || !hasC2) {
        err = "INVALID_PARAM";
        return false;
      }

      if (typeStr.equalsIgnoreCase("LINEAR") || typeStr == "1") {
        out.model.type = 1;
      } else if (typeStr.equalsIgnoreCase("QUADRATIC") || typeStr == "2") {
        out.model.type = 2;
      } else {
        err = "INVALID_PARAM";
        return false;
      }

      out.model.referenceDistance = refStr.toFloat();
      out.model.coefficients[0] = c0Str.toFloat();
      out.model.coefficients[1] = c1Str.toFloat();
      out.model.coefficients[2] = c2Str.toFloat();
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
      case EventType::SAFETY:
        s = "EVT:SAFETY reason=";
        s += faultCodeName(e.fault);
        s += " code=";
        s += String((uint16_t)e.fault);
        s += " effect=";
        s += safetySignalName(e.safety);
        s += " state=";
        s += topStateName(e.state);
        s += " wave=";
        s += e.waveStopped ? "STOPPED" : "RUNNING";
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
