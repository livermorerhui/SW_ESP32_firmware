package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedButton
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
import com.sonicwave.demo.EmptyDistanceStatusUi
import com.sonicwave.demo.UiState
import com.sonicwave.protocol.DeviceState
import com.sonicwave.protocol.PlatformModel
import com.sonicwave.protocol.ProtocolMode
import java.util.Locale

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

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text(
                    text = stringResource(R.string.device_profile_select_model),
                    modifier = Modifier.padding(top = 12.dp),
                    fontWeight = FontWeight.SemiBold,
                )
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

@Composable
fun EmptyDistanceSection(
    uiState: UiState,
    onSetEmptyDistance: (() -> Unit)? = null,
    onClearEmptyDistance: (() -> Unit)? = null,
    modifier: Modifier = Modifier,
) {
    var pendingAction by rememberSaveable { mutableStateOf<EmptyDistanceAction?>(null) }
    val emptyState = uiState.emptyDistance
    val verifiedDistance = emptyState.confirmedDistanceMm
    val liveDistance = uiState.distance?.takeIf { uiState.distanceValid && it.isFinite() }
    val hasDeviceCommand = onSetEmptyDistance != null && onClearEmptyDistance != null &&
        uiState.emptyDistanceControlSupported == true
    val canEdit = hasDeviceCommand && uiState.isConnected &&
        uiState.protocolMode == ProtocolMode.PRIMARY &&
        uiState.deviceState != DeviceState.RUNNING && !uiState.waveOutputActive &&
        emptyState.status != EmptyDistanceStatusUi.PENDING

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = stringResource(R.string.section_empty_distance),
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                text = stringResource(
                    R.string.empty_distance_value,
                    verifiedDistance?.let { String.format(Locale.US, "%.2f mm", it) }
                        ?: stringResource(R.string.empty_distance_unset),
                ),
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = stringResource(
                    R.string.empty_distance_live_value,
                    liveDistance?.let { String.format(Locale.US, "%.2f mm", it) }
                        ?: stringResource(R.string.common_not_available),
                ),
            )
            if (emptyState.status != EmptyDistanceStatusUi.NOT_SET) {
                Text(
                    text = stringResource(
                        when (emptyState.status) {
                            EmptyDistanceStatusUi.NOT_SET -> R.string.empty_distance_status_unset
                            EmptyDistanceStatusUi.PENDING -> R.string.empty_distance_status_pending
                            EmptyDistanceStatusUi.SET -> R.string.empty_distance_status_set
                            EmptyDistanceStatusUi.INVALID -> R.string.empty_distance_status_invalid
                            EmptyDistanceStatusUi.READBACK_MISMATCH -> R.string.empty_distance_status_mismatch
                        },
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { pendingAction = EmptyDistanceAction.SET },
                    enabled = canEdit && liveDistance != null,
                ) {
                    Text(stringResource(R.string.empty_distance_set))
                }
                OutlinedButton(
                    onClick = { pendingAction = EmptyDistanceAction.CLEAR },
                    enabled = canEdit && verifiedDistance != null,
                ) {
                    Text(stringResource(R.string.empty_distance_clear))
                }
            }
            if (uiState.emptyDistanceControlSupported != true) {
                Text(
                    text = stringResource(R.string.empty_distance_protocol_pending),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }

    pendingAction?.let { action ->
        AlertDialog(
            onDismissRequest = { pendingAction = null },
            title = {
                Text(
                    stringResource(
                        if (action == EmptyDistanceAction.SET) {
                            R.string.empty_distance_set_confirm_title
                        } else {
                            R.string.empty_distance_clear_confirm_title
                        },
                    ),
                )
            },
            text = {
                Text(
                    if (action == EmptyDistanceAction.SET) {
                        stringResource(
                            R.string.empty_distance_set_confirm_message,
                            liveDistance?.let { String.format(Locale.US, "%.2f", it) }
                                ?: stringResource(R.string.common_not_available),
                        )
                    } else {
                        stringResource(R.string.empty_distance_clear_confirm_message)
                    },
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        pendingAction = null
                        if (action == EmptyDistanceAction.SET) onSetEmptyDistance?.invoke()
                        else onClearEmptyDistance?.invoke()
                    },
                ) {
                    Text(stringResource(R.string.action_write_to_device))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingAction = null }) {
                    Text(stringResource(R.string.action_cancel))
                }
            },
        )
    }
}

private enum class EmptyDistanceAction { SET, CLEAR }

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
