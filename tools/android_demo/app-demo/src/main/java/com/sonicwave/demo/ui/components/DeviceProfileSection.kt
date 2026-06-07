package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.protocol.PlatformModel
import com.sonicwave.protocol.ProtocolMode

@Composable
fun DeviceProfileSection(
    uiState: UiState,
    onPlatformModelSelected: (PlatformModel) -> Unit,
    onWriteDeviceConfig: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    var detailsExpanded by rememberSaveable { mutableStateOf(false) }
    val selectedSensorLabel = if (uiState.selectedPlatformModel == PlatformModel.BASE) {
        stringResource(R.string.device_profile_sensor_without_distance)
    } else {
        stringResource(R.string.device_profile_sensor_with_distance)
    }
    val currentDeviceLabel = currentDeviceLabel(
        platformModel = uiState.devicePlatformModel,
        laserInstalled = uiState.deviceLaserInstalled,
        unknownLabel = notAvailable,
    )

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.section_device_profile), style = MaterialTheme.typography.titleMedium)

            Text(
                text = stringResource(R.string.device_profile_current_device, currentDeviceLabel),
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
            )

            Text(stringResource(R.string.device_profile_select_model), fontWeight = FontWeight.SemiBold)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                listOf(
                    PlatformModel.BASE,
                    PlatformModel.PLUS,
                    PlatformModel.PRO,
                    PlatformModel.ULTRA,
                ).forEach { model ->
                    val selectable = model == PlatformModel.BASE || model == PlatformModel.PLUS
                    FilterChip(
                        selected = uiState.selectedPlatformModel == model,
                        onClick = { onPlatformModelSelected(model) },
                        enabled = selectable,
                        label = { Text(modelLabel(model)) },
                    )
                }
            }

            Text(
                text = stringResource(R.string.device_profile_sensor_status, selectedSensorLabel),
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
            )

            Button(
                onClick = onWriteDeviceConfig,
                enabled = uiState.isConnected &&
                    uiState.protocolMode == ProtocolMode.PRIMARY &&
                    !uiState.isDeviceConfigWritePending,
            ) {
                Text(
                    if (uiState.isDeviceConfigWritePending) {
                        stringResource(R.string.device_profile_write_pending)
                    } else {
                        stringResource(R.string.device_profile_write)
                    },
                )
            }

            uiState.deviceConfigStatus?.let { status ->
                Text(
                    text = status,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            TextButton(onClick = { detailsExpanded = !detailsExpanded }) {
                Text(
                    if (detailsExpanded) {
                        stringResource(R.string.action_hide_details)
                    } else {
                        stringResource(R.string.action_show_details)
                    },
                )
            }

            if (detailsExpanded) {
                Text(
                    text = stringResource(R.string.device_profile_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(
                        R.string.device_profile_current_truth,
                        uiState.devicePlatformModel?.name ?: notAvailable,
                        boolLabel(
                            value = uiState.deviceLaserInstalled,
                            trueLabel = stringResource(R.string.device_config_laser_installed),
                            falseLabel = stringResource(R.string.device_config_laser_not_installed),
                            notAvailable = notAvailable,
                        ),
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(
                        R.string.device_profile_runtime_truth,
                        boolLabel(
                            value = uiState.deviceLaserAvailable,
                            trueLabel = stringResource(R.string.common_yes),
                            falseLabel = stringResource(R.string.common_no),
                            notAvailable = notAvailable,
                        ),
                        boolLabel(
                            value = uiState.deviceProtectionDegraded,
                            trueLabel = stringResource(R.string.common_yes),
                            falseLabel = stringResource(R.string.common_no),
                            notAvailable = notAvailable,
                        ),
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.device_profile_consistency_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

        }
    }
}

private fun modelLabel(model: PlatformModel): String {
    return when (model) {
        PlatformModel.BASE -> "Base"
        PlatformModel.PLUS -> "Plus"
        PlatformModel.PRO -> "Pro"
        PlatformModel.ULTRA -> "Ultra"
    }
}

@Composable
private fun currentDeviceLabel(
    platformModel: PlatformModel?,
    laserInstalled: Boolean?,
    unknownLabel: String,
): String {
    val model = platformModel ?: return unknownLabel
    val sensorLabel = when (laserInstalled ?: (model != PlatformModel.BASE)) {
        true -> stringResource(R.string.device_profile_sensor_with_distance)
        false -> stringResource(R.string.device_profile_sensor_without_distance)
    }
    return stringResource(
        R.string.device_profile_current_device_value,
        modelLabel(model),
        sensorLabel,
    )
}

@Composable
private fun boolLabel(
    value: Boolean?,
    trueLabel: String,
    falseLabel: String,
    notAvailable: String,
): String {
    return when (value) {
        true -> trueLabel
        false -> falseLabel
        null -> notAvailable
    }
}
