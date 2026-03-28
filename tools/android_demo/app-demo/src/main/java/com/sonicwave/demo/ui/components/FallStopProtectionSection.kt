package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
    modifier: Modifier = Modifier,
) {
    val controlEnabled = uiState.isConnected &&
        uiState.protocolMode == ProtocolMode.PRIMARY &&
        uiState.fallStopStateKnown &&
        !uiState.isFallStopSyncInProgress
    val statusText = when {
        uiState.isFallStopSyncInProgress -> stringResource(R.string.value_fall_stop_protection_syncing)
        !uiState.fallStopStateKnown -> stringResource(R.string.value_fall_stop_protection_unknown)
        uiState.fallStopEnabled -> stringResource(R.string.value_fall_stop_protection_enabled)
        else -> stringResource(R.string.value_fall_stop_protection_disabled)
    }

    Card(modifier = modifier.fillMaxWidth()) {
        androidx.compose.foundation.layout.Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = stringResource(R.string.section_fall_stop_protection),
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                text = stringResource(R.string.label_fall_stop_protection_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(
                    text = stringResource(R.string.label_fall_stop_protection_status, statusText),
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.primary,
                )
                Switch(
                    checked = uiState.fallStopEnabled,
                    onCheckedChange = onToggleEnabled,
                    enabled = controlEnabled,
                )
            }
        }
    }
}
