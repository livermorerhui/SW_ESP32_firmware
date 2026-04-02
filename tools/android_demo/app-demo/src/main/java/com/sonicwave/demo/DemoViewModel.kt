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
import com.sonicwave.protocol.MeasurementCarrier
import com.sonicwave.protocol.PlatformModel
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
import java.util.ArrayDeque
import kotlin.math.roundToInt

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
    val devicePlatformModel: PlatformModel? = null,
    val deviceLaserInstalled: Boolean? = null,
    val deviceLaserAvailable: Boolean? = null,
    val deviceProtectionDegraded: Boolean? = null,
    val deviceRuntimeReady: Boolean? = null,
    val deviceStartReady: Boolean? = null,
    val deviceBaselineReady: Boolean? = null,
    val deviceReasonCode: String = "NONE",
    val deviceSafetyEffectCode: String = "NONE",
    val selectedPlatformModel: PlatformModel = PlatformModel.PLUS,
    val selectedLaserInstalled: Boolean = true,
    val deviceConfigStatus: String? = null,
    val isDeviceConfigWritePending: Boolean = false,
    val deviceState: DeviceState = DeviceState.UNKNOWN,
    val faultStatus: FaultStatusUi = FaultStatusUi(),
    val safetyStatus: SafetyStatusUi = SafetyStatusUi(),
    val distance: Float? = null,
    val weight: Float? = null,
    val ma12: Float? = null,
    val measurementValid: Boolean = false,
    val lastMeasurementSequence: Long? = null,
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
    val waveOutputActive: Boolean = false,
    val isWaveStartPending: Boolean = false,
    val isWaveStopPending: Boolean = false,
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

private data class PendingWaveStopRequest(
    val requestedAtMs: Long,
)

private data class PendingWaveStopCompletion(
    val result: String,
    val stopReason: String,
    val stopSource: String,
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
    private val _measurementDisplayState = MutableStateFlow(MeasurementDisplayUiState())
    val measurementDisplayState: StateFlow<MeasurementDisplayUiState> = _measurementDisplayState.asStateFlow()
    private val _rawConsoleState = MutableStateFlow(RawConsoleUiState())
    val rawConsoleState: StateFlow<RawConsoleUiState> = _rawConsoleState.asStateFlow()
    private val _testSessionPanelState = MutableStateFlow(TestSessionPanelUiState())
    val testSessionPanelState: StateFlow<TestSessionPanelUiState> = _testSessionPanelState.asStateFlow()

    private var selectedDeviceName: String? = null
    private var streamWatchdogJob: Job? = null
    private var waveTruthRefreshJob: Job? = null
    private var lastStreamAtMs: Long = 0L
    private var telemetrySessionStartMs: Long = 0L
    private var latestDistance: Float? = null
    private var latestWeight: Float? = null
    private var latestMa12: Float? = null
    private var latestMeasurementValid = false
    private var latestMeasurementSequence: Long? = null
    private var lastMeasurementDisplayPublishAtMs: Long = 0L
    private var lastRawConsolePublishAtMs: Long = 0L
    private var lastTestSessionPanelPublishAtMs: Long = 0L
    private var recordingSession: RecordingSession? = null
    private var pendingWaveStartRequest: PendingWaveStartRequest? = null
    private var pendingWaveStopRequest: PendingWaveStopRequest? = null
    private var pendingWaveStopCompletion: PendingWaveStopCompletion? = null
    private var sessionCaptureSignals: SessionCaptureSignals = SessionCaptureSignals()
    private var disconnectRequested: Boolean = false
    private var hadConnectedSession: Boolean = false
    private var awaitingCalibrationCaptureResult: Boolean = false
    private var awaitingModelWriteResult: Boolean = false
    private var awaitingDeviceConfigWriteResult: Boolean = false
    private var pendingWriteModelType: CalibrationModelType? = null
    private var preferredLaserPlatformModel: PlatformModel = PlatformModel.PLUS
    private var testSessionStore: TestSessionUi? = null
    private var motionSamplingSessionStore: MotionSamplingSessionUi? = null
    private val rawLogBuffer = ArrayDeque<String>()
    private val telemetryDisplayBuffer = ArrayDeque<TelemetryPointUi>()
    private val recentWeightBuffer = ArrayDeque<Float>()

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
        awaitingDeviceConfigWriteResult = false
        pendingWriteModelType = null
        pendingWaveStartRequest = null
        pendingWaveStopRequest = null
        pendingWaveStopCompletion = null
        preferredLaserPlatformModel = PlatformModel.PLUS
        sessionCaptureSignals = SessionCaptureSignals()
        client.stopScan()
        streamWatchdogJob?.cancel()
        waveTruthRefreshJob?.cancel()
        telemetrySessionStartMs = 0L
        lastStreamAtMs = 0L
        resetMeasurementDisplayState()
        resetRawConsoleState()
        resetSessionStores(clearTestSession = false)

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
                        devicePlatformModel = null,
                        deviceLaserInstalled = null,
                        deviceLaserAvailable = null,
                        deviceProtectionDegraded = null,
                        deviceRuntimeReady = null,
                        deviceStartReady = null,
                        deviceBaselineReady = null,
                        deviceReasonCode = "NONE",
                        deviceSafetyEffectCode = "NONE",
                        selectedPlatformModel = PlatformModel.PLUS,
                        selectedLaserInstalled = true,
                        deviceConfigStatus = null,
                        isDeviceConfigWritePending = false,
                        notifyEnabled = false,
                        notifyError = null,
                        streamWarning = null,
                        measurementValid = false,
                        lastMeasurementSequence = null,
                        ma12 = null,
                        telemetryPoints = emptyList(),
                        isRecording = false,
                        recordingDestination = null,
                        recordingStatus = null,
                        waveOutputActive = false,
                        isWaveStartPending = false,
                        isWaveStopPending = false,
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
        publishTestSessionPanel(force = true)

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
                            devicePlatformModel = probe?.capabilities?.let(::platformModelFromCapabilities),
                            deviceLaserInstalled = probe?.capabilities?.let(::laserInstalledFromCapabilities),
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
                    requestSnapshotRefresh()
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
        awaitingDeviceConfigWriteResult = false
        pendingWriteModelType = null
        pendingWaveStartRequest = null
        pendingWaveStopRequest = null
        pendingWaveStopCompletion = null
        finishTestSessionIfRecording(
            result = "ABNORMAL_STOP",
            stopReason = "BLE_DISCONNECTED",
            stopSource = "FORMAL_SAFETY_OTHER",
        )
        sessionCaptureSignals = SessionCaptureSignals()
        client.disconnect()
        selectedDeviceName = null
        streamWatchdogJob?.cancel()
        waveTruthRefreshJob?.cancel()
        stopRecordingIfActive(text(R.string.recording_stopped_disconnect))
        stopMotionSamplingIfActive(text(R.string.motion_sampling_status_stopped_disconnect))
        telemetrySessionStartMs = 0L
        resetMeasurementDisplayState()
        resetSessionStores(clearTestSession = false)
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
                        devicePlatformModel = null,
                        deviceLaserInstalled = null,
                        deviceLaserAvailable = null,
                        deviceProtectionDegraded = null,
                        deviceRuntimeReady = null,
                        deviceStartReady = null,
                        deviceBaselineReady = null,
                        deviceReasonCode = "NONE",
                        deviceSafetyEffectCode = "NONE",
                        selectedPlatformModel = PlatformModel.PLUS,
                        selectedLaserInstalled = true,
                        deviceConfigStatus = null,
                        isDeviceConfigWritePending = false,
                        streamWarning = null,
                        isRecording = false,
                        recordingDestination = null,
                        recordingStatus = text(R.string.recording_stopped_disconnect),
                        waveOutputActive = false,
                        isWaveStartPending = false,
                        isWaveStopPending = false,
                        fallStopEnabled = true,
                        fallStopStateKnown = false,
                        isFallStopSyncInProgress = false,
                        testSessionNotice = text(R.string.test_session_notice_stopped_disconnect),
                        captureStatus = null,
                        writeModelStatus = null,
                        motionSamplingModeEnabled = false,
                        stableWeight = null,
                        stableWeightActive = false,
                        measurementValid = false,
                        lastMeasurementSequence = null,
                        ma12 = null,
                        safetyStatus = defaultSafetyStatus(),
                        statusLabel = currentStatusLabel(
                            connectionState = ConnectionState.Disconnected,
                            isScanning = isScanActive(it.scanState),
                        ),
                    ),
                ),
            ).resetWaveRuntime()
        }
        publishTestSessionPanel(force = true)
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

    fun updateSelectedPlatformModel(model: PlatformModel) {
        if (model != PlatformModel.BASE) {
            preferredLaserPlatformModel = model
        }
        _uiState.update {
            it.copy(
                selectedPlatformModel = model,
                selectedLaserInstalled = model != PlatformModel.BASE,
            )
        }
    }

    fun updateSelectedLaserInstalled(installed: Boolean) {
        _uiState.update { state ->
            val nextModel = when {
                !installed -> PlatformModel.BASE
                state.selectedPlatformModel != PlatformModel.BASE -> state.selectedPlatformModel
                else -> preferredLaserPlatformModel
            }
            if (nextModel != PlatformModel.BASE) {
                preferredLaserPlatformModel = nextModel
            }
            state.copy(
                selectedPlatformModel = nextModel,
                selectedLaserInstalled = installed,
            )
        }
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
        publishRawConsole(force = true)
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
        if (!normalizedState.canStartWave()) {
            _uiState.update {
                it.copy(lastAckOrError = startBlockedMessage(normalizedState))
            }
            return
        }

        pendingWaveStopRequest = null
        pendingWaveStopCompletion = null
        pendingWaveStartRequest = PendingWaveStartRequest(
            freq = freq,
            intensity = intensity,
            requestedAtMs = System.currentTimeMillis(),
        )
        _uiState.update { it.syncWaveControlFlags() }

        viewModelScope.launch {
            runCatching {
                client.send(Command.WaveSet(freqHz = freq, intensity = intensity))
                client.send(Command.WaveStart)
            }.onSuccess {
                schedulePendingWaveTruthRefresh("WAVE_START_SENT")
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(R.string.message_sent_wave_start_bundle),
                    )
                }
            }.onFailure { error ->
                pendingWaveStartRequest = null
                cancelPendingWaveStop("START_SEND_FAILURE")
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            "WAVE:SET -> WAVE:START",
                            error.message ?: text(R.string.common_not_available),
                        ),
                    ).syncWaveControlFlags()
                }
            }
        }
    }

    fun sendWaveStop() {
        pendingWaveStartRequest = null
        pendingWaveStopRequest = PendingWaveStopRequest(
            requestedAtMs = System.currentTimeMillis(),
        )
        stagePendingWaveStopCompletion(
            result = "NORMAL_STOP",
            stopReason = "USER_STOP",
            stopSource = "USER_MANUAL_OTHER",
        )
        _uiState.update {
            it.copy(
                testSessionNotice = text(R.string.test_session_notice_stopped_manual),
            ).syncWaveControlFlags()
        }
        publishTestSessionPanel(force = true)
        schedulePendingWaveTruthRefresh("WAVE_STOP_REQUESTED")
        viewModelScope.launch {
            runCatching {
                client.send(Command.WaveStop)
            }.onSuccess {
                schedulePendingWaveTruthRefresh("WAVE_STOP_SENT")
                _uiState.update {
                    it.copy(lastAckOrError = text(R.string.message_sent, "WAVE:STOP"))
                }
            }.onFailure { error ->
                cancelPendingWaveStop("STOP_SEND_FAILURE")
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            "WAVE:STOP",
                            error.message ?: text(R.string.common_not_available),
                        ),
                    )
                }
            }
        }
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

    fun sendDeviceConfig() {
        val state = _uiState.value
        if (!state.isConnected || state.protocolMode != ProtocolMode.PRIMARY) {
            _uiState.update {
                it.copy(lastAckOrError = text(R.string.device_config_status_requires_primary))
            }
            return
        }

        val platformModel = state.selectedPlatformModel
        val laserInstalled = state.selectedLaserInstalled
        if ((platformModel == PlatformModel.BASE) != !laserInstalled) {
            _uiState.update {
                it.copy(
                    lastAckOrError = text(R.string.device_config_status_conflict),
                    deviceConfigStatus = text(R.string.device_config_status_conflict),
                    isDeviceConfigWritePending = false,
                )
            }
            return
        }

        awaitingDeviceConfigWriteResult = true
        _uiState.update {
            it.copy(
                isDeviceConfigWritePending = true,
                deviceConfigStatus = text(
                    R.string.device_config_status_pending,
                    platformModel.name,
                    if (laserInstalled) {
                        text(R.string.device_config_laser_installed)
                    } else {
                        text(R.string.device_config_laser_not_installed)
                    },
                ),
            )
        }

        viewModelScope.launch {
            runCatching {
                client.send(
                    Command.DeviceSetConfig(
                        platformModel = platformModel,
                        laserInstalled = laserInstalled,
                    ),
                )
            }.onSuccess {
                _uiState.update {
                    it.copy(
                        lastAckOrError = text(R.string.message_sent, "DEVICE:SET_CONFIG"),
                    )
                }
            }.onFailure { error ->
                awaitingDeviceConfigWriteResult = false
                _uiState.update {
                    it.copy(
                        isDeviceConfigWritePending = false,
                        deviceConfigStatus = text(
                            R.string.device_config_status_send_failed,
                            error.message ?: text(R.string.common_not_available),
                        ),
                        lastAckOrError = text(
                            R.string.message_send_failed,
                            "DEVICE:SET_CONFIG",
                            error.message ?: text(R.string.common_not_available),
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
        val session = testSessionStore ?: return
        if (session.status == TestSessionStatusUi.RECORDING) return
        val sessionId = session.sessionId
        testSessionStore = null
        _uiState.update {
            it.copy(
                testSessionNotice = text(R.string.test_session_notice_cleared),
            )
        }
        publishTestSessionPanel(force = true)
        appendSystemLog("[TEST_SESSION] cleared id=$sessionId")
    }

    fun exportTestSession(request: TestSessionExportRequest) {
        val session = testSessionStore ?: return
        if (session.status != TestSessionStatusUi.FINISHED || session.samples.isEmpty()) {
            _uiState.update {
                it.copy(testSessionNotice = text(R.string.test_session_notice_export_unavailable))
            }
            publishTestSessionPanel(force = true)
            return
        }

        viewModelScope.launch(Dispatchers.IO) {
            runCatching {
                testSessionExporter.exportSession(session, request)
            }.onSuccess { result ->
                testSessionStore = testSessionStore
                    ?.takeIf { it.sessionId == session.sessionId }
                    ?.copy(
                        lastExportCsvPath = result.csvDestinationLabel,
                        lastExportJsonPath = result.jsonDestinationLabel,
                    )
                _uiState.update {
                    it.copy(
                        testSessionNotice = text(
                            R.string.test_session_notice_exported,
                            result.csvDestinationLabel,
                            result.jsonDestinationLabel,
                        ),
                    )
                }
                publishTestSessionPanel(force = true)
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
                publishTestSessionPanel(force = true)
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
            waveWasRunningAtSessionStart = state.waveOutputActive,
            modelTypeCode = model?.type?.name,
            modelReferenceDistance = model?.referenceDistance,
            modelC0 = model?.c0,
            modelC1 = model?.c1,
            modelC2 = model?.c2,
            notes = "",
            rows = mutableListOf(),
        )
        motionSamplingSessionStore = session
        _uiState.update {
            it.copy(
                isMotionSamplingActive = true,
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
        val sessionId = motionSamplingSessionStore?.sessionId ?: return
        motionSamplingSessionStore = null
        _uiState.update {
            it.copy(
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
        val session = motionSamplingSessionStore ?: return
        if (state.isMotionSamplingActive || session.rows.isEmpty()) return

        viewModelScope.launch(Dispatchers.IO) {
            runCatching {
                motionSamplingExporter.exportSession(session, request)
            }.onSuccess { result ->
                motionSamplingSessionStore = motionSamplingSessionStore
                    ?.takeIf { it.sessionId == session.sessionId }
                    ?.copy(
                        exportScenarioLabel = request.scenarioLabel,
                        exportScenarioCategory = request.scenarioCategory,
                        lastExportTimestampMs = request.exportTimestampMs,
                        lastExportCsvPath = result.csvDestinationLabel,
                        lastExportJsonPath = result.jsonDestinationLabel,
                    )
                _uiState.update {
                    it.copy(
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
        resetRawConsoleState()
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
                    publishTestSessionPanel(force = true)
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
                    awaitingDeviceConfigWriteResult = false
                    pendingWriteModelType = null
                    pendingWaveStartRequest = null
                    pendingWaveStopRequest = null
                    pendingWaveStopCompletion = null
                    waveTruthRefreshJob?.cancel()
                    if (disconnectRequested) {
                        disconnectRequested = false
                    }
                    streamWatchdogJob?.cancel()
                    clearStableBaseline()
                    stopRecordingIfActive(text(R.string.recording_stopped_disconnect))
                    stopMotionSamplingIfActive(text(R.string.motion_sampling_status_stopped_disconnect))
                    resetMeasurementDisplayState()
                    resetSessionStores(clearTestSession = false)
                    _uiState.update { state ->
                        resetCalibrationSessionState(
                            withCaptureAvailability(
                                state.copy(
                                    devicePlatformModel = null,
                                    deviceLaserInstalled = null,
                                    deviceLaserAvailable = null,
                                    deviceProtectionDegraded = null,
                                    deviceRuntimeReady = null,
                                    deviceStartReady = null,
                                    deviceBaselineReady = null,
                                    deviceReasonCode = "NONE",
                                    deviceSafetyEffectCode = "NONE",
                                    isRecording = false,
                                    recordingDestination = null,
                                    recordingStatus = text(R.string.recording_stopped_disconnect),
                                    waveOutputActive = false,
                                    isWaveStartPending = false,
                                    isWaveStopPending = false,
                                    captureStatus = null,
                                    writeModelStatus = null,
                                    deviceConfigStatus = null,
                                    isDeviceConfigWritePending = false,
                                    fallStopEnabled = true,
                                    fallStopStateKnown = false,
                                    isFallStopSyncInProgress = false,
                                ),
                            ),
                        ).resetWaveRuntime()
                    }
                    publishTestSessionPanel(force = true)
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
                    is Event.StreamSample -> onStreamSample(event)

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
                                deviceStartReady = event.startReady ?: it.deviceStartReady,
                                deviceBaselineReady = event.baselineReady,
                                stableWeight = event.stableWeightKg ?: it.stableWeight,
                                stableWeightActive = event.baselineReady,
                            ).syncWaveControlFlags()
                        }
                    }

                    is Event.Stop -> {
                        sessionCaptureSignals = sessionCaptureSignals.copy(
                            stopReason = event.stopReason,
                            stopSource = event.stopSource,
                        )
                        if (pendingWaveStopRequest == null && _uiState.value.waveOutputActive) {
                            pendingWaveStopRequest = PendingWaveStopRequest(
                                requestedAtMs = System.currentTimeMillis(),
                            )
                        }
                        cancelPendingWaveStart(
                            "EVT_STOP:${event.stopReason}:${event.stopSource}:${event.effect.name}:${event.state.name}",
                        )
                        stagePendingWaveStopCompletion(
                            result = when {
                                event.stopSource == "USER_MANUAL_OTHER" -> "NORMAL_STOP"
                                event.effect == SafetyEffect.ABNORMAL_STOP -> "ABNORMAL_STOP"
                                else -> "AUTO_STOP"
                            },
                            stopReason = event.stopReason,
                            stopSource = event.stopSource,
                        )
                        schedulePendingWaveTruthRefresh("EVT_STOP")
                        _uiState.update { it.syncWaveControlFlags() }
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
                        it.copy(
                            deviceState = nextState,
                            safetyStatus = nextSafetyStatus,
                        ).syncFormalWaveTruth().syncWaveControlFlags()
                    }.also {
                        if (event.state == DeviceState.IDLE || event.state == DeviceState.FAULT_STOP) {
                            cancelPendingWaveStart("EVT_STATE_${event.state.name}")
                            if (pendingWaveStopCompletion == null &&
                                testSessionStore?.status == TestSessionStatusUi.RECORDING
                            ) {
                                stagePendingWaveStopCompletion(
                                    result = if (event.state == DeviceState.FAULT_STOP) {
                                        "ABNORMAL_STOP"
                                    } else {
                                        "AUTO_STOP"
                                    },
                                    stopReason = event.state.name,
                                    stopSource = sessionCaptureSignals.stopSource,
                                )
                            }
                            if (!_uiState.value.waveOutputActive) {
                                confirmPendingWaveStop("EVT_STATE_${event.state.name}")
                            }
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
                                deviceReasonCode = faultStatus.codeName,
                                lastAckOrError = text(R.string.message_fault, faultStatus.label),
                                stableWeight = if (clearsStableBaseline) null else it.stableWeight,
                                stableWeightActive = if (clearsStableBaseline) false else it.stableWeightActive,
                            ).syncFormalWaveTruth().syncWaveControlFlags()
                        }
                        if (pendingWaveStopRequest == null &&
                            (_uiState.value.waveOutputActive ||
                                testSessionStore?.status == TestSessionStatusUi.RECORDING)
                        ) {
                            pendingWaveStopRequest = PendingWaveStopRequest(
                                requestedAtMs = System.currentTimeMillis(),
                            )
                        }
                        stagePendingWaveStopCompletion(
                            result = if (faultStatus.codeName == "USER_LEFT_PLATFORM") {
                                "AUTO_STOP"
                            } else {
                                "ABNORMAL_STOP"
                            },
                            stopReason = faultStatus.codeName,
                            stopSource = sessionCaptureSignals.stopSource,
                        )
                        schedulePendingWaveTruthRefresh("EVT_FAULT")
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
                            it.copy(
                                deviceState = nextState,
                                faultStatus = event.code?.let(::faultStatusFromCode) ?: it.faultStatus,
                                deviceReasonCode = safetyStatus.reasonCode,
                                deviceSafetyEffectCode = safetyStatus.effectCode,
                                safetyStatus = safetyStatus,
                                lastAckOrError = formatSafetyStatusMessage(safetyStatus),
                                stableWeight = if (clearsStableBaseline) null else it.stableWeight,
                                stableWeightActive = if (clearsStableBaseline) false else it.stableWeightActive,
                            ).syncFormalWaveTruth().syncWaveControlFlags()
                        }
                        if (
                            event.effect == SafetyEffect.ABNORMAL_STOP ||
                            event.effect == SafetyEffect.RECOVERABLE_PAUSE ||
                            event.wave.name == "STOPPED" ||
                            event.state == DeviceState.FAULT_STOP ||
                            event.state == DeviceState.IDLE
                        ) {
                            cancelPendingWaveStart(
                                "EVT_SAFETY:${event.reason}:${event.effect.name}:${event.state.name}:${event.wave.name}",
                            )
                            if (pendingWaveStopRequest == null &&
                                (_uiState.value.waveOutputActive ||
                                    testSessionStore?.status == TestSessionStatusUi.RECORDING)
                            ) {
                                pendingWaveStopRequest = PendingWaveStopRequest(
                                    requestedAtMs = System.currentTimeMillis(),
                                )
                            }
                            stagePendingWaveStopCompletion(
                                result = when (event.effect) {
                                    SafetyEffect.ABNORMAL_STOP -> "ABNORMAL_STOP"
                                    SafetyEffect.RECOVERABLE_PAUSE -> "AUTO_STOP"
                                    else -> "AUTO_STOP"
                                },
                                stopReason = event.reason,
                                stopSource = sessionCaptureSignals.stopSource,
                            )
                            schedulePendingWaveTruthRefresh("EVT_SAFETY")
                        }
                    }

                    is Event.Snapshot -> _uiState.update {
                        val nextDeviceState = if (event.topState != DeviceState.UNKNOWN) {
                            event.topState
                        } else {
                            it.deviceState
                        }
                        val startReadyMergeContext = currentSnapshotStartReadyMergeContext()
                        val nextWaveOutputActive = event.waveOutputActive ?: it.waveOutputActive
                        val nextReasonCode = event.currentReasonCode ?: it.deviceReasonCode
                        val nextSafetyEffectCode = event.currentSafetyEffect ?: it.deviceSafetyEffectCode
                        it.applyWaveOutputTransition(nextWaveOutputActive).copy(
                            deviceState = nextDeviceState,
                            devicePlatformModel = event.platformModel ?: it.devicePlatformModel,
                            deviceLaserInstalled = event.laserInstalled ?: it.deviceLaserInstalled,
                            deviceLaserAvailable = event.laserAvailable,
                            deviceProtectionDegraded = event.protectionDegraded,
                            deviceRuntimeReady = event.runtimeReady,
                            deviceStartReady = resolveSnapshotStartReady(
                                currentStartReady = it.deviceStartReady,
                                snapshotStartReady = event.startReady,
                                mergeContext = startReadyMergeContext,
                            ),
                            deviceBaselineReady = event.baselineReady,
                            deviceReasonCode = nextReasonCode,
                            deviceSafetyEffectCode = nextSafetyEffectCode,
                            faultStatus = faultStatusFromReason(nextReasonCode),
                            safetyStatus = safetyStatusFromSnapshot(
                                reasonCode = nextReasonCode,
                                effectCode = nextSafetyEffectCode,
                                runtimeState = nextDeviceState,
                                waveOutputActive = nextWaveOutputActive,
                            ),
                            stableWeight = event.stableWeightKg ?: it.stableWeight,
                            stableWeightActive = event.baselineReady ?: it.stableWeightActive,
                        ).syncFormalWaveTruth().syncWaveControlFlags()
                    }.also {
                        reconcileTestSessionWithFormalWaveTruth(
                            source = if (_uiState.value.waveOutputActive) {
                                "SNAPSHOT_WAVE_OUTPUT_ACTIVE"
                            } else {
                                "SNAPSHOT_WAVE_OUTPUT_INACTIVE"
                            },
                            waveOutputActive = _uiState.value.waveOutputActive,
                            freqHz = event.currentFrequencyHz,
                            intensity = event.currentIntensity,
                        )
                    }

                    is Event.WaveOutput -> _uiState.update {
                        it.applyWaveOutputTransition(event.active)
                            .syncFormalWaveTruth()
                            .syncWaveControlFlags()
                    }.also {
                        reconcileTestSessionWithFormalWaveTruth(
                            source = if (event.active) {
                                "EVT_WAVE_OUTPUT_ACTIVE"
                            } else {
                                "EVT_WAVE_OUTPUT_INACTIVE"
                            },
                            waveOutputActive = event.active,
                        )
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

                    is Event.Capabilities -> _uiState.update { state ->
                        val platformModel = platformModelFromCapabilities(event)
                        val laserInstalled = laserInstalledFromCapabilities(event)
                        if (platformModel != null && platformModel != PlatformModel.BASE) {
                            preferredLaserPlatformModel = platformModel
                        }
                        state.copy(
                            capabilityInfo = formatCapabilities(event),
                            devicePlatformModel = platformModel ?: state.devicePlatformModel,
                            deviceLaserInstalled = laserInstalled ?: state.deviceLaserInstalled,
                            selectedPlatformModel = platformModel ?: state.selectedPlatformModel,
                            selectedLaserInstalled = laserInstalled ?: state.selectedLaserInstalled,
                            motionSamplingModeEnabled = isMotionSamplingModeEnabled(event),
                            fallStopEnabled = isFallStopEnabled(event) ?: state.fallStopEnabled,
                            fallStopStateKnown = isFallStopEnabled(event) != null || state.fallStopStateKnown,
                            isFallStopSyncInProgress = false,
                        )
                    }

                    is Event.DeviceConfig -> {
                        val devicePlatformModel = event.platformModel
                        if (devicePlatformModel != null && devicePlatformModel != PlatformModel.BASE) {
                            preferredLaserPlatformModel = devicePlatformModel
                        }
                        val systemLogs = mutableListOf<String>()
                        val status = if (awaitingDeviceConfigWriteResult) {
                            awaitingDeviceConfigWriteResult = false
                            systemLogs += "[DEVICE_CONFIG] write success model=${devicePlatformModel?.name ?: "UNKNOWN"} laser=${event.laserInstalled ?: false}"
                            text(
                                R.string.device_config_status_success,
                                devicePlatformModel?.name ?: text(R.string.common_not_available),
                                if (event.laserInstalled == true) {
                                    text(R.string.device_config_laser_installed)
                                } else {
                                    text(R.string.device_config_laser_not_installed)
                                },
                            )
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                devicePlatformModel = devicePlatformModel ?: it.devicePlatformModel,
                                deviceLaserInstalled = event.laserInstalled ?: it.deviceLaserInstalled,
                                selectedPlatformModel = devicePlatformModel ?: it.selectedPlatformModel,
                                selectedLaserInstalled = event.laserInstalled ?: it.selectedLaserInstalled,
                                deviceConfigStatus = status ?: it.deviceConfigStatus,
                                isDeviceConfigWritePending = false,
                                lastAckOrError = event.raw,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                        refreshCapabilityAndSnapshot()
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
                        val deviceConfigStatus = if (awaitingDeviceConfigWriteResult) {
                            awaitingDeviceConfigWriteResult = false
                            systemLogs += "[DEVICE_CONFIG] write generic_ack raw=${event.raw}"
                            text(R.string.device_config_status_ack_fallback, event.raw)
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = event.raw,
                                captureStatus = captureStatus ?: it.captureStatus,
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                                deviceConfigStatus = deviceConfigStatus ?: it.deviceConfigStatus,
                                isDeviceConfigWritePending = false,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    is Event.Nack -> {
                        val reason = event.reason.trim()
                        cancelPendingWaveStart("NACK:$reason")
                        cancelPendingWaveStop("NACK:$reason")
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
                        val deviceConfigStatus = if (awaitingDeviceConfigWriteResult) {
                            awaitingDeviceConfigWriteResult = false
                            systemLogs += "[DEVICE_CONFIG] write failure reason=$reason"
                            text(R.string.device_config_status_failure, formatNackMessage(reason))
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = formatNackMessage(reason),
                                captureStatus = captureStatus ?: it.captureStatus,
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                                deviceConfigStatus = deviceConfigStatus ?: it.deviceConfigStatus,
                                isDeviceConfigWritePending = false,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    is Event.Error -> {
                        cancelPendingWaveStart("ERROR:${event.reason}")
                        cancelPendingWaveStop("ERROR:${event.reason}")
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
                        val deviceConfigStatus = if (awaitingDeviceConfigWriteResult) {
                            awaitingDeviceConfigWriteResult = false
                            systemLogs += "[DEVICE_CONFIG] write error reason=${event.reason}"
                            text(R.string.device_config_status_send_failed, event.reason)
                        } else {
                            null
                        }
                        _uiState.update {
                            it.copy(
                                lastAckOrError = text(R.string.message_error, event.reason),
                                captureStatus = captureStatus ?: it.captureStatus,
                                writeModelStatus = writeModelStatus ?: it.writeModelStatus,
                                deviceConfigStatus = deviceConfigStatus ?: it.deviceConfigStatus,
                                isDeviceConfigWritePending = false,
                            )
                        }
                        systemLogs.forEach(::appendSystemLog)
                    }

                    else -> Unit
                }
            }
        }
    }

    private fun resetMeasurementDisplayState(forcePublish: Boolean = true) {
        telemetryDisplayBuffer.clear()
        recentWeightBuffer.clear()
        latestDistance = null
        latestWeight = null
        latestMa12 = null
        latestMeasurementValid = false
        latestMeasurementSequence = null
        lastMeasurementDisplayPublishAtMs = 0L
        if (forcePublish) {
            publishMeasurementDisplay(force = true)
        }
    }

    private fun resetRawConsoleState(forcePublish: Boolean = true) {
        rawLogBuffer.clear()
        lastRawConsolePublishAtMs = 0L
        if (forcePublish) {
            publishRawConsole(force = true)
        }
    }

    private fun resetSessionStores(
        clearTestSession: Boolean = true,
        clearMotionSamplingSession: Boolean = true,
        forcePublish: Boolean = true,
    ) {
        if (clearTestSession) {
            testSessionStore = null
        }
        if (clearMotionSamplingSession) {
            motionSamplingSessionStore = null
        }
        lastTestSessionPanelPublishAtMs = 0L
        if (forcePublish) {
            publishTestSessionPanel(force = true)
        }
    }

    private fun publishMeasurementDisplay(force: Boolean = false) {
        val now = System.currentTimeMillis()
        if (!force && now - lastMeasurementDisplayPublishAtMs < DISPLAY_THROTTLE_MS) return
        lastMeasurementDisplayPublishAtMs = now

        _measurementDisplayState.value = MeasurementDisplayUiState(
            distance = latestDistance,
            weight = latestWeight,
            ma12 = latestMa12,
            measurementValid = latestMeasurementValid,
            lastMeasurementSequence = latestMeasurementSequence,
            telemetryPoints = telemetryDisplayBuffer.toList(),
        )
        _uiState.update {
            withCaptureAvailability(
                it.copy(
                    distance = latestDistance,
                    weight = latestWeight,
                    ma12 = latestMa12,
                    measurementValid = latestMeasurementValid,
                    lastMeasurementSequence = latestMeasurementSequence,
                ),
            )
        }
    }

    private fun publishRawConsole(force: Boolean = false) {
        val now = System.currentTimeMillis()
        if (!force && now - lastRawConsolePublishAtMs < RAW_LOG_PUBLISH_INTERVAL_MS) return
        lastRawConsolePublishAtMs = now
        _rawConsoleState.value = RawConsoleUiState(
            rawLogLines = rawLogBuffer.toList(),
        )
    }

    private fun publishTestSessionPanel(force: Boolean = false) {
        val now = System.currentTimeMillis()
        if (!force && now - lastTestSessionPanelPublishAtMs < TEST_SESSION_PANEL_PUBLISH_INTERVAL_MS) return
        lastTestSessionPanelPublishAtMs = now
        _testSessionPanelState.value = TestSessionPanelUiState(
            session = testSessionStore,
            notice = _uiState.value.testSessionNotice,
        )
    }

    private fun appendTelemetryDisplayPoint(point: TelemetryPointUi) {
        telemetryDisplayBuffer.addLast(point)
        trimTelemetryDisplayBuffer()
    }

    private fun trimTelemetryDisplayBuffer() {
        val latestTimestampMs = telemetryDisplayBuffer.lastOrNull()?.timestampMs ?: return
        val minTimestampMs = latestTimestampMs - TELEMETRY_WINDOW_MS
        while (telemetryDisplayBuffer.isNotEmpty() &&
            (telemetryDisplayBuffer.firstOrNull()?.timestampMs ?: latestTimestampMs) < minTimestampMs
        ) {
            telemetryDisplayBuffer.removeFirst()
        }
    }

    private fun rememberRecentWeight(weight: Float) {
        recentWeightBuffer.addLast(weight)
        while (recentWeightBuffer.size > 7) {
            recentWeightBuffer.removeFirst()
        }
    }

    private fun recentMovingAverage(windowSize: Int): Float? {
        if (recentWeightBuffer.size < windowSize) return null
        return recentWeightBuffer.toList()
            .takeLast(windowSize)
            .average()
            .toFloat()
    }

    private fun shouldConsumeMeasurementCarrier(sample: Event.StreamSample): Boolean {
        return when (_uiState.value.protocolMode) {
            ProtocolMode.LEGACY -> true
            ProtocolMode.PRIMARY -> sample.carrier == MeasurementCarrier.FORMAL_EVT_STREAM
            ProtocolMode.UNKNOWN -> sample.carrier == MeasurementCarrier.FORMAL_EVT_STREAM
        }
    }

    private fun isHighPriorityLog(line: String): Boolean {
        return line.contains("EVT:FAULT") ||
            line.contains("EVT:SAFETY") ||
            line.contains("[FAULT]") ||
            line.contains("[TEST_SESSION]") ||
            line.contains("[DEVICE_CONFIG]") ||
            line.contains("[LAYER:MEASUREMENT_CONSUME]")
    }

    private fun mutableMotionSamplingRows(session: MotionSamplingSessionUi): MutableList<MotionSamplingRowUi> {
        return session.rows as? MutableList<MotionSamplingRowUi> ?: session.rows.toMutableList()
    }

    private fun mutableTestSessionSamples(session: TestSessionUi): MutableList<TestSessionSampleUi> {
        return session.samples as? MutableList<TestSessionSampleUi> ?: session.samples.toMutableList()
    }

    private fun appendTestSessionSample(sample: TestSessionSampleUi) {
        val session = testSessionStore ?: return
        if (session.status != TestSessionStatusUi.RECORDING) return
        val samples = mutableTestSessionSamples(session)
        samples.add(sample)
        testSessionStore = session.copy(
            samples = samples,
            summary = session.summary.copy(
                baselineReady = (session.summary.baselineReady == true) || sample.baselineReady,
                stableWeight = sample.stableWeight ?: session.summary.stableWeight,
                finalMainState = sample.mainState.ifBlank { session.summary.finalMainState ?: "" },
                finalAbnormalDurationMs = sample.abnormalDurationMs ?: session.summary.finalAbnormalDurationMs,
                finalDangerDurationMs = sample.dangerDurationMs ?: session.summary.finalDangerDurationMs,
                sampleCount = samples.size,
            ),
        )
    }

    private fun appendMotionSamplingRow(row: MotionSamplingRowUi) {
        val session = motionSamplingSessionStore ?: return
        val rows = mutableMotionSamplingRows(session)
        rows.add(row)
        motionSamplingSessionStore = session.copy(rows = rows)
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

    private fun currentWaveStateCode(active: Boolean): String = if (active) "RUNNING" else "STOPPED"

    private fun UiState.syncFormalWaveTruth(): UiState {
        val waveCode = currentWaveStateCode(waveOutputActive)
        return copy(
            safetyStatus = safetyStatus.copy(
                runtimeState = runtimeStateLabel(deviceState.name),
                runtimeCode = deviceState.name,
                waveState = waveStateLabel(waveCode),
                waveCode = waveCode,
            ),
        )
    }

    private fun UiState.syncWaveControlFlags(): UiState {
        val startPending = !waveOutputActive &&
            (pendingWaveStartRequest != null || deviceState == DeviceState.RUNNING)
        val stopPending = waveOutputActive &&
            (pendingWaveStopRequest != null || deviceState != DeviceState.RUNNING)
        return copy(
            isWaveStartPending = startPending,
            isWaveStopPending = stopPending,
        )
    }

    private fun UiState.applyWaveOutputTransition(nextWaveOutputActive: Boolean): UiState {
        val wasRunning = waveOutputActive
        val isRunning = nextWaveOutputActive
        return when {
            !wasRunning && isRunning -> copy(
                waveOutputActive = true,
                waveRuntimeStartMs = System.currentTimeMillis(),
                waveRuntimeElapsedMs = 0L,
            )

            wasRunning && !isRunning -> {
                val finalElapsedMs = waveRuntimeStartMs
                    ?.let { startMs -> (System.currentTimeMillis() - startMs).coerceAtLeast(0L) }
                    ?: waveRuntimeElapsedMs
                copy(
                    waveOutputActive = false,
                    waveRuntimeStartMs = null,
                    waveRuntimeElapsedMs = finalElapsedMs,
                )
            }

            else -> copy(waveOutputActive = nextWaveOutputActive)
        }
    }

    private fun UiState.resetWaveRuntime(): UiState {
        return copy(
            waveRuntimeStartMs = null,
            waveRuntimeElapsedMs = 0L,
        )
    }

    private fun onStreamSample(sample: Event.StreamSample) {
        if (!shouldConsumeMeasurementCarrier(sample)) {
            val ignoredSequence = sample.sequence
            if (ignoredSequence != null && ignoredSequence % MEASUREMENT_CONSUME_LOG_INTERVAL == 0L) {
                appendSystemLog(
                    "[LAYER:MEASUREMENT_CONSUME] ignored carrier=${sample.carrier.name} mode=${_uiState.value.protocolMode.name}",
                )
            }
            return
        }

        val now = System.currentTimeMillis()
        if (telemetrySessionStartMs == 0L) {
            telemetrySessionStartMs = now
        }
        lastStreamAtMs = now
        streamWatchdogJob?.cancel()
        if (_uiState.value.streamWarning != null) {
            _uiState.update { it.copy(streamWarning = null) }
        }
        if (!sample.valid || sample.distance == null || sample.weight == null) {
            val invalidSequence = sample.sequence
            recentWeightBuffer.clear()
            latestDistance = sample.distance
            latestWeight = sample.weight
            latestMa12 = sample.ma12.takeIf { sample.ma12Ready }
            latestMeasurementValid = false
            latestMeasurementSequence = invalidSequence
            publishMeasurementDisplay(force = true)
            if (invalidSequence != null &&
                invalidSequence % MEASUREMENT_CONSUME_LOG_INTERVAL == 0L
            ) {
                appendSystemLog(
                    "[LAYER:MEASUREMENT_CONSUME] seq=$invalidSequence valid=0 reason=${sample.reason ?: "INVALID"}",
                )
            }
            return
        }

        val distance = sample.distance ?: return
        val weight = sample.weight ?: return
        val sampleSequence = sample.sequence
        val currentState = _uiState.value
        val currentSignals = sessionCaptureSignals
        rememberRecentWeight(weight)
        val samplingRow = buildMotionSamplingRow(
            state = currentState,
            sample = sample,
            now = now,
        )
        val point = buildTelemetryPoint(
            measurementSeq = sampleSequence,
            deviceTimestampMs = sample.timestampMs,
            elapsedMs = now - telemetrySessionStartMs,
            timestampMs = now,
            distance = distance,
            unstableWeight = weight,
            measurementValid = true,
            ma12 = sample.ma12.takeIf { sample.ma12Ready },
            stableWeight = currentState.stableWeight.takeIf { currentState.stableWeightActive },
            stableFlag = currentState.stableWeightActive,
            ma3 = recentMovingAverage(3),
            ma5 = recentMovingAverage(5),
            ma7 = recentMovingAverage(7),
        )
        val testSessionSample = buildTestSessionSample(
            telemetryPoint = point,
            signals = currentSignals,
            sessionStartMs = testSessionStore?.startedAtMs,
        )
        appendTelemetryDisplayPoint(point)
        latestDistance = distance
        latestWeight = weight
        latestMa12 = sample.ma12.takeIf { sample.ma12Ready }
        latestMeasurementValid = true
        latestMeasurementSequence = sampleSequence
        samplingRow?.let(::appendMotionSamplingRow)
        testSessionSample?.let(::appendTestSessionSample)
        publishMeasurementDisplay()
        publishTestSessionPanel()

        if (currentSignals.pendingEventAux != null) {
            sessionCaptureSignals = sessionCaptureSignals.copy(pendingEventAux = null)
        }

        samplingRow?.let { row ->
            if (row.sampleIndex % MOTION_SAMPLE_LOG_INTERVAL == 0) {
                appendSystemLog("[MOTION_SAMPLE] row captured count=${row.sampleIndex}")
            }
        }

        if (sampleSequence != null &&
            sampleSequence % MEASUREMENT_CONSUME_LOG_INTERVAL == 0L
        ) {
            appendSystemLog(
                "[LAYER:MEASUREMENT_CONSUME] seq=$sampleSequence valid=1 distance=$distance weight=$weight ma12=${sample.ma12?.toString() ?: "-"}",
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

    private fun startNewTestSession(
        freq: Int,
        intensity: Int,
    ) {
        val now = System.currentTimeMillis()
        testSessionStore = testSessionManager.startSession(
            nowMs = now,
            freqHz = freq,
            intensity = intensity,
            fallStopEnabled = _uiState.value.fallStopEnabled,
            signals = sessionCaptureSignals,
        ).copy(samples = mutableListOf())
        publishTestSessionPanel(force = true)
        appendSystemLog("[TEST_SESSION] started id=${testSessionStore?.sessionId ?: "-"}")
    }

    private fun confirmPendingWaveStart(
        source: String,
        freq: Int? = null,
        intensity: Int? = null,
    ) {
        val pending = pendingWaveStartRequest ?: return
        if (testSessionStore?.status == TestSessionStatusUi.RECORDING) {
            pendingWaveStartRequest = null
            cancelPendingWaveTruthRefreshIfIdle()
            _uiState.update { it.syncWaveControlFlags() }
            return
        }

        startNewTestSession(
            freq = freq ?: pending.freq,
            intensity = intensity ?: pending.intensity,
        )
        pendingWaveStartRequest = null
        cancelPendingWaveTruthRefreshIfIdle()
        _uiState.update {
            it.copy(
                testSessionNotice = text(R.string.test_session_notice_started),
            ).syncWaveControlFlags()
        }
        publishTestSessionPanel(force = true)
        appendSystemLog(
            "[TEST_SESSION] start confirmed source=$source latency_ms=${System.currentTimeMillis() - pending.requestedAtMs}",
        )
    }

    private fun cancelPendingWaveStart(source: String) {
        val pending = pendingWaveStartRequest ?: return
        pendingWaveStartRequest = null
        cancelPendingWaveTruthRefreshIfIdle()
        _uiState.update { it.syncWaveControlFlags() }
        appendSystemLog(
            "[TEST_SESSION] start pending cleared source=$source latency_ms=${System.currentTimeMillis() - pending.requestedAtMs}",
        )
    }

    private fun stagePendingWaveStopCompletion(
        result: String,
        stopReason: String,
        stopSource: String,
    ) {
        pendingWaveStopCompletion = PendingWaveStopCompletion(
            result = result,
            stopReason = stopReason,
            stopSource = stopSource,
        )
        if (!_uiState.value.waveOutputActive) {
            confirmPendingWaveStop("TRUTH_ALREADY_INACTIVE")
        }
    }

    private fun confirmPendingWaveStop(source: String) {
        val pending = pendingWaveStopRequest
        val completion = pendingWaveStopCompletion
        pendingWaveStopRequest = null
        pendingWaveStopCompletion = null
        cancelPendingWaveTruthRefreshIfIdle()
        _uiState.update { it.syncWaveControlFlags() }
        completion?.let {
            finishTestSessionIfRecording(
                result = it.result,
                stopReason = it.stopReason,
                stopSource = it.stopSource,
            )
        }
        pending?.let {
            appendSystemLog(
                "[TEST_SESSION] stop confirmed source=$source latency_ms=${System.currentTimeMillis() - it.requestedAtMs}",
            )
        }
    }

    private fun cancelPendingWaveStop(source: String) {
        val pending = pendingWaveStopRequest
        pendingWaveStopRequest = null
        pendingWaveStopCompletion = null
        cancelPendingWaveTruthRefreshIfIdle()
        _uiState.update { it.syncWaveControlFlags() }
        pending?.let {
            appendSystemLog(
                "[TEST_SESSION] stop pending cleared source=$source latency_ms=${System.currentTimeMillis() - it.requestedAtMs}",
            )
        }
    }

    private fun ensureTestSessionStartedFromFormalTruth(
        source: String,
        freqHz: Float? = null,
        intensity: Int? = null,
    ) {
        if (testSessionStore?.status == TestSessionStatusUi.RECORDING) return
        startNewTestSession(
            freq = resolveFormalSessionFrequency(freqHz),
            intensity = intensity ?: sessionCaptureSignals.intensity ?: _uiState.value.intensity,
        )
        _uiState.update {
            it.copy(
                testSessionNotice = text(R.string.test_session_notice_started),
            ).syncWaveControlFlags()
        }
        publishTestSessionPanel(force = true)
        appendSystemLog("[TEST_SESSION] formal truth started source=$source")
    }

    private fun reconcileTestSessionWithFormalWaveTruth(
        source: String,
        waveOutputActive: Boolean,
        freqHz: Float? = null,
        intensity: Int? = null,
    ) {
        if (waveOutputActive) {
            cancelPendingWaveStop("${source}_ACTIVE")
            confirmPendingWaveStart(
                source = source,
                freq = freqHz?.roundToInt(),
                intensity = intensity,
            )
            ensureTestSessionStartedFromFormalTruth(
                source = source,
                freqHz = freqHz,
                intensity = intensity,
            )
            return
        }

        if (
            testSessionStore?.status == TestSessionStatusUi.RECORDING ||
            pendingWaveStopRequest != null ||
            pendingWaveStopCompletion != null
        ) {
            ensurePendingWaveStopCompletionForInactiveTruth()
            confirmPendingWaveStop(source)
        }
    }

    private fun ensurePendingWaveStopCompletionForInactiveTruth() {
        if (pendingWaveStopCompletion != null || testSessionStore?.status != TestSessionStatusUi.RECORDING) return
        val stopSource = sessionCaptureSignals.stopSource
        stagePendingWaveStopCompletion(
            result = if (stopSource == "USER_MANUAL_OTHER") {
                "NORMAL_STOP"
            } else {
                "AUTO_STOP"
            },
            stopReason = sessionCaptureSignals.stopReason.takeUnless { it.isBlank() || it == "NONE" }
                ?: "WAVE_OUTPUT_INACTIVE",
            stopSource = stopSource,
        )
    }

    private fun resolveFormalSessionFrequency(freqHz: Float?): Int {
        return freqHz?.roundToInt()
            ?: sessionCaptureSignals.freqHz?.roundToInt()
            ?: _uiState.value.freq
    }

    private fun hasPendingWaveLifecycleTruthRefresh(): Boolean {
        return pendingWaveStartRequest != null ||
            pendingWaveStopRequest != null ||
            pendingWaveStopCompletion != null
    }

    private fun currentSnapshotStartReadyMergeContext(): SnapshotStartReadyMergeContext {
        return if (hasPendingWaveLifecycleTruthRefresh()) {
            SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH
        } else {
            SnapshotStartReadyMergeContext.AUTHORITATIVE
        }
    }

    private fun schedulePendingWaveTruthRefresh(source: String) {
        if (!_uiState.value.isConnected || _uiState.value.protocolMode != ProtocolMode.PRIMARY) return
        waveTruthRefreshJob?.cancel()
        waveTruthRefreshJob = viewModelScope.launch {
            repeat(PENDING_WAVE_TRUTH_REFRESH_ATTEMPTS) {
                if (!_uiState.value.isConnected || _uiState.value.protocolMode != ProtocolMode.PRIMARY) {
                    return@launch
                }
                if (!hasPendingWaveLifecycleTruthRefresh()) {
                    return@launch
                }
                runCatching {
                    client.send(Command.SnapshotQuery)
                }.onFailure { error ->
                    appendSystemLog(
                        "[TEST_SESSION] truth refresh failed source=$source reason=${error.message ?: "UNKNOWN"}",
                    )
                    return@launch
                }
                delay(PENDING_WAVE_TRUTH_REFRESH_INTERVAL_MS)
            }
        }
    }

    private fun cancelPendingWaveTruthRefreshIfIdle() {
        if (!hasPendingWaveLifecycleTruthRefresh()) {
            waveTruthRefreshJob?.cancel()
            waveTruthRefreshJob = null
        }
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
        val session = testSessionStore
        if (session?.status == TestSessionStatusUi.RECORDING) {
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
            testSessionStore = finished
        }
        publishTestSessionPanel(force = true)
        finishedSessionId?.let { sessionId ->
            appendSystemLog(
                "[TEST_SESSION] finished id=$sessionId result=$result stop_reason=$stopReason stop_source=$stopSource",
            )
        }
    }

    private fun handleSessionLogLine(line: String) {
        when (val event = SessionLogParser.parse(line)) {
            is SessionLogEvent.TestStart -> {
                sessionCaptureSignals = sessionCaptureSignals.copy(
                    testId = event.testId ?: sessionCaptureSignals.testId,
                    freqHz = event.freqHz ?: sessionCaptureSignals.freqHz,
                    intensity = event.intensity ?: sessionCaptureSignals.intensity,
                    intensityNorm = event.intensityNorm ?: sessionCaptureSignals.intensityNorm,
                    stableWeight = event.stableWeight ?: sessionCaptureSignals.stableWeight,
                )
                testSessionStore = testSessionStore?.let { session ->
                    if (session.status != TestSessionStatusUi.RECORDING) {
                        session
                    } else {
                        testSessionManager.applyTestStart(session, event)
                    }
                }
                publishTestSessionPanel(force = true)
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
                _uiState.update {
                    it.copy(testSessionNotice = text(R.string.test_session_notice_auto_stopped, event.stopReason))
                }
                publishTestSessionPanel(force = true)
            }

            is SessionLogEvent.StopSummary -> {
                testSessionStore = testSessionStore?.let { session ->
                    if (session.status == TestSessionStatusUi.RECORDING) {
                        session
                    } else {
                        testSessionManager.applyStopSummary(
                            session = session,
                            event = event,
                            observedAtMs = System.currentTimeMillis(),
                        )
                    }
                }
                _uiState.update {
                    it.copy(
                        testSessionNotice = text(
                            R.string.test_session_notice_summary_received,
                            event.result,
                            event.stopReason,
                        ),
                    )
                }
                publishTestSessionPanel(force = true)
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
        telemetryPoint: TelemetryPointUi,
        signals: SessionCaptureSignals,
        sessionStartMs: Long?,
    ): TestSessionSampleUi? {
        val session = testSessionStore ?: return null
        if (session.status != TestSessionStatusUi.RECORDING) return null
        return TestSessionSampleUi(
            measurementSeq = telemetryPoint.measurementSeq,
            deviceTimestampMs = telemetryPoint.deviceTimestampMs,
            timestampMs = if (sessionStartMs == null) {
                0L
            } else {
                (telemetryPoint.timestampMs - sessionStartMs).coerceAtLeast(0L)
            },
            measurementValid = telemetryPoint.measurementValid,
            baselineReady = signals.baselineReady,
            stableWeight = signals.stableWeight,
            weight = telemetryPoint.weight,
            distance = telemetryPoint.distance,
            ma12 = telemetryPoint.ma12,
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
            if (state.deviceLaserInstalled == false) {
                appendRawLog("SYS", "INFO measurement plane intentionally absent on no-laser profile")
                return@launch
            }

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
        measurementSeq: Long?,
        deviceTimestampMs: Long?,
        elapsedMs: Long,
        timestampMs: Long,
        distance: Float,
        unstableWeight: Float,
        measurementValid: Boolean,
        ma12: Float?,
        stableWeight: Float?,
        stableFlag: Boolean,
        ma3: Float?,
        ma5: Float?,
        ma7: Float?,
    ): TelemetryPointUi {
        return TelemetryPointUi(
            measurementSeq = measurementSeq,
            deviceTimestampMs = deviceTimestampMs,
            elapsedMs = elapsedMs,
            timestampMs = timestampMs,
            distance = distance,
            unstableWeight = unstableWeight,
            measurementValid = measurementValid,
            ma12 = ma12,
            stableWeight = stableWeight,
            ma3 = ma3,
            ma5 = ma5,
            ma7 = ma7,
            stableFlag = stableFlag,
        )
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

    private fun platformModelFromCapabilities(capabilities: Event.Capabilities): PlatformModel? {
        return parsePlatformModel(capabilities.values["PLATFORM_MODEL"])
    }

    private fun laserInstalledFromCapabilities(capabilities: Event.Capabilities): Boolean? {
        parseCapabilityBoolean(capabilities.values["LASER_INSTALLED"])?.let { return it }
        return platformModelFromCapabilities(capabilities)?.let { it != PlatformModel.BASE }
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

    private fun parsePlatformModel(raw: String?): PlatformModel? {
        return when (raw?.trim()?.uppercase()) {
            "BASE" -> PlatformModel.BASE
            "PLUS" -> PlatformModel.PLUS
            "PRO" -> PlatformModel.PRO
            "ULTRA" -> PlatformModel.ULTRA
            else -> null
        }
    }

    private fun refreshCapabilityAndSnapshot() {
        if (!_uiState.value.isConnected) return
        viewModelScope.launch {
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

            if (probe?.mode == ProtocolMode.PRIMARY) {
                requestSnapshotRefresh()
            }
        }
    }

    private fun requestSnapshotRefresh() {
        if (!_uiState.value.isConnected || _uiState.value.protocolMode != ProtocolMode.PRIMARY) return
        viewModelScope.launch {
            runCatching {
                client.send(Command.SnapshotQuery)
            }.onFailure { error ->
                appendSystemLog(
                    text(
                        R.string.device_truth_refresh_failed,
                        error.message ?: text(R.string.common_not_available),
                    ),
                )
            }
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
        return when (state.waveStartAvailability()) {
            WaveStartAvailabilityUi.DISCONNECTED -> text(R.string.wave_bar_hint_disconnected)
            WaveStartAvailabilityUi.START_PENDING -> text(R.string.wave_bar_hint_start_pending)
            WaveStartAvailabilityUi.STOP_PENDING -> text(R.string.wave_bar_hint_stop_pending)
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
            distance.isFinite() &&
            state.measurementValid
    }

    private fun hasVisibleCaptureQuality(state: UiState): Boolean {
        val distance = state.distance
        val weight = state.weight
        return distance != null &&
            weight != null &&
            distance.isFinite() &&
            weight.isFinite() &&
            state.measurementValid &&
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

    private fun faultCodeFromReason(reasonCode: String): Int? {
        return when (reasonCode.uppercase()) {
            "NONE" -> 0
            "USER_LEFT_PLATFORM" -> 100
            "FALL_SUSPECTED" -> 101
            "BLE_DISCONNECTED" -> 102
            "MEASUREMENT_UNAVAILABLE" -> 200
            else -> null
        }
    }

    private fun faultStatusFromReason(reasonCode: String): FaultStatusUi {
        val code = faultCodeFromReason(reasonCode)
        return when {
            code != null -> faultStatusFromCode(code)
            reasonCode.equals("NONE", ignoreCase = true) -> FaultStatusUi()
            else -> FaultStatusUi(
                code = -1,
                label = safetyReasonLabel(reasonCode),
                codeName = reasonCode.uppercase(),
                severity = FaultSeverityUi.INFO,
            )
        }
    }

    private fun safetyStatusFromSnapshot(
        reasonCode: String,
        effectCode: String,
        runtimeState: DeviceState,
        waveOutputActive: Boolean,
    ): SafetyStatusUi {
        val normalizedReason = reasonCode.ifBlank { "NONE" }
        val normalizedEffect = effectCode.ifBlank { "NONE" }
        val meaningRes = when {
            normalizedReason.equals("BLE_DISCONNECTED", ignoreCase = true) -> R.string.safety_meaning_reconnect_needed
            normalizedEffect.equals(SafetyEffect.RECOVERABLE_PAUSE.name, ignoreCase = true) -> R.string.safety_meaning_recoverable_pause
            normalizedEffect.equals(SafetyEffect.ABNORMAL_STOP.name, ignoreCase = true) -> R.string.safety_meaning_abnormal_stop
            normalizedEffect.equals(SafetyEffect.WARNING_ONLY.name, ignoreCase = true) -> R.string.safety_meaning_warning_only
            else -> R.string.safety_meaning_none
        }
        val severity = when {
            normalizedReason.equals("BLE_DISCONNECTED", ignoreCase = true) -> FaultSeverityUi.WARNING
            normalizedEffect.equals(SafetyEffect.RECOVERABLE_PAUSE.name, ignoreCase = true) -> FaultSeverityUi.WARNING
            normalizedEffect.equals(SafetyEffect.ABNORMAL_STOP.name, ignoreCase = true) -> FaultSeverityUi.BLOCKING
            normalizedEffect.equals(SafetyEffect.WARNING_ONLY.name, ignoreCase = true) -> FaultSeverityUi.INFO
            else -> FaultSeverityUi.NONE
        }
        val sourceCode = if (normalizedReason.equals("NONE", ignoreCase = true) &&
            normalizedEffect.equals("NONE", ignoreCase = true)
        ) {
            "SNAPSHOT:NONE"
        } else {
            "SNAPSHOT"
        }
        return SafetyStatusUi(
            reason = safetyReasonLabel(normalizedReason),
            reasonCode = normalizedReason,
            effect = safetyEffectLabel(normalizedEffect),
            effectCode = normalizedEffect,
            runtimeState = runtimeStateLabel(runtimeState.name),
            runtimeCode = runtimeState.name,
            waveState = waveStateLabel(currentWaveStateCode(waveOutputActive)),
            waveCode = currentWaveStateCode(waveOutputActive),
            meaning = text(meaningRes),
            source = if (sourceCode == "SNAPSHOT:NONE") {
                text(R.string.safety_source_none)
            } else {
                text(R.string.safety_source_protocol)
            },
            sourceCode = sourceCode,
            severity = severity,
            code = faultCodeFromReason(normalizedReason),
            raw = "SNAPSHOT",
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
        motionSamplingSessionStore = motionSamplingSessionStore?.let { session ->
            stoppedSessionId = session.sessionId
            stoppedRowCount = session.rows.size
            session.copy(endedAtMs = now)
        }
        _uiState.update { state ->
            if (!state.isMotionSamplingActive) {
                state
            } else {
                state.copy(
                    isMotionSamplingActive = false,
                    motionSamplingStatus = reason,
                )
            }
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

    private fun appendRawLog(direction: String, payload: String, forcePublish: Boolean = false) {
        val line = "${LOG_TIME_FORMATTER.format(LocalTime.now())} [$direction] $payload"
        if (rawLogBuffer.size >= MAX_RAW_LOG_LINES) {
            rawLogBuffer.removeFirst()
        }
        rawLogBuffer.addLast(line)
        publishRawConsole(force = forcePublish || isHighPriorityLog(line))
    }

    private fun shouldAppendIncomingRawLine(line: String): Boolean {
        if (_uiState.value.verboseStreamLogsEnabled) return true
        if (line.startsWith("EVT:STREAM", ignoreCase = true)) return false
        return !CSV_STREAM_REGEX.matches(line.trim())
    }

    private fun buildMotionSamplingRow(
        state: UiState,
        sample: Event.StreamSample,
        now: Long,
    ): MotionSamplingRowUi? {
        if (!state.isMotionSamplingActive) return null
        val session = motionSamplingSessionStore ?: return null
        val distance = sample.distance ?: return null
        val weight = sample.weight ?: return null
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
            measurementSeq = sample.sequence,
            deviceTimestampMs = sample.timestampMs,
            timestampMs = now,
            elapsedMs = elapsedMs,
            distanceMm = distance,
            liveWeightKg = weight,
            ma12WeightKg = sample.ma12.takeIf { sample.ma12Ready },
            stableWeightKg = if (state.stableWeightActive) state.stableWeight else null,
            measurementValid = sample.valid && distance.isFinite() && weight.isFinite(),
            stableVisible = state.stableWeightActive,
            runtimeStateCode = state.deviceState.name,
            waveStateCode = state.safetyStatus.waveCode.ifBlank {
                currentWaveStateCode(state.waveOutputActive)
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
        waveTruthRefreshJob?.cancel()
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
        private const val DISPLAY_THROTTLE_MS = 125L
        private const val RAW_LOG_PUBLISH_INTERVAL_MS = 200L
        private const val TEST_SESSION_PANEL_PUBLISH_INTERVAL_MS = 200L
        private const val MEASUREMENT_CONSUME_LOG_INTERVAL = 50L
        private const val MAX_RAW_LOG_LINES = 200
        private const val TELEMETRY_WINDOW_MS = 20_000L
        private const val MOTION_SAMPLE_LOG_INTERVAL = 50
        private const val WAVE_FREQUENCY_MIN = 5
        private const val WAVE_FREQUENCY_MAX = 50
        private const val WAVE_INTENSITY_MIN = 0
        private const val WAVE_INTENSITY_MAX = 120
        private const val PENDING_WAVE_TRUTH_REFRESH_ATTEMPTS = 4
        private const val PENDING_WAVE_TRUTH_REFRESH_INTERVAL_MS = 250L
        private val CSV_STREAM_REGEX = Regex("""^-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?$""")
        private val LOG_TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")
    }
}

internal enum class SnapshotStartReadyMergeContext {
    AUTHORITATIVE,
    CONTROL_LIFECYCLE_REFRESH,
}

// Lifecycle-triggered snapshot refresh is used to reconcile formal control/session truth and
// should not permanently clear an already-known pre-start ready state with a transient false.
internal fun resolveSnapshotStartReady(
    currentStartReady: Boolean?,
    snapshotStartReady: Boolean?,
    mergeContext: SnapshotStartReadyMergeContext,
): Boolean? {
    return when {
        snapshotStartReady == null -> currentStartReady
        snapshotStartReady -> true
        mergeContext == SnapshotStartReadyMergeContext.CONTROL_LIFECYCLE_REFRESH &&
            currentStartReady == true -> currentStartReady
        else -> false
    }
}
