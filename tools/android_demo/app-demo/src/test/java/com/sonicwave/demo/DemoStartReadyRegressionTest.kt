package com.sonicwave.demo

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DemoStartReadyRegressionTest {
    @Test
    fun lifecycleRefreshPreservesKnownReadyOnFalseSnapshot() {
        assertEquals(
            expected = true,
            actual = resolveSnapshotStartReady(
                currentStartReady = true,
                snapshotStartReady = false,
                mergeContext = SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH,
            ),
        )
    }

    @Test
    fun authoritativeFalseSnapshotStillClearsKnownReady() {
        assertEquals(
            expected = false,
            actual = resolveSnapshotStartReady(
                currentStartReady = true,
                snapshotStartReady = false,
                mergeContext = SnapshotStartReadyMergeContext.AUTHORITATIVE,
            ),
        )
    }

    @Test
    fun trueSnapshotStillWinsDuringLifecycleRefresh() {
        assertEquals(
            expected = true,
            actual = resolveSnapshotStartReady(
                currentStartReady = false,
                snapshotStartReady = true,
                mergeContext = SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH,
            ),
        )
    }

    @Test
    fun nullSnapshotKeepsCurrentValue() {
        assertEquals(
            expected = true,
            actual = resolveSnapshotStartReady(
                currentStartReady = true,
                snapshotStartReady = null,
                mergeContext = SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH,
            ),
        )
    }

    @Test
    fun lifecycleRefreshDoesNotInventReadyWhenCurrentTruthIsNotReady() {
        assertEquals(
            expected = false,
            actual = resolveSnapshotStartReady(
                currentStartReady = false,
                snapshotStartReady = false,
                mergeContext = SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH,
            ),
        )
    }

    @Test
    fun startButtonStaysBlockedWithoutStableWeight() {
        val state = UiState(
            isConnected = true,
            deviceStartReady = true,
            deviceBaselineReady = true,
            stableWeight = null,
            stableWeightActive = false,
        )

        assertFalse(state.canStartWave())
    }

    @Test
    fun startButtonBecomesReadyWithStableWeightAndRecoveredStartReady() {
        val state = UiState(
            isConnected = true,
            deviceStartReady = true,
            deviceBaselineReady = true,
            stableWeight = 68.4f,
            stableWeightActive = true,
        )

        assertTrue(state.canStartWave())
    }
}
