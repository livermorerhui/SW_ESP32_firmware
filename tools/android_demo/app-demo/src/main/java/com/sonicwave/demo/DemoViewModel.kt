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
    val waveRuntimeStartMs: Long? = null,
    val waveRuntimeElapsedMs: Long = 0L,
    val isWaveStartPending: Boolean = false,
    val testSession: TestSessionUi? = null,
    val testSessionNotice: String? = null,
    val isMotionSamplingActive: Boolean = false,
    val motionSamplingSession: MotionSamplingSessionUi? = null,
    val motionSamplingStatus: String? = null,
    val motionSamplingModeEnabled: Boolean = false,
    val fallStopEnabled: Boolean = true,
    val fallStopStateKnown: Boolean = false,
    val isFallStopSyncInProgress: Boolean = false,
    val isDeviceSheetVisible: Boolean = false,
    val scanResults: List<BleScanResult> = emptyList(),
    val statusLabel: String = "",
    val isConnecting: Boolean = false,
)

private data class PendingWaveStartRequest(
    val freq: Int,
    val intensity: Int,
    val requestedAtMs: Long,
)

class DemoViewModel(application: Application) : AndroidViewModel(application) {
    private val client = SonicWaveClient(application)
    private val recorder = TelemetryRecorder(application)
    private val testSessionManager = TestSessionManager()
    private val testSessionExporter = TestSessionExporter(application)
    private val motionSamplingExporter = MotionSamplingExporter(application)
    private val recordingMutex = Mutex()

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private var selectedDeviceName: String? = null
    private var streamWatchdogJob: Job? = null
    private var lastStreamAtMs: Long = 0L
    private var telemetrySessionStartMs: Long = 0L
    private var recordingSession: RecordingSession? = null
    private var pendingWaveStartRequest: PendingWaveStartRequest? = null
    private var sessionCaptureSignals: SessionCaptureSignals = SessionCaptureSignals()
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
        pendingWaveStartRequest = null
        sessionCaptureSignals = SessionCaptureSignals()
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
                        isWaveStartPending = false,
                        fallStopEnabled = true,
                        fallStopStateKnown = false,
                        isFallStopSyncInProgress = false,
                        testSession = null,
                        testSessionNotice = null,
                        captureStatus = null,
                        writeModelStatus = null,
                        stableWeight = null,
                        stableWeightActive = false,
                        deviceState = DeviceState.UNKNOWN,
                        faultStatus = FaultStatusUi(),
                        safetyStatus = defaultSafetyStatus(),
                    ),
                ),
            ).resetWaveRuntime()
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
                            motionSamplingModeEnabled = probe?.capabilities?.let(::isMotionSamplingModeEnabled) ?: false,
                            fallStopEnabled = probe?.capabilities?.let(::isFallStopEnabled) ?: true,
                            fallStopStateKnown = probe?.capabilities?.let(::isFallStopEnabled) != null,
                            isFallStopSyncInProgress = false,
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
        pendingWaveStartRequest = null
        finishTestSessionIfRecording(
            result = "ABNORMAL_STOP",
            stopReason = "BLE_DISCONNECTED",
            stopSource = "FORMAL_SAFETY_OTHER",
        )
        sessionCaptureSignals = SessionCaptureSignals()
        client.disconnect()
        selectedDeviceName = null
        streamWatchdogJob?.cancel()
        stopRecordingIfActive(text(R.string.recording_stopped_disconnect))
        stopMotionSamplingIfActive(text(R.string.motion_sampling_status_stopped_disconnect))
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
                        isWaveStartPending = false,
                        fallStopEnabled = true,
                        fallStopStateKnown = false,
                        isFallStopSyncInProgress = false,
                        testSessionNotice = text(R.string.test_session_notice_stopped_disconnect),
                        captureStatus = null,
                        writeModelStatus = null,
                        motionSamplingModeEnabled = false,
                        stableWeight = null,
                        stableWeightActive = false,
                        safetyStatus = defaultSafetyStatus(),
                        statusLabel = currentStatusLabel(
                            connectionState = ConnectionState.Disconnected,
                            isScanning = isScanActive(it.scanState),
                        ),
                    ),
                ),
            ).resetWaveRuntime()
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
        val sanitized = sanitizeWaveInput(value)
        _uiState.update { state ->
            state.copy(
                freqInput = sanitized,
                freq = sanitized.toIntOrNull() ?: state.freq,
            )
        }
    }

    fun updateIntensityInput(value: String) {
        val sanitized = sanitizeAndClampWaveInput(
            value = value,
            min = WAVE_INTENSITY_MIN,
            max = WAVE_INTENSITY_MAX,
        )
        _uiState.update { state ->
            state.copy(
                intensityInput = sanitized,
                intensity = sanitized.toIntOrNull() ?: state.intensity,
            )
        }
    }

    fun commitFreqInput() {
        // Keep frequency editing permissive so values like "1" can become "15";
        // final range clamping happens only when Start is pressed.
        _uiState.update { state -> state.copy(freqInput = sanitizeWaveInput(state.freqInput)) }
    }

    fun commitIntensityInput() {
        _uiState.update { state ->
            val normalized = normalizeWaveInput(
                input = state.intensityInput,
                fallback = state.intensity,
                min = WAVE_INTENSITY_MIN,
                max = WAVE_INTENSITY_MAX,
            )
            state.copy(intensityInput = normalized.toString(), intensity = normalized)
        }
    }

    fun setPresetFrequency(freq: Int) {
        _uiState.update { it.copy(freqInput = freq.toString(), freq = freq) }
        appendSystemLog("[WAVE_UI] preset frequency=$freq")
    }

    fun setPresetIntensity(intensity: Int) {
        _uiState.update { it.copy(intensityInput = intensity.toString(), intensity = intensity) }
        appendSystemLog("[WAVE_UI] preset intensity=$intensity")
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
        sendCommand(Command.ScaleCal(zeroDistance = zero, scaleFactor = factor), "SCALE:CAL")
    }

    fun sendWaveStart() {
        val state = _uiState.value
        // Frequency stays editable during typing; Start normalizes it into the
        // actual device send range and updates the field to the sent value.
        val freq = normalizeWaveInput(
            input = state.freqInput,
            fallback = state.freq,
            min = WAVE_FREQUENCY_MIN,
            max = WAVE_FREQUENCY_MAX,
        )
        val intensity = normalizeWaveInput(
            input = state.intensityInput,
            fallback = state.intensity,
            min = WAVE_INTENSITY_MIN,
            max = WAVE_INTENSITY_MAX,
        )

        _uiState.update {
            it.copy(
                freq = freq,
                intensity = intensity,
                freqInput = freq.toString(),
                intensityInput = intensity.toString(),
            )
        }

        val normalizedState = _uiState.value
        if (!normalizedState.canStartWave(hasPendingStartRequest = pendingWaveStartRequest != null)) {
            _uiState.update {
                it.copy(lastAckOrError = startBlockedMessage(normalizedState))
            }
            return
        }

        pendingWaveStartRequest = PendingWaveStartRequest(
            freq = freq,
            intensity = intensity,
            requestedAtMs = System.currentTimeMillis(),
        )
        _uiState.update { it.copy(isWaveStartPending = true) }

        viewModelScope.launch {
            runCatching {
                client.send(Command.WaveSet(freqHz = freq, intensity = intensity))
                client.send(Command.WaveStart)
            }.onSuccess {
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(R.string.message_sent_wave_start_bundle),
                    )
                }
            }.onFailure { error ->
                pendingWaveStartRequest = null
                _uiState.update {
                    it.copy(
                        isWaveStartPending = false,
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
        pendingWaveStartRequest = null
        _uiState.update { it.copy(isWaveStartPending = false) }
        finishTestSessionIfRecording(
            result = "NORMAL_STOP",
            stopReason = "USER_STOP",
            stopSource = "USER_MANUAL_OTHER",
        )
        _uiState.update {
            it.copy(testSessionNotice = text(R.string.test_session_notice_stopped_manual))
        }
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

    fun clearTestSession() {
        val session = _uiState.value.testSession ?: return
        if (session.status == TestSessionStatusUi.RECORDING) return
        val sessionId = session.sessionId
        _uiState.update {
            it.copy(
                testSession = null,
                testSessionNotice = text(R.string.test_session_notice_cleared),
            )
        }
        appendSystemLog("[TEST_SESSION] cleared id=$sessionId")
    }

    fun exportTestSession(request: TestSessionExportRequest) {
        val state = _uiState.value
        val session = state.testSession ?: return
        if (session.status != TestSessionStatusUi.FINISHED || session.samples.isEmpty()) {
            _uiState.update {
                it.copy(testSessionNotice = text(R.string.test_session_notice_export_unavailable))
            }
            return
        }

        viewModelScope.launch(Dispatchers.IO) {
            runCatching {
                testSessionExporter.exportSession(session, request)
            }.onSuccess { result ->
                _uiState.update {
                    val current = it.testSession
                    val updatedSession = if (current?.sessionId == session.sessionId) {
                        current.copy(
                            lastExportCsvPath = result.csvDestinationLabel,
                            lastExportJsonPath = result.jsonDestinationLabel,
                        )
                    } else {
                        current
                    }
                    it.copy(
                        testSession = updatedSession,
                        testSessionNotice = text(
                            R.string.test_session_notice_exported,
                            result.csvDestinationLabel,
                            result.jsonDestinationLabel,
                        ),
                    )
                }
                appendSystemLog(
                    "[TEST_SESSION] export primary=${request.primaryLabel.name} secondary=${request.secondaryLabel.name}",
                )
                appendSystemLog(
                    "[TEST_SESSION] exported csv=${result.csvDestinationLabel} json=${result.jsonDestinationLabel}",
                )
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        testSessionNotice = text(
                            R.string.test_session_notice_export_failed,
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
    }

    fun startMotionSampling() {
        val state = _uiState.value
        if (state.isMotionSamplingActive) return

        val now = System.currentTimeMillis()
        val model = state.latestModel
        val sampledFrequency = state.freq
        val sampledIntensity = state.intensity
        val sessionId = "motion_$now"
        val session = MotionSamplingSessionUi(
            sessionId = sessionId,
            startedAtMs = now,
            appVersion = appVersionName(),
            firmwareMetadata = state.capabilityInfo,
            connectedDeviceName = state.connectedDeviceName,
            protocolModeCode = state.protocolMode.name,
            waveFrequencyHz = sampledFrequency,
            waveIntensity = sampledIntensity,
            fallStopEnabled = state.fallStopEnabled,
            samplingModeEnabled = state.motionSamplingModeEnabled,
            waveWasRunningAtSessionStart =
                state.deviceState == DeviceState.RUNNING || state.safetyStatus.waveCode == "RUNNING",
            modelTypeCode = model?.type?.name,
            modelReferenceDistance = model?.referenceDistance,
            modelC0 = model?.c0,
            modelC1 = model?.c1,
            modelC2 = model?.c2,
            notes = "",
        )
        _uiState.update {
            it.copy(
                isMotionSamplingActive = true,
                motionSamplingSession = session,
                motionSamplingStatus = text(R.string.motion_sampling_status_started, sessionId),
            )
        }
        appendSystemLog("[MOTION_SAMPLE] session started id=$sessionId")
    }

    fun stopMotionSampling() {
        stopMotionSamplingIfActive(text(R.string.motion_sampling_status_stopped_manual))
    }

    fun clearMotionSamplingSession() {
        val state = _uiState.value
        if (state.isMotionSamplingActive) return
        val sessionId = state.motionSamplingSession?.sessionId ?: return
        _uiState.update {
            it.copy(
                motionSamplingSession = null,
                motionSamplingStatus = text(R.string.motion_sampling_status_cleared),
            )
        }
        appendSystemLog("[MOTION_SAMPLE] session cleared id=$sessionId")
    }

    fun setMotionSamplingModeEnabled(enabled: Boolean) {
        val state = _uiState.value
        if (!state.isConnected || state.protocolMode != ProtocolMode.PRIMARY) return

        viewModelScope.launch {
            runCatching {
                client.send(Command.MotionSamplingModeSet(enabled))
            }.onSuccess {
                _uiState.update {
                    it.copy(
                        motionSamplingModeEnabled = enabled,
                        motionSamplingStatus = if (enabled) {
                            text(R.string.motion_sampling_status_mode_enabled)
                        } else {
                            text(R.string.motion_sampling_status_mode_disabled)
                        },
                        lastAckOrError = text(
                            R.string.message_sent,
                            if (enabled) {
                                text(R.string.action_enable_motion_sampling_mode)
                            } else {
                                text(R.string.action_disable_motion_sampling_mode)
                            },
                        ),
                    )
                }
                appendSystemLog("[MOTION_SAMPLE_MODE] enabled=$enabled")
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            if (enabled) {
                                text(R.string.action_enable_motion_sampling_mode)
                            } else {
                                text(R.string.action_disable_motion_sampling_mode)
                            },
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
    }

    fun setFallStopProtectionEnabled(enabled: Boolean) {
        val state = _uiState.value
        if (!state.isConnected ||
            state.protocolMode != ProtocolMode.PRIMARY ||
            state.isFallStopSyncInProgress
        ) {
            return
        }

        val actionLabel = if (enabled) {
            text(R.string.action_enable_fall_stop_protection)
        } else {
            text(R.string.action_disable_fall_stop_protection)
        }

        _uiState.update { it.copy(isFallStopSyncInProgress = true) }

        viewModelScope.launch {
            runCatching {
                client.send(Command.FallStopProtectionSet(enabled))
                client.capabilityProbe()
            }.onSuccess { probe ->
                val actualEnabled = probe.capabilities?.let(::isFallStopEnabled)
                if (probe.mode != ProtocolMode.PRIMARY || actualEnabled == null) {
                    throw IllegalStateException("fall stop capability sync unavailable")
                }

                _uiState.update {
                    it.copy(
                        fallStopEnabled = actualEnabled,
                        fallStopStateKnown = true,
                        isFallStopSyncInProgress = false,
                        capabilityInfo = probe.capabilities?.let(::formatCapabilities) ?: it.capabilityInfo,
                        protocolMode = probe.mode,
                        lastAckOrError = text(R.string.message_sent, actionLabel),
                    )
                }
                appendSystemLog("[FALL_STOP_UI] requested=$enabled applied=$actualEnabled mode=${probe.mode.name}")
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        isFallStopSyncInProgress = false,
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            actionLabel,
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
    }

    fun exportMotionSamplingSession(request: MotionSamplingExportRequest) {
        val state = _uiState.value
        val session = state.motionSamplingSession ?: return
        if (state.isMotionSamplingActive || session.rows.isEmpty()) return

        viewModelScope.launch(Dispatchers.IO) {
            runCatching {
                motionSamplingExporter.exportSession(session, request)
            }.onSuccess { result ->
                _uiState.update {
                    val currentSession = it.motionSamplingSession
                    val updatedSession = if (currentSession?.sessionId == session.sessionId) {
                        currentSession.copy(
                            // Persist the export labels in the session summary so
                            // engineers can confirm what was written out, while
                            // keeping capture/runtime behavior unchanged.
                            exportScenarioLabel = request.scenarioLabel,
                            exportScenarioCategory = request.scenarioCategory,
                            lastExportTimestampMs = request.exportTimestampMs,
                            lastExportCsvPath = result.csvDestinationLabel,
                            lastExportJsonPath = result.jsonDestinationLabel,
                        )
                    } else {
                        currentSession
                    }
                    it.copy(
                        motionSamplingSession = updatedSession,
                        motionSamplingStatus = text(R.string.motion_sampling_status_exported, result.csvDestinationLabel),
                    )
                }
                appendSystemLog(
                    "[MOTION_SAMPLE] export primary=${request.primaryLabel.name} sub=${request.subLabel.name} filename=${result.csvFileName}",
                )
                appendSystemLog(
                    "[MOTION_SAMPLE] metadata waveFrequencyHz=${session.waveFrequencyHz ?: -1} waveIntensity=${session.waveIntensity ?: -1}",
                )
                appendSystemLog(
                    "[MOTION_SAMPLE] export csv=${result.csvDestinationLabel} json=${result.jsonDestinationLabel ?: "-"}",
                )
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        motionSamplingStatus = text(
                            R.string.motion_sampling_status_export_failed,
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
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
                    finishTestSessionIfRecording(
                        result = "ABNORMAL_STOP",
                        stopReason = "BLE_DISCONNECTED",
                        stopSource = "FORMAL_SAFETY_OTHER",
                    )
                    appendSystemLog(text(R.string.log_transport_disconnect_safety, detail))
                    _uiState.update { current ->
                        withCaptureAvailability(
                            current.copy(
                                safetyStatus = safetyStatus,
                                faultStatus = faultStatusFromCode(safetyStatus.code),
                                lastAckOrError = formatSafetyStatusMessage(safetyStatus),
                                stableWeight = null,
                                stableWeightActive = false,
                                testSessionNotice = text(R.string.test_session_notice_stopped_disconnect),
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
                    pendingWaveStartRequest = null
                    if (disconnectRequested) {
                        disconnectRequested = false
                    }
                    streamWatchdogJob?.cancel()
                    clearStableBaseline()
                    stopRecordingIfActive(text(R.string.recording_stopped_disconnect))
                    stopMotionSamplingIfActive(text(R.string.motion_sampling_status_stopped_disconnect))
                    _uiState.update { state ->
                        resetCalibrationSessionState(
                            withCaptureAvailability(
                                state.copy(
                                    isRecording = false,
                                    recordingDestination = null,
                                    recordingStatus = text(R.string.recording_stopped_disconnect),
                                    isWaveStartPending = false,
                                    captureStatus = null,
                                    writeModelStatus = null,
                                    fallStopEnabled = true,
                                    fallStopStateKnown = false,
                                    isFallStopSyncInProgress = false,
                                ),
                            ),
                        ).resetWaveRuntime()
                    }
                }
            }
        }

        viewModelScope.launch {
            client.rawLines.collect { line ->
                handleSessionLogLine(line)
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

                    is Event.BaselineMain -> {
                        sessionCaptureSignals = sessionCaptureSignals.copy(
                            baselineReady = event.baselineReady,
                            stableWeight = event.stableWeightKg,
                            ma7 = event.ma7WeightKg,
                            deviation = event.deviationKg,
                            ratio = event.ratio,
                            mainState = event.mainState,
                            abnormalDurationMs = event.abnormalDurationMs,
                            dangerDurationMs = event.dangerDurationMs,
                            stopReason = event.stopReason,
                            stopSource = event.stopSource,
                        )
                        _uiState.update {
                            it.copy(
                                stableWeight = event.stableWeightKg ?: it.stableWeight,
                                stableWeightActive = event.baselineReady,
                            )
                        }
                    }

                    is Event.Stop -> {
                        sessionCaptureSignals = sessionCaptureSignals.copy(
                            stopReason = event.stopReason,
                            stopSource = event.stopSource,
                        )
                        cancelPendingWaveStart(
                            "EVT_STOP:${event.stopReason}:${event.stopSource}:${event.effect.name}:${event.state.name}",
                        )
                        finishTestSessionIfRecording(
                            result = when {
                                event.stopSource == "USER_MANUAL_OTHER" -> "NORMAL_STOP"
                                event.effect == SafetyEffect.ABNORMAL_STOP -> "ABNORMAL_STOP"
                                else -> "AUTO_STOP"
                            },
                            stopReason = event.stopReason,
                            stopSource = event.stopSource,
                        )
                    }

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
                        val nextState = event.state
                        val nextSafetyStatus = if (
                            nextState == DeviceState.RUNNING &&
                            it.safetyStatus.effectCode == SafetyEffect.RECOVERABLE_PAUSE.name
                        ) {
                            defaultSafetyStatus()
                        } else {
                            it.safetyStatus
                        }
                        it.applyWaveRuntimeTransition(nextState).copy(
                            deviceState = nextState,
                            safetyStatus = nextSafetyStatus,
                        )
                    }.also {
                        if (event.state == DeviceState.RUNNING) {
                            confirmPendingWaveStart(source = "EVT_STATE_RUNNING")
                        } else {
                            if (event.state == DeviceState.IDLE || event.state == DeviceState.FAULT_STOP) {
                                cancelPendingWaveStart("EVT_STATE_${event.state.name}")
                            }
                            finishTestSessionIfRecording(
                                result = if (event.state == DeviceState.FAULT_STOP) {
                                    "ABNORMAL_STOP"
                                } else {
                                    "AUTO_STOP"
                                },
                                stopReason = event.state.name,
                            )
                        }
                    }

                    is Event.Fault -> {
                        val faultStatus = faultStatusFromCode(event.code)
                        cancelPendingWaveStart("EVT_FAULT:${faultStatus.codeName}")
                        _uiState.update {
                            val clearsStableBaseline = shouldClearStableBaseline(
                                reasonCode = faultStatus.codeName,
                                code = faultStatus.code,
                            )
                            it.copy(
                                faultStatus = faultStatus,
                                lastAckOrError = text(R.string.message_fault, faultStatus.label),
                                stableWeight = if (clearsStableBaseline) null else it.stableWeight,
                                stableWeightActive = if (clearsStableBaseline) false else it.stableWeightActive,
                            )
                        }
                        finishTestSessionIfRecording(
                            result = "ABNORMAL_STOP",
                            stopReason = faultStatus.codeName,
                        )
                    }

                    is Event.Safety -> {
                        val safetyStatus = safetyStatusFromEvent(event)
                        _uiState.update {
                            val nextState = if (event.state != DeviceState.UNKNOWN) {
                                event.state
                            } else {
                                it.deviceState
                            }
                            val clearsStableBaseline = shouldClearStableBaseline(
                                reasonCode = safetyStatus.reasonCode,
                                code = safetyStatus.code,
                            )
                            it.applyWaveRuntimeTransition(nextState).copy(
                                deviceState = nextState,
                                faultStatus = event.code?.let(::faultStatusFromCode) ?: it.faultStatus,
                                safetyStatus = safetyStatus,
                                lastAckOrError = formatSafetyStatusMessage(safetyStatus),
                                stableWeight = if (clearsStableBaseline) null else it.stableWeight,
                                stableWeightActive = if (clearsStableBaseline) false else it.stableWeightActive,
                            )
                        }
                        if (event.state == DeviceState.RUNNING || event.wave.name == "RUNNING") {
                            confirmPendingWaveStart(source = "EVT_SAFETY_RUNNING")
                        } else if (
                            event.effect == SafetyEffect.ABNORMAL_STOP ||
                            event.effect == SafetyEffect.RECOVERABLE_PAUSE ||
                            event.wave.name == "STOPPED" ||
                            event.state == DeviceState.FAULT_STOP ||
                            event.state == DeviceState.IDLE
                        ) {
                            cancelPendingWaveStart(
                                "EVT_SAFETY:${event.reason}:${event.effect.name}:${event.state.name}:${event.wave.name}",
                            )
                        }
                        if (event.effect == SafetyEffect.ABNORMAL_STOP ||
                            event.effect == SafetyEffect.RECOVERABLE_PAUSE ||
                            event.wave.name == "STOPPED" ||
                            event.state != DeviceState.RUNNING
                        ) {
                            finishTestSessionIfRecording(
                                result = when (event.effect) {
                                    SafetyEffect.ABNORMAL_STOP -> "ABNORMAL_STOP"
                                    SafetyEffect.RECOVERABLE_PAUSE -> "AUTO_STOP"
                                    else -> "AUTO_STOP"
                                },
                                stopReason = event.reason,
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
                        it.copy(
                            capabilityInfo = formatCapabilities(event),
                            motionSamplingModeEnabled = isMotionSamplingModeEnabled(event),
                            fallStopEnabled = isFallStopEnabled(event) ?: it.fallStopEnabled,
                            fallStopStateKnown = isFallStopEnabled(event) != null || it.fallStopStateKnown,
                            isFallStopSyncInProgress = false,
                        )
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
                        cancelPendingWaveStart("NACK:$reason")
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
                        cancelPendingWaveStart("ERROR:${event.reason}")
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

    private fun UiState.applyWaveRuntimeTransition(nextDeviceState: DeviceState): UiState {
        val wasRunning = deviceState == DeviceState.RUNNING
        val isRunning = nextDeviceState == DeviceState.RUNNING
        return when {
            // Keep the runtime display aligned with the same RUNNING state the control bar uses.
            !wasRunning && isRunning -> copy(
                waveRuntimeStartMs = System.currentTimeMillis(),
                waveRuntimeElapsedMs = 0L,
            )

            wasRunning && !isRunning -> {
                val finalElapsedMs = waveRuntimeStartMs
                    ?.let { startMs -> (System.currentTimeMillis() - startMs).coerceAtLeast(0L) }
                    ?: waveRuntimeElapsedMs
                copy(
                    waveRuntimeStartMs = null,
                    waveRuntimeElapsedMs = finalElapsedMs,
                )
            }

            else -> this
        }
    }

    private fun UiState.resetWaveRuntime(): UiState {
        return copy(
            waveRuntimeStartMs = null,
            waveRuntimeElapsedMs = 0L,
        )
    }

    private fun onStreamSample(distance: Float, weight: Float) {
        val now = System.currentTimeMillis()
        if (telemetrySessionStartMs == 0L) {
            telemetrySessionStartMs = now
        }
        lastStreamAtMs = now
        streamWatchdogJob?.cancel()
        val currentState = _uiState.value
        val currentSignals = sessionCaptureSignals
        val samplingRow = buildMotionSamplingRow(
            state = currentState,
            distance = distance,
            weight = weight,
            now = now,
        )
        val point = buildTelemetryPoint(
            history = currentState.telemetryPoints,
            elapsedMs = now - telemetrySessionStartMs,
            timestampMs = now,
            distance = distance,
            unstableWeight = weight,
            stableWeight = currentState.stableWeight.takeIf { currentState.stableWeightActive },
            stableFlag = currentState.stableWeightActive,
        )
        val testSessionSample = buildTestSessionSample(
            state = currentState,
            telemetryPoint = point,
            signals = currentSignals,
            sessionStartMs = currentState.testSession?.startedAtMs,
        )

        _uiState.update {
            val nextTelemetryPoints = trimTelemetryPoints(it.telemetryPoints + point)
            val updatedSamplingSession = if (samplingRow != null && it.motionSamplingSession != null) {
                it.motionSamplingSession.copy(rows = it.motionSamplingSession.rows + samplingRow)
            } else {
                it.motionSamplingSession
            }
            val updatedTestSession = if (testSessionSample != null && it.testSession != null) {
                testSessionManager.appendSample(it.testSession, testSessionSample)
            } else {
                it.testSession
            }
            it.copy(
                distance = distance,
                weight = weight,
                streamWarning = null,
                telemetryPoints = nextTelemetryPoints,
                testSession = updatedTestSession,
                motionSamplingSession = updatedSamplingSession,
            )
        }

        if (currentSignals.pendingEventAux != null) {
            sessionCaptureSignals = sessionCaptureSignals.copy(pendingEventAux = null)
        }

        samplingRow?.let { row ->
            if (row.sampleIndex % MOTION_SAMPLE_LOG_INTERVAL == 0) {
                appendSystemLog("[MOTION_SAMPLE] row captured count=${row.sampleIndex}")
            }
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

    private fun startNewTestSession(
        freq: Int,
        intensity: Int,
    ) {
        val now = System.currentTimeMillis()
        _uiState.update { state ->
            state.copy(
                testSession = testSessionManager.startSession(
                    nowMs = now,
                    freqHz = freq,
                    intensity = intensity,
                    fallStopEnabled = state.fallStopEnabled,
                    signals = sessionCaptureSignals,
                ),
            )
        }
        appendSystemLog("[TEST_SESSION] started id=${_uiState.value.testSession?.sessionId ?: "-"}")
    }

    private fun confirmPendingWaveStart(
        source: String,
        freq: Int? = null,
        intensity: Int? = null,
    ) {
        val pending = pendingWaveStartRequest ?: return
        val state = _uiState.value
        if (state.testSession?.status == TestSessionStatusUi.RECORDING) {
            pendingWaveStartRequest = null
            _uiState.update { it.copy(isWaveStartPending = false) }
            return
        }

        startNewTestSession(
            freq = freq ?: pending.freq,
            intensity = intensity ?: pending.intensity,
        )
        pendingWaveStartRequest = null
        _uiState.update {
            it.copy(
                isWaveStartPending = false,
                testSessionNotice = text(R.string.test_session_notice_started),
            )
        }
        appendSystemLog(
            "[TEST_SESSION] start confirmed source=$source latency_ms=${System.currentTimeMillis() - pending.requestedAtMs}",
        )
    }

    private fun cancelPendingWaveStart(source: String) {
        val pending = pendingWaveStartRequest ?: return
        pendingWaveStartRequest = null
        _uiState.update { it.copy(isWaveStartPending = false) }
        appendSystemLog(
            "[TEST_SESSION] start pending cleared source=$source latency_ms=${System.currentTimeMillis() - pending.requestedAtMs}",
        )
    }

    private fun finishTestSessionIfRecording(
        result: String,
        stopReason: String,
        stopSource: String = sessionCaptureSignals.stopSource,
        finalMainState: String? = sessionCaptureSignals.mainState,
        finalAbnormalDurationMs: Long? = sessionCaptureSignals.abnormalDurationMs,
        finalDangerDurationMs: Long? = sessionCaptureSignals.dangerDurationMs,
    ) {
        var finishedSessionId: String? = null
        _uiState.update { state ->
            val session = state.testSession ?: return@update state
            if (session.status != TestSessionStatusUi.RECORDING) return@update state
            val finished = testSessionManager.finishSession(
                session = session,
                finishedAtMs = System.currentTimeMillis(),
                result = result,
                stopReason = stopReason,
                stopSource = stopSource,
                finalMainState = finalMainState,
                finalAbnormalDurationMs = finalAbnormalDurationMs,
                finalDangerDurationMs = finalDangerDurationMs,
            )
            finishedSessionId = finished.sessionId
            state.copy(testSession = finished)
        }
        finishedSessionId?.let { sessionId ->
            appendSystemLog(
                "[TEST_SESSION] finished id=$sessionId result=$result stop_reason=$stopReason stop_source=$stopSource",
            )
        }
    }

    private fun handleSessionLogLine(line: String) {
        when (val event = SessionLogParser.parse(line)) {
            is SessionLogEvent.TestStart -> {
                confirmPendingWaveStart(
                    source = "TEST_START",
                    freq = event.freqHz?.toInt(),
                    intensity = event.intensity,
                )
                sessionCaptureSignals = sessionCaptureSignals.copy(
                    testId = event.testId ?: sessionCaptureSignals.testId,
                    freqHz = event.freqHz ?: sessionCaptureSignals.freqHz,
                    intensity = event.intensity ?: sessionCaptureSignals.intensity,
                    intensityNorm = event.intensityNorm ?: sessionCaptureSignals.intensityNorm,
                    stableWeight = event.stableWeight ?: sessionCaptureSignals.stableWeight,
                )
                _uiState.update { state ->
                    val session = state.testSession
                    if (session?.status != TestSessionStatusUi.RECORDING) {
                        state
                    } else {
                        state.copy(testSession = testSessionManager.applyTestStart(session, event))
                    }
                }
            }

            is SessionLogEvent.MainState -> {
                sessionCaptureSignals = sessionCaptureSignals.copy(
                    mainState = event.state,
                    stableWeight = event.stableWeight ?: sessionCaptureSignals.stableWeight,
                    ma7 = event.ma7 ?: sessionCaptureSignals.ma7,
                    deviation = event.deviation ?: sessionCaptureSignals.deviation,
                    ratio = event.ratio ?: sessionCaptureSignals.ratio,
                    abnormalDurationMs = event.abnormalDurationMs ?: sessionCaptureSignals.abnormalDurationMs,
                    dangerDurationMs = event.dangerDurationMs ?: sessionCaptureSignals.dangerDurationMs,
                )
            }

            is SessionLogEvent.EventAux -> {
                sessionCaptureSignals = sessionCaptureSignals.copy(pendingEventAux = event.eventAux)
            }

            is SessionLogEvent.RiskAdvisory -> {
                sessionCaptureSignals = sessionCaptureSignals.copy(riskAdvisory = event.advisory)
            }

            is SessionLogEvent.AutoStop -> {
                cancelPendingWaveStart("AUTO_STOP:${event.stopReason}")
                finishTestSessionIfRecording(
                    result = "AUTO_STOP",
                    stopReason = event.stopReason,
                    stopSource = event.stopSource,
                )
                _uiState.update {
                    it.copy(testSessionNotice = text(R.string.test_session_notice_auto_stopped, event.stopReason))
                }
            }

            is SessionLogEvent.StopSummary -> {
                cancelPendingWaveStart("STOP_SUMMARY:${event.stopReason}")
                _uiState.update { state ->
                    val session = state.testSession ?: return@update state
                    state.copy(
                        testSession = testSessionManager.applyStopSummary(
                            session = session,
                            event = event,
                            observedAtMs = System.currentTimeMillis(),
                        ),
                        testSessionNotice = text(
                            R.string.test_session_notice_summary_received,
                            event.result,
                            event.stopReason,
                        ),
                    )
                }
                sessionCaptureSignals = sessionCaptureSignals.copy(
                    testId = event.testId ?: sessionCaptureSignals.testId,
                    freqHz = event.freqHz ?: sessionCaptureSignals.freqHz,
                    intensity = event.intensity ?: sessionCaptureSignals.intensity,
                    intensityNorm = event.intensityNorm ?: sessionCaptureSignals.intensityNorm,
                    baselineReady = event.baselineReady ?: sessionCaptureSignals.baselineReady,
                    stableWeight = event.stableWeight ?: sessionCaptureSignals.stableWeight,
                    mainState = event.finalMainState ?: sessionCaptureSignals.mainState,
                    stopReason = event.stopReason,
                    stopSource = event.stopSource ?: sessionCaptureSignals.stopSource,
                    abnormalDurationMs = event.finalAbnormalDurationMs ?: sessionCaptureSignals.abnormalDurationMs,
                    dangerDurationMs = event.finalDangerDurationMs ?: sessionCaptureSignals.dangerDurationMs,
                )
            }

            null -> Unit
        }
    }

    private fun buildTestSessionSample(
        state: UiState,
        telemetryPoint: TelemetryPointUi,
        signals: SessionCaptureSignals,
        sessionStartMs: Long?,
    ): TestSessionSampleUi? {
        val session = state.testSession ?: return null
        if (session.status != TestSessionStatusUi.RECORDING) return null
        return TestSessionSampleUi(
            timestampMs = if (sessionStartMs == null) {
                0L
            } else {
                (telemetryPoint.timestampMs - sessionStartMs).coerceAtLeast(0L)
            },
            baselineReady = signals.baselineReady,
            stableWeight = signals.stableWeight,
            weight = telemetryPoint.weight,
            distance = telemetryPoint.distance,
            ma3 = telemetryPoint.ma3,
            ma5 = telemetryPoint.ma5,
            ma7 = signals.ma7,
            deviation = signals.deviation,
            ratio = signals.ratio,
            mainState = signals.mainState,
            abnormalDurationMs = signals.abnormalDurationMs,
            dangerDurationMs = signals.dangerDurationMs,
            stopReason = signals.stopReason,
            stopSource = signals.stopSource,
            eventAux = signals.pendingEventAux ?: "NONE",
            riskAdvisory = signals.riskAdvisory,
        )
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

    private fun buildTelemetryPoint(
        history: List<TelemetryPointUi>,
        elapsedMs: Long,
        timestampMs: Long,
        distance: Float,
        unstableWeight: Float,
        stableWeight: Float?,
        stableFlag: Boolean,
    ): TelemetryPointUi {
        // MA3/5/7 are always computed from the rhythm-weight (unstable weight) stream,
        // using the same bounded history that the live telemetry chart renders.
        val weightHistory = history.map(TelemetryPointUi::unstableWeight) + unstableWeight
        return TelemetryPointUi(
            elapsedMs = elapsedMs,
            timestampMs = timestampMs,
            distance = distance,
            unstableWeight = unstableWeight,
            stableWeight = stableWeight,
            ma3 = movingAverage(weightHistory, 3),
            ma5 = movingAverage(weightHistory, 5),
            ma7 = movingAverage(weightHistory, 7),
            stableFlag = stableFlag,
        )
    }

    private fun trimTelemetryPoints(points: List<TelemetryPointUi>): List<TelemetryPointUi> {
        val latestTimestampMs = points.lastOrNull()?.timestampMs ?: return emptyList()
        val minTimestampMs = latestTimestampMs - TELEMETRY_WINDOW_MS
        // The chart window is defined in milliseconds end-to-end so the visible
        // 20-second domain stays aligned with trimming and fills the full width.
        return points.filter { sample -> sample.timestampMs >= minTimestampMs }
    }

    private fun movingAverage(values: List<Float>, windowSize: Int): Float? {
        if (values.size < windowSize) return null
        return values.takeLast(windowSize).average().toFloat()
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

    private fun isMotionSamplingModeEnabled(capabilities: Event.Capabilities): Boolean {
        return parseCapabilityBoolean(capabilities.values["MOTION_SAMPLING_MODE"]) ?: false
    }

    private fun isFallStopEnabled(capabilities: Event.Capabilities): Boolean? {
        parseCapabilityBoolean(capabilities.values["FALL_STOP_ENABLED"])?.let { return it }
        val suppressed = parseCapabilityBoolean(capabilities.values["FALL_ACTION_SUPPRESSED"]) ?: return null
        return !suppressed
    }

    private fun parseCapabilityBoolean(raw: String?): Boolean? {
        return when (raw?.trim()?.uppercase()) {
            "1", "TRUE", "ON", "ENABLED" -> true
            "0", "FALSE", "OFF", "DISABLED" -> false
            else -> null
        }
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

    private fun startBlockedMessage(state: UiState): String {
        return when (state.waveStartAvailability(hasPendingStartRequest = pendingWaveStartRequest != null)) {
            WaveStartAvailabilityUi.DISCONNECTED -> text(R.string.wave_bar_hint_disconnected)
            WaveStartAvailabilityUi.START_PENDING -> text(R.string.wave_bar_hint_start_pending)
            WaveStartAvailabilityUi.RUNNING -> text(R.string.wave_bar_hint_running)
            WaveStartAvailabilityUi.INVALID_PARAMETERS -> text(R.string.wave_bar_hint_invalid_values)
            WaveStartAvailabilityUi.LEFT_PLATFORM_BLOCKED -> text(R.string.wave_bar_hint_left_platform)
            WaveStartAvailabilityUi.ABNORMAL_STOP_BLOCKED -> text(R.string.wave_bar_hint_abnormal_stop)
            WaveStartAvailabilityUi.SAFETY_BLOCKED -> text(
                R.string.wave_bar_hint_recoverable_pause,
                state.safetyStatus.reason,
            )

            WaveStartAvailabilityUi.NOT_READY -> text(R.string.wave_bar_hint_not_ready)
            WaveStartAvailabilityUi.READY -> text(R.string.wave_bar_hint_ready)
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

    private fun clearStableBaseline() {
        _uiState.update { it.copy(stableWeight = null, stableWeightActive = false) }
    }

    private fun sanitizeWaveInput(value: String): String {
        return value.filter(Char::isDigit).take(3)
    }

    // Clamp typed values immediately so out-of-range numbers never linger in the UI field.
    private fun sanitizeAndClampWaveInput(
        value: String,
        min: Int,
        max: Int,
    ): String {
        val sanitized = sanitizeWaveInput(value)
        val parsed = sanitized.toIntOrNull() ?: return sanitized
        return parsed.coerceIn(min, max).toString()
    }

    private fun normalizeWaveInput(
        input: String,
        fallback: Int,
        min: Int,
        max: Int,
    ): Int {
        return (input.toIntOrNull() ?: fallback).coerceIn(min, max)
    }

    private fun shouldClearStableBaseline(
        reasonCode: String,
        code: Int?,
    ): Boolean {
        return reasonCode.equals("USER_LEFT_PLATFORM", ignoreCase = true) || code == 100
    }

    private fun stopMotionSamplingIfActive(reason: String) {
        var stoppedSessionId: String? = null
        var stoppedRowCount = 0
        val now = System.currentTimeMillis()
        _uiState.update { state ->
            if (!state.isMotionSamplingActive) return@update state
            val session = state.motionSamplingSession ?: return@update state.copy(
                isMotionSamplingActive = false,
                motionSamplingStatus = reason,
            )
            stoppedSessionId = session.sessionId
            stoppedRowCount = session.rows.size
            state.copy(
                isMotionSamplingActive = false,
                motionSamplingSession = session.copy(endedAtMs = now),
                motionSamplingStatus = reason,
            )
        }
        stoppedSessionId?.let { sessionId ->
            appendSystemLog("[MOTION_SAMPLE] session stopped id=$sessionId rows=$stoppedRowCount")
        }
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

    private fun buildMotionSamplingRow(
        state: UiState,
        distance: Float,
        weight: Float,
        now: Long,
    ): MotionSamplingRowUi? {
        if (!state.isMotionSamplingActive) return null
        val session = state.motionSamplingSession ?: return null
        val previous = session.rows.lastOrNull()
        val elapsedMs = now - session.startedAtMs
        val dtSeconds = previous?.let { ((now - it.timestampMs).coerceAtLeast(1L)) / 1000f }
        val ddDt = if (dtSeconds != null) {
            (distance - previous.distanceMm) / dtSeconds
        } else {
            null
        }
        val dwDt = if (dtSeconds != null) {
            (weight - previous.liveWeightKg) / dtSeconds
        } else {
            null
        }

        return MotionSamplingRowUi(
            sampleIndex = session.rows.size + 1,
            timestampMs = now,
            elapsedMs = elapsedMs,
            distanceMm = distance,
            liveWeightKg = weight,
            stableWeightKg = if (state.stableWeightActive) state.stableWeight else null,
            measurementValid = distance.isFinite() && weight.isFinite(),
            stableVisible = state.stableWeightActive,
            runtimeStateCode = state.deviceState.name,
            waveStateCode = state.safetyStatus.waveCode.ifBlank {
                if (state.deviceState == DeviceState.RUNNING) "RUNNING" else "UNKNOWN"
            },
            safetyStateCode = state.safetyStatus.effectCode.ifBlank { "NONE" },
            safetyReasonCode = state.safetyStatus.reasonCode.ifBlank { "NONE" },
            safetyCode = state.safetyStatus.code,
            connectionStateCode = connectionStateCode(state.connectionState),
            modelTypeCode = state.latestModel?.type?.name,
            ddDt = ddDt,
            dwDt = dwDt,
        )
    }

    private fun connectionStateCode(connectionState: ConnectionState): String {
        return when (connectionState) {
            is ConnectionState.Connected -> "CONNECTED"
            is ConnectionState.Connecting,
            is ConnectionState.DiscoveringServices,
            is ConnectionState.Subscribing -> "CONNECTING"

            is ConnectionState.Error -> "ERROR"
            ConnectionState.Disconnected -> "DISCONNECTED"
        }
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

    private fun appVersionName(): String {
        val application = getApplication<Application>()
        val packageInfo = application.packageManager.getPackageInfo(application.packageName, 0)
        return packageInfo.versionName ?: text(R.string.common_not_available)
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
        private const val MOTION_SAMPLE_LOG_INTERVAL = 50
        private const val WAVE_FREQUENCY_MIN = 5
        private const val WAVE_FREQUENCY_MAX = 50
        private const val WAVE_INTENSITY_MIN = 0
        private const val WAVE_INTENSITY_MAX = 120
        private val CSV_STREAM_REGEX = Regex("""^-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?$""")
        private val LOG_TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")
    }
}
