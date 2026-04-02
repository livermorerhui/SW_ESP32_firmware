package com.sonicwave.demo

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull

class TestSessionManagerTest {
    private val manager = TestSessionManager()

    @Test
    fun finishSessionBindsDurationToFormalStartAndStopWindow() {
        val session = manager.startSession(
            nowMs = 1_000L,
            freqHz = 20,
            intensity = 80,
            fallStopEnabled = true,
            signals = SessionCaptureSignals(
                baselineReady = true,
                mainState = "RUNNING",
                stableWeight = 68.5f,
            ),
        )

        val finished = manager.finishSession(
            session = session,
            finishedAtMs = 6_500L,
            result = "NORMAL_STOP",
            stopReason = "USER_STOP",
            stopSource = "USER_MANUAL_OTHER",
            finalMainState = "RUNNING",
        )

        assertEquals(TestSessionStatusUi.FINISHED, finished.status)
        assertEquals(5_500L, finished.summary.durationMs)
        assertEquals(1_000L, finished.summary.startTimeMs)
        assertEquals(6_500L, finished.summary.endTimeMs)
    }

    @Test
    fun applyStopSummaryPrefersFormalSummaryFieldsForExport() {
        val session = manager.startSession(
            nowMs = 2_000L,
            freqHz = 30,
            intensity = 90,
            fallStopEnabled = false,
            signals = SessionCaptureSignals(
                baselineReady = true,
                stableWeight = 70.0f,
                mainState = "RUNNING",
                abnormalDurationMs = 120L,
                dangerDurationMs = 40L,
            ),
        ).copy(
            samples = listOf(
                TestSessionSampleUi(
                    measurementSeq = 7L,
                    deviceTimestampMs = 1_234L,
                    timestampMs = 300L,
                    measurementValid = true,
                    baselineReady = true,
                    stableWeight = 70.0f,
                    weight = 69.8f,
                    distance = 123.4f,
                    ma12 = 69.7f,
                    ma3 = 69.6f,
                    ma5 = 69.5f,
                    ma7 = 69.4f,
                    deviation = 0.6f,
                    ratio = 0.008f,
                    mainState = "RUNNING",
                    abnormalDurationMs = 120L,
                    dangerDurationMs = 40L,
                    stopReason = "NONE",
                    stopSource = "NONE",
                    eventAux = "NONE",
                    riskAdvisory = "NONE",
                ),
            ),
        )

        val summarized = manager.applyStopSummary(
            session = session,
            event = SessionLogEvent.StopSummary(
                result = "AUTO_STOP",
                stopReason = "USER_LEFT_PLATFORM",
                stopSource = "FORMAL_SAFETY_OTHER",
                testId = 42L,
                freqHz = 31.5f,
                intensity = 95,
                intensityNorm = 0.95f,
                fallStopEnabled = true,
                baselineReady = true,
                stableWeight = 70.2f,
                durationMs = 9_000L,
                weightRange = SessionValueRangeUi(69.8f, 70.4f),
                ma7Range = SessionValueRangeUi(69.4f, 70.1f),
                ratioMax = 0.012f,
                finalMainState = "DANGER",
                finalAbnormalDurationMs = 2_000L,
                finalDangerDurationMs = 800L,
                sampleCount = 18,
            ),
            observedAtMs = 11_000L,
        )

        assertEquals(TestSessionStatusUi.FINISHED, summarized.status)
        assertEquals(9_000L, summarized.summary.durationMs)
        assertEquals(42L, summarized.summary.testId)
        assertEquals(31.5f, summarized.summary.freqHz)
        assertEquals(95, summarized.summary.intensity)
        assertEquals("FORMAL_SAFETY_OTHER", summarized.summary.stopSource)
        assertEquals("DANGER", summarized.summary.finalMainState)
        assertEquals(18, summarized.summary.sampleCount)
        assertNotNull(summarized.summary.weightRange)
        assertNotNull(summarized.summary.ma7Range)
    }
}
