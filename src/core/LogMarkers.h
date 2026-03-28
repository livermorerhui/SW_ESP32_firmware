#pragma once

namespace LogMarker {

// Keep the marker set intentionally small and stable. These are scan aids only;
// the readable text that follows remains the primary meaning of each log line.
constexpr const char* kBleConnected = "🔗";
constexpr const char* kBleDisconnected = "🔌";
constexpr const char* kBaselineReady = "📍";
constexpr const char* kTestStart = "▶️";
constexpr const char* kTestStop = "⏹️";
constexpr const char* kTestAbort = "🛑";
constexpr const char* kFsm = "🔄";
constexpr const char* kResearch = "🧭";
constexpr const char* kSafety = "⚠️";
constexpr const char* kFaultBlock = "🚫";
constexpr const char* kClear = "✅";
constexpr const char* kWave = "🌊";

}  // namespace LogMarker
