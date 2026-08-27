package com.sonicwave.demo

import kotlin.math.roundToInt

internal data class TestSessionClearResult(
    val session: TestSessionUi?,
    val clearedSessionId: String?,
)

internal data class TestSessionFinishResult(
    val session: TestSessionUi?,
    val finishedSessionId: String?,
)

internal data class TestSessionInactiveStopPlan(
    val result: String,
    val stopReason: String,
    val stopSource: String,
)

internal class TestSessionBridge(
    private val manager: TestSessionManager = TestSessionManager(),
    private val nowProvider: () -> Long,
) {
    fun startSession(
        freq: Int,
        intensity: Int,
        fallStopEnabled: Boolean?,
        signals: SessionCaptureSignals,
    ): TestSessionUi {
        return manager.startSession(
            nowMs = nowProvider(),
            freqHz = freq,
            intensity = intensity,
            fallStopEnabled = fallStopEnabled,
            signals = signals,
        ).copy(samples = mutableListOf())
    }

    fun clearIfFinished(session: TestSessionUi?): TestSessionClearResult {
        if (session == null || session.status == TestSessionStatusUi.RECORDING) {
            return TestSessionClearResult(session = session, clearedSessionId = null)
        }
        return TestSessionClearResult(session = null, clearedSessionId = session.sessionId)
    }

    fun markExported(
        session: TestSessionUi?,
        expectedSessionId: String,
        csvPath: String,
        jsonPath: String?,
    ): TestSessionUi? {
        return session
            ?.takeIf { it.sessionId == expectedSessionId }
            ?.copy(
                lastExportCsvPath = csvPath,
                lastExportJsonPath = jsonPath,
            )
    }

    fun appendSample(
        session: TestSessionUi?,
        sample: TestSessionSampleUi,
    ): TestSessionUi? {
        if (session?.status != TestSessionStatusUi.RECORDING) return session
        val samples = session.samples as? MutableList<TestSessionSampleUi>
            ?: session.samples.toMutableList()
        samples.add(sample)
        return session.copy(
            samples = samples,
            summary = session.summary.copy(
                baselineReady = (session.summary.baselineReady == true) || sample.baselineReady,
                stableWeight = sample.stableWeight ?: session.summary.stableWeight,
                finalMainState = sample.mainState.ifBlank { session.summary.finalMainState ?: "" },
                finalAbnormalDurationMs = sample.abnormalDurationMs ?: session.summary.finalAbnormalDurationMs,
                finalDangerDurationMs = sample.dangerDurationMs ?: session.summary.finalDangerDurationMs,
                sampleCount = samples.size,
            ),
        )
    }

    fun finishIfRecording(
        session: TestSessionUi?,
        result: String,
        stopReason: String,
        stopSource: String,
        finalMainState: String?,
        finalAbnormalDurationMs: Long?,
        finalDangerDurationMs: Long?,
    ): TestSessionFinishResult {
        if (session?.status != TestSessionStatusUi.RECORDING) {
            return TestSessionFinishResult(session = session, finishedSessionId = null)
        }
        val finished = manager.finishSession(
            session = session,
            finishedAtMs = nowProvider(),
            result = result,
            stopReason = stopReason,
            stopSource = stopSource,
            finalMainState = finalMainState,
            finalAbnormalDurationMs = finalAbnormalDurationMs,
            finalDangerDurationMs = finalDangerDurationMs,
        )
        return TestSessionFinishResult(
            session = finished,
            finishedSessionId = finished.sessionId,
        )
    }

    fun applyTestStart(
        session: TestSessionUi?,
        event: SessionLogEvent.TestStart,
    ): TestSessionUi? {
        if (session?.status != TestSessionStatusUi.RECORDING) return session
        return manager.applyTestStart(session, event)
    }

    fun applyStopSummary(
        session: TestSessionUi?,
        event: SessionLogEvent.StopSummary,
        trackTestSessions: Boolean,
    ): TestSessionUi? {
        val finishedSession = session ?: return null
        if (!trackTestSessions || finishedSession.status == TestSessionStatusUi.RECORDING) return finishedSession
        return manager.applyStopSummary(
            session = finishedSession,
            event = event,
            observedAtMs = nowProvider(),
        )
    }

    fun shouldStartFromFormalTruth(
        trackTestSessions: Boolean,
        session: TestSessionUi?,
    ): Boolean {
        return trackTestSessions && session?.status != TestSessionStatusUi.RECORDING
    }

    fun shouldFinishForInactiveTruth(
        session: TestSessionUi?,
        hasPendingStopRequest: Boolean,
        hasPendingStopCompletion: Boolean,
    ): Boolean {
        return session?.status == TestSessionStatusUi.RECORDING ||
            hasPendingStopRequest ||
            hasPendingStopCompletion
    }

    fun shouldStageInactiveStopCompletion(
        session: TestSessionUi?,
        hasPendingStopCompletion: Boolean,
    ): Boolean {
        return !hasPendingStopCompletion && session?.status == TestSessionStatusUi.RECORDING
    }

    fun buildInactiveStopPlan(signals: SessionCaptureSignals): TestSessionInactiveStopPlan {
        val stopSource = signals.stopSource
        return TestSessionInactiveStopPlan(
            result = if (stopSource == "USER_MANUAL_OTHER") {
                "NORMAL_STOP"
            } else {
                "AUTO_STOP"
            },
            stopReason = signals.stopReason.takeUnless { it.isBlank() || it == "NONE" }
                ?: "WAVE_OUTPUT_INACTIVE",
            stopSource = stopSource,
        )
    }

    fun resolveFormalSessionFrequency(
        freqHz: Float?,
        signals: SessionCaptureSignals,
        fallbackFreq: Int,
    ): Int {
        return freqHz?.roundToInt()
            ?: signals.freqHz?.roundToInt()
            ?: fallbackFreq
    }

    fun buildSample(
        session: TestSessionUi?,
        telemetryPoint: TelemetryPointUi,
        signals: SessionCaptureSignals,
    ): TestSessionSampleUi? {
        if (session?.status != TestSessionStatusUi.RECORDING) return null
        return TestSessionSampleUi(
            measurementSeq = telemetryPoint.measurementSeq,
            deviceTimestampMs = telemetryPoint.deviceTimestampMs,
            timestampMs = (telemetryPoint.timestampMs - session.startedAtMs).coerceAtLeast(0L),
            measurementValid = telemetryPoint.measurementValid,
            baselineReady = signals.baselineReady,
            stableWeight = signals.stableWeight,
            weight = telemetryPoint.weight,
            distance = telemetryPoint.distance,
            ma12 = telemetryPoint.ma12,
            ma3 = telemetryPoint.ma3,
            ma5 = telemetryPoint.ma5,
            mainMa12 = signals.mainMa12,
            deviation = signals.deviation,
            ratio = signals.ratio,
            mainState = signals.mainState,
            abnormalDurationMs = signals.abnormalDurationMs,
            dangerDurationMs = signals.dangerDurationMs,
            stopReason = signals.stopReason,
            stopSource = signals.stopSource,
            eventAux = signals.pendingEventAux ?: "NONE",
            riskAdvisory = signals.riskAdvisory,
        )
    }
}
