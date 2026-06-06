package com.sonicwave.demo

import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.SafetyEffect
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class WaveControlStateReducerTest {
    private var nowMs: Long = 1_000L
    private val reducer = WaveControlStateReducer(timeProvider = { nowMs })

    @Test
    fun pendingStartFlagTakesPriorityBeforeReady() {
        val state = reducer.syncWaveControlFlags(
            state = readyState(),
            hasPendingStart = true,
            hasPendingStop = false,
        )

        assertEquals(true, state.isWaveStartPending)
        assertEquals(false, state.isWaveStopPending)
        assertEquals(WaveStartAvailabilityUi.START_PENDING, state.waveStartAvailability())
    }

    @Test
    fun pendingStopFlagTakesPriorityWhileRunning() {
        val state = reducer.syncWaveControlFlags(
            state = readyState().copy(
                waveOutputActive = true,
                deviceState = DeviceState.RUNNING,
            ),
            hasPendingStart = false,
            hasPendingStop = true,
        )

        assertEquals(false, state.isWaveStartPending)
        assertEquals(true, state.isWaveStopPending)
        assertEquals(WaveStartAvailabilityUi.STOP_PENDING, state.waveStartAvailability())
    }

    @Test
    fun runningAvailabilityWinsWhenNoPendingStopExists() {
        val state = reducer.syncWaveControlFlags(
            state = readyState().copy(
                waveOutputActive = true,
                deviceState = DeviceState.RUNNING,
            ),
            hasPendingStart = false,
            hasPendingStop = false,
        )

        assertEquals(WaveStartAvailabilityUi.RUNNING, state.waveStartAvailability())
    }

    @Test
    fun readyAvailabilityRemainsReadyWhenNoLifecycleBlockExists() {
        val state = reducer.syncWaveControlFlags(
            state = readyState(),
            hasPendingStart = false,
            hasPendingStop = false,
        )

        assertEquals(WaveStartAvailabilityUi.READY, state.waveStartAvailability())
    }

    @Test
    fun safetyBlockedAvailabilityStillBlocksStart() {
        val state = reducer.syncWaveControlFlags(
            state = readyState().copy(
                safetyStatus = SafetyStatusUi(
                    effectCode = SafetyEffect.RECOVERABLE_PAUSE.name,
                ),
            ),
            hasPendingStart = false,
            hasPendingStop = false,
        )

        assertEquals(WaveStartAvailabilityUi.SAFETY_BLOCKED, state.waveStartAvailability())
    }

    @Test
    fun waveOutputTransitionStartsRuntimeClock() {
        nowMs = 2_000L

        val state = reducer.applyWaveOutputTransition(
            state = UiState(),
            nextWaveOutputActive = true,
        )

        assertEquals(true, state.waveOutputActive)
        assertEquals(2_000L, state.waveRuntimeStartMs)
        assertEquals(0L, state.waveRuntimeElapsedMs)
    }

    @Test
    fun waveOutputTransitionStopsRuntimeClockWithElapsedTime() {
        nowMs = 2_750L

        val state = reducer.applyWaveOutputTransition(
            state = UiState(
                waveOutputActive = true,
                waveRuntimeStartMs = 2_000L,
                waveRuntimeElapsedMs = 123L,
            ),
            nextWaveOutputActive = false,
        )

        assertEquals(false, state.waveOutputActive)
        assertNull(state.waveRuntimeStartMs)
        assertEquals(750L, state.waveRuntimeElapsedMs)
    }

    @Test
    fun formalWaveTruthMirrorsRuntimeAndWaveLabels() {
        val state = reducer.syncFormalWaveTruth(
            UiState(
                deviceState = DeviceState.RUNNING,
                waveOutputActive = true,
            ),
        )

        assertEquals("RUNNING", state.safetyStatus.runtimeCode)
        assertEquals("运行中", state.safetyStatus.runtimeState)
        assertEquals("RUNNING", state.safetyStatus.waveCode)
        assertEquals("运行中", state.safetyStatus.waveState)
    }

    private fun readyState(): UiState {
        return UiState(
            isConnected = true,
            deviceStartReady = true,
            freqInput = "20",
            intensityInput = "80",
        )
    }
}
