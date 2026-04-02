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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sonicwave.demo.DemoViewModel
import com.sonicwave.demo.PermissionState
import com.sonicwave.demo.R
import com.sonicwave.demo.ScanState
import com.sonicwave.demo.UiState
import com.sonicwave.demo.ui.components.CalibrationToolsSection
import com.sonicwave.demo.ui.components.DeviceConnectSection
import com.sonicwave.demo.ui.components.DeviceProfileSection
import com.sonicwave.demo.ui.components.FallStopProtectionSection
import com.sonicwave.demo.ui.components.RawConsoleSection
import com.sonicwave.demo.ui.components.SystemStatusSection
import com.sonicwave.demo.ui.components.TelemetryChartSection
import com.sonicwave.demo.ui.components.TestSessionSection
import com.sonicwave.demo.ui.components.WaveControlBottomBar
import com.sonicwave.transport.BleScanResult

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(viewModel: DemoViewModel = viewModel()) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions(),
        onResult = { viewModel.refreshPermissionState() },
    )

    LaunchedEffect(Unit) {
        viewModel.refreshPermissionState()
    }

    Scaffold(
        topBar = {
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
        },
        bottomBar = {
            WaveControlBottomBar(
                uiState = uiState,
                onFreqInputChange = viewModel::updateFreqInput,
                onIntensityInputChange = viewModel::updateIntensityInput,
                onFreqInputCommit = viewModel::commitFreqInput,
                onIntensityInputCommit = viewModel::commitIntensityInput,
                onFreqPresetSelected = viewModel::setPresetFrequency,
                onIntensityPresetSelected = viewModel::setPresetIntensity,
                onStart = viewModel::sendWaveStart,
                onStop = viewModel::sendWaveStop,
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
            DeviceConnectSection(uiState = uiState)

            DeviceProfileSection(
                uiState = uiState,
                onPlatformModelSelected = viewModel::updateSelectedPlatformModel,
                onLaserInstalledSelected = viewModel::updateSelectedLaserInstalled,
                onWriteDeviceConfig = viewModel::sendDeviceConfig,
            )

            if (uiState.permissionState is PermissionState.Missing) {
                PermissionCard(
                    permissionState = uiState.permissionState,
                    onRequest = { launcher.launch(requiredAppPermissions()) },
                )
            }

            SystemStatusSection(uiState = uiState)

            FallStopProtectionSection(
                uiState = uiState,
                onToggleEnabled = viewModel::setFallStopProtectionEnabled,
            )

            TelemetryChartSectionHost(
                viewModel = viewModel,
                stableWeight = uiState.stableWeight,
                stableWeightActive = uiState.stableWeightActive,
            )
            TestSessionSectionHost(viewModel = viewModel)

            CalibrationToolsSection(
                uiState = uiState,
                onZeroInputChange = viewModel::updateZeroInput,
                onFactorInputChange = viewModel::updateFactorInput,
                onCaptureReferenceChange = viewModel::updateCaptureReferenceInput,
                onModelReferenceChange = viewModel::updateModelReferenceInput,
                onModelC0Change = viewModel::updateModelC0Input,
                onModelC1Change = viewModel::updateModelC1Input,
                onModelC2Change = viewModel::updateModelC2Input,
                onModelTypeChange = viewModel::updateModelType,
                onZero = viewModel::sendZero,
                onCalibrate = viewModel::sendCalibrate,
                onCapturePoint = viewModel::sendCalibrationCapture,
                onStartRecording = viewModel::startRecording,
                onStopRecording = viewModel::stopRecording,
                onGetModel = viewModel::sendCalibrationGetModel,
                onSetModel = viewModel::sendCalibrationSetModel,
                onCalibrationZero = viewModel::sendCalibrationZero,
                onToggleEngineeringSection = viewModel::toggleEngineeringSection,
                onToggleVerboseStreamLogs = viewModel::toggleVerboseStreamLogs,
            )

            RawConsoleSectionHost(viewModel = viewModel)
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
}

@Composable
private fun TelemetryChartSectionHost(
    viewModel: DemoViewModel,
    stableWeight: Float?,
    stableWeightActive: Boolean,
) {
    val measurementDisplayState by viewModel.measurementDisplayState.collectAsStateWithLifecycle()
    TelemetryChartSection(
        telemetryPoints = measurementDisplayState.telemetryPoints,
        stableWeight = stableWeight,
        stableWeightActive = stableWeightActive,
    )
}

@Composable
private fun TestSessionSectionHost(
    viewModel: DemoViewModel,
) {
    val panelState by viewModel.testSessionPanelState.collectAsStateWithLifecycle()
    TestSessionSection(
        panelState = panelState,
        onClearSession = viewModel::clearTestSession,
        onExportSession = viewModel::exportTestSession,
    )
}

@Composable
private fun RawConsoleSectionHost(
    viewModel: DemoViewModel,
) {
    val rawConsoleState by viewModel.rawConsoleState.collectAsStateWithLifecycle()
    RawConsoleSection(
        rawLogLines = rawConsoleState.rawLogLines,
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
