#pragma once
#include "DeviceConfig.h"
#include "Types.h"

enum class CmdType : uint8_t {
  CAP_QUERY,
  DEVICE_IDENTITY_QUERY,
  DEVICE_SECURITY_QUERY,
  DEVICE_GET_CONFIG,
  DEVICE_SET_PROFILE_CONFIG,
  DEVICE_SET_CONFIG,
  DEGRADED_START_SET,
  WAVE_SET,      // set freq/intensity (not necessarily start)
  WAVE_START,
  WAVE_STOP,
  AUDIO_CUE,
  SCALE_ZERO,
  // Formal calibration reset; distinct from the legacy SCALE:ZERO ACK.
  CAL_ZERO,
  SCALE_CAL,
  CAL_CAPTURE,
  CAL_GET_MODEL,
  CAL_SET_MODEL,
  EMPTY_DISTANCE_GET,
  EMPTY_DISTANCE_SET_CURRENT,
  EMPTY_DISTANCE_SET,
  EMPTY_DISTANCE_CLEAR,
  CAL_CLEAR_WEIGHT_MODEL,
  FALL_STOP_SET,
  LEAVE_PROTECTION_SET,
  MOTION_SAMPLING_MODE_SET,
  STREAM_SET,
  LEGACY_FIE     // 兼容 F/I/E 组合命令
};

// One-shot audio cues are separate from the low-frequency WAVE control path.
// Keep the identifier in the command contract so the parser, HubHandler and
// WaveModule cannot silently invent different spellings or meanings.
enum class AudioCueId : uint8_t {
  CONNECT = 0,
  DISCONNECT = 1,
};

inline const char* audioCueIdName(AudioCueId id) {
  switch (id) {
    case AudioCueId::CONNECT: return "CONNECT";
    case AudioCueId::DISCONNECT: return "DISCONNECT";
  }
  return "UNKNOWN";
}

struct AudioCueCommand {
  AudioCueId id = AudioCueId::CONNECT;
};

struct CalibrationCaptureRequest {
  float referenceWeightKg = 0.0f;
};

struct CalibrationModelCommand {
  uint8_t type = 1;
  float referenceDistance = 0.0f;
  float coefficients[3] = {0.0f, 1.0f, 0.0f};
};

struct EmptyDistanceCommand {
  float distanceMm = NAN;
};

struct MotionSamplingModeCommand {
  bool enabled = false;
};

struct FallStopCommand {
  bool enabled = true;
};

struct LeaveProtectionCommand {
  bool enabled = true;
};

struct DeviceConfigCommand {
  PlatformModel platformModel = PlatformModel::PLUS;
  bool laserInstalled = true;
};

// The profile-aware configuration command is distinct from the legacy
// platform_model/laser_installed command. The latter must never silently
// select a sensor when an older Demo APP writes its existing request.
struct DeviceProfileConfigCommand {
  PlatformModel platformModel = PlatformModel::BASE;
  SensorProfileId sensorProfile = SensorProfileId::NONE;
};

struct DegradedStartCommand {
  bool enabled = false;
};

struct StreamSubscriptionCommand {
  bool enabled = false;
  uint8_t rateHz = 10;
};

struct Command {
  CmdType type = CmdType::CAP_QUERY;
  WaveParams wave{};
  AudioCueCommand audioCue{};
  float p1 = 0; // zero
  float p2 = 0; // factor
  CalibrationCaptureRequest capture{};
  CalibrationModelCommand model{};
  EmptyDistanceCommand emptyDistance{};
  FallStopCommand fallStop{};
  LeaveProtectionCommand leaveProtection{};
  MotionSamplingModeCommand motionSamplingMode{};
  DeviceConfigCommand deviceConfig{};
  DeviceProfileConfigCommand deviceProfileConfig{};
  DegradedStartCommand degradedStart{};
  StreamSubscriptionCommand streamSubscription{};

  // Captured by BleTransport when the command enters the control queue.
  // They are not wire fields; they fence delayed work from an older BLE
  // connection before it reaches a business owner.
  uint32_t transportSessionId = 0;
  uint16_t connectionId = 0;
  bool connectionIdKnown = false;
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
