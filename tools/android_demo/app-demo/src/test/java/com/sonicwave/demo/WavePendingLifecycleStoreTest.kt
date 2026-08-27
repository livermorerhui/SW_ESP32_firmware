package com.sonicwave.demo

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class WavePendingLifecycleStoreTest {
    private var nowMs = 1_000L
    private val store = WavePendingLifecycleStore(nowProvider = { nowMs })

    @Test
    fun beginStartClearsStopLifecycle() {
        store.beginStop()
        store.stageStopCompletion(
            result = "NORMAL_STOP",
            stopReason = "USER_STOP",
            stopSource = "USER_MANUAL_OTHER",
        )

        nowMs = 2_000L
        val start = store.beginStart(freq = 20, intensity = 80)

        assertEquals(20, start.freq)
        assertEquals(80, start.intensity)
        assertEquals(2_000L, start.requestedAtMs)
        assertTrue(store.hasPendingStart)
        assertFalse(store.hasPendingStop)
        assertFalse(store.hasPendingStopCompletion)
    }

    @Test
    fun beginStopClearsStartButKeepsCompletionUntilConsumed() {
        store.beginStart(freq = 20, intensity = 80)
        nowMs = 3_000L

        val stop = store.beginStop()
        store.stageStopCompletion(
            result = "AUTO_STOP",
            stopReason = "USER_LEFT_PLATFORM",
            stopSource = "FORMAL_SAFETY_OTHER",
        )

        assertEquals(3_000L, stop.requestedAtMs)
        assertFalse(store.hasPendingStart)
        assertTrue(store.hasPendingStop)
        assertTrue(store.hasPendingStopCompletion)
    }

    @Test
    fun ensureStopRequestIsIdempotent() {
        nowMs = 4_000L
        val first = store.ensureStopRequestIf(condition = true)
        nowMs = 5_000L
        val second = store.ensureStopRequestIf(condition = true)

        assertNotNull(first)
        assertEquals(first, second)
        assertEquals(4_000L, second?.requestedAtMs)
    }

    @Test
    fun ensureStopRequestCanSkipCreation() {
        val request = store.ensureStopRequestIf(condition = false)

        assertNull(request)
        assertFalse(store.hasPendingLifecycle)
    }

    @Test
    fun consumeStopReturnsRequestAndCompletionThenClears() {
        val request = store.beginStop()
        val completion = store.stageStopCompletion(
            result = "NORMAL_STOP",
            stopReason = "USER_STOP",
            stopSource = "USER_MANUAL_OTHER",
        )

        val consumption = store.consumeStop()

        assertEquals(request, consumption.request)
        assertEquals(completion, consumption.completion)
        assertFalse(store.hasPendingStop)
        assertFalse(store.hasPendingStopCompletion)
        assertFalse(store.hasPendingLifecycle)
    }

    @Test
    fun clearStartAndClearStopReturnPreviousRequests() {
        val start = store.beginStart(freq = 25, intensity = 60)
        assertEquals(start, store.clearStart())
        assertFalse(store.hasPendingStart)

        val stop = store.beginStop()
        store.stageStopCompletion(
            result = "AUTO_STOP",
            stopReason = "WAVE_OUTPUT_INACTIVE",
            stopSource = "FORMAL_STATE_OTHER",
        )
        assertEquals(stop, store.clearStop())
        assertFalse(store.hasPendingStop)
        assertFalse(store.hasPendingStopCompletion)
    }
}
