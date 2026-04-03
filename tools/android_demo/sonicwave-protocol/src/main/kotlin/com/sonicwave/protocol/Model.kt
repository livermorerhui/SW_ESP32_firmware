package com.sonicwave.protocol

sealed class Command {
    data object CapabilityQuery : Command()
    data object SnapshotQuery : Command()
    data class DeviceSetConfig(
        val platformModel: PlatformModel,
        val laserInstalled: Boolean,
    ) : Command()
    data class WaveSet(val freqHz: Int, val intensity: Int) : Command()
    data object WaveStart : Command()
    data object WaveStop : Command()
    data object ScaleZero : Command()
    data object CalibrationZero : Command()
    data class ScaleCal(
        val zeroDistance: Float = -22.0f,
        val scaleFactor: Float = 1.0f,
    ) : Command()
    data class CalibrationCapture(val referenceWeightKg: Float) : Command()
    data object CalibrationGetModel : Command()
    data class CalibrationSetModel(
        val type: CalibrationModelType,
        val referenceDistance: Float,
        val c0: Float,
        val c1: Float,
        val c2: Float,
    ) : Command()
    data class FallStopProtectionSet(val enabled: Boolean) : Command()
    data class MotionSamplingModeSet(val enabled: Boolean) : Command()

    // Legacy commands for fallback mode.
    data object LegacyZero : Command()
    data class LegacySetPs(val zeroDistance: Float, val scaleFactor: Float) : Command()
    data class LegacyWaveFie(
        val freqHz: Int? = null,
        val intensity: Int? = null,
        val enable: Boolean? = null,
    ) : Command()
    data class LegacyRaw(val raw: String) : Command()
}

enum class DeviceState {
    IDLE,
    ARMED,
    RUNNING,
    FAULT_STOP,
    UNKNOWN,
}

enum class SafetyEffect {
    WARNING_ONLY,
    RECOVERABLE_PAUSE,
    ABNORMAL_STOP,
    UNKNOWN,
}

enum class WaveState {
    STOPPED,
    RUNNING,
    UNKNOWN,
}

enum class CalibrationModelType {
    LINEAR,
    QUADRATIC,
}

enum class PlatformModel {
    BASE,
    PLUS,
    PRO,
    ULTRA,
}

enum class MeasurementCarrier {
    FORMAL_EVT_STREAM,
    LEGACY_CSV_FALLBACK,
}

sealed class Event {
    data class State(val state: DeviceState) : Event()
    data class Snapshot(
        val topState: DeviceState,
        val userPresent: Boolean?,
        val runtimeReady: Boolean?,
        val startReady: Boolean?,
        val baselineReady: Boolean?,
        val waveOutputActive: Boolean?,
        val currentReasonCode: String?,
        val currentSafetyEffect: String?,
        val stableWeightKg: Float?,
        val currentFrequencyHz: Float?,
        val currentIntensity: Int?,
        val platformModel: PlatformModel?,
        val laserInstalled: Boolean?,
        val laserAvailable: Boolean?,
        val protectionDegraded: Boolean?,
        val raw: String,
    ) : Event()
    data class WaveOutput(
        val active: Boolean,
        val raw: String,
    ) : Event()
    data class Fault(val code: Int?, val reason: String) : Event()
    // 基线型主判断 verification contract。
    // 这里的字段应优先视为 firmware truth，而不是前端派生值。
    data class BaselineMain(
        val startReady: Boolean?,
        val baselineReady: Boolean,
        val stableWeightKg: Float?,
        val ma12WeightKg: Float?,
        val deviationKg: Float?,
        val ratio: Float?,
        val mainState: String,
        val abnormalDurationMs: Long?,
        val dangerDurationMs: Long?,
        val stopReason: String,
        val stopSource: String,
        val raw: String,
    ) : Event()
    data class Stop(
        val stopReason: String,
        val stopSource: String,
        val code: Int?,
        val effect: SafetyEffect,
        val state: DeviceState,
        val raw: String,
    ) : Event()
    data class Safety(
        val reason: String,
        val code: Int?,
        val effect: SafetyEffect,
        val state: DeviceState,
        val wave: WaveState,
        val extras: Map<String, String>,
        val raw: String,
    ) : Event()
    data class Nack(val reason: String) : Event()
    data class Error(val reason: String) : Event()
    data class Param(
        val zeroDistance: Float?,
        val scaleFactor: Float?,
        val extras: Map<String, String>,
    ) : Event()

    data class Stable(val stableWeightKg: Float?, val raw: String) : Event()
    data class StreamSample(
        val carrier: MeasurementCarrier,
        val sequence: Long?,
        val timestampMs: Long?,
        val distance: Float?,
        val weight: Float?,
        val ma12: Float?,
        val ma12Ready: Boolean,
        val valid: Boolean,
        val reason: String?,
        val raw: String,
    ) : Event()
    data class CalibrationPoint(
        val index: Int?,
        val timestampMs: Long?,
        val distanceMm: Float?,
        val referenceWeightKg: Float?,
        val predictedWeightKg: Float?,
        val stableFlag: Boolean?,
        val validFlag: Boolean?,
        val raw: String,
    ) : Event()
    data class CalibrationModel(
        val type: CalibrationModelType?,
        val referenceDistance: Float?,
        val c0: Float?,
        val c1: Float?,
        val c2: Float?,
        val raw: String,
    ) : Event()
    data class CalibrationSetModelResult(
        val success: Boolean,
        val type: CalibrationModelType?,
        val reason: String?,
        val raw: String,
    ) : Event()
    data class DeviceConfig(
        val platformModel: PlatformModel?,
        val laserInstalled: Boolean?,
        val raw: String,
    ) : Event()
    data class Capabilities(val values: Map<String, String>, val raw: String) : Event()
    data class LegacyInfo(val message: String) : Event()
    data class Ack(val raw: String) : Event()
}

enum class ProtocolMode {
    UNKNOWN,
    PRIMARY,
    LEGACY,
}

data class CapabilityResult(
    val mode: ProtocolMode,
    val capabilities: Event.Capabilities? = null,
    val reason: String? = null,
)
