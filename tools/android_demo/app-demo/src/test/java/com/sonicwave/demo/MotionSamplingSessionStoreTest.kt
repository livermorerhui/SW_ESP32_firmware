package com.sonicwave.demo

import com.sonicwave.protocol.CalibrationModelType
import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.Event
import com.sonicwave.protocol.MeasurementCarrier
import com.sonicwave.protocol.ProtocolMode
import com.sonicwave.transport.ConnectionState
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class MotionSamplingSessionStoreTest {
    private val store = MotionSamplingSessionStore()

    @Test
    fun startFreezesSessionMetadataFromCurrentState() {
        val session = store.start(
            state = activeReadyState().copy(
                capabilityInfo = "CAP",
                connectedDeviceName = "SonicWave",
                protocolMode = ProtocolMode.PRIMARY,
                freq = 18,
                intensity = 70,
                fallStopEnabled = true,
                motionSamplingModeEnabled = true,
                waveOutputActive = true,
                latestModel = CalibrationModelUi(
                    type = CalibrationModelType.QUADRATIC,
                    referenceDistance = 1.25f,
                    c0 = 0.1f,
                    c1 = 0.2f,
                    c2 = 0.3f,
                ),
            ),
            metadata = MotionSamplingStartMetadata(
                sessionId = "motion_1",
                startedAtMs = 1_000L,
                appVersion = "1.2.3",
            ),
        )

        assertEquals("motion_1", session.sessionId)
        assertEquals(1_000L, session.startedAtMs)
        assertEquals("1.2.3", session.appVersion)
        assertEquals("CAP", session.firmwareMetadata)
        assertEquals("SonicWave", session.connectedDeviceName)
        assertEquals("PRIMARY", session.protocolModeCode)
        assertEquals(18, session.waveFrequencyHz)
        assertEquals(70, session.waveIntensity)
        assertEquals(true, session.fallStopEnabled)
        assertEquals(true, session.samplingModeEnabled)
        assertEquals(true, session.waveWasRunningAtSessionStart)
        assertEquals("QUADRATIC", session.modelTypeCode)
        assertEquals(1.25f, session.modelReferenceDistance)
        assertEquals(0.1f, session.modelC0)
        assertEquals(0.2f, session.modelC1)
        assertEquals(0.3f, session.modelC2)
        assertEquals(0, session.rows.size)
    }

    @Test
    fun firstRowHasNoDeltaAndCapturesStateMetadata() {
        val session = startedSession()

        val result = store.appendRow(
            session = session,
            state = activeReadyState(),
            sample = sample(sequence = 10L, timestampMs = 20L, distance = 100f, weight = 70f, ma12 = 69f),
            nowMs = 1_100L,
        )

        val row = requireNotNull(result.row)
        assertEquals(1, row.sampleIndex)
        assertEquals(10L, row.measurementSeq)
        assertEquals(20L, row.deviceTimestampMs)
        assertEquals(1_100L, row.timestampMs)
        assertEquals(100L, row.elapsedMs)
        assertEquals(100f, row.distanceMm)
        assertEquals(70f, row.liveWeightKg)
        assertEquals(69f, row.ma12WeightKg)
        assertEquals(68f, row.stableWeightKg)
        assertEquals(true, row.measurementValid)
        assertEquals(true, row.stableVisible)
        assertEquals(DeviceState.RUNNING.name, row.runtimeStateCode)
        assertEquals("RUNNING", row.waveStateCode)
        assertEquals("NONE", row.safetyStateCode)
        assertEquals("NONE", row.safetyReasonCode)
        assertEquals("CONNECTED", row.connectionStateCode)
        assertEquals("LINEAR", row.modelTypeCode)
        assertNull(row.ddDt)
        assertNull(row.dwDt)
        assertEquals(1, result.session?.rows?.size)
    }

    @Test
    fun secondRowCalculatesDistanceAndWeightDeltaPerSecond() {
        val first = store.appendRow(
            session = startedSession(),
            state = activeReadyState(),
            sample = sample(distance = 100f, weight = 70f),
            nowMs = 1_100L,
        ).session

        val result = store.appendRow(
            session = first,
            state = activeReadyState(),
            sample = sample(distance = 140f, weight = 74f),
            nowMs = 1_300L,
        )

        val row = requireNotNull(result.row)
        assertEquals(2, row.sampleIndex)
        assertEquals(200f, row.ddDt)
        assertEquals(20f, row.dwDt)
        assertEquals(2, result.session?.rows?.size)
    }

    @Test
    fun inactiveStateDoesNotAppendRow() {
        val session = startedSession()

        val result = store.appendRow(
            session = session,
            state = activeReadyState().copy(isMotionSamplingActive = false),
            sample = sample(distance = 100f, weight = 70f),
            nowMs = 1_100L,
        )

        assertNull(result.row)
        assertEquals(session, result.session)
    }

    @Test
    fun stopWritesEndedAtAndReportsStoppedSummary() {
        val session = store.appendRow(
            session = startedSession(),
            state = activeReadyState(),
            sample = sample(distance = 100f, weight = 70f),
            nowMs = 1_100L,
        ).session

        val result = store.stop(session, nowMs = 2_000L)

        assertEquals("motion_1", result.stoppedSessionId)
        assertEquals(1, result.stoppedRowCount)
        assertEquals(2_000L, result.session?.endedAtMs)
    }

    @Test
    fun clearOnlyClearsInactiveSession() {
        val session = startedSession()

        val activeClear = store.clear(session, isActive = true)
        val inactiveClear = store.clear(session, isActive = false)

        assertEquals(session, activeClear.first)
        assertNull(activeClear.second)
        assertNull(inactiveClear.first)
        assertEquals("motion_1", inactiveClear.second)
    }

    @Test
    fun markExportedUpdatesOnlyMatchingSession() {
        val request = MotionSamplingExportRequest(
            primaryLabel = MotionSamplingPrimaryLabel.NORMAL_USE,
            subLabel = MotionSamplingSubLabel.NORMAL_VIBRATION,
            notes = "ok",
            exportTimestampMs = 3_000L,
        )

        val exported = store.markExported(
            session = startedSession(),
            expectedSessionId = "motion_1",
            request = request,
            csvDestinationLabel = "motion.csv",
            jsonDestinationLabel = "motion.json",
        )
        val stale = store.markExported(
            session = startedSession(),
            expectedSessionId = "other",
            request = request,
            csvDestinationLabel = "motion.csv",
            jsonDestinationLabel = "motion.json",
        )

        requireNotNull(exported)
        assertEquals("NORMAL_VIBRATION", exported.exportScenarioLabel)
        assertEquals("NORMAL_USE", exported.exportScenarioCategory)
        assertEquals(3_000L, exported.lastExportTimestampMs)
        assertEquals("motion.csv", exported.lastExportCsvPath)
        assertEquals("motion.json", exported.lastExportJsonPath)
        assertNull(stale)
    }

    private fun startedSession(): MotionSamplingSessionUi {
        return MotionSamplingSessionUi(
            sessionId = "motion_1",
            startedAtMs = 1_000L,
        )
    }

    private fun activeReadyState(): UiState {
        return UiState(
            isMotionSamplingActive = true,
            connectionState = ConnectionState.Connected,
            deviceState = DeviceState.RUNNING,
            safetyStatus = SafetyStatusUi(
                effectCode = "NONE",
                reasonCode = "NONE",
                waveCode = "RUNNING",
            ),
            stableWeight = 68f,
            stableWeightActive = true,
            latestModel = CalibrationModelUi(type = CalibrationModelType.LINEAR),
        )
    }

    private fun sample(
        sequence: Long? = 1L,
        timestampMs: Long? = null,
        distance: Float?,
        weight: Float?,
        ma12: Float? = null,
        ma12Ready: Boolean = ma12 != null,
        valid: Boolean = true,
    ): Event.StreamSample {
        return Event.StreamSample(
            carrier = MeasurementCarrier.FORMAL_EVT_STREAM,
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
