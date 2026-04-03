package com.sonicwave.demo

import java.util.Locale
import kotlin.math.abs

enum class TestSessionStatusUi {
    IDLE,
    RECORDING,
    FINISHED,
}

data class SessionValueRangeUi(
    val min: Float,
    val max: Float,
) {
    fun toExportString(): String = String.format(Locale.US, "%.2f..%.2f", min, max)
}

data class TestSessionSampleUi(
    val measurementSeq: Long?,
    val deviceTimestampMs: Long?,
    val timestampMs: Long,
    val measurementValid: Boolean,
    val baselineReady: Boolean,
    val stableWeight: Float?,
    val weight: Float,
    val distance: Float?,
    val ma12: Float?,
    val ma3: Float?,
    val ma5: Float?,
    val mainMa12: Float?,
    val deviation: Float?,
    val ratio: Float?,
    val mainState: String,
    val abnormalDurationMs: Long?,
    val dangerDurationMs: Long?,
    val stopReason: String,
    val stopSource: String,
    val eventAux: String,
    val riskAdvisory: String,
)

data class TestSessionSummaryUi(
    val testId: Long? = null,
    val freqHz: Float? = null,
    val intensity: Int? = null,
    val intensityNorm: Float? = null,
    val fallStopEnabled: Boolean? = null,
    val startTimeMs: Long,
    val endTimeMs: Long? = null,
    val durationMs: Long = 0L,
    val result: String? = null,
    val stopReason: String? = null,
    val stopSource: String? = null,
    val baselineReady: Boolean? = null,
    val stableWeight: Float? = null,
    val weightRange: SessionValueRangeUi? = null,
    val mainMa12Range: SessionValueRangeUi? = null,
    val ratioMax: Float? = null,
    val finalMainState: String? = null,
    val finalAbnormalDurationMs: Long? = null,
    val finalDangerDurationMs: Long? = null,
    val sampleCount: Int = 0,
)

data class TestSessionUi(
    val sessionId: String,
    val status: TestSessionStatusUi,
    val startedAtMs: Long,
    val endedAtMs: Long? = null,
    val samples: List<TestSessionSampleUi> = emptyList(),
    val summary: TestSessionSummaryUi,
    val lastExportCsvPath: String? = null,
    val lastExportJsonPath: String? = null,
)

enum class TestSessionPrimaryLabel {
    NORMAL_RHYTHM,
    RHYTHM_LEAVE,
    FALL_ABNORMAL,
}

enum class TestSessionSecondaryLabel(
    val primaryLabel: TestSessionPrimaryLabel,
) {
    NORMAL_SWAY(TestSessionPrimaryLabel.NORMAL_RHYTHM),
    HAND_SWING(TestSessionPrimaryLabel.NORMAL_RHYTHM),
    ADJUST_STANCE(TestSessionPrimaryLabel.NORMAL_RHYTHM),
    SQUAT_STAND_NORMAL(TestSessionPrimaryLabel.NORMAL_RHYTHM),
    OTHER_NORMAL(TestSessionPrimaryLabel.NORMAL_RHYTHM),
    RHYTHM_LEAVE(TestSessionPrimaryLabel.RHYTHM_LEAVE),
    OTHER_LEAVE(TestSessionPrimaryLabel.RHYTHM_LEAVE),
    SQUAT_STAND_FALL(TestSessionPrimaryLabel.FALL_ABNORMAL),
    LEGS_ON_PLATFORM(TestSessionPrimaryLabel.FALL_ABNORMAL),
    HIPS_ON_PLATFORM(TestSessionPrimaryLabel.FALL_ABNORMAL),
    UPPER_BODY_ON_PLATFORM(TestSessionPrimaryLabel.FALL_ABNORMAL),
    OTHER_FALL(TestSessionPrimaryLabel.FALL_ABNORMAL),
}

data class TestSessionExportRequest(
    val primaryLabel: TestSessionPrimaryLabel,
    val secondaryLabel: TestSessionSecondaryLabel,
    val notes: String,
)

fun TestSessionPrimaryLabel.displayNameZh(): String = when (this) {
    TestSessionPrimaryLabel.NORMAL_RHYTHM -> "正常律动"
    TestSessionPrimaryLabel.RHYTHM_LEAVE -> "律动离开"
    TestSessionPrimaryLabel.FALL_ABNORMAL -> "摔倒异常"
}

fun TestSessionSecondaryLabel.displayNameZh(): String = when (this) {
    TestSessionSecondaryLabel.NORMAL_SWAY -> "四周摇摆"
    TestSessionSecondaryLabel.HAND_SWING -> "手部摆动"
    TestSessionSecondaryLabel.ADJUST_STANCE -> "调整站姿"
    TestSessionSecondaryLabel.SQUAT_STAND_NORMAL -> "下蹲站起"
    TestSessionSecondaryLabel.OTHER_NORMAL -> "其他"
    TestSessionSecondaryLabel.RHYTHM_LEAVE -> "律动离开"
    TestSessionSecondaryLabel.OTHER_LEAVE -> "其他"
    TestSessionSecondaryLabel.SQUAT_STAND_FALL -> "下蹲站起"
    TestSessionSecondaryLabel.LEGS_ON_PLATFORM -> "腿在平台"
    TestSessionSecondaryLabel.HIPS_ON_PLATFORM -> "屁股在平台"
    TestSessionSecondaryLabel.UPPER_BODY_ON_PLATFORM -> "上身在平台"
    TestSessionSecondaryLabel.OTHER_FALL -> "其他"
}

fun testSessionSecondaryLabelsFor(
    primaryLabel: TestSessionPrimaryLabel,
): List<TestSessionSecondaryLabel> {
    return TestSessionSecondaryLabel.entries.filter { it.primaryLabel == primaryLabel }
}

data class SessionCaptureSignals(
    val baselineReady: Boolean = false,
    val mainState: String = "BASELINE_PENDING",
    val stableWeight: Float? = null,
    val mainMa12: Float? = null,
    val deviation: Float? = null,
    val ratio: Float? = null,
    val abnormalDurationMs: Long? = null,
    val dangerDurationMs: Long? = null,
    val stopReason: String = "NONE",
    val stopSource: String = "NONE",
    val riskAdvisory: String = "NONE",
    val pendingEventAux: String? = null,
    val testId: Long? = null,
    val freqHz: Float? = null,
    val intensity: Int? = null,
    val intensityNorm: Float? = null,
)

sealed interface SessionLogEvent {
    data class TestStart(
        val testId: Long?,
        val freqHz: Float?,
        val intensity: Int?,
        val intensityNorm: Float?,
        val fallStopEnabled: Boolean?,
        val stableWeight: Float?,
    ) : SessionLogEvent

    data class MainState(
        val state: String,
        val stableWeight: Float?,
        val mainMa12: Float?,
        val deviation: Float?,
        val ratio: Float?,
        val abnormalDurationMs: Long?,
        val dangerDurationMs: Long?,
    ) : SessionLogEvent

    data class EventAux(
        val eventAux: String,
    ) : SessionLogEvent

    data class RiskAdvisory(
        val advisory: String,
    ) : SessionLogEvent

    data class AutoStop(
        val stopReason: String,
        val stopSource: String,
    ) : SessionLogEvent

    data class StopSummary(
        val result: String,
        val stopReason: String,
        val stopSource: String?,
        val testId: Long?,
        val freqHz: Float?,
        val intensity: Int?,
        val intensityNorm: Float?,
        val fallStopEnabled: Boolean?,
        val baselineReady: Boolean?,
        val stableWeight: Float?,
        val durationMs: Long?,
        val weightRange: SessionValueRangeUi?,
        val mainMa12Range: SessionValueRangeUi?,
        val ratioMax: Float?,
        val finalMainState: String?,
        val finalAbnormalDurationMs: Long?,
        val finalDangerDurationMs: Long?,
        val sampleCount: Int?,
    ) : SessionLogEvent
}

object SessionLogParser {
    private val keyValueRegex = Regex("""([A-Za-z0-9_]+)=([^\s]+)""")
    private val rangeRegex = Regex("""(-?\d+(?:\.\d+)?)\.\.(-?\d+(?:\.\d+)?)""")

    fun parse(line: String): SessionLogEvent? {
        val marker = marker(line) ?: return null
        val kv = parseKeyValues(line)
        return when (marker) {
            "TEST_START" -> SessionLogEvent.TestStart(
                testId = kv["test_id"]?.toLongOrNull(),
                freqHz = kv["freq_hz"]?.toFloatOrNull(),
                intensity = kv["intensity"]?.toIntOrNull(),
                intensityNorm = kv["intensity_norm"]?.toFloatOrNull(),
                fallStopEnabled = parseBooleanFlag(kv["fall_stop_enabled"]),
                stableWeight = kv["stable_weight_kg"]?.toFloatOrNull(),
            )

            "BASELINE_MAIN_STATE" -> SessionLogEvent.MainState(
                state = kv["next"] ?: "BASELINE_PENDING",
                stableWeight = kv["stable_weight_kg"]?.toFloatOrNull(),
                mainMa12 = kv["ma12_weight_kg"]?.toFloatOrNull()
                    ?: kv["ma7_weight_kg"]?.toFloatOrNull(),
                deviation = kv["deviation_kg"]?.toFloatOrNull(),
                ratio = kv["ratio"]?.toFloatOrNull(),
                abnormalDurationMs = kv["abnormal_duration_ms"]?.toLongOrNull(),
                dangerDurationMs = kv["danger_duration_ms"]?.toLongOrNull(),
            )

            "EVENT_AUX" -> {
                val isCandidate = kv["formal_event_candidate"] == "1"
                if (!isCandidate) {
                    null
                } else {
                    SessionLogEvent.EventAux(eventAux = "FORMAL_EVENT_CANDIDATE")
                }
            }

            "RISK_ADVISORY" -> {
                val advisoryState = kv["advisory_state"] ?: "NONE"
                val advisoryType = kv["advisory_type"] ?: "NONE"
                val advisoryLevel = kv["advisory_level"] ?: "NONE"
                SessionLogEvent.RiskAdvisory(
                    advisory = if (advisoryState.equals("NONE", ignoreCase = true)) {
                        "NONE"
                    } else {
                        "$advisoryState:$advisoryType:$advisoryLevel"
                    },
                )
            }

            "AUTO_STOP_BY_DANGER" -> SessionLogEvent.AutoStop(
                stopReason = kv["stop_reason"] ?: "AUTO_STOP_BY_DANGER",
                stopSource = kv["stop_source"] ?: "BASELINE_MAIN_LOGIC",
            )

            "STOP_SUMMARY" -> buildStopSummaryEvent(
                kv = kv,
                defaultResult = "NORMAL_STOP",
                stopReasonKey = "stop_reason",
            )

            "ABORT_SUMMARY" -> buildStopSummaryEvent(
                kv = kv,
                defaultResult = "ABNORMAL_STOP",
                stopReasonKey = "abort_reason",
            )

            else -> null
        }
    }

    private fun buildStopSummaryEvent(
        kv: Map<String, String>,
        defaultResult: String,
        stopReasonKey: String,
    ): SessionLogEvent.StopSummary {
        val rawResult = kv["result"] ?: defaultResult
        return SessionLogEvent.StopSummary(
            result = normalizeResult(rawResult),
            stopReason = kv[stopReasonKey] ?: "NONE",
            stopSource = kv["stop_source"],
            testId = kv["test_id"]?.toLongOrNull(),
            freqHz = kv["freq_hz"]?.toFloatOrNull(),
            intensity = kv["intensity"]?.toIntOrNull(),
            intensityNorm = kv["intensity_norm"]?.toFloatOrNull(),
            fallStopEnabled = parseBooleanFlag(kv["fall_stop_enabled"]),
            baselineReady = parseBooleanFlag(kv["baseline_ready"]),
            stableWeight = kv["stable_weight_kg"]?.toFloatOrNull(),
            durationMs = kv["duration_ms"]?.toLongOrNull(),
            weightRange = kv["weight_range_kg"]?.let(::parseRange),
            mainMa12Range = kv["ma12_weight_range_kg"]?.let(::parseRange)
                ?: kv["ma7_weight_range_kg"]?.let(::parseRange),
            ratioMax = parseRatioMax(kv),
            finalMainState = kv["main_status"],
            finalAbnormalDurationMs = kv["final_abnormal_duration_ms"]?.toLongOrNull(),
            finalDangerDurationMs = kv["final_danger_duration_ms"]?.toLongOrNull(),
            sampleCount = kv["samples"]?.toIntOrNull(),
        )
    }

    private fun marker(line: String): String? {
        return Regex("""\[([A-Z_]+)\]""").find(line)?.groupValues?.getOrNull(1)
    }

    private fun parseKeyValues(line: String): Map<String, String> {
        return buildMap {
            keyValueRegex.findAll(line).forEach { match ->
                put(match.groupValues[1], match.groupValues[2])
            }
        }
    }

    private fun parseRange(raw: String): SessionValueRangeUi? {
        val match = rangeRegex.matchEntire(raw) ?: return null
        val min = match.groupValues[1].toFloatOrNull() ?: return null
        val max = match.groupValues[2].toFloatOrNull() ?: return null
        return SessionValueRangeUi(min = min, max = max)
    }

    private fun parseBooleanFlag(raw: String?): Boolean? {
        return when (raw?.trim()?.uppercase()) {
            "1", "TRUE", "ON", "ENABLED" -> true
            "0", "FALSE", "OFF", "DISABLED" -> false
            else -> null
        }
    }

    private fun parseRatioMax(kv: Map<String, String>): Float? {
        val direct = kv["ratio_max"]?.toFloatOrNull()
        if (direct != null) return direct
        val stableWeight = kv["stable_weight_kg"]?.toFloatOrNull()
        val mainMa12Range = kv["ma12_weight_range_kg"]?.let(::parseRange)
            ?: kv["ma7_weight_range_kg"]?.let(::parseRange)
        if (stableWeight == null || stableWeight == 0.0f || mainMa12Range == null) return null
        val maxDeviation = maxOf(
            abs(mainMa12Range.min - stableWeight),
            abs(mainMa12Range.max - stableWeight),
        )
        return maxDeviation / stableWeight
    }

    private fun normalizeResult(raw: String): String {
        return when (raw.uppercase()) {
            "NORMAL" -> "NORMAL_STOP"
            "AUTO_STOP" -> "AUTO_STOP"
            "ABNORMAL", "ABNORMAL_STOP" -> "ABNORMAL_STOP"
            else -> raw.uppercase()
        }
    }
}

class TestSessionManager {
    fun startSession(
        nowMs: Long,
        freqHz: Int,
        intensity: Int,
        fallStopEnabled: Boolean?,
        signals: SessionCaptureSignals,
    ): TestSessionUi {
        val sessionId = "session_$nowMs"
        return TestSessionUi(
            sessionId = sessionId,
            status = TestSessionStatusUi.RECORDING,
            startedAtMs = nowMs,
            summary = TestSessionSummaryUi(
                testId = signals.testId,
                freqHz = signals.freqHz ?: freqHz.toFloat(),
                intensity = signals.intensity ?: intensity,
                intensityNorm = signals.intensityNorm,
                fallStopEnabled = fallStopEnabled,
                startTimeMs = nowMs,
                baselineReady = signals.baselineReady,
                stableWeight = signals.stableWeight,
            ),
        )
    }

    fun appendSample(
        session: TestSessionUi,
        sample: TestSessionSampleUi,
    ): TestSessionUi {
        if (session.status != TestSessionStatusUi.RECORDING) return session
        val nextSamples = session.samples + sample
        return session.copy(
            samples = nextSamples,
            summary = buildSummary(session.summary, nextSamples, endTimeMs = null, status = session.status),
        )
    }

    fun finishSession(
        session: TestSessionUi,
        finishedAtMs: Long,
        result: String,
        stopReason: String,
        stopSource: String? = session.summary.stopSource,
        finalMainState: String? = null,
        finalAbnormalDurationMs: Long? = session.summary.finalAbnormalDurationMs,
        finalDangerDurationMs: Long? = session.summary.finalDangerDurationMs,
    ): TestSessionUi {
        val summary = buildSummary(
            current = session.summary.copy(
                result = result,
                stopReason = stopReason,
                stopSource = stopSource ?: session.summary.stopSource,
                finalMainState = finalMainState ?: session.summary.finalMainState,
                finalAbnormalDurationMs = finalAbnormalDurationMs ?: session.summary.finalAbnormalDurationMs,
                finalDangerDurationMs = finalDangerDurationMs ?: session.summary.finalDangerDurationMs,
            ),
            samples = session.samples,
            endTimeMs = finishedAtMs,
            status = TestSessionStatusUi.FINISHED,
        )
        return session.copy(
            status = TestSessionStatusUi.FINISHED,
            endedAtMs = finishedAtMs,
            summary = summary,
        )
    }

    fun applyTestStart(
        session: TestSessionUi,
        event: SessionLogEvent.TestStart,
    ): TestSessionUi {
        return session.copy(
            summary = session.summary.copy(
                testId = event.testId ?: session.summary.testId,
                freqHz = event.freqHz ?: session.summary.freqHz,
                intensity = event.intensity ?: session.summary.intensity,
                intensityNorm = event.intensityNorm ?: session.summary.intensityNorm,
                fallStopEnabled = event.fallStopEnabled ?: session.summary.fallStopEnabled,
                baselineReady = session.summary.baselineReady,
                stableWeight = event.stableWeight ?: session.summary.stableWeight,
            ),
        )
    }

    fun applyStopSummary(
        session: TestSessionUi,
        event: SessionLogEvent.StopSummary,
        observedAtMs: Long,
    ): TestSessionUi {
        val effectiveEndTimeMs = session.endedAtMs ?: observedAtMs
        val base = finishSession(
            session = session,
            finishedAtMs = effectiveEndTimeMs,
            result = event.result,
            stopReason = event.stopReason,
            stopSource = event.stopSource,
            finalMainState = event.finalMainState,
            finalAbnormalDurationMs = event.finalAbnormalDurationMs,
            finalDangerDurationMs = event.finalDangerDurationMs,
        )
        return base.copy(
            summary = base.summary.copy(
                testId = event.testId ?: base.summary.testId,
                freqHz = event.freqHz ?: base.summary.freqHz,
                intensity = event.intensity ?: base.summary.intensity,
                intensityNorm = event.intensityNorm ?: base.summary.intensityNorm,
                fallStopEnabled = event.fallStopEnabled ?: base.summary.fallStopEnabled,
                baselineReady = event.baselineReady ?: base.summary.baselineReady,
                stableWeight = event.stableWeight ?: base.summary.stableWeight,
                durationMs = event.durationMs ?: base.summary.durationMs,
                weightRange = event.weightRange ?: base.summary.weightRange,
                mainMa12Range = event.mainMa12Range ?: base.summary.mainMa12Range,
                ratioMax = event.ratioMax ?: base.summary.ratioMax,
                finalMainState = event.finalMainState ?: base.summary.finalMainState,
                finalAbnormalDurationMs = event.finalAbnormalDurationMs ?: base.summary.finalAbnormalDurationMs,
                finalDangerDurationMs = event.finalDangerDurationMs ?: base.summary.finalDangerDurationMs,
                sampleCount = event.sampleCount ?: base.summary.sampleCount,
            ),
        )
    }

    private fun buildSummary(
        current: TestSessionSummaryUi,
        samples: List<TestSessionSampleUi>,
        endTimeMs: Long?,
        status: TestSessionStatusUi,
    ): TestSessionSummaryUi {
        val effectiveEndTimeMs = endTimeMs ?: current.endTimeMs
        return current.copy(
            endTimeMs = effectiveEndTimeMs,
            durationMs = when {
                effectiveEndTimeMs != null -> (effectiveEndTimeMs - current.startTimeMs).coerceAtLeast(0L)
                status == TestSessionStatusUi.RECORDING -> 0L
                else -> current.durationMs
            },
            baselineReady = current.baselineReady ?: samples.lastOrNull()?.baselineReady,
            stableWeight = current.stableWeight ?: samples.mapNotNull { it.stableWeight }.lastOrNull(),
            weightRange = samples.rangeOf { it.weight },
            mainMa12Range = samples.rangeOfNotNull { it.mainMa12 },
            ratioMax = samples.maxOfOrNull { it.ratio ?: Float.NEGATIVE_INFINITY }
                ?.takeIf { it.isFinite() },
            finalMainState = current.finalMainState ?: samples.lastOrNull()?.mainState,
            finalAbnormalDurationMs = current.finalAbnormalDurationMs ?: samples.lastOrNull()?.abnormalDurationMs,
            finalDangerDurationMs = current.finalDangerDurationMs ?: samples.lastOrNull()?.dangerDurationMs,
            sampleCount = samples.size,
        )
    }

    private fun List<TestSessionSampleUi>.rangeOf(selector: (TestSessionSampleUi) -> Float): SessionValueRangeUi? {
        if (isEmpty()) return null
        val values = map(selector)
        return SessionValueRangeUi(min = values.min(), max = values.max())
    }

    private fun List<TestSessionSampleUi>.rangeOfNotNull(
        selector: (TestSessionSampleUi) -> Float?,
    ): SessionValueRangeUi? {
        val values = mapNotNull(selector)
        if (values.isEmpty()) return null
        return SessionValueRangeUi(min = values.min(), max = values.max())
    }
}
