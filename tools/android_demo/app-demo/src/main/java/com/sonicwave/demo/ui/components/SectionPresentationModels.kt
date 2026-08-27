package com.sonicwave.demo.ui.components

import com.sonicwave.demo.MotionSamplingExportRequest
import com.sonicwave.demo.TestSessionExportRequest
import com.sonicwave.protocol.CalibrationModelType
import com.sonicwave.protocol.PlatformModel

data class CalibrationInputCallbacks(
    val onCaptureReferenceChange: (String) -> Unit,
    val onModelReferenceChange: (String) -> Unit,
    val onModelC0Change: (String) -> Unit,
    val onModelC1Change: (String) -> Unit,
    val onModelC2Change: (String) -> Unit,
    val onModelTypeChange: (CalibrationModelType) -> Unit,
)

data class CalibrationCommandCallbacks(
    val onZero: () -> Unit,
    val onCapturePoint: () -> Unit,
    val onStartRecording: () -> Unit,
    val onStopRecording: () -> Unit,
    val onClearCalibrationPoints: () -> Unit,
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

data class DeviceToolsActions(
    val onPlatformModelSelected: (PlatformModel) -> Unit,
    val onWriteDeviceConfig: () -> Unit,
    val onToggleFallStopEnabled: (Boolean) -> Unit,
    val onToggleLeaveProtectionEnabled: (Boolean) -> Unit,
)

data class WaveControlActions(
    val onFreqInputChange: (String) -> Unit,
    val onIntensityInputChange: (String) -> Unit,
    val onFreqInputCommit: () -> Unit,
    val onIntensityInputCommit: () -> Unit,
    val onFreqPresetSelected: (Int) -> Unit,
    val onIntensityPresetSelected: (Int) -> Unit,
    val onStart: () -> Unit,
    val onStop: () -> Unit,
)

data class TestSessionActions(
    val onClearSession: () -> Unit,
    val onExportSession: (TestSessionExportRequest) -> Unit,
)

data class RawConsoleActions(
    val onClear: () -> Unit,
)
