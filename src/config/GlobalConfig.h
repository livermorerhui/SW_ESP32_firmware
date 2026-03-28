#pragma once
#include <Arduino.h>

// ===== Versions =====
#define FW_VER        "SW-HUB-1.0.0"
#define PROTO_VER     2

// ===== I2S =====
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000
#define BUFFER_LEN      128
#define WAVE_I2S_SAMPLE_BITS 16
#define WAVE_INTENSITY_MAX 120

#define I2S_BCLK_PIN    4
#define I2S_LRCK_PIN    5
#define I2S_DOUT_PIN    6

// ===== Wave Ramp =====
static constexpr uint32_t RAMP_START_TIME_MS = 800UL;
static constexpr uint32_t RAMP_STOP_TIME_MS = 500UL;
static constexpr uint32_t RAMP_UPDATE_INTERVAL_MS = 10UL;
static constexpr float RAMP_FREQ_STEP_HZ = 0.5f;

// ===== Laser / Modbus =====
#define RX_PIN 15
#define TX_PIN 14
#define MODBUS_BAUD 9600
#define MODBUS_SLAVE_ID 1
#define REG_DISTANCE 0x0064
// 基线建立的体感瓶颈主要来自 stable build 前置采样成本：
// 旧路径是约 200ms 读一次，且必须等到满 10 个样本后才能评估 latch。
// 本轮只在“基线尚未建立”的 build 阶段提速，运行态和安全态仍保持原读频，
// 避免为了追求更快体感而直接改 APP 表现层或放松 leave / danger stop 语义。
static constexpr uint32_t LASER_READ_INTERVAL_DEFAULT_MS = 200UL;
static constexpr uint32_t LASER_READ_INTERVAL_STABLE_BUILD_MS = 160UL;
// The sensor register is currently treated as a signed fixed-point value with
// two decimal places of displayed distance/displacement resolution.
static constexpr int16_t LASER_VALID_MEASUREMENT_MIN_RAW = -3570;
static constexpr int16_t LASER_VALID_MEASUREMENT_MAX_RAW = 3570;
static constexpr uint16_t LASER_SENTINEL_OVER_RANGE_RAW = 32767;
static constexpr float LASER_DISTANCE_RUNTIME_DIVISOR = 100.0f;
static constexpr float LASER_DISTANCE_MM_TO_RUNTIME_UNITS =
    1.0f / LASER_DISTANCE_RUNTIME_DIVISOR;
static constexpr uint32_t LASER_INVALID_LOG_INTERVAL_MS = 1000UL;

// ===== Scale Algo =====
#define WINDOW_N 10
#define MIN_WEIGHT 5.0f
#define LEAVE_TH 3.0f
#define STD_TH 0.20f
// 不能简单粗暴把所有窗口统一缩短。
// 因此保留 legacy 满窗 10 样本 + STD_TH 的兜底路径，只额外增加一个更保守的
// “9 样本提前锁定”分支：只有离散度更小、且最新样本没有明显偏离均值时才允许提前 latch。
// 这些值是本阶段把体感从约 3 秒压到约 2 秒级的阶段性参数，不代表最终全局最优。
// 本轮是 baseline build 时延点位的最后一次固件侧小范围尝试：
// 主问题不是 fallback 缺失，而是 early_strict 现场命中率仍然偏低。
// 因此这里只补一个“整窗略松、尾段更严”的 guarded 条件，专门兜住
// “整体已接近 legacy 合格，但 recent tail 明显已经站稳”的 case。
// 如果现场之后仍主要落在 legacy_full_window，就应视为本点位收益不足并停止继续投入。
static constexpr uint8_t STABLE_EARLY_LATCH_SAMPLES = 9;
static constexpr float STABLE_EARLY_STRICT_STD_TH = 0.16f;
static constexpr float STABLE_EARLY_STRICT_LATEST_DELTA_TH = 0.18f;
static constexpr float STABLE_EARLY_GUARDED_STD_TH = STD_TH;
static constexpr float STABLE_EARLY_GUARDED_LATEST_DELTA_TH = 0.24f;
static constexpr uint8_t STABLE_EARLY_GUARDED_RECENT_SAMPLES = 4;
static constexpr float STABLE_EARLY_GUARDED_RECENT_STD_TH = 0.10f;
static constexpr float STABLE_EARLY_GUARDED_RECENT_RANGE_TH = 0.22f;
static constexpr float STABLE_EARLY_GUARDED_RECENT_MEAN_DELTA_TH = 0.12f;
#define STABLE_REARM_DISTANCE_DELTA_TH 1.0f
#define STABLE_REARM_WEIGHT_DELTA_TH 0.5f
#define STABLE_INVALID_GRACE_SAMPLES 2
#define LOG_TH 1.0f
static constexpr float CALIBRATION_TARGET_RANGE_MIN_KG = 40.0f;
static constexpr float CALIBRATION_TARGET_RANGE_MAX_KG = 120.0f;
static constexpr float CALIBRATION_QUADRATIC_RMSE_IMPROVEMENT_RATIO = 0.15f;

// ===== Stream telemetry policy (Phase-1 stabilization) =====
#define DEBUG_STREAM false
#define STREAM_KEEPALIVE_MS 500UL
#define STREAM_DISTANCE_DELTA_TH 1.0f
#define STREAM_WEIGHT_DELTA_TH 0.2f
#define DEBUG_LASER_STREAM 0
#define DEBUG_BLE_TX_VERBOSE 0

// ===== Diagnostic Flags =====
#define DIAG_DISABLE_LASER_SAFETY 0
#define DIAG_DISABLE_WAVE_RAMP 0

// ===== Safety =====
#define FALL_DW_DT_SUSPECT_TH 25.0f   // kg/s（先用保守默认，后续真机调）
#define FAULT_COOLDOWN_MS     3000
#define CLEAR_CONFIRM_MS      1000
static constexpr uint32_t MOTION_SAMPLING_SUPPRESSED_FALL_NOTICE_INTERVAL_MS = 1000UL;
// 摔倒停波保护默认开启：达到跌倒危险停波候选时执行真实停波动作。
static constexpr bool FALL_STOP_ENABLED_DEFAULT = true;

// ===== Safety Policy（Task-4 对齐）=====
// 用户离台默认走“可恢复暂停”风格：停波，但不进入异常停机。
static constexpr bool SAFETY_POLICY_USER_LEFT_RECOVERABLE_PAUSE = true;
// 跌倒疑似默认走“异常停机”风格：停波并进入 FAULT_STOP。
static constexpr bool SAFETY_POLICY_FALL_ABNORMAL_STOP = true;
// BLE 断连默认仅提醒，不强制停波；产品策略可按需改为 true。
static constexpr bool SAFETY_POLICY_DISCONNECT_STOPS_WAVE = false;
// 测量不可用默认仅告警，不强制停波；产品策略可按需改为 true。
static constexpr bool SAFETY_POLICY_MEASUREMENT_UNAVAILABLE_STOPS_WAVE = false;

// ===== BLE =====
#define BLE_DEVICE_NAME "SonicWave_Hub"

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX           "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX           "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ===== WDT (optional) =====
#define ENABLE_WDT 1
static const int WDT_TIMEOUT_S = 5;
