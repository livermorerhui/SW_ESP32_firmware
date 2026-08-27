package com.sonicwave.demo

import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.Event
import com.sonicwave.protocol.ProtocolMode
import com.sonicwave.protocol.SafetyEffect
import com.sonicwave.protocol.WaveState

internal class WaveControlStateReducer(
    private val timeProvider: () -> Long,
) {
    fun syncFormalWaveTruth(state: UiState): UiState {
        val waveCode = currentWaveStateCode(state.waveOutputActive)
        return state.copy(
            safetyStatus = state.safetyStatus.copy(
                runtimeState = runtimeStateLabel(state.deviceState.name),
                runtimeCode = state.deviceState.name,
                waveState = waveStateLabel(waveCode),
                waveCode = waveCode,
            ),
        )
    }

    fun syncWaveControlFlags(
        state: UiState,
        hasPendingStart: Boolean,
        hasPendingStop: Boolean,
    ): UiState {
        val startPending = !state.waveOutputActive &&
            (hasPendingStart || state.deviceState == DeviceState.RUNNING)
        val stopPending = state.waveOutputActive &&
            (hasPendingStop || state.deviceState != DeviceState.RUNNING)
        return state.copy(
            isWaveStartPending = startPending,
            isWaveStopPending = stopPending,
        )
    }

    fun applyWaveOutputTransition(
        state: UiState,
        nextWaveOutputActive: Boolean,
    ): UiState {
        val wasRunning = state.waveOutputActive
        val isRunning = nextWaveOutputActive
        return when {
            !wasRunning && isRunning -> state.copy(
                waveOutputActive = true,
                waveRuntimeStartMs = timeProvider(),
                waveRuntimeElapsedMs = 0L,
            )

            wasRunning && !isRunning -> {
                val nowMs = timeProvider()
                val finalElapsedMs = state.waveRuntimeStartMs
                    ?.let { startMs -> (nowMs - startMs).coerceAtLeast(0L) }
                    ?: state.waveRuntimeElapsedMs
                state.copy(
                    waveOutputActive = false,
                    waveRuntimeStartMs = null,
                    waveRuntimeElapsedMs = finalElapsedMs,
                )
            }

            else -> state.copy(waveOutputActive = nextWaveOutputActive)
        }
    }
}

internal enum class SnapshotStartReadyMergeContext {
    AUTHORITATIVE,
    CONTROL_LIFECYCLE_REFRESH,
}

internal fun currentWaveStateCode(active: Boolean): String = if (active) "RUNNING" else "STOPPED"

internal fun resolveAuthoritativeWaveOutput(
    currentWaveOutputActive: Boolean,
    authoritativeTopState: DeviceState,
    authoritativeWaveOutputActive: Boolean?,
): Boolean {
    authoritativeWaveOutputActive?.let { return it }
    return when (authoritativeTopState) {
        DeviceState.IDLE, DeviceState.ARMED, DeviceState.FAULT_STOP -> false
        DeviceState.RUNNING, DeviceState.UNKNOWN -> currentWaveOutputActive
    }
}

internal fun shouldTreatSafetyAsWaveStopped(event: Event.Safety): Boolean {
    return event.wave == WaveState.STOPPED ||
        event.state == DeviceState.IDLE ||
        event.state == DeviceState.FAULT_STOP ||
        event.effect == SafetyEffect.ABNORMAL_STOP ||
        event.effect == SafetyEffect.RECOVERABLE_PAUSE
}

// Lifecycle-triggered snapshot refresh is used to reconcile formal control/session truth and
// should not permanently clear an already-known pre-start ready state with a transient false.
internal fun resolveSnapshotStartReady(
    currentStartReady: Boolean?,
    snapshotStartReady: Boolean?,
    mergeContext: SnapshotStartReadyMergeContext,
): Boolean? {
    return when {
        snapshotStartReady == null -> currentStartReady
        snapshotStartReady -> true
        mergeContext == SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH &&
            currentStartReady == true -> currentStartReady
        else -> false
    }
}

internal fun mergeProtocolMode(
    currentMode: ProtocolMode,
    observedMode: ProtocolMode?,
): ProtocolMode {
    return when {
        currentMode == ProtocolMode.PRIMARY || observedMode == ProtocolMode.PRIMARY -> ProtocolMode.PRIMARY
        currentMode == ProtocolMode.LEGACY || observedMode == ProtocolMode.LEGACY -> ProtocolMode.LEGACY
        else -> ProtocolMode.UNKNOWN
    }
}

internal fun shouldAttemptPrimarySnapshotRefresh(protocolMode: ProtocolMode): Boolean {
    return protocolMode != ProtocolMode.LEGACY
}

internal fun resolveOptimisticStopState(
    deviceStartReady: Boolean?,
    deviceBaselineReady: Boolean?,
    stableWeightActive: Boolean,
    stableWeight: Float?,
): DeviceState {
    return if (
        deviceStartReady == true &&
        deviceBaselineReady == true &&
        stableWeightActive &&
        stableWeight != null
    ) {
        DeviceState.ARMED
    } else {
        DeviceState.IDLE
    }
}
