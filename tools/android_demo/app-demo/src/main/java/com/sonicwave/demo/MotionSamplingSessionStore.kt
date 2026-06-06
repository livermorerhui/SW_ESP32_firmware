package com.sonicwave.demo

import com.sonicwave.protocol.Event
import com.sonicwave.transport.ConnectionState

internal data class MotionSamplingStartMetadata(
    val sessionId: String,
    val startedAtMs: Long,
    val appVersion: String?,
)

internal data class MotionSamplingStopResult(
    val session: MotionSamplingSessionUi?,
    val stoppedSessionId: String?,
    val stoppedRowCount: Int,
)

internal data class MotionSamplingAppendResult(
    val session: MotionSamplingSessionUi?,
    val row: MotionSamplingRowUi?,
)

internal class MotionSamplingSessionStore {
    fun start(
        state: UiState,
        metadata: MotionSamplingStartMetadata,
    ): MotionSamplingSessionUi {
        val model = state.latestModel
        return MotionSamplingSessionUi(
            sessionId = metadata.sessionId,
            startedAtMs = metadata.startedAtMs,
            appVersion = metadata.appVersion,
            firmwareMetadata = state.capabilityInfo,
            connectedDeviceName = state.connectedDeviceName,
            protocolModeCode = state.protocolMode.name,
            waveFrequencyHz = state.freq,
            waveIntensity = state.intensity,
            fallStopEnabled = state.fallStopEnabled,
            samplingModeEnabled = state.motionSamplingModeEnabled,
            waveWasRunningAtSessionStart = state.waveOutputActive,
            modelTypeCode = model?.type?.name,
            modelReferenceDistance = model?.referenceDistance,
            modelC0 = model?.c0,
            modelC1 = model?.c1,
            modelC2 = model?.c2,
            notes = "",
            rows = emptyList(),
        )
    }

    fun stop(
        session: MotionSamplingSessionUi?,
        nowMs: Long,
    ): MotionSamplingStopResult {
        val stopped = session?.copy(endedAtMs = nowMs)
        return MotionSamplingStopResult(
            session = stopped,
            stoppedSessionId = stopped?.sessionId,
            stoppedRowCount = stopped?.rows?.size ?: 0,
        )
    }

    fun clear(
        session: MotionSamplingSessionUi?,
        isActive: Boolean,
    ): Pair<MotionSamplingSessionUi?, String?> {
        if (isActive) return session to null
        return null to session?.sessionId
    }

    fun appendRow(
        session: MotionSamplingSessionUi?,
        state: UiState,
        sample: Event.StreamSample,
        nowMs: Long,
    ): MotionSamplingAppendResult {
        if (!state.isMotionSamplingActive) return MotionSamplingAppendResult(session, null)
        val activeSession = session ?: return MotionSamplingAppendResult(null, null)
        val distance = sample.distance ?: return MotionSamplingAppendResult(activeSession, null)
        val weight = sample.weight ?: return MotionSamplingAppendResult(activeSession, null)
        val previous = activeSession.rows.lastOrNull()
        val elapsedMs = nowMs - activeSession.startedAtMs
        val dtSeconds = previous?.let { ((nowMs - it.timestampMs).coerceAtLeast(1L)) / 1000f }
        val ddDt = dtSeconds?.let { (distance - previous.distanceMm) / it }
        val dwDt = dtSeconds?.let { (weight - previous.liveWeightKg) / it }
        val row = MotionSamplingRowUi(
            sampleIndex = activeSession.rows.size + 1,
            measurementSeq = sample.sequence,
            deviceTimestampMs = sample.timestampMs,
            timestampMs = nowMs,
            elapsedMs = elapsedMs,
            distanceMm = distance,
            liveWeightKg = weight,
            ma12WeightKg = sample.ma12.takeIf { sample.ma12Ready },
            stableWeightKg = if (state.stableWeightActive) state.stableWeight else null,
            measurementValid = sample.valid && distance.isFinite() && weight.isFinite(),
            stableVisible = state.stableWeightActive,
            runtimeStateCode = state.deviceState.name,
            waveStateCode = state.safetyStatus.waveCode.ifBlank {
                currentWaveStateCode(state.waveOutputActive)
            },
            safetyStateCode = state.safetyStatus.effectCode.ifBlank { "NONE" },
            safetyReasonCode = state.safetyStatus.reasonCode.ifBlank { "NONE" },
            safetyCode = state.safetyStatus.code,
            connectionStateCode = connectionStateCode(state.connectionState),
            modelTypeCode = state.latestModel?.type?.name,
            ddDt = ddDt,
            dwDt = dwDt,
        )
        return MotionSamplingAppendResult(
            session = activeSession.copy(rows = activeSession.rows + row),
            row = row,
        )
    }

    fun markExported(
        session: MotionSamplingSessionUi?,
        expectedSessionId: String,
        request: MotionSamplingExportRequest,
        csvDestinationLabel: String,
        jsonDestinationLabel: String?,
    ): MotionSamplingSessionUi? {
        return session
            ?.takeIf { it.sessionId == expectedSessionId }
            ?.copy(
                exportScenarioLabel = request.scenarioLabel,
                exportScenarioCategory = request.scenarioCategory,
                lastExportTimestampMs = request.exportTimestampMs,
                lastExportCsvPath = csvDestinationLabel,
                lastExportJsonPath = jsonDestinationLabel,
            )
    }

    private fun connectionStateCode(connectionState: ConnectionState): String {
        return when (connectionState) {
            is ConnectionState.Connected -> "CONNECTED"
            is ConnectionState.Connecting,
            is ConnectionState.DiscoveringServices,
            is ConnectionState.Subscribing -> "CONNECTING"

            is ConnectionState.Error -> "ERROR"
            ConnectionState.Disconnected -> "DISCONNECTED"
        }
    }
}
