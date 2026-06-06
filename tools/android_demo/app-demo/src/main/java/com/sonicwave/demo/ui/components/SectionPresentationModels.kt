package com.sonicwave.demo.ui.components

import com.sonicwave.demo.MotionSamplingExportRequest
import com.sonicwave.protocol.CalibrationModelType

data class CalibrationInputCallbacks(
    val onZeroInputChange: (String) -> Unit,
    val onFactorInputChange: (String) -> Unit,
    val onCaptureReferenceChange: (String) -> Unit,
    val onModelReferenceChange: (String) -> Unit,
    val onModelC0Change: (String) -> Unit,
    val onModelC1Change: (String) -> Unit,
    val onModelC2Change: (String) -> Unit,
    val onModelTypeChange: (CalibrationModelType) -> Unit,
)

data class CalibrationCommandCallbacks(
    val onZero: () -> Unit,
    val onCalibrate: () -> Unit,
    val onCapturePoint: () -> Unit,
    val onStartRecording: () -> Unit,
    val onStopRecording: () -> Unit,
    val onGetModel: () -> Unit,
    val onSetModel: () -> Unit,
    val onCalibrationZero: () -> Unit,
    val onToggleEngineeringSection: () -> Unit,
    val onToggleVerboseStreamLogs: () -> Unit,
)

data class CalibrationToolsActions(
    val input: CalibrationInputCallbacks,
    val commands: CalibrationCommandCallbacks,
)

data class MotionSamplingActions(
    val onStartSampling: () -> Unit,
    val onStopSampling: () -> Unit,
    val onEnableSamplingMode: () -> Unit,
    val onDisableSamplingMode: () -> Unit,
    val onClearSession: () -> Unit,
    val onExportSession: (MotionSamplingExportRequest) -> Unit,
)
