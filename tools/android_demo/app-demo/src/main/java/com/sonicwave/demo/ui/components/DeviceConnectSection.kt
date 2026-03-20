package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState

@Composable
fun DeviceConnectSection(
    uiState: UiState,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    val notifyStatus = if (uiState.notifyEnabled) {
        stringResource(R.string.notify_status_enabled)
    } else {
        stringResource(R.string.notify_status_disabled)
    }

    val notifyLine = if (uiState.notifyEnabled || uiState.notifyError.isNullOrBlank()) {
        stringResource(R.string.status_notify, notifyStatus)
    } else {
        stringResource(R.string.status_notify_with_error, notifyStatus, uiState.notifyError)
    }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text(stringResource(R.string.connection_status_title), style = MaterialTheme.typography.titleMedium)
            Text(
                text = uiState.statusLabel.ifBlank { stringResource(R.string.connection_state_disconnected) },
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
            )
            Text(stringResource(R.string.status_connected_device, uiState.connectedDeviceName ?: notAvailable))
            Text(notifyLine)
            Text(stringResource(R.string.status_protocol_mode, uiState.protocolMode.name))
            uiState.capabilityInfo?.let { capability ->
                Text(stringResource(R.string.status_capability_info, capability))
            }
            if (!uiState.streamWarning.isNullOrBlank()) {
                Text(
                    text = uiState.streamWarning.orEmpty(),
                    color = MaterialTheme.colorScheme.error,
                    fontWeight = FontWeight.Bold,
                )
            }
            Text(stringResource(R.string.status_last_ack_error, uiState.lastAckOrError ?: notAvailable))
        }
    }
}
