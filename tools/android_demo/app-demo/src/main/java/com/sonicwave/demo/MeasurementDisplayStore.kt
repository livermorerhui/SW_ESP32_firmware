package com.sonicwave.demo

import com.sonicwave.protocol.Event
import com.sonicwave.protocol.MeasurementCarrier
import com.sonicwave.protocol.ProtocolMode
import java.util.ArrayDeque

internal data class MeasurementDisplaySnapshot(
    val state: MeasurementDisplayUiState,
    val latestPoint: TelemetryPointUi? = null,
)

internal class MeasurementDisplayStore(
    private val telemetryWindowMs: Long,
) {
    private val telemetryPoints = ArrayDeque<TelemetryPointUi>()
    private val recentWeights = ArrayDeque<Float>()
    private var state = MeasurementDisplayUiState()

    fun reset(): MeasurementDisplaySnapshot {
        telemetryPoints.clear()
        recentWeights.clear()
        state = MeasurementDisplayUiState()
        return currentSnapshot()
    }

    fun currentSnapshot(): MeasurementDisplaySnapshot {
        return MeasurementDisplaySnapshot(state = state)
    }

    fun shouldConsume(
        protocolMode: ProtocolMode,
        carrier: MeasurementCarrier,
    ): Boolean {
        return when (protocolMode) {
            ProtocolMode.LEGACY -> true
            ProtocolMode.PRIMARY -> carrier == MeasurementCarrier.FORMAL_EVT_STREAM
            ProtocolMode.UNKNOWN -> carrier == MeasurementCarrier.FORMAL_EVT_STREAM
        }
    }

    fun shouldConsume(
        protocolMode: ProtocolMode,
        sample: Event.StreamSample,
    ): Boolean = shouldConsume(protocolMode = protocolMode, carrier = sample.carrier)

    fun applyInvalid(sample: Event.StreamSample): MeasurementDisplaySnapshot {
        recentWeights.clear()
        state = state.copy(
            distance = sample.distance,
            weight = sample.weight,
            ma12 = sample.ma12.takeIf { sample.ma12Ready },
            measurementValid = false,
            lastMeasurementSequence = sample.sequence,
        )
        return currentSnapshot()
    }

    fun applyValid(
        sample: Event.StreamSample,
        nowMs: Long,
        telemetrySessionStartMs: Long,
        stableWeight: Float?,
        stableWeightActive: Boolean,
    ): MeasurementDisplaySnapshot {
        val distance = sample.distance ?: return applyInvalid(sample)
        val weight = sample.weight ?: return applyInvalid(sample)
        rememberRecentWeight(weight)
        val point = TelemetryPointUi(
            measurementSeq = sample.sequence,
            deviceTimestampMs = sample.timestampMs,
            elapsedMs = nowMs - telemetrySessionStartMs,
            timestampMs = nowMs,
            distance = distance,
            unstableWeight = weight,
            measurementValid = true,
            ma12 = sample.ma12.takeIf { sample.ma12Ready },
            stableWeight = stableWeight.takeIf { stableWeightActive },
            ma3 = recentMovingAverage(3),
            ma5 = recentMovingAverage(5),
            ma7 = recentMovingAverage(7),
            stableFlag = stableWeightActive,
        )
        appendTelemetryPoint(point)
        state = MeasurementDisplayUiState(
            distance = distance,
            weight = weight,
            ma12 = sample.ma12.takeIf { sample.ma12Ready },
            measurementValid = true,
            lastMeasurementSequence = sample.sequence,
            telemetryPoints = telemetryPoints.toList(),
        )
        return MeasurementDisplaySnapshot(
            state = state,
            latestPoint = point,
        )
    }

    private fun appendTelemetryPoint(point: TelemetryPointUi) {
        telemetryPoints.addLast(point)
        trimTelemetryPoints()
    }

    private fun trimTelemetryPoints() {
        val latestTimestampMs = telemetryPoints.lastOrNull()?.timestampMs ?: return
        val minTimestampMs = latestTimestampMs - telemetryWindowMs
        while (telemetryPoints.isNotEmpty() &&
            (telemetryPoints.firstOrNull()?.timestampMs ?: latestTimestampMs) < minTimestampMs
        ) {
            telemetryPoints.removeFirst()
        }
    }

    private fun rememberRecentWeight(weight: Float) {
        recentWeights.addLast(weight)
        while (recentWeights.size > RECENT_WEIGHT_WINDOW_MAX) {
            recentWeights.removeFirst()
        }
    }

    private fun recentMovingAverage(windowSize: Int): Float? {
        if (recentWeights.size < windowSize) return null
        return recentWeights.toList()
            .takeLast(windowSize)
            .average()
            .toFloat()
    }

    private companion object {
        private const val RECENT_WEIGHT_WINDOW_MAX = 7
    }
}
