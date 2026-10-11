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
    private val recentDistances = ArrayDeque<Float>()
    private val recentWeights = ArrayDeque<Float>()
    private var state = MeasurementDisplayUiState()

    fun reset(): MeasurementDisplaySnapshot {
        telemetryPoints.clear()
        recentDistances.clear()
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

    /** Keep the old method name for callers that only have complete samples. */
    fun applyValid(
        sample: Event.StreamSample,
        nowMs: Long,
        telemetrySessionStartMs: Long,
        stableWeight: Float? = null,
        stableWeightActive: Boolean = false,
    ): MeasurementDisplaySnapshot {
        return applySample(
            sample = sample,
            nowMs = nowMs,
            telemetrySessionStartMs = telemetrySessionStartMs,
            stableWeight = stableWeight,
            stableWeightActive = stableWeightActive,
        )
    }

    /**
     * Distance is the primary display/recording signal. A missing weight does
     * not erase a valid distance; it only makes the combined measurement
     * invalid and suppresses weight moving averages.
     */
    fun applySample(
        sample: Event.StreamSample,
        nowMs: Long,
        telemetrySessionStartMs: Long,
        stableWeight: Float? = null,
        stableWeightActive: Boolean = false,
    ): MeasurementDisplaySnapshot {
        val distanceValid = sample.distanceValid &&
            sample.distance?.isFinite() == true
        val weightValid = sample.weightValid &&
            sample.weight?.isFinite() == true
        val distance = sample.distance.takeIf { distanceValid }
        val weight = sample.weight.takeIf { weightValid }

        if (distance == null) {
            return applyInvalid(sample)
        }
        rememberRecentDistance(distance)
        if (weight != null) {
            rememberRecentWeight(weight)
        } else {
            // A gap in weight must not make the next moving average bridge
            // across an unknown value.
            recentWeights.clear()
        }

        val point = TelemetryPointUi(
            measurementSeq = sample.sequence,
            deviceTimestampMs = sample.timestampMs,
            elapsedMs = nowMs - telemetrySessionStartMs,
            timestampMs = nowMs,
            distance = distance,
            unstableWeight = weight,
            measurementValid = distanceValid && weightValid,
            distanceValid = distanceValid,
            weightValid = weightValid,
            distanceReason = sample.distanceReason,
            weightReason = sample.weightReason ?: sample.reason.takeIf { !weightValid },
            sensorBackend = sample.sensorBackend,
            ma12 = sample.ma12.takeIf { sample.ma12Ready && weightValid },
            maDistance3 = recentMovingAverage(recentDistances, 3),
            maDistance5 = recentMovingAverage(recentDistances, 5),
            maDistance7 = recentMovingAverage(recentDistances, 7),
            maDistance12 = recentMovingAverage(recentDistances, 12),
            stableWeight = stableWeight.takeIf { stableWeightActive && weightValid },
            ma3 = recentMovingAverage(recentWeights, 3).takeIf { weightValid },
            ma5 = recentMovingAverage(recentWeights, 5).takeIf { weightValid },
            ma7 = recentMovingAverage(recentWeights, 7).takeIf { weightValid },
            stableFlag = stableWeightActive && weightValid,
        )
        appendTelemetryPoint(point)
        state = MeasurementDisplayUiState(
            distance = distance,
            weight = weight,
            ma12 = sample.ma12.takeIf { sample.ma12Ready && weightValid },
            maDistance12 = recentMovingAverage(recentDistances, 12),
            measurementValid = distanceValid && weightValid,
            distanceValid = distanceValid,
            weightValid = weightValid,
            distanceReason = sample.distanceReason,
            weightReason = sample.weightReason ?: sample.reason.takeIf { !weightValid },
            sensorBackend = sample.sensorBackend,
            lastMeasurementSequence = sample.sequence,
            telemetryPoints = telemetryPoints.toList(),
        )
        return MeasurementDisplaySnapshot(state = state, latestPoint = point)
    }

    fun applyInvalid(sample: Event.StreamSample): MeasurementDisplaySnapshot {
        recentDistances.clear()
        recentWeights.clear()
        val distanceValid = sample.distanceValid && sample.distance?.isFinite() == true
        val weightValid = sample.weightValid && sample.weight?.isFinite() == true
        state = state.copy(
            distance = sample.distance.takeIf { distanceValid },
            weight = sample.weight.takeIf { weightValid },
            ma12 = sample.ma12.takeIf { sample.ma12Ready && weightValid },
            maDistance12 = null,
            measurementValid = false,
            distanceValid = distanceValid,
            weightValid = weightValid,
            distanceReason = sample.distanceReason ?: sample.reason.takeIf { !distanceValid },
            weightReason = sample.weightReason ?: sample.reason.takeIf { !weightValid },
            sensorBackend = sample.sensorBackend,
            lastMeasurementSequence = sample.sequence,
        )
        return currentSnapshot()
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

    private fun rememberRecentDistance(distance: Float) {
        recentDistances.addLast(distance)
        while (recentDistances.size > RECENT_DISTANCE_WINDOW_MAX) {
            recentDistances.removeFirst()
        }
    }

    private fun recentMovingAverage(values: ArrayDeque<Float>, windowSize: Int): Float? {
        if (values.size < windowSize) return null
        return values.toList().takeLast(windowSize).average().toFloat()
    }

    private companion object {
        private const val RECENT_DISTANCE_WINDOW_MAX = 12
        private const val RECENT_WEIGHT_WINDOW_MAX = 7
    }
}
