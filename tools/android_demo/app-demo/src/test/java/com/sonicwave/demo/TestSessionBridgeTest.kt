package com.sonicwave.demo

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class TestSessionBridgeTest {
    private var nowMs = 1_000L
    private val bridge = TestSessionBridge(nowProvider = { nowMs })

    @Test
    fun startSessionCreatesRecordingSessionWithMutableSampleBuffer() {
        val session = bridge.startSession(
            freq = 20,
            intensity = 80,
            fallStopEnabled = true,
            signals = SessionCaptureSignals(
                baselineReady = true,
                stableWeight = 68.5f,
            ),
        )

        assertEquals(TestSessionStatusUi.RECORDING, session.status)
        assertEquals(1_000L, session.startedAtMs)
        assertEquals(20f, session.summary.freqHz)
        assertEquals(80, session.summary.intensity)
        assertEquals(true, session.summary.baselineReady)
        assertEquals(68.5f, session.summary.stableWeight)
    }

    @Test
    fun appendSampleUpdatesSummaryWithoutFinishingSession() {
        val session = bridge.startSession(
            freq = 20,
            intensity = 80,
            fallStopEnabled = true,
            signals = SessionCaptureSignals(),
        )

        val next = bridge.appendSample(
            session = session,
            sample = sample(
                stableWeight = 70.0f,
                mainState = "RUNNING",
                abnormalDurationMs = 120L,
            ),
        )

        assertNotNull(next)
        assertEquals(TestSessionStatusUi.RECORDING, next.status)
        assertEquals(1, next.samples.size)
        assertEquals(1, next.summary.sampleCount)
        assertEquals(true, next.summary.baselineReady)
        assertEquals(70.0f, next.summary.stableWeight)
        assertEquals("RUNNING", next.summary.finalMainState)
        assertEquals(120L, next.summary.finalAbnormalDurationMs)
    }

    @Test
    fun finishIfRecordingBindsFormalStopFields() {
        val session = bridge.startSession(
            freq = 20,
            intensity = 80,
            fallStopEnabled = true,
            signals = SessionCaptureSignals(),
        )
        nowMs = 6_000L

        val result = bridge.finishIfRecording(
            session = session,
            result = "AUTO_STOP",
            stopReason = "USER_LEFT_PLATFORM",
            stopSource = "FORMAL_SAFETY_OTHER",
            finalMainState = "IDLE",
            finalAbnormalDurationMs = 200L,
            finalDangerDurationMs = 50L,
        )

        val finished = result.session
        assertNotNull(finished)
        assertEquals(finished.sessionId, result.finishedSessionId)
        assertEquals(TestSessionStatusUi.FINISHED, finished.status)
        assertEquals(5_000L, finished.summary.durationMs)
        assertEquals("AUTO_STOP", finished.summary.result)
        assertEquals("USER_LEFT_PLATFORM", finished.summary.stopReason)
        assertEquals("FORMAL_SAFETY_OTHER", finished.summary.stopSource)
        assertEquals("IDLE", finished.summary.finalMainState)
    }

    @Test
    fun inactiveTruthPlanPreservesManualStopAsNormalStop() {
        val plan = bridge.buildInactiveStopPlan(
            SessionCaptureSignals(
                stopReason = "USER_STOP",
                stopSource = "USER_MANUAL_OTHER",
            ),
        )

        assertEquals("NORMAL_STOP", plan.result)
        assertEquals("USER_STOP", plan.stopReason)
        assertEquals("USER_MANUAL_OTHER", plan.stopSource)
    }

    @Test
    fun inactiveTruthPlanFallsBackWhenReasonIsNone() {
        val plan = bridge.buildInactiveStopPlan(
            SessionCaptureSignals(
                stopReason = "NONE",
                stopSource = "FORMAL_SAFETY_OTHER",
            ),
        )

        assertEquals("AUTO_STOP", plan.result)
        assertEquals("WAVE_OUTPUT_INACTIVE", plan.stopReason)
        assertEquals("FORMAL_SAFETY_OTHER", plan.stopSource)
    }

    @Test
    fun startAndFinishGatesMatchFormalTruthRules() {
        val recording = bridge.startSession(
            freq = 20,
            intensity = 80,
            fallStopEnabled = true,
            signals = SessionCaptureSignals(),
        )

        assertTrue(bridge.shouldStartFromFormalTruth(trackTestSessions = true, session = null))
        assertFalse(bridge.shouldStartFromFormalTruth(trackTestSessions = false, session = null))
        assertFalse(bridge.shouldStartFromFormalTruth(trackTestSessions = true, session = recording))
        assertTrue(
            bridge.shouldFinishForInactiveTruth(
                session = null,
                hasPendingStopRequest = true,
                hasPendingStopCompletion = false,
            ),
        )
        assertTrue(
            bridge.shouldStageInactiveStopCompletion(
                session = recording,
                hasPendingStopCompletion = false,
            ),
        )
    }

    @Test
    fun clearOnlyRemovesFinishedSessions() {
        val recording = bridge.startSession(
            freq = 20,
            intensity = 80,
            fallStopEnabled = true,
            signals = SessionCaptureSignals(),
        )
        val recordingClear = bridge.clearIfFinished(recording)
        assertEquals(recording, recordingClear.session)
        assertNull(recordingClear.clearedSessionId)

        nowMs = 2_000L
        val finished = bridge.finishIfRecording(
            session = recording,
            result = "NORMAL_STOP",
            stopReason = "USER_STOP",
            stopSource = "USER_MANUAL_OTHER",
            finalMainState = "IDLE",
            finalAbnormalDurationMs = null,
            finalDangerDurationMs = null,
        ).session
        val finishedClear = bridge.clearIfFinished(finished)
        assertNull(finishedClear.session)
        assertEquals(finished?.sessionId, finishedClear.clearedSessionId)
    }

    private fun sample(
        stableWeight: Float? = null,
        mainState: String = "RUNNING",
        abnormalDurationMs: Long? = null,
    ): TestSessionSampleUi {
        return TestSessionSampleUi(
            measurementSeq = 1L,
            deviceTimestampMs = 500L,
            timestampMs = 100L,
            measurementValid = true,
            baselineReady = true,
            stableWeight = stableWeight,
            weight = 70.0f,
            distance = 100.0f,
            ma12 = 69.8f,
            ma3 = 69.7f,
            ma5 = 69.6f,
            mainMa12 = 69.5f,
            deviation = 0.5f,
            ratio = 0.01f,
            mainState = mainState,
            abnormalDurationMs = abnormalDurationMs,
            dangerDurationMs = null,
            stopReason = "NONE",
            stopSource = "NONE",
            eventAux = "NONE",
            riskAdvisory = "NONE",
        )
    }
}
