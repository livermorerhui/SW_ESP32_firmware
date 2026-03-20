package com.sonicwave.demo

import android.Manifest
import android.app.Application
import android.content.pm.PackageManager
import android.os.Build
import androidx.annotation.StringRes
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sonicwave.protocol.CalibrationComparisonEngine
import com.sonicwave.protocol.CalibrationComparisonResult
import com.sonicwave.protocol.CalibrationFitResult
import com.sonicwave.protocol.CalibrationFitSample
import com.sonicwave.protocol.CalibrationModelType
import com.sonicwave.protocol.CALIBRATION_DISTANCE_RUNTIME_DIVISOR
import com.sonicwave.protocol.CapabilityResult
import com.sonicwave.protocol.Command
import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.Event
import com.sonicwave.protocol.ProtocolMode
import com.sonicwave.protocol.SafetyEffect
import com.sonicwave.sdk.SonicWaveClient
import com.sonicwave.transport.BleScanResult
import com.sonicwave.transport.ConnectionState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.time.LocalTime
import java.time.format.DateTimeFormatter

sealed interface ScanState {
    data object Idle : ScanState
    data object Scanning : ScanState
    data class Results(val count: Int) : ScanState
}

sealed interface PermissionState {
    data object Granted : PermissionState
    data class Missing(val missingPermissions: List<String>) : PermissionState
}

data class UiState(
    val scanState: ScanState = ScanState.Idle,
    val permissionState: PermissionState = PermissionState.Missing(emptyList()),
    val connectionState: ConnectionState = ConnectionState.Disconnected,
    val isConnected: Boolean = false,
    val connectedDeviceName: String? = null,
    val notifyEnabled: Boolean = false,
    val notifyError: String? = null,
    val capabilityInfo: String? = null,
    val protocolMode: ProtocolMode = ProtocolMode.UNKNOWN,
    val deviceState: DeviceState = DeviceState.UNKNOWN,
    val faultStatus: FaultStatusUi = FaultStatusUi(),
    val safetyStatus: SafetyStatusUi = SafetyStatusUi(),
    val distance: Float? = null,
    val weight: Float? = null,
    val telemetryPoints: List<TelemetryPointUi> = emptyList(),
    val stableWeight: Float? = null,
    val stableWeightActive: Boolean = false,
    val calibrationZero: Float? = null,
    val calibrationFactor: Float? = null,
    val latestModel: CalibrationModelUi? = null,
    val latestCalibrationPoint: CalibrationPointUi? = null,
    val calibrationPoints: List<CalibrationPointUi> = emptyList(),
    val comparisonResult: CalibrationComparisonResult? = null,
    val selectedComparisonModel: CalibrationModelType = CalibrationModelType.LINEAR,
    val modelOptions: List<CalibrationModelOptionUi> = SUPPORTED_CALIBRATION_MODEL_TYPES.map { type ->
        CalibrationModelOptionUi(
            type = type,
            selected = type == CalibrationModelType.LINEAR,
            available = false,
            prepared = false,
        )
    },
    val preparedModel: PreparedCalibrationModelUi? = null,
    val canCaptureCalibrationPoint: Boolean = false,
    val captureStatus: CaptureFeedbackUi? = null,
    val writeModelStatus: WriteModelFeedbackUi? = null,
    val lastAckOrError: String? = null,
    val streamWarning: String? = null,
    val rawLogLines: List<String> = emptyList(),
    val verboseStreamLogsEnabled: Boolean = false,
    val freq: Int = 20,
    val intensity: Int = 80,
    val freqInput: String = "20",
    val intensityInput: String = "80",
    val zeroInput: String = "-22.0",
    val factorInput: String = "1.0",
    val captureReferenceInput: String = "70.0",
    val modelType: CalibrationModelType = CalibrationModelType.LINEAR,
    val modelRefInput: String = "0.0",
    val modelC0Input: String = "0.0",
    val modelC1Input: String = "1.0",
    val modelC2Input: String = "0.0",
    val isEngineeringSectionExpanded: Boolean = false,
    val isRecording: Boolean = false,
    val recordingDestination: String? = null,
    val recordingStatus: String? = null,
    val isDeviceSheetVisible: Boolean = false,
    val scanResults: List<BleScanResult> = emptyList(),
    val statusLabel: String = "",
    val isConnecting: Boolean = false,
)

class DemoViewModel(application: Application) : AndroidViewModel(application) {
    private val client = SonicWaveClient(application)
    private val recorder = TelemetryRecorder(application)
    private val recordingMutex = Mutex()

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private var selectedDeviceName: String? = null
    private var streamWatchdogJob: Job? = null
    private var lastStreamAtMs: Long = 0L
    private var telemetrySessionStartMs: Long = 0L
    private var recordingSession: RecordingSession? = null
    private var disconnectRequested: Boolean = false
    private var hadConnectedSession: Boolean = false
    private var awaitingCalibrationCaptureResult: Boolean = false
    private var awaitingModelWriteResult: Boolean = false
    private var pendingWriteModelType: CalibrationModelType? = null

    init {
        observeClient()
        refreshPermissionState()
        _uiState.update {
            withCaptureAvailability(
                it.copy(
                    safetyStatus = defaultSafetyStatus(),
                    statusLabel = currentStatusLabel(
                        connectionState = ConnectionState.Disconnected,
                        isScanning = false,
                    ),
                ),
            )
        }
    }

    fun refreshPermissionState() {
        val missing = requiredAppPermissions().filterNot(::hasPermission)
        _uiState.update { current ->
            current.copy(
                permissionState = if (missing.isEmpty()) {
                    PermissionState.Granted
                } else {
                    PermissionState.Missing(missing)
                },
            )
        }
    }

    fun openScanSheetAndStartScan() {
        refreshPermissionState()
        val hasPermission = _uiState.value.permissionState is PermissionState.Granted
        if (!hasPermission) {
            _uiState.update {
                withCaptureAvailability(
                    it.copy(
                        isDeviceSheetVisible = false,
                        scanState = ScanState.Idle,
                        statusLabel = currentStatusLabel(connectionState = it.connectionState, isScanning = false),
                    ),
                )
            }
            return
        }

        client.startScan(preferredNamePrefixes = listOf("SonicWave", "Sonicwave", "Vibrate"))
        _uiState.update {
            withCaptureAvailability(
                it.copy(
                    isDeviceSheetVisible = true,
                    scanState = ScanState.Scanning,
                    statusLabel = currentStatusLabel(connectionState = it.connectionState, isScanning = true),
                ),
            )
        }
    }

    fun closeScanSheet() {
        client.stopScan()
        _uiState.update {
            withCaptureAvailability(
                it.copy(
                    isDeviceSheetVisible = false,
                    scanState = ScanState.Idle,
                    statusLabel = currentStatusLabel(connectionState = it.connectionState, isScanning = false),
                ),
            )
        }
    }

    fun connectToDevice(device: BleScanResult) {
        selectedDeviceName = device.name ?: device.address
        disconnectRequested = false
        hadConnectedSession = false
        awaitingCalibrationCaptureResult = false
        awaitingModelWriteResult = false
        pendingWriteModelType = null
        client.stopScan()
        streamWatchdogJob?.cancel()
        telemetrySessionStartMs = 0L
        lastStreamAtMs = 0L

        _uiState.update {
            resetCalibrationSessionState(
                withCaptureAvailability(
                    it.copy(
                        scanState = ScanState.Idle,
                        statusLabel = currentStatusLabel(connectionState = ConnectionState.Connecting, isScanning = false),
                        isConnecting = true,
                        connectedDeviceName = selectedDeviceName,
                        capabilityInfo = null,
                        protocolMode = ProtocolMode.UNKNOWN,
                        notifyEnabled = false,
                        notifyError = null,
                        streamWarning = null,
                        telemetryPoints = emptyList(),
                        isRecording = false,
                        recordingDestination = null,
                        recordingStatus = null,
                        captureStatus = null,
                        writeModelStatus = null,
                        stableWeightActive = false,
                        deviceState = DeviceState.UNKNOWN,
                        faultStatus = FaultStatusUi(),
                        safetyStatus = defaultSafetyStatus(),
                    ),
                ),
            )
        }

        viewModelScope.launch {
            runCatching {
                client.connect(device.id)
            }.onSuccess {
                _uiState.update {
                    withCaptureAvailability(
                        it.copy(
                            notifyEnabled = true,
                            notifyError = null,
                        ),
                    )
                }
                appendSystemLog(text(R.string.log_notify_enabled))

                val connectAtMs = System.currentTimeMillis()
                telemetrySessionStartMs = connectAtMs
                startStreamWatchdog(connectAtMs)

                val probe = runCatching {
                    client.capabilityProbe()
                }.onFailure { error ->
                    appendSystemLog(
                        text(
                            R.string.log_capability_probe_failed,
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }.getOrNull()

                _uiState.update {
                    withCaptureAvailability(
                        it.copy(
                            isDeviceSheetVisible = false,
                            capabilityInfo = probe?.let(::formatCapabilityInfo),
                            protocolMode = probe?.mode ?: ProtocolMode.UNKNOWN,
                            lastAckOrError = probe?.reason ?: text(R.string.message_connected),
                        ),
                    )
                }
                probe?.reason?.let { reason ->
                    appendSystemLog(text(R.string.log_capability_probe_result, reason))
                }
                if (probe?.mode == ProtocolMode.PRIMARY) {
                    appendSystemLog(text(R.string.log_canonical_protocol_confirmed))
                }
            }.onFailure { error ->
                _uiState.update {
                    withCaptureAvailability(
                        it.copy(
                            lastAckOrError = text(
                                R.string.message_connect_failed,
                                error.message ?: text(R.string.common_not_available),
                            ),
                            isConnecting = false,
                            notifyEnabled = false,
                            notifyError = error.message,
                            statusLabel = currentStatusLabel(
                                connectionState = it.connectionState,
                                isScanning = false,
                            ),
                        ),
                    )
                }
            }
        }
    }

    fun disconnect() {
        disconnectRequested = true
        hadConnectedSession = false
        awaitingCalibrationCaptureResult = false
        awaitingModelWriteResult = false
        pendingWriteModelType = null
        client.disconnect()
        selectedDeviceName = null
        streamWatchdogJob?.cancel()
        stopRecordingIfActive(text(R.string.recording_stopped_disconnect))
        telemetrySessionStartMs = 0L
        _uiState.update {
            resetCalibrationSessionState(
                withCaptureAvailability(
                    it.copy(
                        connectedDeviceName = null,
                        isConnecting = false,
                        notifyEnabled = false,
                        notifyError = null,
                        capabilityInfo = null,
                        protocolMode = ProtocolMode.UNKNOWN,
                        streamWarning = null,
                        isRecording = false,
                        recordingDestination = null,
                        recordingStatus = text(R.string.recording_stopped_disconnect),
                        captureStatus = null,
                        writeModelStatus = null,
                        stableWeightActive = false,
                        safetyStatus = defaultSafetyStatus(),
                        statusLabel = currentStatusLabel(
                            connectionState = ConnectionState.Disconnected,
                            isScanning = isScanActive(it.scanState),
                        ),
                    ),
                ),
            )
        }
    }

    fun updateZeroInput(value: String) {
        _uiState.update { it.copy(zeroInput = value) }
    }

    fun updateFactorInput(value: String) {
        _uiState.update { it.copy(factorInput = value) }
    }

    fun updateCaptureReferenceInput(value: String) {
        _uiState.update { it.copy(captureReferenceInput = value) }
    }

    fun updateModelReferenceInput(value: String) {
        _uiState.update { state ->
            synchronizeModelSelectionUi(
                state.copy(
                    modelRefInput = value,
                    preparedModel = parsePreparedModelFromInputs(
                        state = state.copy(modelRefInput = value),
                        source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
                    ),
                ),
            )
        }
    }

    fun updateModelC0Input(value: String) {
        _uiState.update { state ->
            synchronizeModelSelectionUi(
                state.copy(
                    modelC0Input = value,
                    preparedModel = parsePreparedModelFromInputs(
                        state = state.copy(modelC0Input = value),
                        source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
                    ),
                ),
            )
        }
    }

    fun updateModelC1Input(value: String) {
        _uiState.update { state ->
            synchronizeModelSelectionUi(
                state.copy(
                    modelC1Input = value,
                    preparedModel = parsePreparedModelFromInputs(
                        state = state.copy(modelC1Input = value),
                        source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
                    ),
                ),
            )
        }
    }

    fun updateModelC2Input(value: String) {
        _uiState.update { state ->
            synchronizeModelSelectionUi(
                state.copy(
                    modelC2Input = value,
                    preparedModel = parsePreparedModelFromInputs(
                        state = state.copy(modelC2Input = value),
                        source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
                    ),
                ),
            )
        }
    }

    fun toggleEngineeringSection() {
        _uiState.update { it.copy(isEngineeringSectionExpanded = !it.isEngineeringSectionExpanded) }
    }

    fun toggleVerboseStreamLogs() {
        _uiState.update { state ->
            state.copy(verboseStreamLogsEnabled = !state.verboseStreamLogsEnabled)
        }
        appendSystemLog("[LOG] streamVerbose enabled=${_uiState.value.verboseStreamLogsEnabled}")
    }

    fun updateModelType(type: CalibrationModelType) {
        _uiState.update { state ->
            prepareSelectedModelForDeployment(
                state.copy(
                    modelType = type,
                    selectedComparisonModel = type,
                ),
                forceSelectedFit = true,
            )
        }
        val prepared = _uiState.value.preparedModel
        if (prepared != null && prepared.type == type) {
            appendSystemLog(
                "[CAL_UI] selectedModel=${type.name} preparedRef=${prepared.referenceDistance} c0=${prepared.c0} c1=${prepared.c1} c2=${prepared.c2}",
            )
        } else {
            appendSystemLog("[CAL_UI] selectedModel=${type.name} preparedModel=UNAVAILABLE")
        }
    }

    fun updateFreqInput(value: String) {
        _uiState.update { state ->
            state.copy(
                freqInput = value,
                freq = value.toIntOrNull() ?: state.freq,
            )
        }
    }

    fun updateIntensityInput(value: String) {
        _uiState.update { state ->
            state.copy(
                intensityInput = value,
                intensity = value.toIntOrNull() ?: state.intensity,
            )
        }
    }

    fun setPresetFrequency(freq: Int) {
        _uiState.update { it.copy(freqInput = freq.toString(), freq = freq) }
    }

    fun sendZero() {
        sendCommand(Command.ScaleZero, "SCALE:ZERO")
    }

    fun sendCalibrate() {
        val zero = _uiState.value.zeroInput.toFloatOrNull()
        val factor = _uiState.value.factorInput.toFloatOrNull()
        if (zero == null || factor == null) {
            _uiState.update { it.copy(lastAckOrError = text(R.string.message_invalid_calibration)) }
            return
        }
        clearStableIndicator()
        sendCommand(Command.ScaleCal(zeroDistance = zero, scaleFactor = factor), "SCALE:CAL")
    }

    fun sendWaveStart() {
        val freq = _uiState.value.freqInput.toIntOrNull()
        val intensity = _uiState.value.intensityInput.toIntOrNull()
        if (freq == null || intensity == null) {
            _uiState.update { it.copy(lastAckOrError = text(R.string.message_invalid_wave_parameters)) }
            return
        }
        if (freq !in 0..50 || intensity !in 0..120) {
            _uiState.update { it.copy(lastAckOrError = text(R.string.message_wave_range)) }
            return
        }

        _uiState.update { it.copy(freq = freq, intensity = intensity) }

        viewModelScope.launch {
            runCatching {
                client.send(Command.WaveSet(freqHz = freq, intensity = intensity))
                client.send(Command.WaveStart)
            }.onSuccess {
                _uiState.update {
                    it.copy(lastAckOrError = text(R.string.message_sent_wave_start_bundle))
                }
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            "WAVE:SET -> WAVE:START",
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
    }

    fun sendWaveStop() {
        sendCommand(Command.WaveStop, "WAVE:STOP")
    }

    fun sendCalibrationCapture() {
        val state = _uiState.value
        appendSystemLog(buildCapturePreconditionLog(state))
        if (!state.canCaptureCalibrationPoint) {
            _uiState.update {
                it.copy(
                    lastAckOrError = describeCaptureUnavailable(state),
                    captureStatus = captureFeedback(
                        kind = CaptureFeedbackKind.FAILURE,
                        message = describeCaptureUnavailable(state),
                    ),
                )
            }
            return
        }
        val referenceWeight = state.captureReferenceInput.toFloatOrNull()
        if (referenceWeight == null || referenceWeight < 0.0f) {
            _uiState.update {
                it.copy(
                    lastAckOrError = text(R.string.message_invalid_capture_reference),
                    captureStatus = captureFeedback(
                        kind = CaptureFeedbackKind.FAILURE,
                        message = text(R.string.capture_status_invalid_reference),
                        rawReason = "INVALID_REFERENCE_WEIGHT",
                    ),
                )
            }
            return
        }
        val distanceRuntime = state.distance
        if (distanceRuntime == null || !distanceRuntime.isFinite()) {
            _uiState.update {
                it.copy(
                    lastAckOrError = text(R.string.capture_unavailable_no_live_distance),
                    captureStatus = captureFeedback(
                        kind = CaptureFeedbackKind.FAILURE,
                        message = text(R.string.capture_unavailable_no_live_distance),
                    ),
                )
            }
            return
        }

        val nextIndex = state.calibrationPoints.size + 1
        val point = CalibrationPointUi(
            index = nextIndex,
            timestampMs = System.currentTimeMillis(),
            distanceMm = distanceRuntime * CALIBRATION_DISTANCE_RUNTIME_DIVISOR,
            referenceWeightKg = referenceWeight,
            predictedWeightKg = state.weight,
            liveWeightKg = state.weight,
            stableWeightKg = state.stableWeight,
            captureRoute = CalibrationCaptureRouteUi.APP_LIVE_SNAPSHOT,
            visibleSampleValid = hasVisibleCaptureQuality(state),
            stableFlag = state.stableWeightActive,
            validFlag = true,
        )

        appendSystemLog(
            "[CAL_APP] record clicked route=APP_LIVE_SNAPSHOT recording=${state.isRecording} connected=${state.isConnected} stableVisible=${state.stableWeightActive} refValid=${isCaptureReferenceValid(state.captureReferenceInput)} distancePresent=${hasCaptureDistanceSnapshot(state)}",
        )
        _uiState.update { current ->
            val nextPoints = current.calibrationPoints + point
            rebuildCalibrationComparison(
                current.copy(
                    latestCalibrationPoint = point,
                    calibrationPoints = nextPoints,
                    captureStatus = captureFeedback(
                        kind = CaptureFeedbackKind.SUCCESS,
                        message = text(
                            R.string.capture_status_recorded,
                            nextPoints.size,
                            point.index ?: nextPoints.size,
                        ),
                        rawReason = point.captureRoute.name,
                    ),
                    lastAckOrError = text(
                        R.string.message_calibration_point_recorded_with_count,
                        nextPoints.size,
                    ),
                ),
            )
        }
        appendSystemLog(
            "[CAL_APP] point appended count=${_uiState.value.calibrationPoints.size} distance_mm=${point.distanceMm} ref=${point.referenceWeightKg} stableVisible=${point.stableFlag} route=${point.captureRoute.name}",
        )
        appendSystemLog("[CAL_APP] fit dataset size=${_uiState.value.comparisonResult?.sampleCount ?: 0}")
    }

    fun sendCalibrationGetModel() {
        sendCommand(Command.CalibrationGetModel, "CAL:GET_MODEL")
    }

    fun sendCalibrationSetModel() {
        val state = _uiState.value
        val prepared = state.preparedModel ?: parsePreparedModelFromInputs(
            state = state,
            source = PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE,
        )
        if (prepared == null) {
            _uiState.update {
                it.copy(
                    lastAckOrError = text(R.string.message_invalid_model_values),
                    writeModelStatus = writeModelFeedback(
                        kind = WriteModelFeedbackKind.FAILURE,
                        message = text(
                            R.string.write_model_status_failure_with_reason,
                            text(R.string.message_invalid_model_values),
                        ),
                    ),
                )
            }
            return
        }
        clearStableIndicator()
        awaitingModelWriteResult = true
        pendingWriteModelType = prepared.type
        _uiState.update {
            it.copy(
                writeModelStatus = writeModelFeedback(
                    kind = WriteModelFeedbackKind.PENDING,
                    message = text(
                        R.string.write_model_status_pending,
                        modelTypeLabel(prepared.type),
                    ),
                    modelType = prepared.type,
                ),
            )
        }
        appendSystemLog(
            "[CAL_UI] writeModel using selectedModel=${prepared.type.name} preparedRef=${prepared.referenceDistance} c0=${prepared.c0} c1=${prepared.c1} c2=${prepared.c2} source=${prepared.source.name}",
        )
        viewModelScope.launch {
            runCatching {
                client.send(
                    Command.CalibrationSetModel(
                        type = prepared.type,
                        referenceDistance = prepared.referenceDistance,
                        c0 = prepared.c0,
                        c1 = prepared.c1,
                        c2 = prepared.c2,
                    ),
                )
            }.onSuccess {
                _uiState.update { it.copy(lastAckOrError = text(R.string.message_sent, "CAL:SET_MODEL")) }
            }.onFailure { error ->
                awaitingModelWriteResult = false
                pendingWriteModelType = null
                appendSystemLog("[CAL_UI] writeModel result=failure reason=${error.message ?: "send_failed"}")
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            "CAL:SET_MODEL",
                            error.message ?: text(R.string.common_not_available),
                        ),
                        writeModelStatus = writeModelFeedback(
                            kind = WriteModelFeedbackKind.FAILURE,
                            message = text(
                                R.string.write_model_status_failure_with_reason,
                                text(
                                    R.string.write_model_status_send_failed_reason,
                                    error.message ?: text(R.string.common_not_available),
                                ),
                            ),
                            modelType = prepared.type,
                            rawReason = error.message,
                        ),
                    )
                }
            }
        }
    }

    fun sendCalibrationZero() {
        clearStableIndicator()
        appendSystemLog("[CAL_UI] calZeroRequested")
        sendCommand(Command.CalibrationZero, "CAL:ZERO")
    }

    fun startRecording() {
        if (_uiState.value.isRecording) return
        viewModelScope.launch(Dispatchers.IO) {
            recordingMutex.withLock {
                runCatching {
                    recorder.startSession()
                }.onSuccess { session ->
                    recordingSession = session
                    _uiState.update {
                        withCaptureAvailability(
                            it.copy(
                                isRecording = true,
                                recordingDestination = session.destinationLabel,
                                recordingStatus = text(R.string.recording_started, session.destinationLabel),
                            ),
                        )
                    }
                    appendSystemLog(text(R.string.log_recording_started, session.destinationLabel))
                    appendSystemLog("[CAL_UI] recording=true pointRecordEnabled=${_uiState.value.canCaptureCalibrationPoint}")
                }.onFailure { error ->
                    _uiState.update {
                        withCaptureAvailability(
                            it.copy(
                                isRecording = false,
                                recordingStatus = text(
                                    R.string.recording_failed,
                                    error.message ?: text(R.string.common_not_available),
                                ),
                            ),
                        )
                    }
                }
            }
        }
    }

    fun stopRecording() {
        stopRecordingIfActive(text(R.string.recording_stopped_manual))
    }

    fun clearRawLog() {
        _uiState.update { it.copy(rawLogLines = emptyList()) }
    }

    private fun observeClient() {
        viewModelScope.launch {
            client.scanResults.collect { devices ->
                val filtered = devices
                    .filter(::isPreferredDevice)
                    .sortedWith(
                        compareByDescending<BleScanResult> { it.advertisesUartService }
                            .thenByDescending { it.rssi },
                    )

                _uiState.update { state ->
                    val activeScan = isScanActive(state.scanState)
                    val nextScanState = when {
                        !activeScan -> ScanState.Idle
                        filtered.isNotEmpty() -> ScanState.Results(filtered.size)
                        else -> ScanState.Scanning
                    }
                    state.copy(scanResults = filtered, scanState = nextScanState)
                }
            }
        }

        viewModelScope.launch {
            client.connection.collect { connection ->
                val transportDrop = hadConnectedSession &&
                    !disconnectRequested &&
                    (connection is ConnectionState.Disconnected || connection is ConnectionState.Error)
                if (transportDrop) {
                    val detail = (connection as? ConnectionState.Error)?.message
                        ?: text(R.string.connection_state_disconnected)
                    val safetyStatus = transportDisconnectSafetyStatus(detail)
                    appendSystemLog(text(R.string.log_transport_disconnect_safety, detail))
                    _uiState.update { current ->
                        withCaptureAvailability(
                            current.copy(
                                safetyStatus = safetyStatus,
                                faultStatus = faultStatusFromCode(safetyStatus.code),
                                lastAckOrError = formatSafetyStatusMessage(safetyStatus),
                                stableWeightActive = false,
                            ),
                        )
                    }
                }

                _uiState.update { state ->
                    val isConnected = connection is ConnectionState.Connected
                    val isConnecting = when (connection) {
                        ConnectionState.Connecting,
                        ConnectionState.DiscoveringServices,
                        ConnectionState.Subscribing,
                        -> true

                        else -> false
                    }

                    val errorMessage = if (connection is ConnectionState.Error) {
                        connection.message
                    } else {
                        null
                    }

                    val notifyEnabled = connection is ConnectionState.Connected
                    val notifyError = when {
                        connection is ConnectionState.Connected -> null
                        connection is ConnectionState.Error -> connection.message
                        connection is ConnectionState.Disconnected -> null
                        else -> state.notifyError
                    }

                    withCaptureAvailability(
                        state.copy(
                            connectionState = connection,
                            isConnected = isConnected,
                            isConnecting = isConnecting,
                            connectedDeviceName = if (isConnected) {
                                state.connectedDeviceName ?: selectedDeviceName
                            } else if (state.isDeviceSheetVisible || isConnecting) {
                                state.connectedDeviceName
                            } else {
                                null
                            },
                            statusLabel = currentStatusLabel(connectionState = connection, isScanning = isScanActive(state.scanState)),
                            notifyEnabled = notifyEnabled,
                            notifyError = notifyError,
                            lastAckOrError = errorMessage ?: state.lastAckOrError,
                        ),
                    )
                }

                if (connection is ConnectionState.Connected) {
                    hadConnectedSession = true
                    disconnectRequested = false
                }

                if (connection is ConnectionState.Error) {
                    appendSystemLog(
                        text(
                            R.string.log_notify_failed,
                            connection.message,
                        ),
                    )
                }
                if (connection is ConnectionState.Error || connection is ConnectionState.Disconnected) {
                    hadConnectedSession = false
                    awaitingCalibrationCaptureResult = false
                    awaitingModelWriteResult = false
                    pendingWriteModelType = null
                    if (disconnectRequested) {
                        disconnectRequested = false
                    }
                    streamWatchdogJob?.cancel()
                    clearStableIndicator()
                    stopRecordingIfActive(text(R.string.recording_stopped_disconnect))
                    _uiState.update { state ->
                        resetCalibrationSessionState(
                            withCaptureAvailability(
                                state.copy(
                                    isRecording = false,
                                    recordingDestination = null,
                                    recordingStatus = text(R.string.recording_stopped_disconnect),
                                    captureStatus = null,
                                    writeModelStatus = null,
                                ),
                            ),
                        )
                    }
                }
            }
        }

        viewModelScope.launch {
            client.rawLines.collect { line ->
                if (shouldAppendIncomingRawLine(line)) {
                    appendRawLog("RX", line)
                }
            }
        }

        viewModelScope.launch {
            client.rawChunks.collect { chunk ->
                if (_uiState.value.verboseStreamLogsEnabled) {
                    appendRawLog("RX-RAW", chunk)
                }
            }
        }

        viewModelScope.launch {
            client.transportLogs.collect { line ->
                appendRawLog("SYS", line)
            }
        }

        viewModelScope.launch {
            client.outgoingLines.collect { line ->
                appendRawLog("TX", line)
            }
        }

        viewModelScope.launch {
            client.events.collect { event ->
                when (event) {
                    is Event.StreamSample -> onStreamSample(event.distance, event.weight)

                    is Event.Stable -> _uiState.update {
                        it.copy(
                            stableWeight = event.stableWeightKg,
                            stableWeightActive = true,
                        )
                    }

                    is Event.Param -> _uiState.update {
                        it.copy(
                            calibrationZero = event.zeroDistance,
                            calibrationFactor = event.scaleFactor,
                        )
                    }

                    is Event.State -> _uiState.update {
                        val nextSafetyStatus = if (
                            event.state == DeviceState.RUNNING &&
                            it.safetyStatus.effectCode == SafetyEffect.RECOVERABLE_PAUSE.name
                        ) {
                            defaultSafetyStatus()
                        } else {
                            it.safetyStatus
                        }
                        it.copy(
                            deviceState = event.state,
                            stableWeightActive = false,
                            safetyStatus = nextSafetyStatus,
                        )
                    }

                    is Event.Fault -> {
                        val faultStatus = faultStatusFromCode(event.code)
                        _uiState.update {
                            it.copy(
                                faultStatus = faultStatus,
                                lastAckOrError = text(R.string.message_fault, faultStatus.label),
                                stableWeightActive = if (faultStatus.code == 0) {
                                    it.stableWeightActive
                                } else {
                                    false
                                },
                            )
                        }
                    }

                    is Event.Safety -> {
                        val safetyStatus = safetyStatusFromEvent(event)
                        _uiState.update {
                            it.copy(
                                deviceState = if (event.state != DeviceState.UNKNOWN) {
                                    event.state
                                } else {
                                    it.deviceState
                                },
                                faultStatus = event.code?.let(::faultStatusFromCode) ?: it.faultStatus,
                                safetyStatus = safetyStatus,
                                lastAckOrError = formatSafetyStatusMessage(safetyStatus),
                                stableWeightActive = when (event.effect) {
                                    SafetyEffect.WARNING_ONLY -> it.stableWeightActive
                                    else -> false
                                },
                            )
                        }
                    }

                    is Event.CalibrationPoint -> _uiState.update { state ->
                        val point = CalibrationPointUi(
                            index = event.index,
                            timestampMs = event.timestampMs,
                            distanceMm = event.distanceMm,
                            referenceWeightKg = event.referenceWeightKg,
                            predictedWeightKg = event.predictedWeightKg,
                            liveWeightKg = event.predictedWeightKg,
                            stableWeightKg = event.predictedWeightKg,
                            captureRoute = CalibrationCaptureRouteUi.DEVICE_STABLE_CAPTURE,
                            visibleSampleValid = event.validFlag,
                            stableFlag = event.stableFlag,
                            validFlag = event.validFlag,
                            raw = event.raw,
                        )
                        val nextPoints = state.calibrationPoints + point
                        rebuildCalibrationComparison(
                            state.copy(
                                latestCalibrationPoint = point,
                                calibrationPoints = nextPoints,
                                captureStatus = captureFeedback(
                                    kind = CaptureFeedbackKind.INFO,
                                    message = text(
                                        R.string.capture_status_legacy_recorded,
                                        nextPoints.size,
                                        point.index ?: nextPoints.size,
                                    ),
                                    rawReason = point.captureRoute.name,
                                ),
                                lastAckOrError = text(
                                    R.string.message_calibration_point_recorded_with_count,
                                    nextPoints.size,
                                ),
                            ),
                        )
                    }.also {
                        appendSystemLog("[CAL_UI] pointRecorded count=${_uiState.value.calibrationPoints.size}")
                    }

                    is Event.CalibrationModel -> _uiState.update {
                        val type = event.type ?: CalibrationModelType.LINEAR
                        synchronizeModelSelectionUi(
                            it.copy(
                            latestModel = CalibrationModelUi(
                                type = type,
                                referenceDistance = event.referenceDistance ?: 0.0f,
                                c0 = event.c0 ?: 0.0f,
                                c1 = event.c1 ?: 1.0f,
                                c2 = event.c2 ?: 0.0f,
                                raw = event.raw,
                            ),
                            lastAckOrError = event.raw,
                            ),
                        )
                    }.also {
                        val model = _uiState.value.latestModel
                        if (model != null) {
                            appendSystemLog(
                                "[CAL_UI] fetchedModel ref=${model.referenceDistance} c0=${model.c0} c1=${model.c1} c2=${model.c2}",
                            )
                        }
                    }

                    is Event.Capabilities -> _uiState.update {
                        it.copy(capabilityInfo = formatCapabilities(event))
                    }

                    is Event.CalibrationSetModelResult -> {
                        val systemLogs = mutableListOf<String>()
                        val activeModelType = event.type ?: pendingWriteModelType
                        val writeModelStatus = if (awaitingModelWriteResult) {
                            awaitingModelWriteResult = false
                            pendingWriteModelType = null
                            if (event.success) {
                                systemLogs += "[CAL_UI] writeModel ack success selected=${activeModelType?.name ?: "UNKNOWN"} raw=${event.raw}"
                                systemLogs += "[CAL_UI_BIND] buttonTextSource=writeModelStatus value=SUCCESS"
                                systemLogs += "[CAL_UI_BIND] buttonVisualSource=writeModelStatus value=SUCCESS"
                                systemLogs += "[CAL_UI_BIND] resultCardSource=writeModelStatus value=SUCCESS"
                                systemLogs += "[CAL_UI] writeResult state=SUCCESS visible=true model=${activeModelType?.name ?: "UNKNOWN"}"
                                writeModelFeedback(
                                    kind = WriteModelFeedbackKind.SUCCESS,
                                    message = text(
                                        R.string.write_model_status_success,
                                        modelTypeLabel(activeModelType),
                                    ),
                                    modelType = activeModelType,
                                    rawReason = event.raw,
                                )
                            } else {
                                val reason = event.reason ?: "UNKNOWN"
                                systemLogs += "[CAL_UI] writeModel ack failure selected=${activeModelType?.name ?: "UNKNOWN"} reason=$reason"
                                systemLogs += "[CAL_UI_BIND] buttonTextSource=writeModelStatus value=FAILURE"
                                systemLogs += "[CAL_UI_BIND] buttonVisualSource=writeModelStatus value=FAILURE"
                                systemLogs += "[CAL_UI_BIND] resultCardSource=writeModelStatus value=FAILURE"
                                systemLogs += "[CAL_UI] writeResult state=FAILURE visible=true reason=$reason"
                                writeModelFeedback(
                                    kind = WriteModelFeedbackKind.FAILURE,
                                    message = text(
                                        R.string.write_model_status_failure_with_reason,
                                        formatModelWriteFailure(reason),
                                    ),
                                    modelType = activeModelType,
                                    rawReason = reason,
                                )
                            }
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = if (event.success) {
                                    event.raw
                                } else {
                                    formatNackMessage(event.reason ?: "UNKNOWN")
                                },
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    is Event.Ack -> {
                        val systemLogs = mutableListOf<String>()
                        val captureStatus = if (awaitingCalibrationCaptureResult) {
                            awaitingCalibrationCaptureResult = false
                            systemLogs += "[CAL_AUDIT] captureAckUnexpected raw=${event.raw}"
                            captureFeedback(
                                kind = CaptureFeedbackKind.FAILURE,
                                message = text(R.string.capture_status_unexpected_ack, event.raw),
                                rawReason = event.raw,
                            )
                        } else {
                            null
                        }
                        val writeModelStatus = if (awaitingModelWriteResult) {
                            val modelType = pendingWriteModelType
                            awaitingModelWriteResult = false
                            pendingWriteModelType = null
                            systemLogs += "[CAL_UI] writeModel result=success model=${modelType?.name ?: "UNKNOWN"}"
                            systemLogs += "[CAL_UI_BIND] buttonTextSource=writeModelStatus value=SUCCESS"
                            systemLogs += "[CAL_UI_BIND] buttonVisualSource=writeModelStatus value=SUCCESS"
                            systemLogs += "[CAL_UI_BIND] resultCardSource=writeModelStatus value=SUCCESS"
                            writeModelFeedback(
                                kind = WriteModelFeedbackKind.SUCCESS,
                                message = text(
                                    R.string.write_model_status_success,
                                    modelTypeLabel(modelType),
                                ),
                                modelType = modelType,
                                rawReason = event.raw,
                            )
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = event.raw,
                                captureStatus = captureStatus ?: it.captureStatus,
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    is Event.Nack -> {
                        val reason = event.reason.trim()
                        val systemLogs = mutableListOf<String>()
                        val captureStatus = if (awaitingCalibrationCaptureResult) {
                            awaitingCalibrationCaptureResult = false
                            systemLogs += "[CAL_AUDIT] captureFailed reason=$reason"
                            captureFeedback(
                                kind = CaptureFeedbackKind.FAILURE,
                                message = formatCalibrationCaptureFailure(reason),
                                rawReason = reason,
                            )
                        } else {
                            null
                        }
                        val writeModelStatus = if (awaitingModelWriteResult) {
                            val modelType = pendingWriteModelType
                            awaitingModelWriteResult = false
                            pendingWriteModelType = null
                            systemLogs += "[CAL_UI] writeModel result=failure reason=$reason"
                            systemLogs += "[CAL_UI_BIND] buttonTextSource=writeModelStatus value=FAILURE"
                            systemLogs += "[CAL_UI_BIND] buttonVisualSource=writeModelStatus value=FAILURE"
                            systemLogs += "[CAL_UI_BIND] resultCardSource=writeModelStatus value=FAILURE"
                            writeModelFeedback(
                                kind = WriteModelFeedbackKind.FAILURE,
                                message = text(
                                    R.string.write_model_status_failure_with_reason,
                                    formatNackMessage(reason),
                                ),
                                modelType = modelType,
                                rawReason = reason,
                            )
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = formatNackMessage(reason),
                                captureStatus = captureStatus ?: it.captureStatus,
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    is Event.Error -> {
                        val systemLogs = mutableListOf<String>()
                        val captureStatus = if (awaitingCalibrationCaptureResult) {
                            awaitingCalibrationCaptureResult = false
                            systemLogs += "[CAL_AUDIT] captureError reason=${event.reason}"
                            captureFeedback(
                                kind = CaptureFeedbackKind.FAILURE,
                                message = text(R.string.capture_status_send_failed, event.reason),
                                rawReason = event.reason,
                            )
                        } else {
                            null
                        }
                        val writeModelStatus = if (awaitingModelWriteResult) {
                            val modelType = pendingWriteModelType
                            awaitingModelWriteResult = false
                            pendingWriteModelType = null
                            systemLogs += "[CAL_UI] writeModel result=failure reason=${event.reason}"
                            systemLogs += "[CAL_UI_BIND] buttonTextSource=writeModelStatus value=FAILURE"
                            systemLogs += "[CAL_UI_BIND] buttonVisualSource=writeModelStatus value=FAILURE"
                            systemLogs += "[CAL_UI_BIND] resultCardSource=writeModelStatus value=FAILURE"
                            writeModelFeedback(
                                kind = WriteModelFeedbackKind.FAILURE,
                                message = text(
                                    R.string.write_model_status_failure_with_reason,
                                    text(R.string.write_model_status_send_failed_reason, event.reason),
                                ),
                                modelType = modelType,
                                rawReason = event.reason,
                            )
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = text(R.string.message_error, event.reason),
                                captureStatus = captureStatus ?: it.captureStatus,
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    else -> Unit
                }
            }
        }
    }

    private fun sendCommand(command: Command, label: String) {
        viewModelScope.launch {
            runCatching {
                client.send(command)
            }.onSuccess {
                _uiState.update { it.copy(lastAckOrError = text(R.string.message_sent, label)) }
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            label,
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
    }

    private fun isPreferredDevice(device: BleScanResult): Boolean {
        if (device.advertisesUartService) return true
        val name = device.name ?: return false
        return NAME_KEYWORDS.any { keyword -> name.contains(keyword, ignoreCase = true) }
    }

    private fun isScanActive(scanState: ScanState): Boolean {
        return scanState is ScanState.Scanning || scanState is ScanState.Results
    }

    private fun currentStatusLabel(connectionState: ConnectionState, isScanning: Boolean): String {
        return when {
            connectionState is ConnectionState.Connected -> text(R.string.connection_state_connected)
            connectionState is ConnectionState.Connecting ||
                connectionState is ConnectionState.DiscoveringServices ||
                connectionState is ConnectionState.Subscribing -> text(R.string.connection_state_connecting)

            isScanning -> text(R.string.connection_state_scanning)
            else -> text(R.string.connection_state_disconnected)
        }
    }

    private fun onStreamSample(distance: Float, weight: Float) {
        val now = System.currentTimeMillis()
        if (telemetrySessionStartMs == 0L) {
            telemetrySessionStartMs = now
        }
        lastStreamAtMs = now
        streamWatchdogJob?.cancel()
        val point = TelemetryPointUi(
            elapsedMs = now - telemetrySessionStartMs,
            timestampMs = now,
            distance = distance,
            weight = weight,
            stableFlag = _uiState.value.stableWeightActive,
        )
        val minElapsedMs = (point.elapsedMs - TELEMETRY_WINDOW_MS).coerceAtLeast(0L)

        _uiState.update {
            it.copy(
                distance = distance,
                weight = weight,
                streamWarning = null,
                telemetryPoints = (it.telemetryPoints + point).filter { sample -> sample.elapsedMs >= minElapsedMs },
            )
        }

        if (_uiState.value.isRecording) {
            viewModelScope.launch(Dispatchers.IO) {
                recordingMutex.withLock {
                    recordingSession?.let { session ->
                        runCatching {
                            recorder.appendRow(session, point)
                        }.onFailure { error ->
                            _uiState.update {
                                withCaptureAvailability(
                                    it.copy(
                                        isRecording = false,
                                        recordingStatus = text(
                                            R.string.recording_failed,
                                            error.message ?: text(R.string.common_not_available),
                                        ),
                                    ),
                                )
                            }
                            runCatching { recorder.stopSession(session) }
                            recordingSession = null
                            appendSystemLog("[CAL_UI] recording=false pointRecordEnabled=false")
                        }
                    }
                }
            }
        }
    }

    private fun startStreamWatchdog(connectAtMs: Long) {
        streamWatchdogJob?.cancel()
        streamWatchdogJob = viewModelScope.launch {
            delay(NO_STREAM_WARNING_TIMEOUT_MS)
            val state = _uiState.value
            if (!state.isConnected || lastStreamAtMs >= connectAtMs) return@launch

            val warning = text(R.string.warning_no_stream_data)
            val debug = "state=${state.connectionState.javaClass.simpleName}, notifyEnabled=${state.notifyEnabled}, " +
                "notifyError=${state.notifyError ?: "-"}, capability=${state.capabilityInfo ?: "-"}"
            _uiState.update {
                it.copy(
                    streamWarning = warning,
                    lastAckOrError = warning,
                )
            }
            appendRawLog("SYS", "WARN $warning | $debug")
        }
    }

    private fun formatCapabilityInfo(result: CapabilityResult): String {
        return result.capabilities?.let(::formatCapabilities)
            ?: result.reason
            ?: text(R.string.common_not_available)
    }

    private fun formatCapabilities(capabilities: Event.Capabilities): String {
        if (capabilities.values.isEmpty()) return capabilities.raw
        return capabilities.values.entries.joinToString(", ") { (key, value) -> "$key=$value" }
    }

    private fun formatNackMessage(reason: String): String {
        val hint = when (reason.uppercase()) {
            "NOT_ARMED" -> text(R.string.nack_hint_not_armed)
            "FAULT_LOCKED" -> text(R.string.nack_hint_fault_locked)
            "UNSUPPORTED" -> text(R.string.nack_hint_unsupported)
            else -> null
        }
        return if (hint == null) {
            text(R.string.message_nack, reason)
        } else {
            text(R.string.message_nack_with_hint, reason, hint)
        }
    }

    private fun formatCalibrationCaptureFailure(reason: String): String {
        return when (reason.uppercase()) {
            "NOT_STABLE" -> text(R.string.capture_status_not_stable)
            "INVALID_SAMPLE" -> text(R.string.capture_status_invalid_sample)
            "INVALID_STABLE_BASELINE" -> text(R.string.capture_status_invalid_stable_baseline)
            "INVALID_REFERENCE_WEIGHT" -> text(R.string.capture_status_invalid_reference)
            else -> text(R.string.capture_status_generic_failure, reason)
        }
    }

    private fun formatModelWriteFailure(reason: String): String {
        return when (reason.uppercase()) {
            "INVALID_MODEL" -> text(R.string.message_invalid_model_values)
            "NON_MONOTONIC" -> text(R.string.write_model_status_non_monotonic)
            "INVALID_PARAM" -> text(R.string.message_invalid_model_values)
            else -> formatNackMessage(reason)
        }
    }

    private fun describeCaptureUnavailable(state: UiState): String {
        return when {
            !state.isConnected -> text(R.string.capture_unavailable_disconnected)
            !state.isRecording -> text(R.string.message_capture_requires_recording)
            !isCaptureReferenceValid(state.captureReferenceInput) -> text(R.string.capture_status_invalid_reference)
            !hasCaptureDistanceSnapshot(state) -> text(R.string.capture_unavailable_no_live_distance)
            else -> text(R.string.capture_unavailable_generic)
        }
    }

    private fun captureFeedback(
        kind: CaptureFeedbackKind,
        message: String,
        rawReason: String? = null,
    ): CaptureFeedbackUi {
        return CaptureFeedbackUi(
            kind = kind,
            message = message,
            rawReason = rawReason,
        )
    }

    private fun isCaptureReferenceValid(input: String): Boolean {
        val value = input.toFloatOrNull() ?: return false
        return value >= 0.0f
    }

    private fun hasCaptureDistanceSnapshot(state: UiState): Boolean {
        val distance = state.distance
        return distance != null &&
            distance.isFinite()
    }

    private fun hasVisibleCaptureQuality(state: UiState): Boolean {
        val distance = state.distance
        val weight = state.weight
        return distance != null &&
            weight != null &&
            distance.isFinite() &&
            weight.isFinite() &&
            state.streamWarning == null
    }

    private fun buildCapturePreconditionLog(state: UiState): String {
        return "[CAL_APP] preconditions " +
            "recording=${state.isRecording} " +
            "connected=${state.isConnected} " +
            "stableVisible=${state.stableWeightActive} " +
            "refValid=${isCaptureReferenceValid(state.captureReferenceInput)} " +
            "distancePresent=${hasCaptureDistanceSnapshot(state)} " +
            "qualityVisible=${hasVisibleCaptureQuality(state)}"
    }

    private fun defaultSafetyStatus(): SafetyStatusUi {
        return SafetyStatusUi(
            meaning = text(R.string.safety_meaning_none),
            source = text(R.string.safety_source_none),
            sourceCode = "NONE",
        )
    }

    private fun safetyStatusFromEvent(event: Event.Safety): SafetyStatusUi {
        val reasonCode = event.reason.ifBlank { "NONE" }
        val effectCode = event.effect.name
        val runtimeCode = event.state.name
        val waveCode = event.wave.name
        val meaningRes = when {
            event.reason.equals("BLE_DISCONNECTED", ignoreCase = true) -> R.string.safety_meaning_reconnect_needed
            event.effect == SafetyEffect.RECOVERABLE_PAUSE -> R.string.safety_meaning_recoverable_pause
            event.effect == SafetyEffect.ABNORMAL_STOP -> R.string.safety_meaning_abnormal_stop
            event.effect == SafetyEffect.WARNING_ONLY -> R.string.safety_meaning_warning_only
            else -> R.string.safety_meaning_unknown
        }
        val severity = when {
            event.reason.equals("BLE_DISCONNECTED", ignoreCase = true) -> FaultSeverityUi.WARNING
            event.effect == SafetyEffect.RECOVERABLE_PAUSE -> FaultSeverityUi.WARNING
            event.effect == SafetyEffect.ABNORMAL_STOP -> FaultSeverityUi.BLOCKING
            event.effect == SafetyEffect.WARNING_ONLY -> FaultSeverityUi.INFO
            else -> FaultSeverityUi.INFO
        }
        return SafetyStatusUi(
            reason = safetyReasonLabel(reasonCode),
            reasonCode = reasonCode,
            effect = safetyEffectLabel(effectCode),
            effectCode = effectCode,
            runtimeState = runtimeStateLabel(runtimeCode),
            runtimeCode = runtimeCode,
            waveState = waveStateLabel(waveCode),
            waveCode = waveCode,
            meaning = text(meaningRes),
            source = text(R.string.safety_source_protocol),
            sourceCode = "EVT:SAFETY",
            severity = severity,
            code = event.code,
            raw = event.raw,
        )
    }

    private fun transportDisconnectSafetyStatus(detail: String): SafetyStatusUi {
        return SafetyStatusUi(
            reason = safetyReasonLabel("BLE_DISCONNECTED"),
            reasonCode = "BLE_DISCONNECTED",
            effect = safetyEffectLabel(SafetyEffect.WARNING_ONLY.name),
            effectCode = SafetyEffect.WARNING_ONLY.name,
            runtimeState = runtimeStateLabel("DISCONNECTED"),
            runtimeCode = "DISCONNECTED",
            waveState = waveStateLabel("UNKNOWN"),
            waveCode = "UNKNOWN",
            meaning = text(R.string.safety_meaning_reconnect_needed),
            source = text(R.string.safety_source_transport),
            sourceCode = "BLE transport",
            severity = FaultSeverityUi.WARNING,
            code = 102,
            raw = detail,
        )
    }

    private fun formatSafetyStatusMessage(status: SafetyStatusUi): String {
        return text(
            R.string.message_safety_event,
            "${status.reason} (${status.reasonCode})",
            "${status.effect} (${status.effectCode})",
            status.meaning.ifBlank { text(R.string.safety_meaning_unknown) },
        )
    }

    private fun withCaptureAvailability(state: UiState): UiState {
        return state.copy(
            canCaptureCalibrationPoint = state.isConnected &&
                state.isRecording &&
                isCaptureReferenceValid(state.captureReferenceInput) &&
                hasCaptureDistanceSnapshot(state),
        )
    }

    private fun resetCalibrationSessionState(state: UiState): UiState {
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

    private fun rebuildCalibrationComparison(state: UiState): UiState {
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

    private fun prepareSelectedModelForDeployment(
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

    private fun synchronizeModelSelectionUi(state: UiState): UiState {
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

    private fun fitForType(
        comparisonResult: CalibrationComparisonResult?,
        type: CalibrationModelType,
    ): CalibrationFitResult? {
        return when (type) {
            CalibrationModelType.LINEAR -> comparisonResult?.linear
            CalibrationModelType.QUADRATIC -> comparisonResult?.quadratic
        }
    }

    private fun parsePreparedModelFromInputs(
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

    private fun writeModelFeedback(
        kind: WriteModelFeedbackKind,
        message: String,
        modelType: CalibrationModelType? = null,
        rawReason: String? = null,
    ): WriteModelFeedbackUi {
        return WriteModelFeedbackUi(
            kind = kind,
            message = message,
            modelType = modelType,
            rawReason = rawReason,
        )
    }

    private fun modelTypeLabel(type: CalibrationModelType?): String {
        return when (type) {
            CalibrationModelType.LINEAR -> text(R.string.model_type_linear)
            CalibrationModelType.QUADRATIC -> text(R.string.model_type_quadratic)
            null -> text(R.string.common_not_available)
        }
    }

    private fun clearStableIndicator() {
        _uiState.update { it.copy(stableWeightActive = false) }
    }

    private fun stopRecordingIfActive(reason: String) {
        if (recordingSession == null) return
        viewModelScope.launch(Dispatchers.IO) {
            recordingMutex.withLock {
                val session = recordingSession ?: return@withLock
                runCatching {
                    recorder.stopSession(session)
                }
                recordingSession = null
                _uiState.update {
                    withCaptureAvailability(
                        it.copy(
                            isRecording = false,
                            recordingStatus = reason,
                            recordingDestination = null,
                        ),
                    )
                }
                appendSystemLog(reason)
                appendSystemLog("[CAL_UI] recording=false pointRecordEnabled=${_uiState.value.canCaptureCalibrationPoint}")
            }
        }
    }

    private fun appendSystemLog(message: String) {
        appendRawLog("SYS", message)
    }

    private fun appendRawLog(direction: String, payload: String) {
        val line = "${LOG_TIME_FORMATTER.format(LocalTime.now())} [$direction] $payload"
        _uiState.update { state ->
            state.copy(rawLogLines = (state.rawLogLines + line).takeLast(MAX_RAW_LOG_LINES))
        }
    }

    private fun shouldAppendIncomingRawLine(line: String): Boolean {
        if (_uiState.value.verboseStreamLogsEnabled) return true
        if (line.startsWith("EVT:STREAM:", ignoreCase = true)) return false
        return !CSV_STREAM_REGEX.matches(line.trim())
    }

    private fun text(@StringRes resId: Int, vararg args: Any): String {
        return getApplication<Application>().getString(resId, *args)
    }

    private fun requiredAppPermissions(): List<String> {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions += listOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
            )
        } else {
            permissions += Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
            permissions += Manifest.permission.WRITE_EXTERNAL_STORAGE
        }
        return permissions
    }

    private fun hasPermission(permission: String): Boolean {
        return ContextCompat.checkSelfPermission(getApplication(), permission) == PackageManager.PERMISSION_GRANTED
    }

    override fun onCleared() {
        streamWatchdogJob?.cancel()
        runCatching {
            recordingSession?.let { recorder.stopSession(it) }
        }
        recordingSession = null
        client.close()
        super.onCleared()
    }

    companion object {
        private val NAME_KEYWORDS = listOf("sonicwave", "vibrate")
        private const val NO_STREAM_WARNING_TIMEOUT_MS = 3_000L
        private const val MAX_RAW_LOG_LINES = 200
        private const val TELEMETRY_WINDOW_MS = 20_000L
        private val CSV_STREAM_REGEX = Regex("""^-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?$""")
        private val LOG_TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")
    }
}
