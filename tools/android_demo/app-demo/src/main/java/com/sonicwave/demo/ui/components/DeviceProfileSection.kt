package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
    onLaserInstalledSelected: (Boolean) -> Unit,
    onWriteDeviceConfig: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)

    Card(modifier = modifier.fillMaxWidth()) {
        androidx.compose.foundation.layout.Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.section_device_profile), style = MaterialTheme.typography.titleMedium)
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
                fontWeight = FontWeight.SemiBold,
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
                    FilterChip(
                        selected = uiState.selectedPlatformModel == model,
                        onClick = { onPlatformModelSelected(model) },
                        label = { Text(model.name) },
                    )
                }
            }

            Text(stringResource(R.string.device_profile_select_laser), fontWeight = FontWeight.SemiBold)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                FilterChip(
                    selected = !uiState.selectedLaserInstalled,
                    onClick = { onLaserInstalledSelected(false) },
                    label = { Text(stringResource(R.string.device_config_laser_not_installed)) },
                )
                FilterChip(
                    selected = uiState.selectedLaserInstalled,
                    onClick = { onLaserInstalledSelected(true) },
                    label = { Text(stringResource(R.string.device_config_laser_installed)) },
                )
            }

            Text(
                text = stringResource(R.string.device_profile_consistency_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
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
        }
    }
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
