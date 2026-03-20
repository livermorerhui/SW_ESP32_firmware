package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState

@Composable
fun WaveSection(
    uiState: UiState,
    onFreqInputChange: (String) -> Unit,
    onIntensityInputChange: (String) -> Unit,
    onPresetSelected: (Int) -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    val response = uiState.lastAckOrError ?: notAvailable

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.section_wave), style = MaterialTheme.typography.titleMedium)
            Text(
                text = stringResource(R.string.label_last_device_response, response),
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary,
            )

            OutlinedTextField(
                value = uiState.freqInput,
                onValueChange = onFreqInputChange,
                label = { Text(stringResource(R.string.field_frequency)) },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            OutlinedTextField(
                value = uiState.intensityInput,
                onValueChange = onIntensityInputChange,
                label = { Text(stringResource(R.string.field_intensity)) },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(20, 30, 40).forEach { preset ->
                    FilterChip(
                        selected = uiState.freq == preset,
                        onClick = { onPresetSelected(preset) },
                        label = { Text(stringResource(R.string.wave_preset_hz, preset)) },
                    )
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onStart) {
                    Text(stringResource(R.string.action_start))
                }
                Button(onClick = onStop) {
                    Text(stringResource(R.string.action_stop))
                }
            }
        }
    }
}
