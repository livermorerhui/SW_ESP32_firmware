package com.sonicwave.demo

import com.sonicwave.protocol.MeasurementCarrier
import com.sonicwave.protocol.ProtocolMode

internal data class DemoMeasurementTraceSnapshot(
    val validCount: Long,
    val invalidCount: Long,
    val ignoredCount: Long,
    val lastSeq: Long?,
    val lastDistance: Float?,
    val lastWeight: Float?,
    val lastInvalidReason: String?,
    val lastIgnoredCarrier: MeasurementCarrier?,
    val lastProtocolMode: ProtocolMode?,
)

internal class DemoMeasurementTrace {
    private var validCount: Long = 0
    private var invalidCount: Long = 0
    private var ignoredCount: Long = 0
    private var lastSeq: Long? = null
    private var lastDistance: Float? = null
    private var lastWeight: Float? = null
    private var lastInvalidReason: String? = null
    private var lastIgnoredCarrier: MeasurementCarrier? = null
    private var lastProtocolMode: ProtocolMode? = null

    fun reset() {
        validCount = 0
        invalidCount = 0
        ignoredCount = 0
        lastSeq = null
        lastDistance = null
        lastWeight = null
        lastInvalidReason = null
        lastIgnoredCarrier = null
        lastProtocolMode = null
    }

    fun recordValid(sequence: Long?, distance: Float, weight: Float): DemoMeasurementTraceSnapshot {
        validCount += 1
        lastSeq = sequence
        lastDistance = distance
        lastWeight = weight
        lastInvalidReason = null
        return snapshot()
    }

    fun recordInvalid(
        sequence: Long?,
        distance: Float?,
        weight: Float?,
        reason: String?,
    ): DemoMeasurementTraceSnapshot {
        invalidCount += 1
        lastSeq = sequence
        lastDistance = distance
        lastWeight = weight
        lastInvalidReason = reason ?: "INVALID"
        return snapshot()
    }

    fun recordIgnored(
        sequence: Long?,
        carrier: MeasurementCarrier,
        protocolMode: ProtocolMode,
    ): DemoMeasurementTraceSnapshot {
        ignoredCount += 1
        lastSeq = sequence
        lastIgnoredCarrier = carrier
        lastProtocolMode = protocolMode
        return snapshot()
    }

    fun snapshot(): DemoMeasurementTraceSnapshot {
        return DemoMeasurementTraceSnapshot(
            validCount = validCount,
            invalidCount = invalidCount,
            ignoredCount = ignoredCount,
            lastSeq = lastSeq,
            lastDistance = lastDistance,
            lastWeight = lastWeight,
            lastInvalidReason = lastInvalidReason,
            lastIgnoredCarrier = lastIgnoredCarrier,
            lastProtocolMode = lastProtocolMode,
        )
    }
}
