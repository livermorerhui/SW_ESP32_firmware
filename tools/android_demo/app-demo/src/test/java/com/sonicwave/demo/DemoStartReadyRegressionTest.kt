package com.sonicwave.demo

import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.Event
import com.sonicwave.protocol.PlatformModel
import com.sonicwave.protocol.ProtocolMode
import com.sonicwave.protocol.SafetyEffect
import com.sonicwave.protocol.WaveState
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
    fun plusStartButtonUsesAuthoritativeDeviceReadyTruth() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = false,
            deviceStartReady = true,
            stableWeight = null,
            stableWeightActive = false,
        )

        assertTrue(state.canStartWave())
    }

    @Test
    fun baseStartButtonBecomesReadyWithoutStableWeight() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.BASE,
            deviceLaserInstalled = false,
            deviceStartReady = false,
            stableWeight = null,
            stableWeightActive = false,
        )

        assertTrue(state.canStartWave())
        assertTrue(isLaserlessStartProfile(state))
    }

    @Test
    fun plusWithoutLaserIsNotTreatedAsLaserlessDeliveryProfile() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = false,
            deviceStartReady = false,
            stableWeight = null,
            stableWeightActive = false,
        )

        assertFalse(state.canStartWave())
        assertFalse(isLaserlessStartProfile(state))
    }

    @Test
    fun plusDegradedDeliveryProfileTreatsFormalMeasurementPlaneAsOptional() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = true,
            deviceDegradedStartEnabled = false,
        )

        assertTrue(isPlusDegradedDeliveryProfile(state))
        assertTrue(formalMeasurementPlaneIsOptionalForCurrentDeliverySubset(state))
    }

    @Test
    fun fullMeasurementPlusProfileStillExpectsFormalMeasurementPlane() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = false,
            deviceDegradedStartEnabled = false,
        )

        assertFalse(isPlusDegradedDeliveryProfile(state))
        assertFalse(formalMeasurementPlaneIsOptionalForCurrentDeliverySubset(state))
    }

    @Test
    fun deliverySubsetProfilesDoNotTrackTestSessionAutomation() {
        val base = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.BASE,
            deviceLaserInstalled = false,
        )
        val plusDegraded = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = true,
        )
        val fullMeasurementPlus = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = false,
            deviceDegradedStartEnabled = false,
        )

        assertFalse(shouldTrackTestSessionAutomation(base))
        assertFalse(shouldTrackTestSessionAutomation(plusDegraded))
        assertTrue(shouldTrackTestSessionAutomation(fullMeasurementPlus))
    }

    @Test
    fun plusStartButtonBecomesReadyWithStableWeightAndRecoveredStartReady() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = false,
            deviceStartReady = true,
            deviceBaselineReady = true,
            stableWeight = 68.4f,
            stableWeightActive = true,
        )

        assertTrue(state.canStartWave())
    }

    @Test
    fun startButtonBecomesReadyWhenDegradedStartIsAvailableEvenBeforeConfirmation() {
        val state = UiState(
            isConnected = true,
            devicePlatformModel = PlatformModel.PLUS,
            deviceLaserInstalled = true,
            deviceDegradedStartAvailable = true,
            deviceDegradedStartEnabled = false,
            stableWeight = null,
            stableWeightActive = false,
            deviceStartReady = false,
            deviceBaselineReady = false,
        )

        assertTrue(state.canStartWave())
    }

    @Test
    fun authoritativeSnapshotClearsWaveOutputWhenDeviceLeavesRunning() {
        assertFalse(
            resolveAuthoritativeWaveOutput(
                currentWaveOutputActive = true,
                authoritativeTopState = DeviceState.IDLE,
                authoritativeWaveOutputActive = null,
            ),
        )
    }

    @Test
    fun explicitSnapshotWaveOutputStillWinsWhenPresent() {
        assertTrue(
            resolveAuthoritativeWaveOutput(
                currentWaveOutputActive = false,
                authoritativeTopState = DeviceState.IDLE,
                authoritativeWaveOutputActive = true,
            ),
        )
    }

    @Test
    fun abnormalStopSafetyIsTreatedAsWaveStopped() {
        val event = Event.Safety(
            reason = "FALL_SUSPECTED",
            code = 101,
            effect = SafetyEffect.ABNORMAL_STOP,
            state = DeviceState.RUNNING,
            wave = WaveState.UNKNOWN,
            extras = emptyMap(),
            raw = "EVT:SAFETY",
        )

        assertTrue(shouldTreatSafetyAsWaveStopped(event))
    }

    @Test
    fun observedUnknownDoesNotOverwriteKnownPrimaryProtocol() {
        assertEquals(
            expected = ProtocolMode.PRIMARY,
            actual = mergeProtocolMode(
                currentMode = ProtocolMode.PRIMARY,
                observedMode = ProtocolMode.UNKNOWN,
            ),
        )
    }

    @Test
    fun snapshotRefreshFallbackStillRunsWhileProtocolIsUnknown() {
        assertTrue(shouldAttemptPrimarySnapshotRefresh(ProtocolMode.UNKNOWN))
        assertFalse(shouldAttemptPrimarySnapshotRefresh(ProtocolMode.LEGACY))
    }

    @Test
    fun matchingObservedDeviceConfigConfirmsPendingWrite() {
        assertTrue(
            doesObservedDeviceConfigMatchRequested(
                requestedPlatformModel = PlatformModel.PRO,
                requestedLaserInstalled = true,
                observedPlatformModel = PlatformModel.PRO,
                observedLaserInstalled = true,
            ),
        )
        assertFalse(
            doesObservedDeviceConfigMatchRequested(
                requestedPlatformModel = PlatformModel.PRO,
                requestedLaserInstalled = true,
                observedPlatformModel = PlatformModel.PLUS,
                observedLaserInstalled = true,
            ),
        )
    }

    @Test
    fun optimisticStopStateReturnsArmedWhenRestartConditionsStillHold() {
        assertEquals(
            expected = DeviceState.ARMED,
            actual = resolveOptimisticStopState(
                deviceStartReady = true,
                deviceBaselineReady = true,
                stableWeightActive = true,
                stableWeight = 68.4f,
            ),
        )
    }

    @Test
    fun optimisticStopStateReturnsIdleWhenReadyConditionsAreMissing() {
        assertEquals(
            expected = DeviceState.IDLE,
            actual = resolveOptimisticStopState(
                deviceStartReady = true,
                deviceBaselineReady = false,
                stableWeightActive = true,
                stableWeight = 68.4f,
            ),
        )
    }
}
