package com.sonicwave.demo.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.FaultSeverityUi
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.demo.displayName

@Composable
fun SystemStatusSection(
    uiState: UiState,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    val faultColor = when (uiState.faultStatus.severity) {
        FaultSeverityUi.WARNING -> Color(0xFFD97706)
        FaultSeverityUi.BLOCKING -> Color(0xFFB91C1C)
        FaultSeverityUi.INFO -> Color(0xFF0F766E)
        FaultSeverityUi.NONE -> Color(0xFF166534)
    }
    val safetyColor = when (uiState.safetyStatus.severity) {
        FaultSeverityUi.WARNING -> Color(0xFFD97706)
        FaultSeverityUi.BLOCKING -> Color(0xFFB91C1C)
        FaultSeverityUi.INFO -> Color(0xFF0F766E)
        FaultSeverityUi.NONE -> Color(0xFF166534)
    }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.section_system_status), style = MaterialTheme.typography.titleMedium)
            Text(
                text = stringResource(R.string.label_system_status_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                StatusBox(
                    label = stringResource(R.string.label_state),
                    value = uiState.deviceState.displayName(),
                    secondary = uiState.deviceState.name,
                    modifier = Modifier.weight(1f),
                )
                StatusBox(
                    label = stringResource(R.string.label_fault),
                    value = uiState.faultStatus.label,
                    secondary = uiState.faultStatus.codeName,
                    background = faultColor.copy(alpha = 0.14f),
                    textColor = faultColor,
                    modifier = Modifier.weight(1f),
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                StatusBox(
                    label = stringResource(R.string.label_safety_reason),
                    value = uiState.safetyStatus.reason,
                    secondary = uiState.safetyStatus.reasonCode,
                    background = safetyColor.copy(alpha = 0.14f),
                    textColor = safetyColor,
                    modifier = Modifier.weight(1f),
                )
                StatusBox(
                    label = stringResource(R.string.label_safety_effect),
                    value = uiState.safetyStatus.effect,
                    secondary = uiState.safetyStatus.effectCode,
                    background = safetyColor.copy(alpha = 0.14f),
                    textColor = safetyColor,
                    modifier = Modifier.weight(1f),
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                StatusBox(
                    label = stringResource(R.string.label_safety_state),
                    value = uiState.safetyStatus.runtimeState,
                    secondary = uiState.safetyStatus.runtimeCode,
                    modifier = Modifier.weight(1f),
                )
                StatusBox(
                    label = stringResource(R.string.label_wave_state),
                    value = uiState.safetyStatus.waveState,
                    secondary = uiState.safetyStatus.waveCode,
                    modifier = Modifier.weight(1f),
                )
            }
            Text(
                text = stringResource(R.string.label_fault_code, uiState.faultStatus.code),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(
                    R.string.label_safety_meaning,
                    uiState.safetyStatus.meaning.ifBlank { stringResource(R.string.safety_meaning_none) },
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(
                    R.string.label_safety_source,
                    uiState.safetyStatus.source.ifBlank { stringResource(R.string.safety_source_none) },
                    uiState.safetyStatus.sourceCode.ifBlank { stringResource(R.string.common_not_available) },
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(
                    R.string.label_safety_code,
                    uiState.safetyStatus.code?.toString() ?: notAvailable,
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun StatusBox(
    label: String,
    value: String,
    secondary: String? = null,
    modifier: Modifier = Modifier,
    background: Color = MaterialTheme.colorScheme.secondaryContainer,
    textColor: Color = MaterialTheme.colorScheme.onSecondaryContainer,
) {
    Column(
        modifier = modifier
            .background(background, RoundedCornerShape(16.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = textColor.copy(alpha = 0.8f))
        Text(
            text = value,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = textColor,
        )
        secondary
            ?.takeIf { it.isNotBlank() && it != value }
            ?.let {
                Text(
                    text = it,
                    style = MaterialTheme.typography.bodySmall,
                    color = textColor.copy(alpha = 0.85f),
                )
            }
    }
}
