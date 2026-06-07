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
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
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
    compact: Boolean = false,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    var showEngineeringDetails by rememberSaveable { mutableStateOf(!compact) }
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
    val cardPadding = if (compact) 10.dp else 12.dp
    val contentSpacing = if (compact) 8.dp else 10.dp
    val statusBoxSpacing = if (compact) 8.dp else 12.dp

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(cardPadding),
            verticalArrangement = Arrangement.spacedBy(contentSpacing),
        ) {
            Text(
                stringResource(R.string.section_system_status),
                style = if (compact) {
                    MaterialTheme.typography.titleSmall
                } else {
                    MaterialTheme.typography.titleMedium
                },
            )
            if (!compact) {
                Text(
                    text = stringResource(R.string.label_system_status_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_system_status_primary_group),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            PrimaryStatusBox(
                label = stringResource(R.string.label_state),
                value = uiState.deviceState.displayName(),
                secondary = uiState.deviceState.name,
                compact = compact,
                modifier = Modifier.fillMaxWidth(),
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(statusBoxSpacing),
            ) {
                StatusBox(
                    label = stringResource(R.string.label_safety_reason),
                    value = uiState.safetyStatus.reason,
                    secondary = uiState.safetyStatus.reasonCode,
                    background = safetyColor.copy(alpha = 0.14f),
                    textColor = safetyColor,
                    compact = compact,
                    modifier = Modifier.weight(1f),
                )
                StatusBox(
                    label = stringResource(R.string.label_safety_effect),
                    value = uiState.safetyStatus.effect,
                    secondary = uiState.safetyStatus.effectCode,
                    background = safetyColor.copy(alpha = 0.14f),
                    textColor = safetyColor,
                    compact = compact,
                    modifier = Modifier.weight(1f),
                )
            }
            if (uiState.faultStatus.severity != FaultSeverityUi.NONE) {
                Text(
                    text = stringResource(
                        R.string.label_fault_reference,
                        uiState.faultStatus.label,
                        uiState.faultStatus.codeName,
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = faultColor,
                )
            }
            if (compact) {
                TextButton(onClick = { showEngineeringDetails = !showEngineeringDetails }) {
                    Text(
                        stringResource(
                            if (showEngineeringDetails) {
                                R.string.action_hide_details
                            } else {
                                R.string.action_show_details
                            },
                        ),
                    )
                }
            }
            if (showEngineeringDetails) {
                Text(
                    text = stringResource(R.string.label_system_status_secondary_group),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(statusBoxSpacing),
                ) {
                    ReferenceStatusBox(
                        label = stringResource(R.string.label_safety_state),
                        value = uiState.safetyStatus.runtimeState,
                        secondary = uiState.safetyStatus.runtimeCode,
                        compact = compact,
                        modifier = Modifier.weight(1f),
                    )
                    ReferenceStatusBox(
                        label = stringResource(R.string.label_wave_state),
                        value = uiState.safetyStatus.waveState,
                        secondary = uiState.safetyStatus.waveCode,
                        compact = compact,
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
}

@Composable
private fun PrimaryStatusBox(
    label: String,
    value: String,
    secondary: String? = null,
    compact: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val shape = RoundedCornerShape(if (compact) 12.dp else 18.dp)
    val horizontalPadding = if (compact) 12.dp else 14.dp
    val verticalPadding = if (compact) 10.dp else 14.dp
    val spacing = if (compact) 3.dp else 6.dp
    Column(
        modifier = modifier
            .background(MaterialTheme.colorScheme.primaryContainer, shape)
            .padding(horizontal = horizontalPadding, vertical = verticalPadding),
        verticalArrangement = Arrangement.spacedBy(spacing),
    ) {
        Text(
            text = label,
            style = if (compact) {
                MaterialTheme.typography.labelMedium
            } else {
                MaterialTheme.typography.labelLarge
            },
            color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f),
        )
        Text(
            text = value,
            style = if (compact) {
                MaterialTheme.typography.titleMedium
            } else {
                MaterialTheme.typography.headlineSmall
            },
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onPrimaryContainer,
        )
        secondary
            ?.takeIf { it.isNotBlank() && it != value }
            ?.let {
                Text(
                    text = it,
                    style = if (compact) {
                        MaterialTheme.typography.labelSmall
                    } else {
                        MaterialTheme.typography.bodySmall
                    },
                    color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.85f),
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
    compact: Boolean = false,
) {
    val shape = RoundedCornerShape(if (compact) 12.dp else 16.dp)
    val boxPadding = if (compact) 9.dp else 12.dp
    val spacing = if (compact) 3.dp else 6.dp
    Column(
        modifier = modifier
            .background(background, shape)
            .padding(boxPadding),
        verticalArrangement = Arrangement.spacedBy(spacing),
    ) {
        Text(
            label,
            style = if (compact) {
                MaterialTheme.typography.labelSmall
            } else {
                MaterialTheme.typography.labelMedium
            },
            color = textColor.copy(alpha = 0.8f),
        )
        Text(
            text = value,
            style = if (compact) {
                MaterialTheme.typography.titleSmall
            } else {
                MaterialTheme.typography.titleMedium
            },
            fontWeight = FontWeight.Bold,
            color = textColor,
        )
        secondary
            ?.takeIf { it.isNotBlank() && it != value }
            ?.let {
                Text(
                    text = it,
                    style = if (compact) {
                        MaterialTheme.typography.labelSmall
                    } else {
                        MaterialTheme.typography.bodySmall
                    },
                    color = textColor.copy(alpha = 0.85f),
                )
            }
    }
}

@Composable
private fun ReferenceStatusBox(
    label: String,
    value: String,
    secondary: String? = null,
    compact: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val shape = RoundedCornerShape(if (compact) 12.dp else 16.dp)
    val horizontalPadding = if (compact) 10.dp else 12.dp
    val verticalPadding = if (compact) 8.dp else 10.dp
    val spacing = if (compact) 3.dp else 4.dp
    Column(
        modifier = modifier
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.65f), shape)
            .padding(horizontal = horizontalPadding, vertical = verticalPadding),
        verticalArrangement = Arrangement.spacedBy(spacing),
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            text = value,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.onSurface,
        )
        secondary
            ?.takeIf { it.isNotBlank() && it != value }
            ?.let {
                Text(
                    text = it,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
    }
}
