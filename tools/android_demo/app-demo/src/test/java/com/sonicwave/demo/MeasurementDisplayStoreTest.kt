package com.sonicwave.demo

import com.sonicwave.protocol.Event
import com.sonicwave.protocol.MeasurementCarrier
import com.sonicwave.protocol.ProtocolMode
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class MeasurementDisplayStoreTest {
    @Test
    fun primaryAndUnknownOnlyConsumeFormalStreamCarrier() {
        val store = MeasurementDisplayStore(telemetryWindowMs = 20_000L)

        assertTrue(store.shouldConsume(ProtocolMode.PRIMARY, MeasurementCarrier.FORMAL_EVT_STREAM))
        assertFalse(store.shouldConsume(ProtocolMode.PRIMARY, MeasurementCarrier.LEGACY_CSV_FALLBACK))
        assertTrue(store.shouldConsume(ProtocolMode.UNKNOWN, MeasurementCarrier.FORMAL_EVT_STREAM))
        assertFalse(store.shouldConsume(ProtocolMode.UNKNOWN, MeasurementCarrier.LEGACY_CSV_FALLBACK))
    }

    @Test
    fun legacyConsumesFormalAndLegacyCarriers() {
        val store = MeasurementDisplayStore(telemetryWindowMs = 20_000L)

        assertTrue(store.shouldConsume(ProtocolMode.LEGACY, MeasurementCarrier.FORMAL_EVT_STREAM))
        assertTrue(store.shouldConsume(ProtocolMode.LEGACY, MeasurementCarrier.LEGACY_CSV_FALLBACK))
    }

    @Test
    fun invalidSampleClearsRecentMovingAverageAndMarksDisplayInvalid() {
        val store = MeasurementDisplayStore(telemetryWindowMs = 20_000L)
        store.applyValid(sample(sequence = 1L, weight = 10f), nowMs = 1_000L, telemetrySessionStartMs = 1_000L)
        store.applyValid(sample(sequence = 2L, weight = 20f), nowMs = 1_100L, telemetrySessionStartMs = 1_000L)

        val invalid = store.applyInvalid(
            sample(
                sequence = 3L,
                distance = 120f,
                weight = null,
                ma12 = 12f,
                ma12Ready = true,
                valid = false,
            ),
        )
        val nextValid = store.applyValid(
            sample(sequence = 4L, weight = 30f),
            nowMs = 1_200L,
            telemetrySessionStartMs = 1_000L,
        )

        assertEquals(120f, invalid.state.distance)
        assertNull(invalid.state.weight)
        assertEquals(12f, invalid.state.ma12)
        assertFalse(invalid.state.measurementValid)
        assertEquals(3L, invalid.state.lastMeasurementSequence)
        assertNull(nextValid.latestPoint?.ma3)
    }

    @Test
    fun validSamplesBuildTelemetryPointWithMovingAverages() {
        val store = MeasurementDisplayStore(telemetryWindowMs = 20_000L)
        store.applyValid(sample(sequence = 1L, weight = 10f), nowMs = 1_000L, telemetrySessionStartMs = 1_000L)
        store.applyValid(sample(sequence = 2L, weight = 20f), nowMs = 1_100L, telemetrySessionStartMs = 1_000L)
        store.applyValid(sample(sequence = 3L, weight = 30f), nowMs = 1_200L, telemetrySessionStartMs = 1_000L)
        store.applyValid(sample(sequence = 4L, weight = 40f), nowMs = 1_300L, telemetrySessionStartMs = 1_000L)
        val snapshot = store.applyValid(
            sample(sequence = 5L, timestampMs = 88L, distance = 155f, weight = 50f, ma12 = 42f),
            nowMs = 1_400L,
            telemetrySessionStartMs = 1_000L,
            stableWeight = 49f,
            stableWeightActive = true,
        )

        val point = assertNotNull(snapshot.latestPoint)
        assertEquals(5L, point.measurementSeq)
        assertEquals(88L, point.deviceTimestampMs)
        assertEquals(400L, point.elapsedMs)
        assertEquals(1_400L, point.timestampMs)
        assertEquals(155f, point.distance)
        assertEquals(50f, point.unstableWeight)
        assertEquals(42f, point.ma12)
        assertEquals(49f, point.stableWeight)
        assertTrue(point.stableFlag)
        assertEquals(40f, point.ma3)
        assertEquals(30f, point.ma5)
        assertNull(point.ma7)
        assertEquals(5L, snapshot.state.lastMeasurementSequence)
        assertEquals(5, snapshot.state.telemetryPoints.size)
    }

    @Test
    fun telemetryWindowTrimKeepsLatestPoint() {
        val store = MeasurementDisplayStore(telemetryWindowMs = 100L)
        store.applyValid(sample(sequence = 1L), nowMs = 1_000L, telemetrySessionStartMs = 1_000L)
        store.applyValid(sample(sequence = 2L), nowMs = 1_050L, telemetrySessionStartMs = 1_000L)
        val snapshot = store.applyValid(sample(sequence = 3L), nowMs = 1_250L, telemetrySessionStartMs = 1_000L)

        assertEquals(listOf(3L), snapshot.state.telemetryPoints.map { it.measurementSeq })
        assertEquals(3L, snapshot.latestPoint?.measurementSeq)
    }

    @Test
    fun resetClearsDisplayStateAndTelemetryBuffer() {
        val store = MeasurementDisplayStore(telemetryWindowMs = 20_000L)
        store.applyValid(sample(sequence = 1L), nowMs = 1_000L, telemetrySessionStartMs = 1_000L)

        val snapshot = store.reset()

        assertNull(snapshot.state.distance)
        assertNull(snapshot.state.weight)
        assertNull(snapshot.state.ma12)
        assertFalse(snapshot.state.measurementValid)
        assertNull(snapshot.state.lastMeasurementSequence)
        assertTrue(snapshot.state.telemetryPoints.isEmpty())
        assertNull(snapshot.latestPoint)
    }

    private fun MeasurementDisplayStore.applyValid(
        sample: Event.StreamSample,
        nowMs: Long,
        telemetrySessionStartMs: Long,
    ): MeasurementDisplaySnapshot {
        return applyValid(
            sample = sample,
            nowMs = nowMs,
            telemetrySessionStartMs = telemetrySessionStartMs,
            stableWeight = null,
            stableWeightActive = false,
        )
    }

    private fun sample(
        sequence: Long?,
        timestampMs: Long? = null,
        distance: Float? = 100f,
        weight: Float? = 10f,
        ma12: Float? = null,
        ma12Ready: Boolean = ma12 != null,
        valid: Boolean = true,
        carrier: MeasurementCarrier = MeasurementCarrier.FORMAL_EVT_STREAM,
    ): Event.StreamSample {
        return Event.StreamSample(
            carrier = carrier,
            sequence = sequence,
            timestampMs = timestampMs,
            distance = distance,
            weight = weight,
            ma12 = ma12,
            ma12Ready = ma12Ready,
            valid = valid,
            reason = null,
            raw = "EVT:STREAM",
        )
    }
}
