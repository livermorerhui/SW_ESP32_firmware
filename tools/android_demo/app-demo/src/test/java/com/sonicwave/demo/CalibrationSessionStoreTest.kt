package com.sonicwave.demo

import com.sonicwave.protocol.CalibrationModelType
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class CalibrationSessionStoreTest {
    private val store = CalibrationSessionStore()

    @Test
    fun failedEmptyReadbackRequiresReconnectInsteadOfWaitingForever() {
        val failed = store.markEmptyVerificationNeedsReconnect(UiState(
            emptyDistance = EmptyDistanceState(status = EmptyDistanceStatusUi.INVALID),
            weightModel = WeightModelState(status = WeightModelStatusUi.SET),
        ))

        assertTrue(failed.emptyModelReadbackPending)
        assertTrue(failed.emptyModelReadbackNeedsReconnect)
        assertFalse(failed.weightModel.isDeviceConfirmed)
    }

    @Test
    fun captureAvailabilityRequiresConnectionRecordingReferenceAndDistance() {
        val unavailable = store.withCaptureAvailability(
            UiState(
                isConnected = true,
                deviceConfigVerified = true,
                profileHistoryReady = true,
                isRecording = true,
                captureReferenceInput = "bad",
                distance = 10f,
                distanceValid = true,
                sensorBackend = "LASER_485",
            ),
        )
        val available = store.withCaptureAvailability(
            unavailable.copy(captureReferenceInput = "70.0", sensorBackend = "LASER_485"),
        )

        assertFalse(unavailable.canCaptureCalibrationPoint)
        assertTrue(available.canCaptureCalibrationPoint)
    }

    @Test
    fun captureAvailabilityWaitsForVerifiedProfileAndLoadedHistory() {
        val ready = UiState(
            isConnected = true,
            deviceConfigVerified = true,
            profileHistoryReady = true,
            isRecording = true,
            captureReferenceInput = "70.0",
            distance = 10f,
            distanceValid = true,
            sensorBackend = "LASER_485",
        )

        assertFalse(store.withCaptureAvailability(ready.copy(deviceConfigVerified = false)).canCaptureCalibrationPoint)
        assertFalse(store.withCaptureAvailability(ready.copy(profileHistoryReady = false)).canCaptureCalibrationPoint)
        assertTrue(store.withCaptureAvailability(ready).canCaptureCalibrationPoint)
    }

    @Test
    fun captureAvailabilityIsDisabledBeforeRecordingStarts() {
        val state = store.withCaptureAvailability(
            UiState(
                isConnected = true,
                isRecording = false,
                captureReferenceInput = "70.0",
                distance = 10f,
                distanceValid = true,
                sensorBackend = "LASER_485",
            ),
        )

        assertFalse(state.canCaptureCalibrationPoint)
    }

    @Test
    fun manualModelInputParsesPreparedModelAndMarksOptionPrepared() {
        val state = store.updateManualModelInput(
            UiState(selectedComparisonModel = CalibrationModelType.QUADRATIC),
            referenceInput = "1.25",
            c0Input = "0.1",
            c1Input = "2.0",
            c2Input = "3.0",
        )

        val prepared = assertNotNull(state.preparedModel)
        assertEquals(CalibrationModelType.QUADRATIC, prepared.type)
        assertEquals(1.25f, prepared.referenceDistance)
        assertEquals(0.1f, prepared.c0)
        assertEquals(2.0f, prepared.c1)
        assertEquals(3.0f, prepared.c2)
        assertEquals(PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE, prepared.source)
        assertTrue(state.modelOptions.single { it.type == CalibrationModelType.QUADRATIC }.prepared)
    }

    @Test
    fun invalidManualModelInputClearsPreparedModel() {
        val state = store.updateManualModelInput(
            UiState(),
            referenceInput = "0.0",
            c0Input = "bad",
            c1Input = "1.0",
            c2Input = "0.0",
        )

        assertNull(state.preparedModel)
        assertFalse(state.modelOptions.any { it.prepared })
    }

    @Test
    fun validPointsBuildComparisonAndAutoPrepareSelectedFit() {
        val state = listOf(
            point(distanceMm = 100f, referenceWeightKg = 0f),
            point(distanceMm = 200f, referenceWeightKg = 50f),
            point(distanceMm = 300f, referenceWeightKg = 100f),
        ).fold(UiState(isConnected = true, isRecording = true, distance = 1f, emptyDistance = confirmedEmptyDistance())) { current, point ->
            store.appendPoint(
                state = current,
                point = point,
                captureStatus = null,
                lastAckOrError = null,
            )
        }

        assertEquals(3, state.calibrationPoints.size)
        assertEquals(3, state.comparisonResult?.sampleCount)
        assertTrue(state.modelOptions.single { it.type == CalibrationModelType.LINEAR }.available)
        assertTrue(state.modelOptions.single { it.type == CalibrationModelType.QUADRATIC }.available)
        assertEquals(CalibrationModelType.LINEAR, state.preparedModel?.type)
        assertEquals(PreparedCalibrationModelSourceUi.AUTO_SELECTED_FIT, state.preparedModel?.source)
    }

    @Test
    fun selectingQuadraticUsesQuadraticFitWhenAvailable() {
        val fitted = listOf(
            point(distanceMm = 100f, referenceWeightKg = 0f),
            point(distanceMm = 200f, referenceWeightKg = 50f),
            point(distanceMm = 300f, referenceWeightKg = 100f),
        ).fold(UiState(emptyDistance = confirmedEmptyDistance())) { current, point ->
            store.appendPoint(
                state = current,
                point = point,
                captureStatus = null,
                lastAckOrError = null,
            )
        }

        val selected = store.selectModelType(fitted, CalibrationModelType.QUADRATIC)

        assertEquals(CalibrationModelType.QUADRATIC, selected.selectedComparisonModel)
        assertEquals(CalibrationModelType.QUADRATIC, selected.modelType)
        assertEquals(CalibrationModelType.QUADRATIC, selected.preparedModel?.type)
        assertTrue(selected.modelOptions.single { it.type == CalibrationModelType.QUADRATIC }.selected)
    }

    @Test
    fun invalidPointsStayVisibleButDoNotFeedFitDataset() {
        val state = listOf(
            point(distanceMm = 100f, referenceWeightKg = 0f),
            point(distanceMm = 200f, referenceWeightKg = 50f, validFlag = false),
            point(distanceMm = 300f, referenceWeightKg = 90f),
        ).fold(UiState(emptyDistance = confirmedEmptyDistance())) { current, point ->
            store.appendPoint(
                state = current,
                point = point,
                captureStatus = null,
                lastAckOrError = null,
            )
        }

        assertEquals(3, state.calibrationPoints.size)
        assertEquals(2, state.comparisonResult?.sampleCount)
        assertTrue(state.modelOptions.single { it.type == CalibrationModelType.LINEAR }.available)
        assertFalse(state.modelOptions.single { it.type == CalibrationModelType.QUADRATIC }.available)
    }

    @Test
    fun resetClearsSessionDatasetAndPreparedModel() {
        val fitted = store.appendPoint(
            state = UiState(
                modelType = CalibrationModelType.QUADRATIC,
                selectedComparisonModel = CalibrationModelType.QUADRATIC,
                emptyDistance = confirmedEmptyDistance(),
            ),
            point = point(distanceMm = -1000f, referenceWeightKg = 50f),
            captureStatus = CaptureFeedbackUi(kind = CaptureFeedbackKind.SUCCESS, message = "recorded"),
            lastAckOrError = "recorded",
        )

        val reset = store.reset(fitted)

        assertTrue(reset.calibrationPoints.isEmpty())
        assertNull(reset.latestCalibrationPoint)
        assertNull(reset.comparisonResult)
        assertNull(reset.captureStatus)
        assertNull(reset.writeModelStatus)
        assertNull(reset.preparedModel)
        assertEquals(100f, reset.emptyDistance.confirmedDistanceMm)
        assertEquals(CalibrationModelType.QUADRATIC, reset.selectedComparisonModel)
    }

    @Test
    fun zeroWeightCalibrationPointDoesNotEstablishDeviceEmptyDistance() {
        val points = listOf(
            point(distanceMm = 100f, referenceWeightKg = 0f),
            point(distanceMm = 200f, referenceWeightKg = 50f),
        )
        val withoutDeviceAnchor = points.fold(UiState()) { current, point ->
            store.appendPoint(current, point, null, null)
        }
        val withLocalDraft = store.rebuildComparison(
            withoutDeviceAnchor.copy(
                emptyDistance = confirmedEmptyDistance().copy(
                    source = EmptyDistanceSourceUi.LOCAL_ONLY,
                ),
            ),
        )

        assertNull(store.emptyDistanceMm(withoutDeviceAnchor))
        assertFalse(withoutDeviceAnchor.canDeployCalibrationModel)
        assertNull(withoutDeviceAnchor.comparisonResult?.emptyDistanceMm)
        assertNull(store.emptyDistanceMm(withLocalDraft))
        assertFalse(withLocalDraft.canDeployCalibrationModel)
    }

    @Test
    fun resettingEmptyDistanceToTheSameValueRevokesThePreviouslyConfirmedModel() {
        val before = stateWithConfirmedEmptyAndModel()
        val pending = store.markModelUnverifiedForEmptyMutation(
            store.markEmptyDistancePending(before, 112.33f, "LASER_485", 0.10f, 10L),
        )
        assertFalse(pending.weightModel.isDeviceConfirmed)
        assertEquals(WeightModelStatusUi.INVALID, pending.weightModel.status)
        assertNull(pending.latestModel)
        assertFalse(pending.canDeployCalibrationModel)

        val failedReadback = store.markModelUnverifiedForEmptyMutation(pending.copy(
            emptyDistance = pending.emptyDistance.copy(status = EmptyDistanceStatusUi.INVALID),
        ))
        assertEquals(WeightModelStatusUi.INVALID, failedReadback.weightModel.status)

        val confirmed = store.applyProfileEmptyReadback(
            pending,
            confirmedEmptyDistance(112.33f),
            revision = 5L,
            raw = "ACK:EMPTY op=GET status=SET empty_revision=5",
            modelInvalidated = true,
        )
        assertEquals(112.33f, confirmed.emptyDistance.confirmedDistanceMm)
        assertEquals(5L, confirmed.deviceEmptyRevision)
        assertEquals(WeightModelStatusUi.NOT_SET, confirmed.weightModel.status)
        assertNull(confirmed.latestModel)
        assertTrue(confirmed.emptyModelReadbackPending)
        assertFalse(confirmed.emptyModelReadbackNeedsReconnect)
    }

    @Test
    fun clearingEmptyDistanceRevokesThePreviouslyConfirmedModel() {
        val before = stateWithConfirmedEmptyAndModel()
        val pending = store.markModelUnverifiedForEmptyMutation(
            before.copy(emptyDistance = before.emptyDistance.copy(status = EmptyDistanceStatusUi.PENDING)),
        )
        val confirmed = store.applyProfileEmptyReadback(
            pending,
            EmptyDistanceState(),
            revision = 5L,
            raw = "ACK:EMPTY op=GET status=UNSET empty_revision=5",
            modelInvalidated = true,
        )

        assertNull(confirmed.emptyDistance.confirmedDistanceMm)
        assertEquals(WeightModelStatusUi.NOT_SET, confirmed.weightModel.status)
        assertNull(confirmed.latestModel)
        assertFalse(confirmed.canDeployCalibrationModel)
        assertTrue(confirmed.emptyModelReadbackPending)
        assertFalse(confirmed.emptyModelReadbackNeedsReconnect)
    }

    @Test
    fun emptyDistanceStateRequiresDeviceReadbackBeforeFitUsesIt() {
        val pending = store.markEmptyDistancePending(
            state = UiState(),
            distanceMm = 112.56f,
            backend = "LASER_485",
            toleranceMm = 0.1f,
            nowMs = 10L,
        )
        assertEquals(EmptyDistanceStatusUi.PENDING, pending.emptyDistance.status)
        assertNull(store.emptyDistanceMm(pending))

        val confirmed = store.markEmptyDistanceReadback(
            state = pending,
            distanceMm = 112.56f,
            backend = "LASER_485",
            toleranceMm = 0.1f,
            nowMs = 11L,
        )
        assertEquals(112.56f, store.emptyDistanceMm(confirmed))
        assertEquals(EmptyDistanceSourceUi.DEVICE_READBACK, confirmed.emptyDistance.source)

        val cleared = store.clearEmptyDistance(confirmed)
        assertEquals(EmptyDistanceStatusUi.NOT_SET, cleared.emptyDistance.status)
        assertNull(store.emptyDistanceMm(cleared))

        val switched = store.invalidateEmptyDistanceForBackend(confirmed, "CYD3S1612_IIC")
        assertEquals(EmptyDistanceStatusUi.NOT_SET, switched.emptyDistance.status)
        assertEquals("CYD3S1612_IIC", switched.emptyDistance.backend)
        assertNull(store.emptyDistanceMm(switched))
    }

    @Test
    fun weightModelStateSeparatesPreparedPendingAndDeviceReadback() {
        val prepared = PreparedCalibrationModelUi(
            type = CalibrationModelType.LINEAR,
            referenceDistance = 1.1256f,
            c0 = 0f,
            c1 = 10f,
            c2 = 0f,
            source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
        )
        val pending = store.markWeightModelPending(UiState(), prepared, nowMs = 20L)
        assertEquals(WeightModelStatusUi.PENDING, pending.weightModel.status)
        assertFalse(pending.weightModel.isDeviceConfirmed)

        val confirmed = store.markWeightModelReadback(pending, prepared, ready = true, nowMs = 21L)
        assertTrue(confirmed.weightModel.isDeviceConfirmed)
        assertEquals(10f, confirmed.weightModel.b)

        val mismatch = store.markWeightModelReadbackMismatch(confirmed, nowMs = 22L)
        assertEquals(WeightModelStatusUi.READBACK_MISMATCH, mismatch.weightModel.status)
        assertFalse(mismatch.weightModel.isDeviceConfirmed)

        val cleared = store.clearWeightModel(confirmed)
        assertEquals(WeightModelStatusUi.NOT_SET, cleared.weightModel.status)
        assertFalse(cleared.weightModel.isDeviceConfirmed)
    }

    private fun confirmedEmptyDistance(distanceMm: Float = 100f) = EmptyDistanceState(
        status = EmptyDistanceStatusUi.SET,
        distanceMm = distanceMm,
        backend = "LASER_485",
        source = EmptyDistanceSourceUi.DEVICE_READBACK,
    )

    private fun stateWithConfirmedEmptyAndModel() = UiState(
        deviceEmptyRevision = 4L,
        emptyDistance = confirmedEmptyDistance(112.33f),
        canDeployCalibrationModel = true,
        latestModel = CalibrationModelUi(
            type = CalibrationModelType.LINEAR,
            referenceDistance = 1.1233f,
            c0 = 0f,
            c1 = 25f,
            c2 = 0f,
            ready = true,
        ),
        weightModel = WeightModelState(
            status = WeightModelStatusUi.SET,
            type = CalibrationModelType.LINEAR,
            a = 0f,
            b = 25f,
            c = 0f,
        ),
    )

    private fun point(
        distanceMm: Float,
        referenceWeightKg: Float,
        validFlag: Boolean = true,
    ): CalibrationPointUi {
        return CalibrationPointUi(
            distanceMm = distanceMm,
            referenceWeightKg = referenceWeightKg,
            validFlag = validFlag,
        )
    }
}
