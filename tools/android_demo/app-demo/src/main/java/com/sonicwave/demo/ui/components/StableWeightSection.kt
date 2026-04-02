package com.sonicwave.demo.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
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
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.demo.hasStableWeightEvidence

@Composable
fun StableWeightSection(
    uiState: UiState,
    modifier: Modifier = Modifier,
) {
    val active = uiState.stableWeightActive && uiState.stableWeight != null
    val held = !active && uiState.hasStableWeightEvidence()
    val background = if (active) {
        Color(0xFFDCFCE7)
    } else if (held) {
        Color(0xFFFEF3C7)
    } else {
        MaterialTheme.colorScheme.surfaceVariant
    }
    val contentColor = if (active) {
        Color(0xFF166534)
    } else if (held) {
        Color(0xFF92400E)
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(background, RoundedCornerShape(16.dp))
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text(
                text = stringResource(R.string.section_stable_weight),
                style = MaterialTheme.typography.titleMedium,
                color = contentColor,
            )
            Text(
                text = uiState.stableWeight?.let { stringResource(R.string.value_weight_kg, it) }
                    ?: stringResource(R.string.common_not_available),
                style = MaterialTheme.typography.displaySmall,
                fontWeight = FontWeight.Bold,
                color = contentColor,
            )
            Text(
                text = when {
                    active -> stringResource(R.string.stable_indicator_active)
                    held -> stringResource(R.string.stable_indicator_held)
                    else -> stringResource(R.string.stable_indicator_idle)
                },
                style = MaterialTheme.typography.bodySmall,
                color = contentColor,
            )
        }
    }
}
