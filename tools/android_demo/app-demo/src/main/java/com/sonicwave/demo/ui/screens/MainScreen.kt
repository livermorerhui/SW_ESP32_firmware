package com.sonicwave.demo.ui.screens

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Scaffold
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sonicwave.demo.DemoViewModel
import com.sonicwave.demo.PermissionState
import com.sonicwave.demo.R
import com.sonicwave.demo.ScanState
import com.sonicwave.demo.TelemetryPointUi
import com.sonicwave.demo.TestSessionPanelUiState
import com.sonicwave.demo.UiState
import com.sonicwave.demo.ui.components.CalibrationCommandCallbacks
import com.sonicwave.demo.ui.components.CalibrationInputCallbacks
import com.sonicwave.demo.ui.components.CalibrationToolsSection
import com.sonicwave.demo.ui.components.CalibrationToolsActions
import com.sonicwave.demo.ui.components.DeviceToolsActions
import com.sonicwave.demo.ui.components.DeviceProfileSection
import com.sonicwave.demo.ui.components.FallStopProtectionSection
import com.sonicwave.demo.ui.components.MotionSamplingActions
import com.sonicwave.demo.ui.components.MotionSamplingSection
import com.sonicwave.demo.ui.components.RawConsoleActions
import com.sonicwave.demo.ui.components.RawConsoleSection
import com.sonicwave.demo.ui.components.SystemStatusSection
import com.sonicwave.demo.ui.components.TelemetryChartSection
import com.sonicwave.demo.ui.components.TestSessionActions
import com.sonicwave.demo.ui.components.TestSessionSection
import com.sonicwave.demo.ui.components.WaveControlActions
import com.sonicwave.demo.ui.components.WaveControlBottomBar
import com.sonicwave.transport.BleScanResult

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(viewModel: DemoViewModel = viewModel()) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val measurementDisplayState by viewModel.measurementDisplayState.collectAsStateWithLifecycle()
    val testSessionPanelState by viewModel.testSessionPanelState.collectAsStateWithLifecycle()
    val rawConsoleState by viewModel.rawConsoleState.collectAsStateWithLifecycle()
    val deviceToolsActions = remember(viewModel) {
        buildDeviceToolsActions(viewModel)
    }
    val motionSamplingActions = remember(viewModel) {
        buildMotionSamplingActions(viewModel)
    }
    val waveControlActions = remember(viewModel) {
        buildWaveControlActions(viewModel)
    }
    val testSessionActions = remember(viewModel) {
        buildTestSessionActions(viewModel)
    }
    val rawConsoleActions = remember(viewModel) {
        buildRawConsoleActions(viewModel)
    }
    var selectedTab by rememberSaveable { mutableStateOf(DemoMainTab.DEVICE) }
    var pendingCalibrationDeviceWrite by rememberSaveable {
        mutableStateOf<PendingCalibrationDeviceWrite?>(null)
    }
    val calibrationActions = buildCalibrationToolsActions(
        viewModel = viewModel,
        onZero = { pendingCalibrationDeviceWrite = PendingCalibrationDeviceWrite.SCALE_ZERO },
        onCalibrationZero = { pendingCalibrationDeviceWrite = PendingCalibrationDeviceWrite.CAL_ZERO },
    )
    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions(),
        onResult = { viewModel.refreshPermissionState() },
    )

    LaunchedEffect(Unit) {
        viewModel.refreshPermissionState()
    }

    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    title = { Text(stringResource(R.string.screen_title_main)) },
                    actions = {
                        TextButton(
                            onClick = { viewModel.openScanSheetAndStartScan() },
                        ) {
                            Text(stringResource(R.string.action_search_connect))
                        }
                        if (uiState.isConnected) {
                            TextButton(onClick = viewModel::disconnect) {
                                Text(stringResource(R.string.action_disconnect))
                            }
                        }
                    },
                )
                DemoMainTabs(
                    selectedTab = selectedTab,
                    onTabSelected = { selectedTab = it },
                )
            }
        },
        bottomBar = {
            WaveControlBottomBar(
                uiState = uiState,
                actions = waveControlActions,
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(12.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (uiState.permissionState is PermissionState.Missing) {
                PermissionCard(
                    permissionState = uiState.permissionState,
                    onRequest = { launcher.launch(requiredAppPermissions()) },
                )
            }

            when (selectedTab) {
                DemoMainTab.DEVICE -> DeviceToolsContent(
                    uiState = uiState,
                    actions = deviceToolsActions,
                )

                DemoMainTab.CALIBRATION -> CalibrationToolsSection(
                    uiState = uiState,
                    actions = calibrationActions,
                )

                DemoMainTab.SAMPLING -> MotionSamplingSection(
                    uiState = uiState,
                    actions = motionSamplingActions,
                )

                DemoMainTab.RUN -> RunDashboardContent(
                    uiState = uiState,
                    telemetryPoints = measurementDisplayState.telemetryPoints,
                    testSessionPanelState = testSessionPanelState,
                    testSessionActions = testSessionActions,
                )

                DemoMainTab.LOGS -> RawConsoleSection(
                    rawLogLines = rawConsoleState.rawLogLines,
                    actions = rawConsoleActions,
                )
            }
        }
    }

    if (uiState.isDeviceSheetVisible) {
        ModalBottomSheet(onDismissRequest = viewModel::closeScanSheet) {
            DevicePickerContent(
                uiState = uiState,
                onConnect = viewModel::connectToDevice,
            )
        }
    }

    if (uiState.showDegradedStartDialog) {
        AlertDialog(
            onDismissRequest = viewModel::dismissDegradedStartDialog,
            title = {
                Text(stringResource(R.string.degraded_start_dialog_title))
            },
            text = {
                Text(stringResource(R.string.degraded_start_dialog_message))
            },
            confirmButton = {
                TextButton(
                    onClick = viewModel::confirmDegradedStart,
                    enabled = !uiState.isDegradedStartWritePending,
                ) {
                    Text(
                        if (uiState.isDegradedStartWritePending) {
                            stringResource(R.string.degraded_start_pending_action)
                        } else {
                            stringResource(R.string.degraded_start_confirm_action)
                        },
                    )
                }
            },
            dismissButton = {
                TextButton(
                    onClick = viewModel::dismissDegradedStartDialog,
                    enabled = !uiState.isDegradedStartWritePending,
                ) {
                    Text(stringResource(R.string.degraded_start_cancel_action))
                }
            },
        )
    }

    pendingCalibrationDeviceWrite?.let { pending ->
        AlertDialog(
            onDismissRequest = { pendingCalibrationDeviceWrite = null },
            title = { Text(stringResource(pending.titleRes)) },
            text = { Text(stringResource(pending.messageRes)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        pendingCalibrationDeviceWrite = null
                        when (pending) {
                            PendingCalibrationDeviceWrite.SCALE_ZERO -> viewModel.sendZero()
                            PendingCalibrationDeviceWrite.CAL_ZERO -> viewModel.sendCalibrationZero()
                        }
                    },
                ) {
                    Text(stringResource(R.string.action_write_to_device))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingCalibrationDeviceWrite = null }) {
                    Text(stringResource(R.string.action_cancel))
                }
            },
        )
    }
}

private enum class DemoMainTab(val labelRes: Int) {
    DEVICE(R.string.tab_device),
    CALIBRATION(R.string.tab_calibration),
    SAMPLING(R.string.tab_sampling),
    RUN(R.string.tab_run),
    LOGS(R.string.tab_logs),
}

private enum class PendingCalibrationDeviceWrite(
    val titleRes: Int,
    val messageRes: Int,
) {
    SCALE_ZERO(
        titleRes = R.string.confirm_scale_zero_title,
        messageRes = R.string.confirm_scale_zero_message,
    ),
    CAL_ZERO(
        titleRes = R.string.confirm_cal_zero_title,
        messageRes = R.string.confirm_cal_zero_message,
    ),
}

@Composable
private fun DemoMainTabs(
    selectedTab: DemoMainTab,
    onTabSelected: (DemoMainTab) -> Unit,
) {
    val tabs = DemoMainTab.values()
    ScrollableTabRow(
        selectedTabIndex = tabs.indexOf(selectedTab),
        edgePadding = 12.dp,
    ) {
        tabs.forEach { tab ->
            Tab(
                selected = selectedTab == tab,
                onClick = { onTabSelected(tab) },
                text = { Text(stringResource(tab.labelRes)) },
            )
        }
    }
}

@Composable
private fun RunDashboardContent(
    uiState: UiState,
    telemetryPoints: List<TelemetryPointUi>,
    testSessionPanelState: TestSessionPanelUiState,
    testSessionActions: TestSessionActions,
) {
    SystemStatusSection(uiState = uiState, compact = true)
    TelemetryChartSection(
        telemetryPoints = telemetryPoints,
        stableWeight = uiState.stableWeight,
        stableWeightActive = uiState.stableWeightActive,
    )
    TestSessionSection(
        panelState = testSessionPanelState,
        actions = testSessionActions,
    )
}

@Composable
private fun DeviceToolsContent(
    uiState: UiState,
    actions: DeviceToolsActions,
) {
    DeviceProfileSection(
        uiState = uiState,
        onPlatformModelSelected = actions.onPlatformModelSelected,
        onWriteDeviceConfig = actions.onWriteDeviceConfig,
    )
    FallStopProtectionSection(
        uiState = uiState,
        onToggleEnabled = actions.onToggleFallStopEnabled,
        onToggleLeaveEnabled = actions.onToggleLeaveProtectionEnabled,
    )
}

private fun buildDeviceToolsActions(viewModel: DemoViewModel): DeviceToolsActions {
    return DeviceToolsActions(
        onPlatformModelSelected = viewModel::updateSelectedPlatformModel,
        onWriteDeviceConfig = viewModel::sendDeviceConfig,
        onToggleFallStopEnabled = viewModel::setFallStopProtectionEnabled,
        onToggleLeaveProtectionEnabled = viewModel::setLeaveProtectionEnabled,
    )
}

private fun buildCalibrationToolsActions(
    viewModel: DemoViewModel,
    onZero: () -> Unit,
    onCalibrationZero: () -> Unit,
): CalibrationToolsActions {
    return CalibrationToolsActions(
        input = CalibrationInputCallbacks(
            onCaptureReferenceChange = viewModel::updateCaptureReferenceInput,
            onModelReferenceChange = viewModel::updateModelReferenceInput,
            onModelC0Change = viewModel::updateModelC0Input,
            onModelC1Change = viewModel::updateModelC1Input,
            onModelC2Change = viewModel::updateModelC2Input,
            onModelTypeChange = viewModel::updateModelType,
        ),
        commands = CalibrationCommandCallbacks(
            onZero = onZero,
            onCapturePoint = viewModel::sendCalibrationCapture,
            onStartRecording = viewModel::startRecording,
            onStopRecording = viewModel::stopRecording,
            onClearCalibrationPoints = viewModel::clearCalibrationPoints,
            onGetModel = viewModel::sendCalibrationGetModel,
            onSetModel = viewModel::sendCalibrationSetModel,
            onCalibrationZero = onCalibrationZero,
            onToggleEngineeringSection = viewModel::toggleEngineeringSection,
            onToggleVerboseStreamLogs = viewModel::toggleVerboseStreamLogs,
        ),
    )
}

private fun buildMotionSamplingActions(viewModel: DemoViewModel): MotionSamplingActions {
    return MotionSamplingActions(
        onStartSampling = viewModel::startMotionSampling,
        onStopSampling = viewModel::stopMotionSampling,
        onEnableSamplingMode = { viewModel.setMotionSamplingModeEnabled(true) },
        onDisableSamplingMode = { viewModel.setMotionSamplingModeEnabled(false) },
        onClearSession = viewModel::clearMotionSamplingSession,
        onExportSession = viewModel::exportMotionSamplingSession,
    )
}

private fun buildWaveControlActions(viewModel: DemoViewModel): WaveControlActions {
    return WaveControlActions(
        onFreqInputChange = viewModel::updateFreqInput,
        onIntensityInputChange = viewModel::updateIntensityInput,
        onFreqInputCommit = viewModel::commitFreqInput,
        onIntensityInputCommit = viewModel::commitIntensityInput,
        onFreqPresetSelected = viewModel::setPresetFrequency,
        onIntensityPresetSelected = viewModel::setPresetIntensity,
        onStart = viewModel::sendWaveStart,
        onStop = viewModel::sendWaveStop,
    )
}

private fun buildTestSessionActions(viewModel: DemoViewModel): TestSessionActions {
    return TestSessionActions(
        onClearSession = viewModel::clearTestSession,
        onExportSession = viewModel::exportTestSession,
    )
}

private fun buildRawConsoleActions(viewModel: DemoViewModel): RawConsoleActions {
    return RawConsoleActions(
        onClear = viewModel::clearRawLog,
    )
}

@Composable
private fun PermissionCard(
    permissionState: PermissionState,
    onRequest: () -> Unit,
) {
    val missing = (permissionState as? PermissionState.Missing)?.missingPermissions.orEmpty()
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(stringResource(R.string.permission_required_hint))
            if (missing.isNotEmpty()) {
                Text(stringResource(R.string.permission_missing, missing.joinToString()))
            }
            Button(onClick = onRequest) {
                Text(stringResource(R.string.action_request_permission))
            }
        }
    }
}

@Composable
private fun DevicePickerContent(
    uiState: UiState,
    onConnect: (BleScanResult) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text(stringResource(R.string.scan_sheet_title), style = MaterialTheme.typography.titleMedium)
        when {
            uiState.isConnecting -> {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CircularProgressIndicator(modifier = Modifier.size(20.dp))
                    Text(stringResource(R.string.scan_connecting))
                }
            }

            uiState.scanResults.isEmpty() -> {
                val content = when (uiState.scanState) {
                    ScanState.Scanning,
                    is ScanState.Results ->
                        stringResource(R.string.scan_no_devices)

                    else ->
                        stringResource(R.string.scan_no_devices_idle)
                }
                Text(content)
            }

            else -> {
                uiState.scanResults.forEach { device ->
                    DeviceRow(device = device, onConnect = onConnect)
                }
            }
        }
        Spacer(modifier = Modifier.height(8.dp))
    }
}

@Composable
private fun DeviceRow(
    device: BleScanResult,
    onConnect: (BleScanResult) -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(device.name ?: stringResource(R.string.device_name_unknown))
            Text(device.address)
            Text(stringResource(R.string.scan_rssi, device.rssi))
            Button(onClick = { onConnect(device) }) {
                Text(stringResource(R.string.action_connect))
            }
        }
    }
}

private fun requiredAppPermissions(): Array<String> {
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
    return permissions.toTypedArray()
}
