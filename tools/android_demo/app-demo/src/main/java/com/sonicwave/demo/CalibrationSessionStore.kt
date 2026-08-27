package com.sonicwave.demo

import com.sonicwave.protocol.CalibrationComparisonEngine
import com.sonicwave.protocol.CalibrationComparisonResult
import com.sonicwave.protocol.CalibrationFitResult
import com.sonicwave.protocol.CalibrationFitSample
import com.sonicwave.protocol.CalibrationModelType

internal class CalibrationSessionStore {
    fun withCaptureAvailability(state: UiState): UiState {
        return state.copy(
            canCaptureCalibrationPoint = state.isConnected &&
                state.isRecording &&
                isCaptureReferenceValid(state.captureReferenceInput) &&
                hasCaptureDistanceSnapshot(state),
        )
    }

    fun reset(state: UiState): UiState {
        return synchronizeModelSelectionUi(
            withCaptureAvailability(
                state.copy(
                    latestCalibrationPoint = null,
                    calibrationPoints = emptyList(),
                    comparisonResult = null,
                    selectedComparisonModel = state.modelType,
                    captureStatus = null,
                    writeModelStatus = null,
                    preparedModel = null,
                ),
            ),
        )
    }

    fun appendPoint(
        state: UiState,
        point: CalibrationPointUi,
        captureStatus: CaptureFeedbackUi?,
        lastAckOrError: String?,
    ): UiState {
        val nextPoints = state.calibrationPoints + point
        return rebuildComparison(
            state.copy(
                latestCalibrationPoint = point,
                calibrationPoints = nextPoints,
                captureStatus = captureStatus,
                lastAckOrError = lastAckOrError,
            ),
        )
    }

    fun updateManualModelInput(
        state: UiState,
        referenceInput: String = state.modelRefInput,
        c0Input: String = state.modelC0Input,
        c1Input: String = state.modelC1Input,
        c2Input: String = state.modelC2Input,
    ): UiState {
        val nextState = state.copy(
            modelRefInput = referenceInput,
            modelC0Input = c0Input,
            modelC1Input = c1Input,
            modelC2Input = c2Input,
        )
        return synchronizeModelSelectionUi(
            nextState.copy(
                preparedModel = parsePreparedModelFromInputs(
                    state = nextState,
                    source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
                ),
            ),
        )
    }

    fun selectModelType(state: UiState, type: CalibrationModelType): UiState {
        return prepareSelectedModelForDeployment(
            state.copy(
                modelType = type,
                selectedComparisonModel = type,
            ),
            forceSelectedFit = true,
        )
    }

    fun rebuildComparison(state: UiState): UiState {
        val samples = state.calibrationPoints.mapNotNull { point ->
            val distanceMm = point.distanceMm
            val referenceWeightKg = point.referenceWeightKg
            val isValid = point.validFlag != false
            if (distanceMm == null || referenceWeightKg == null || !isValid) {
                null
            } else {
                CalibrationFitSample(
                    distanceMm = distanceMm,
                    referenceWeightKg = referenceWeightKg,
                )
            }
        }
        return prepareSelectedModelForDeployment(
            state.copy(
                comparisonResult = if (samples.isEmpty()) {
                    null
                } else {
                    CalibrationComparisonEngine.compare(samples)
                },
                canCaptureCalibrationPoint = state.isConnected &&
                    state.isRecording &&
                    isCaptureReferenceValid(state.captureReferenceInput) &&
                    hasCaptureDistanceSnapshot(state),
            ),
        )
    }

    fun prepareSelectedModelForDeployment(
        state: UiState,
        forceSelectedFit: Boolean = false,
    ): UiState {
        val selectedPreparedModel = fitForType(
            comparisonResult = state.comparisonResult,
            type = state.selectedComparisonModel,
        )?.let { fit ->
            if (fit.isAvailable) {
                fit.coefficients?.let { coefficients ->
                    PreparedCalibrationModelUi(
                        type = state.selectedComparisonModel,
                        referenceDistance = coefficients.referenceDistance,
                        c0 = coefficients.c0,
                        c1 = coefficients.c1,
                        c2 = coefficients.c2,
                        source = PreparedCalibrationModelSourceUi.AUTO_SELECTED_FIT,
                    )
                }
            } else {
                null
            }
        }
        val shouldUseAutoFit = forceSelectedFit ||
            state.preparedModel == null ||
            state.preparedModel.source == PreparedCalibrationModelSourceUi.AUTO_SELECTED_FIT
        val preparedModel = if (shouldUseAutoFit) {
            selectedPreparedModel
        } else {
            state.preparedModel
        }
        val stateWithPrepared = if (shouldUseAutoFit && preparedModel != null) {
            state.copy(
                modelType = preparedModel.type,
                preparedModel = preparedModel,
                modelRefInput = preparedModel.referenceDistance.toString(),
                modelC0Input = preparedModel.c0.toString(),
                modelC1Input = preparedModel.c1.toString(),
                modelC2Input = preparedModel.c2.toString(),
            )
        } else {
            state.copy(
                modelType = state.selectedComparisonModel,
                preparedModel = preparedModel,
            )
        }
        return synchronizeModelSelectionUi(stateWithPrepared)
    }

    fun synchronizeModelSelectionUi(state: UiState): UiState {
        return state.copy(
            modelOptions = SUPPORTED_CALIBRATION_MODEL_TYPES.map { type ->
                CalibrationModelOptionUi(
                    type = type,
                    selected = type == state.selectedComparisonModel,
                    available = fitForType(state.comparisonResult, type)?.isAvailable == true,
                    prepared = type == state.preparedModel?.type,
                )
            },
        )
    }

    fun parsePreparedModelFromInputs(
        state: UiState,
        source: PreparedCalibrationModelSourceUi,
    ): PreparedCalibrationModelUi? {
        val referenceDistance = state.modelRefInput.toFloatOrNull()
        val c0 = state.modelC0Input.toFloatOrNull()
        val c1 = state.modelC1Input.toFloatOrNull()
        val c2 = state.modelC2Input.toFloatOrNull()
        if (referenceDistance == null || c0 == null || c1 == null || c2 == null) {
            return null
        }
        return PreparedCalibrationModelUi(
            type = state.selectedComparisonModel,
            referenceDistance = referenceDistance,
            c0 = c0,
            c1 = c1,
            c2 = c2,
            source = source,
        )
    }

    fun fitForType(
        comparisonResult: CalibrationComparisonResult?,
        type: CalibrationModelType,
    ): CalibrationFitResult? {
        return when (type) {
            CalibrationModelType.LINEAR -> comparisonResult?.linear
            CalibrationModelType.QUADRATIC -> comparisonResult?.quadratic
        }
    }

    private fun isCaptureReferenceValid(input: String): Boolean {
        return input.toFloatOrNull()?.let { it >= 0.0f } == true
    }

    private fun hasCaptureDistanceSnapshot(state: UiState): Boolean {
        return state.distance?.isFinite() == true
    }
}
