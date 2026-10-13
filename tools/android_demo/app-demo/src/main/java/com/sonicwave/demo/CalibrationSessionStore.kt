package com.sonicwave.demo

import com.sonicwave.protocol.CalibrationComparisonEngine
import com.sonicwave.protocol.CalibrationComparisonResult
import com.sonicwave.protocol.CalibrationFitResult
import com.sonicwave.protocol.CalibrationFitSample
import com.sonicwave.protocol.CalibrationModelType
import com.sonicwave.protocol.calibrationDistanceRangeForBackend
import com.sonicwave.protocol.calibrationEmptyDistanceToleranceForBackend
import kotlin.math.abs

internal class CalibrationSessionStore {
    fun markEmptyDistancePending(
        state: UiState,
        distanceMm: Float?,
        backend: String?,
        toleranceMm: Float?,
        nowMs: Long,
    ): UiState {
        val validDistance = distanceMm?.takeIf { it.isFinite() && it > 0.0f }
        return state.copy(
            emptyDistance = EmptyDistanceState(
                status = if (validDistance == null) EmptyDistanceStatusUi.INVALID else EmptyDistanceStatusUi.PENDING,
                distanceMm = validDistance,
                toleranceMm = toleranceMm?.takeIf { it.isFinite() && it >= 0.0f },
                backend = backend,
                source = EmptyDistanceSourceUi.USER_SET,
                updatedAtMs = nowMs,
            ),
        )
    }

    fun markEmptyDistanceReadback(
        state: UiState,
        distanceMm: Float?,
        backend: String?,
        toleranceMm: Float?,
        nowMs: Long,
    ): UiState {
        val validDistance = distanceMm?.takeIf { it.isFinite() && it > 0.0f }
        return state.copy(
            emptyDistance = EmptyDistanceState(
                status = if (validDistance == null) EmptyDistanceStatusUi.INVALID else EmptyDistanceStatusUi.SET,
                distanceMm = validDistance,
                toleranceMm = toleranceMm?.takeIf { it.isFinite() && it >= 0.0f },
                backend = backend,
                source = EmptyDistanceSourceUi.DEVICE_READBACK.takeIf { validDistance != null },
                updatedAtMs = nowMs,
            ),
        )
    }

    fun clearEmptyDistance(state: UiState): UiState = state.copy(
        emptyDistance = EmptyDistanceState(),
    )

    fun invalidateEmptyDistanceForBackend(state: UiState, backend: String?): UiState {
        val current = state.emptyDistance
        if (backend.isNullOrBlank() || current.backend.isNullOrBlank() ||
            current.backend.equals(backend, ignoreCase = true)
        ) {
            return state
        }
        return state.copy(
            emptyDistance = current.copy(
                status = EmptyDistanceStatusUi.NOT_SET,
                backend = backend,
                source = current.distanceMm?.let { EmptyDistanceSourceUi.LOCAL_ONLY },
            ),
        )
    }

    fun markEmptyDistanceReadbackMismatch(state: UiState, nowMs: Long): UiState = state.copy(
        emptyDistance = state.emptyDistance.copy(
            status = EmptyDistanceStatusUi.READBACK_MISMATCH,
            updatedAtMs = nowMs,
        ),
    )

    fun markWeightModelPending(state: UiState, model: PreparedCalibrationModelUi, nowMs: Long): UiState =
        state.copy(
            weightModel = WeightModelState(
                status = WeightModelStatusUi.PENDING,
                type = model.type,
                a = model.c0,
                b = model.c1,
                c = model.c2,
                updatedAtMs = nowMs,
            ),
        )

    fun markWeightModelReadback(
        state: UiState,
        model: PreparedCalibrationModelUi?,
        ready: Boolean,
        nowMs: Long,
    ): UiState = state.copy(
        weightModel = if (model == null || !ready) {
            WeightModelState(
                status = if (model == null) WeightModelStatusUi.NOT_SET else WeightModelStatusUi.INVALID,
                type = model?.type,
                a = model?.c0,
                b = model?.c1,
                c = model?.c2,
                updatedAtMs = nowMs,
            )
        } else {
            WeightModelState(
                status = WeightModelStatusUi.SET,
                type = model.type,
                a = model.c0,
                b = model.c1,
                c = model.c2,
                updatedAtMs = nowMs,
            )
        },
    )

    fun markWeightModelReadbackMismatch(state: UiState, nowMs: Long): UiState = state.copy(
        weightModel = state.weightModel.copy(
            status = WeightModelStatusUi.READBACK_MISMATCH,
            updatedAtMs = nowMs,
        ),
    )

    fun clearWeightModel(state: UiState): UiState = state.copy(
        weightModel = WeightModelState(),
    )

    fun withCaptureAvailability(state: UiState): UiState {
        return state.copy(
            canCaptureCalibrationPoint = captureAvailable(state),
        )
    }

    private fun captureAvailable(state: UiState): Boolean =
        state.isConnected && state.deviceConfigVerified && state.profileHistoryReady &&
            state.isRecording &&
            state.sensorBackend?.equals("LASER_485", ignoreCase = true) == true &&
            isCaptureReferenceValid(state.captureReferenceInput) &&
            hasCaptureDistanceSnapshot(state)

    fun reset(state: UiState): UiState {
        return synchronizeModelSelectionUi(
            withCaptureAvailability(
                state.copy(
                    latestCalibrationPoint = null,
                    calibrationPoints = emptyList(),
                    comparisonResult = null,
                    latestModel = null,
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
                    CalibrationComparisonEngine.compare(
                        samples = samples,
                        // Passing NaN deliberately blocks the protocol engine's legacy
                        // "infer anchor from 0 kg point" fallback. A fit may use only
                        // the independently confirmed device empty distance.
                        emptyDistanceMm = emptyDistanceMm(state) ?: Float.NaN,
                        emptyDistanceToleranceMm = calibrationEmptyDistanceToleranceForBackend(
                            state.sensorBackend,
                        ),
                        validationDistanceRangeMm = calibrationDistanceRangeForBackend(
                            state.sensorBackend,
                        ),
                    )
                },
                canCaptureCalibrationPoint = captureAvailable(state),
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
        val deployable = state.preparedModel?.let { model ->
            canDeployModel(state, model)
        } == true
        return state.copy(
            canDeployCalibrationModel = deployable,
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

    /** A 0 kg calibration sample does not set the device's empty-distance anchor. */
    fun emptyDistanceMm(state: UiState): Float? {
        return state.emptyDistance.confirmedDistanceMm
    }

    fun canDeployModel(state: UiState, model: PreparedCalibrationModelUi): Boolean {
        if (!state.sensorBackend.equals("LASER_485", ignoreCase = true)) return false
        val emptyDistance = emptyDistanceMm(state) ?: return false
        val expectedReference = emptyDistance / 100.0f
        val referenceMatches = abs(model.referenceDistance - expectedReference) <=
            calibrationEmptyDistanceToleranceForBackend(state.sensorBackend) / 100.0f
        val fixedIntercept = abs(model.c2) <= 0.0001f
        val fit = fitForType(state.comparisonResult, model.type)
        return referenceMatches && fixedIntercept && fit?.isAvailable == true
    }

    private fun isCaptureReferenceValid(input: String): Boolean {
        return input.toFloatOrNull()?.let { it >= 0.0f } == true
    }

    private fun hasCaptureDistanceSnapshot(state: UiState): Boolean {
        return state.distanceValid && state.distance?.isFinite() == true
    }
}
