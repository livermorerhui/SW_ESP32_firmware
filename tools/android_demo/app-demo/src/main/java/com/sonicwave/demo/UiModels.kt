package com.sonicwave.demo

import com.sonicwave.protocol.CalibrationModelType
import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.SafetyEffect
import com.sonicwave.protocol.WaveState

enum class FaultSeverityUi {
    NONE,
    WARNING,
    BLOCKING,
    INFO,
}

data class FaultStatusUi(
    val code: Int = 0,
    val label: String = "无故障",
    val codeName: String = "NONE",
    val severity: FaultSeverityUi = FaultSeverityUi.NONE,
)

data class SafetyStatusUi(
    val reason: String = "未观察到安全信号",
    val reasonCode: String = "NONE",
    val effect: String = "无影响",
    val effectCode: String = "NONE",
    val runtimeState: String = "未知",
    val runtimeCode: String = "UNKNOWN",
    val waveState: String = "未知",
    val waveCode: String = "UNKNOWN",
    val meaning: String = "",
    val source: String = "",
    val sourceCode: String = "NONE",
    val severity: FaultSeverityUi = FaultSeverityUi.NONE,
    val code: Int? = null,
    val raw: String? = null,
)

enum class WaveStartAvailabilityUi {
    DISCONNECTED,
    START_PENDING,
    STOP_PENDING,
    RUNNING,
    INVALID_PARAMETERS,
    LEFT_PLATFORM_BLOCKED,
    ABNORMAL_STOP_BLOCKED,
    SAFETY_BLOCKED,
    NOT_READY,
    READY,
}

data class TelemetryPointUi(
    val measurementSeq: Long? = null,
    val deviceTimestampMs: Long? = null,
    val elapsedMs: Long,
    val timestampMs: Long,
    // The live EVT:STREAM distance is exposed in the telemetry UI as "rhythm distance".
    val distance: Float,
    // The live EVT:STREAM weight is the app-side "rhythm weight"/"unstable weight" signal.
    val unstableWeight: Float,
    val measurementValid: Boolean = true,
    val ma12: Float? = null,
    val stableWeight: Float? = null,
    val ma3: Float? = null,
    val ma5: Float? = null,
    val ma7: Float? = null,
    val stableFlag: Boolean,
) {
    val rhythmDistance: Float
        get() = distance

    val rhythmWeight: Float
        get() = unstableWeight

    val weight: Float
        get() = unstableWeight
}

data class MeasurementDisplayUiState(
    val distance: Float? = null,
    val weight: Float? = null,
    val ma12: Float? = null,
    val measurementValid: Boolean = false,
    val lastMeasurementSequence: Long? = null,
    val telemetryPoints: List<TelemetryPointUi> = emptyList(),
)

data class RawConsoleUiState(
    val rawLogLines: List<String> = emptyList(),
)

data class TestSessionPanelUiState(
    val session: TestSessionUi? = null,
    val notice: String? = null,
)

data class MotionSamplingRowUi(
    val sampleIndex: Int,
    val measurementSeq: Long? = null,
    val deviceTimestampMs: Long? = null,
    val timestampMs: Long,
    val elapsedMs: Long,
    val distanceMm: Float,
    val liveWeightKg: Float,
    val ma12WeightKg: Float? = null,
    val stableWeightKg: Float? = null,
    val measurementValid: Boolean,
    val stableVisible: Boolean,
    val runtimeStateCode: String,
    val waveStateCode: String,
    val safetyStateCode: String,
    val safetyReasonCode: String,
    val safetyCode: Int? = null,
    val connectionStateCode: String,
    val modelTypeCode: String? = null,
    val userMarker: String? = null,
    val motionSafetyState: String? = null,
    val ddDt: Float? = null,
    val dwDt: Float? = null,
)

data class MotionSamplingSessionUi(
    val sessionId: String,
    val startedAtMs: Long,
    val endedAtMs: Long? = null,
    val schemaVersion: String = "motion_sampling_mvp_v1",
    val appVersion: String? = null,
    val firmwareMetadata: String? = null,
    val connectedDeviceName: String? = null,
    val protocolModeCode: String? = null,
    val waveFrequencyHz: Int? = null,
    val waveIntensity: Int? = null,
    val fallStopEnabled: Boolean? = null,
    val samplingModeEnabled: Boolean = false,
    val waveWasRunningAtSessionStart: Boolean = false,
    val modelTypeCode: String? = null,
    val modelReferenceDistance: Float? = null,
    val modelC0: Float? = null,
    val modelC1: Float? = null,
    val modelC2: Float? = null,
    val notes: String? = null,
    val rows: List<MotionSamplingRowUi> = emptyList(),
    val exportScenarioLabel: String? = null,
    val exportScenarioCategory: String? = null,
    val lastExportTimestampMs: Long? = null,
    val lastExportCsvPath: String? = null,
    val lastExportJsonPath: String? = null,
)

// 导出标签仍保持英文内部值，方便 metadata、日志和脚本稳定消费；
// 面向采样人员的显示文本和文件名则统一走中文映射。
enum class MotionSamplingPrimaryLabel {
    NORMAL_USE,
    LEAVE_PLATFORM,
    DANGER_STATE,
}

enum class MotionSamplingSubLabel {
    NORMAL_VIBRATION,
    LEAVE_PLATFORM,
    PARTIAL_LEAVE,
    FALL_ON_PLATFORM,
    FALL_OFF_PLATFORM,
    LEFT_RIGHT_SWAY,
    SQUAT_STAND,
    RAPID_UNLOAD,
    OTHER_DISTURBANCE,
}

data class MotionSamplingExportRequest(
    val primaryLabel: MotionSamplingPrimaryLabel,
    val subLabel: MotionSamplingSubLabel,
    val notes: String,
    val exportTimestampMs: Long,
) {
    // 继续保留旧的 scenario 字段，避免已有 JSON 消费方因为这次中文化修正而失配。
    val scenarioLabel: String
        get() = subLabel.name

    val scenarioCategory: String
        get() = primaryLabel.name
}

fun MotionSamplingPrimaryLabel.displayNameZh(): String = when (this) {
    MotionSamplingPrimaryLabel.NORMAL_USE -> "正常使用"
    MotionSamplingPrimaryLabel.LEAVE_PLATFORM -> "离开平台"
    MotionSamplingPrimaryLabel.DANGER_STATE -> "危险状态"
}

fun MotionSamplingSubLabel.displayNameZh(): String = when (this) {
    MotionSamplingSubLabel.NORMAL_VIBRATION -> "正常律动"
    MotionSamplingSubLabel.LEAVE_PLATFORM -> "离开平台"
    MotionSamplingSubLabel.PARTIAL_LEAVE -> "半离台"
    MotionSamplingSubLabel.FALL_ON_PLATFORM -> "平台上摔倒"
    MotionSamplingSubLabel.FALL_OFF_PLATFORM -> "平台外摔倒"
    MotionSamplingSubLabel.LEFT_RIGHT_SWAY -> "左右摇摆"
    MotionSamplingSubLabel.SQUAT_STAND -> "下蹲站起"
    MotionSamplingSubLabel.RAPID_UNLOAD -> "快速减载"
    MotionSamplingSubLabel.OTHER_DISTURBANCE -> "其他扰动"
}

data class CalibrationModelUi(
    val type: CalibrationModelType = CalibrationModelType.LINEAR,
    val referenceDistance: Float = 0.0f,
    val c0: Float = 0.0f,
    val c1: Float = 1.0f,
    val c2: Float = 0.0f,
    val raw: String = "",
)

data class CalibrationPointUi(
    val index: Int? = null,
    val timestampMs: Long? = null,
    val distanceMm: Float? = null,
    val referenceWeightKg: Float? = null,
    val predictedWeightKg: Float? = null,
    val liveWeightKg: Float? = null,
    val stableWeightKg: Float? = null,
    val captureRoute: CalibrationCaptureRouteUi = CalibrationCaptureRouteUi.APP_LIVE_SNAPSHOT,
    val visibleSampleValid: Boolean? = null,
    val stableFlag: Boolean? = null,
    val validFlag: Boolean? = null,
    val raw: String = "",
)

enum class CalibrationCaptureRouteUi {
    APP_LIVE_SNAPSHOT,
    DEVICE_STABLE_CAPTURE,
}

enum class PreparedCalibrationModelSourceUi {
    AUTO_SELECTED_FIT,
    MANUAL_OVERRIDE,
}

data class PreparedCalibrationModelUi(
    val type: CalibrationModelType,
    val referenceDistance: Float,
    val c0: Float,
    val c1: Float,
    val c2: Float,
    val source: PreparedCalibrationModelSourceUi,
)

data class CalibrationModelOptionUi(
    val type: CalibrationModelType,
    val selected: Boolean,
    val available: Boolean,
    val prepared: Boolean,
)

val SUPPORTED_CALIBRATION_MODEL_TYPES: List<CalibrationModelType> = listOf(
    CalibrationModelType.LINEAR,
    CalibrationModelType.QUADRATIC,
)

enum class CaptureFeedbackKind {
    INFO,
    PENDING,
    SUCCESS,
    FAILURE,
}

data class CaptureFeedbackUi(
    val kind: CaptureFeedbackKind,
    val message: String,
    val rawReason: String? = null,
)

enum class WriteModelFeedbackKind {
    PENDING,
    SUCCESS,
    FAILURE,
}

data class WriteModelFeedbackUi(
    val kind: WriteModelFeedbackKind,
    val message: String,
    val modelType: CalibrationModelType? = null,
    val rawReason: String? = null,
)

fun faultStatusFromCode(code: Int?): FaultStatusUi {
    return when (code ?: 0) {
        0 -> FaultStatusUi()
        100 -> FaultStatusUi(
            code = 100,
            label = "用户离开平台",
            codeName = "USER_LEFT_PLATFORM",
            severity = FaultSeverityUi.WARNING,
        )
        101 -> FaultStatusUi(
            code = 101,
            label = "疑似摔倒",
            codeName = "FALL_SUSPECTED",
            severity = FaultSeverityUi.BLOCKING,
        )
        102 -> FaultStatusUi(
            code = 102,
            label = "蓝牙连接中断",
            codeName = "BLE_DISCONNECTED",
            severity = FaultSeverityUi.WARNING,
        )
        200 -> FaultStatusUi(
            code = 200,
            label = "测量不可用",
            codeName = "MEASUREMENT_UNAVAILABLE",
            severity = FaultSeverityUi.WARNING,
        )
        else -> FaultStatusUi(
            code = code ?: -1,
            label = "未知故障",
            codeName = "UNKNOWN(${code ?: -1})",
            severity = FaultSeverityUi.INFO,
        )
    }
}

fun safetyReasonLabel(raw: String): String = when (raw.uppercase()) {
    "NONE" -> "未观察到安全信号"
    "USER_LEFT_PLATFORM" -> "用户离开平台"
    "FALL_SUSPECTED" -> "疑似摔倒"
    "BLE_DISCONNECTED" -> "蓝牙连接中断"
    "MEASUREMENT_UNAVAILABLE" -> "测量不可用"
    else -> "未知安全原因"
}

fun safetyEffectLabel(raw: String): String = when (raw.uppercase()) {
    "NONE" -> "无影响"
    "WARNING_ONLY" -> "仅警告"
    "RECOVERABLE_PAUSE" -> "可恢复暂停"
    "ABNORMAL_STOP" -> "异常停止"
    else -> "未知影响"
}

fun runtimeStateLabel(raw: String): String = when (raw.uppercase()) {
    "IDLE" -> "空闲"
    "ARMED", "READY" -> "就绪"
    "RUNNING" -> "运行中"
    "FAULT_STOP", "FAULT" -> "故障停止"
    "PAUSED" -> "暂停"
    "STOPPED" -> "已停止"
    "DISCONNECTED" -> "连接已断开"
    else -> "未知"
}

fun waveStateLabel(raw: String): String = when (raw.uppercase()) {
    "RUNNING", "ACTIVE" -> "运行中"
    "STOPPED" -> "已停止"
    else -> "未知"
}

fun DeviceState.displayName(): String = runtimeStateLabel(name)

fun WaveState.displayName(): String = waveStateLabel(name)

fun UiState.hasValidWaveInputs(): Boolean {
    val freqValue = freqInput.toIntOrNull()
    val intensityValue = intensityInput.toIntOrNull()
    return freqValue != null &&
        intensityValue != null &&
        intensityValue in 0..120
}

fun UiState.hasStableWeightEvidence(): Boolean {
    return stableWeight != null && deviceBaselineReady == true
}

fun UiState.waveStartAvailability(): WaveStartAvailabilityUi {
    val leftPlatformBlocked = safetyStatus.reasonCode.equals("USER_LEFT_PLATFORM", ignoreCase = true) ||
        deviceReasonCode.equals("USER_LEFT_PLATFORM", ignoreCase = true) ||
        faultStatus.codeName.equals("USER_LEFT_PLATFORM", ignoreCase = true) ||
        faultStatus.code == 100
    val abnormalStopBlocked = deviceState == DeviceState.FAULT_STOP ||
        safetyStatus.effectCode == SafetyEffect.ABNORMAL_STOP.name ||
        deviceSafetyEffectCode == SafetyEffect.ABNORMAL_STOP.name ||
        faultStatus.severity == FaultSeverityUi.BLOCKING
    val safetyBlocked = safetyStatus.effectCode == SafetyEffect.RECOVERABLE_PAUSE.name ||
        deviceSafetyEffectCode == SafetyEffect.RECOVERABLE_PAUSE.name
    val hasStableEvidence = hasStableWeightEvidence()
    val startReady = hasStableEvidence &&
        deviceStartReady == true &&
        deviceBaselineReady == true

    return when {
        !isConnected -> WaveStartAvailabilityUi.DISCONNECTED
        isWaveStopPending -> WaveStartAvailabilityUi.STOP_PENDING
        isWaveStartPending -> WaveStartAvailabilityUi.START_PENDING
        waveOutputActive -> WaveStartAvailabilityUi.RUNNING
        !hasValidWaveInputs() -> WaveStartAvailabilityUi.INVALID_PARAMETERS
        leftPlatformBlocked -> WaveStartAvailabilityUi.LEFT_PLATFORM_BLOCKED
        abnormalStopBlocked -> WaveStartAvailabilityUi.ABNORMAL_STOP_BLOCKED
        safetyBlocked -> WaveStartAvailabilityUi.SAFETY_BLOCKED
        startReady -> WaveStartAvailabilityUi.READY
        else -> WaveStartAvailabilityUi.NOT_READY
    }
}

fun UiState.canStartWave(): Boolean = waveStartAvailability() == WaveStartAvailabilityUi.READY
