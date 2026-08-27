package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.protocol.ProtocolMode

@Composable
fun FallStopProtectionSection(
    uiState: UiState,
    onToggleEnabled: (Boolean) -> Unit,
    onToggleLeaveEnabled: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    var detailsExpanded by rememberSaveable { mutableStateOf(false) }
    val fallControlEnabled = uiState.isConnected &&
        uiState.protocolMode != ProtocolMode.LEGACY &&
        !uiState.isFallStopSyncInProgress
    val leaveControlEnabled = uiState.isConnected &&
        uiState.protocolMode != ProtocolMode.LEGACY &&
        uiState.leaveProtectionSupported &&
        !uiState.isLeaveProtectionSyncInProgress
    val statusText = when {
        uiState.isFallStopSyncInProgress && uiState.fallStopAckConfirmed && uiState.fallStopEnabled ->
            stringResource(R.string.value_fall_stop_protection_ack_enabled_verifying)
        uiState.isFallStopSyncInProgress && uiState.fallStopAckConfirmed ->
            stringResource(R.string.value_fall_stop_protection_ack_disabled_verifying)
        uiState.isFallStopSyncInProgress -> stringResource(R.string.value_fall_stop_protection_syncing)
        uiState.fallStopAckConfirmed && uiState.fallStopEnabled ->
            stringResource(R.string.value_fall_stop_protection_ack_enabled_unverified)
        uiState.fallStopAckConfirmed ->
            stringResource(R.string.value_fall_stop_protection_ack_disabled_unverified)
        !uiState.fallStopStateKnown -> stringResource(R.string.value_fall_stop_protection_unknown)
        uiState.fallStopEnabled -> stringResource(R.string.value_fall_stop_protection_enabled)
        else -> stringResource(R.string.value_fall_stop_protection_disabled)
    }
    val leaveStatusText = when {
        !uiState.leaveProtectionSupported -> stringResource(R.string.value_leave_protection_unsupported)
        uiState.isLeaveProtectionSyncInProgress && uiState.leaveProtectionAckConfirmed && uiState.leaveProtectionEnabled ->
            stringResource(R.string.value_leave_protection_ack_enabled_verifying)
        uiState.isLeaveProtectionSyncInProgress && uiState.leaveProtectionAckConfirmed ->
            stringResource(R.string.value_leave_protection_ack_disabled_verifying)
        uiState.isLeaveProtectionSyncInProgress -> stringResource(R.string.value_leave_protection_syncing)
        uiState.leaveProtectionAckConfirmed && uiState.leaveProtectionEnabled ->
            stringResource(R.string.value_leave_protection_ack_enabled_unverified)
        uiState.leaveProtectionAckConfirmed ->
            stringResource(R.string.value_leave_protection_ack_disabled_unverified)
        !uiState.leaveProtectionStateKnown -> stringResource(R.string.value_leave_protection_unknown)
        uiState.leaveProtectionEnabled -> stringResource(R.string.value_leave_protection_enabled)
        else -> stringResource(R.string.value_leave_protection_disabled)
    }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = stringResource(R.string.section_protection_switches),
                style = MaterialTheme.typography.titleMedium,
            )
            ProtectionSwitchRow(
                label = stringResource(R.string.label_fall_protection),
                checked = uiState.fallStopEnabled,
                enabled = fallControlEnabled,
                onCheckedChange = onToggleEnabled,
            )
            ProtectionSwitchRow(
                label = stringResource(R.string.label_leave_protection),
                checked = uiState.leaveProtectionEnabled,
                enabled = leaveControlEnabled,
                onCheckedChange = onToggleLeaveEnabled,
            )
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
                    text = stringResource(R.string.label_fall_stop_protection_status, statusText),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_leave_protection_status, leaveStatusText),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_fall_stop_protection_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_leave_protection_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun ProtectionSwitchRow(
    label: String,
    checked: Boolean,
    enabled: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
        )
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            enabled = enabled,
        )
    }
}
