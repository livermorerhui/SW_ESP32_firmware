#include "CommandDecoder.h"

#include <math.h>

#include "core/ProtocolCodec.h"

bool CommandDecoder::decode(const String& in, Command& out, String& err) {
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

    auto parseBoolValue = [&](const String& raw, bool& valueOut) -> bool {
      if (raw.equalsIgnoreCase("1") ||
          raw.equalsIgnoreCase("true") ||
          raw.equalsIgnoreCase("on") ||
          raw.equalsIgnoreCase("enable") ||
          raw.equalsIgnoreCase("enabled")) {
        valueOut = true;
        return true;
      }
      if (raw.equalsIgnoreCase("0") ||
          raw.equalsIgnoreCase("false") ||
          raw.equalsIgnoreCase("off") ||
          raw.equalsIgnoreCase("disable") ||
          raw.equalsIgnoreCase("disabled")) {
        valueOut = false;
        return true;
      }
      return false;
    };

    // CAP
    if (s.equalsIgnoreCase("CAP?")) { out.type = CmdType::CAP_QUERY; return true; }
    if (s.equalsIgnoreCase("DEVICE:IDENTITY?")) {
      out.type = CmdType::DEVICE_IDENTITY_QUERY;
      return true;
    }
    if (s.equalsIgnoreCase("DEVICE:SECURITY?")) {
      out.type = CmdType::DEVICE_SECURITY_QUERY;
      return true;
    }
    // Keep this exact query ahead of the legacy F/I/E substring parser below:
    // DEVICE:GET_CONFIG contains "E:" and would otherwise be mistaken for a
    // legacy wave command.
    if (s.equalsIgnoreCase("DEVICE:GET_CONFIG")) {
      out.type = CmdType::DEVICE_GET_CONFIG;
      return true;
    }
    if (s.startsWith("DEVICE:GET_CONFIG")) {
      err = "DEVICE_CONFIG op=GET reason=INVALID_PARAM";
      return false;
    }
    if (s.startsWith("DEVICE:SET_CONFIG ") &&
        ProtocolCodec::containsAsciiIgnoreCase(s, "sensor_profile")) {
      // Profile-aware SET has exactly two named fields. The old SET parser
      // intentionally remains separate so laser_installed cannot act as an
      // implicit sensor selection. Reject duplicate/unknown fields instead of
      // allowing a misleading ACK after ignoring part of the request.
      const String prefix = "DEVICE:SET_CONFIG ";
      String params = s.substring(prefix.length());
      String modelRaw;
      String profileRaw;
      bool seenModel = false;
      bool seenProfile = false;
      int fieldCount = 0;

      while (params.length() > 0) {
        const int comma = params.indexOf(',');
        String field = (comma < 0) ? params : params.substring(0, comma);
        field.trim();
        if (field.length() == 0) {
          err = "DEVICE_CONFIG op=SET reason=INVALID_PARAM";
          return false;
        }
        if (field.startsWith("platform_model=")) {
          if (seenModel) {
            err = "DEVICE_CONFIG op=SET reason=INVALID_PARAM";
            return false;
          }
          modelRaw = field.substring(String("platform_model=").length());
          seenModel = true;
        } else if (field.startsWith("sensor_profile=")) {
          if (seenProfile) {
            err = "DEVICE_CONFIG op=SET reason=INVALID_PARAM";
            return false;
          }
          profileRaw = field.substring(String("sensor_profile=").length());
          seenProfile = true;
        } else {
          err = "DEVICE_CONFIG op=SET reason=INVALID_PARAM";
          return false;
        }
        ++fieldCount;
        if (comma < 0) break;
        params = params.substring(comma + 1);
        if (params.length() == 0) {
          err = "DEVICE_CONFIG op=SET reason=INVALID_PARAM";
          return false;
        }
      }

      if (fieldCount != 2 || !seenModel || !seenProfile ||
          modelRaw.length() == 0 || profileRaw.length() == 0) {
        err = "DEVICE_CONFIG op=SET reason=INVALID_PARAM";
        return false;
      }

      PlatformModel model = PlatformModel::BASE;
      if (!parsePlatformModel(modelRaw, model) ||
          (model != PlatformModel::BASE && model != PlatformModel::PLUS)) {
        err = "DEVICE_CONFIG op=SET reason=UNSUPPORTED_MODEL";
        return false;
      }

      SensorProfileId profile = SensorProfileId::NONE;
      if (!parseSensorProfile(profileRaw, profile)) {
        err = "DEVICE_CONFIG op=SET reason=UNSUPPORTED_PROFILE";
        return false;
      }

      if (model == PlatformModel::BASE && profile != SensorProfileId::NONE) {
        err = "DEVICE_CONFIG op=SET reason=INVALID_COMBINATION";
        return false;
      }
      out.type = CmdType::DEVICE_SET_PROFILE_CONFIG;
      out.deviceProfileConfig.platformModel = model;
      out.deviceProfileConfig.sensorProfile = profile;
      return true;
    }
    if (s.startsWith("DEVICE:SET_CONFIG")) {
      out.type = CmdType::DEVICE_SET_CONFIG;

      String modelStr, laserInstalledStr;
      bool hasModel = readParam("platform_model=", modelStr) || readParam("model=", modelStr);
      bool hasLaserInstalled =
          readParam("laser_installed=", laserInstalledStr) || readParam("laser=", laserInstalledStr);
      if (!hasModel || !hasLaserInstalled) {
        err = "INVALID_PARAM";
        return false;
      }

      PlatformModel platformModel = PlatformModel::PLUS;
      if (!parsePlatformModel(modelStr, platformModel)) {
        err = "INVALID_PARAM";
        return false;
      }

      bool laserInstalled = false;
      if (!parseBoolValue(laserInstalledStr, laserInstalled)) {
        err = "INVALID_PARAM";
        return false;
      }

      out.deviceConfig.platformModel = platformModel;
      out.deviceConfig.laserInstalled = laserInstalled;
      return true;
    }
    if (s.startsWith("DEBUG:DEGRADED_START")) {
      out.type = CmdType::DEGRADED_START_SET;

      String enabledStr;
      bool hasEnabled = readParam("enabled=", enabledStr) || readParam("mode=", enabledStr);
      if (!hasEnabled) { err = "INVALID_PARAM"; return false; }

      bool enabled = false;
      if (!parseBoolValue(enabledStr, enabled)) {
        err = "INVALID_PARAM";
        return false;
      }
      out.degradedStart.enabled = enabled;
      return true;
    }
    if (s.startsWith("STREAM:SET")) {
      out.type = CmdType::STREAM_SET;

      String enabledStr, rateStr;
      bool hasEnabled = readParam("enabled=", enabledStr) || readParam("mode=", enabledStr);
      if (!hasEnabled) { err = "INVALID_PARAM"; return false; }

      bool enabled = false;
      if (!parseBoolValue(enabledStr, enabled)) {
        err = "INVALID_PARAM";
        return false;
      }

      int rateHz = ProtocolCodec::kStreamDefaultRateHz;
      if (readParam("rate_hz=", rateStr) || readParam("rate=", rateStr) || readParam("hz=", rateStr)) {
        if (!ProtocolCodec::parseStrictInt(rateStr, rateHz)) {
          err = "INVALID_PARAM";
          return false;
        }
      }
      if (rateHz < ProtocolCodec::kStreamMinRateHz || rateHz > ProtocolCodec::kStreamMaxRateHz) {
        err = "INVALID_PARAM";
        return false;
      }

      out.streamSubscription.enabled = enabled;
      out.streamSubscription.rateHz = static_cast<uint8_t>(rateHz);
      return true;
    }

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

      float f = 0.0f;
      int a = 0;
      if (!ProtocolCodec::parseStrictFloat(fStr, f) || !ProtocolCodec::parseStrictInt(iStr, a)) {
        err = "INVALID_PARAM";
        return false;
      }
      if (f < 0 || f > 50.0f || a < 0 || a > 120) { err = "INVALID_PARAM"; return false; }
      out.wave.freqHz = f; out.wave.intensity = a;
      return true;
    }
    if (s.equalsIgnoreCase("WAVE:START")) { out.type = CmdType::WAVE_START; return true; }
    if (s.equalsIgnoreCase("WAVE:STOP"))  { out.type = CmdType::WAVE_STOP;  return true; }
    if (s.startsWith("AUDIO:CUE ") || s.equalsIgnoreCase("AUDIO:CUE")) {
      out.type = CmdType::AUDIO_CUE;

      // Keep this command deliberately strict. CUE is optional and one-shot;
      // accepting a partially parsed or unknown id could produce an audible
      // event for a stale/typoed command.
      String rest = s.substring(String("AUDIO:CUE").length());
      rest.trim();
      if (!rest.startsWith("id=")) {
        err = "INVALID_PARAM";
        return false;
      }
      String id = rest.substring(3);
      id.trim();
      if (id.length() == 0 || id.indexOf(' ') >= 0 || id.indexOf(',') >= 0) {
        err = "INVALID_PARAM";
        return false;
      }
      if (id.equalsIgnoreCase("CONNECT")) {
        out.audioCue.id = AudioCueId::CONNECT;
        return true;
      }
      if (id.equalsIgnoreCase("DISCONNECT")) {
        out.audioCue.id = AudioCueId::DISCONNECT;
        return true;
      }
      err = "INVALID_PARAM";
      return false;
    }
    if (s.equalsIgnoreCase("SCALE:ZERO")) { out.type = CmdType::SCALE_ZERO; return true; }
    if (s.equalsIgnoreCase("CAL:ZERO"))   { out.type = CmdType::CAL_ZERO; return true; }
    if (s.startsWith("SCALE:CAL")) {
      out.type = CmdType::SCALE_CAL;
      String zStr, kStr;
      bool hasZ = readParam("z=", zStr);
      bool hasK = readParam("k=", kStr);
      if (!hasZ || !hasK) { err = "INVALID_PARAM"; return false; }
      if (!ProtocolCodec::parseStrictFloat(zStr, out.p1) || !ProtocolCodec::parseStrictFloat(kStr, out.p2)) {
        err = "INVALID_PARAM";
        return false;
      }
      return true;
    }
    if (s.startsWith("CAL:CAPTURE")) {
      out.type = CmdType::CAL_CAPTURE;
      String wStr;
      bool hasW = readParam("w=", wStr) || readParam("ref=", wStr);
      if (!hasW) { err = "INVALID_PARAM"; return false; }
      if (!ProtocolCodec::parseStrictFloat(wStr, out.capture.referenceWeightKg)) {
        err = "INVALID_PARAM";
        return false;
      }
      return true;
    }
    if (s.equalsIgnoreCase("CAL:GET_MODEL")) {
      out.type = CmdType::CAL_GET_MODEL;
      return true;
    }
    if (s.equalsIgnoreCase("EMPTY:GET")) {
      out.type = CmdType::EMPTY_DISTANCE_GET;
      return true;
    }
    if (s.equalsIgnoreCase("EMPTY:SET_CURRENT")) {
      out.type = CmdType::EMPTY_DISTANCE_SET_CURRENT;
      return true;
    }
    if (s.startsWith("EMPTY:SET_CURRENT")) {
      err = "INVALID_PARAM";
      return false;
    }
    if (s.startsWith("EMPTY:SET")) {
      out.type = CmdType::EMPTY_DISTANCE_SET;
      String distanceStr;
      if (!readParam("distance_mm=", distanceStr)) {
        err = "INVALID_PARAM";
        return false;
      }
      if (!ProtocolCodec::parseStrictFloat(distanceStr, out.emptyDistance.distanceMm) ||
          !isfinite(out.emptyDistance.distanceMm) ||
          out.emptyDistance.distanceMm <= 0.0f) {
        err = "INVALID_PARAM";
        return false;
      }
      return true;
    }
    if (s.equalsIgnoreCase("EMPTY:CLEAR")) {
      out.type = CmdType::EMPTY_DISTANCE_CLEAR;
      return true;
    }
    if (s.equalsIgnoreCase("CAL:CLEAR_WEIGHT_MODEL")) {
      out.type = CmdType::CAL_CLEAR_WEIGHT_MODEL;
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

      if (!ProtocolCodec::parseStrictFloat(refStr, out.model.referenceDistance) ||
          !ProtocolCodec::parseStrictFloat(c0Str, out.model.coefficients[0]) ||
          !ProtocolCodec::parseStrictFloat(c1Str, out.model.coefficients[1]) ||
          !ProtocolCodec::parseStrictFloat(c2Str, out.model.coefficients[2])) {
        err = "INVALID_PARAM";
        return false;
      }
      return true;
    }
    if (s.startsWith("DEBUG:FALL_STOP")) {
      out.type = CmdType::FALL_STOP_SET;

      String enabledStr;
      bool hasEnabled = readParam("enabled=", enabledStr) || readParam("mode=", enabledStr);
      if (!hasEnabled) { err = "INVALID_PARAM"; return false; }

      bool enabled = false;
      if (!parseBoolValue(enabledStr, enabled)) {
        err = "INVALID_PARAM";
        return false;
      }
      out.fallStop.enabled = enabled;
      return true;
    }
    if (s.startsWith("SAFETY:LEAVE_PROTECTION") || s.startsWith("DEBUG:LEAVE_STOP")) {
      out.type = CmdType::LEAVE_PROTECTION_SET;

      String enabledStr;
      bool hasEnabled = readParam("enabled=", enabledStr) || readParam("mode=", enabledStr);
      if (!hasEnabled) { err = "INVALID_PARAM"; return false; }

      bool enabled = false;
      if (!parseBoolValue(enabledStr, enabled)) {
        err = "INVALID_PARAM";
        return false;
      }
      out.leaveProtection.enabled = enabled;
      return true;
    }
    if (s.startsWith("DEBUG:MOTION_SAMPLING")) {
      out.type = CmdType::MOTION_SAMPLING_MODE_SET;

      String enabledStr;
      bool hasEnabled = readParam("enabled=", enabledStr) || readParam("mode=", enabledStr);
      if (!hasEnabled) { err = "INVALID_PARAM"; return false; }

      bool enabled = false;
      if (!parseBoolValue(enabledStr, enabled)) {
        err = "INVALID_PARAM";
        return false;
      }
      out.motionSamplingMode.enabled = enabled;
      return true;
    }

    // Legacy scale
    if (s.equalsIgnoreCase("ZERO")) { out.type = CmdType::SCALE_ZERO; return true; }
    if (s.startsWith("SET_PS:")) {
      out.type = CmdType::SCALE_CAL;
      String p = s.substring(7);
      int idx = p.indexOf(',');
      if (idx <= 0) { err = "INVALID_PARAM"; return false; }
      if (!ProtocolCodec::parseStrictFloat(p.substring(0, idx), out.p1) ||
          !ProtocolCodec::parseStrictFloat(p.substring(idx + 1), out.p2)) {
        err = "INVALID_PARAM";
        return false;
      }
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
        if (!ProtocolCodec::parseStrictFloat(sub, out.wave.freqHz)) {
          err = "INVALID_PARAM";
          return false;
        }
      } else out.wave.freqHz = -1; // 表示“未触碰”
      // intensity
      int iIndex = s.indexOf("I:");
      if (iIndex != -1) {
        int comma = s.indexOf(",", iIndex);
        String sub = (comma == -1) ? s.substring(iIndex + 2) : s.substring(iIndex + 2, comma);
        if (!ProtocolCodec::parseStrictInt(sub, out.wave.intensity)) {
          err = "INVALID_PARAM";
          return false;
        }
      } else out.wave.intensity = -1;
      // enable
      int eIndex = s.indexOf("E:");
      if (eIndex != -1) {
        int comma = s.indexOf(",", eIndex);
        String sub = (comma == -1) ? s.substring(eIndex + 2) : s.substring(eIndex + 2, comma);
        int enableInt = 0;
        if (!ProtocolCodec::parseStrictInt(sub, enableInt)) {
          err = "INVALID_PARAM";
          return false;
        }
        out.wave.enable = (enableInt != 0);
        out.wave.hasEnable = true;
      } else {
        out.wave.hasEnable = false;
      }
      return true;
    }

    err = "UNKNOWN_CMD";
    return false;
  }


