package com.sonicwave.demo

import com.sonicwave.protocol.CalibrationModelType
import com.sonicwave.protocol.DeviceState
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

data class TelemetryPointUi(
    val elapsedMs: Long,
    val timestampMs: Long,
    val distance: Float,
    val weight: Float,
    val stableFlag: Boolean,
)

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
