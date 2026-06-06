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
    fun captureAvailabilityRequiresConnectionRecordingReferenceAndDistance() {
        val unavailable = store.withCaptureAvailability(
            UiState(
                isConnected = true,
                isRecording = true,
                captureReferenceInput = "bad",
                distance = 10f,
            ),
        )
        val available = store.withCaptureAvailability(
            unavailable.copy(captureReferenceInput = "70.0"),
        )

        assertFalse(unavailable.canCaptureCalibrationPoint)
        assertTrue(available.canCaptureCalibrationPoint)
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
            point(distanceMm = -1000f, referenceWeightKg = 50f),
            point(distanceMm = 0f, referenceWeightKg = 70f),
            point(distanceMm = 1000f, referenceWeightKg = 90f),
        ).fold(UiState(isConnected = true, isRecording = true, distance = 1f)) { current, point ->
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
            point(distanceMm = -1000f, referenceWeightKg = 50f),
            point(distanceMm = 0f, referenceWeightKg = 70f),
            point(distanceMm = 1000f, referenceWeightKg = 90f),
        ).fold(UiState()) { current, point ->
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
            point(distanceMm = -1000f, referenceWeightKg = 50f),
            point(distanceMm = 0f, referenceWeightKg = 70f, validFlag = false),
            point(distanceMm = 1000f, referenceWeightKg = 90f),
        ).fold(UiState()) { current, point ->
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
        assertEquals(CalibrationModelType.QUADRATIC, reset.selectedComparisonModel)
    }

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
