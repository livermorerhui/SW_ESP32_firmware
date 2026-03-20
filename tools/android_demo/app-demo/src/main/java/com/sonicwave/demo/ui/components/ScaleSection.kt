package com.sonicwave.demo.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState

@Composable
fun ScaleSection(
    uiState: UiState,
    onZeroInputChange: (String) -> Unit,
    onFactorInputChange: (String) -> Unit,
    onZero: () -> Unit,
    onCalibrate: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    val calibrationZero = uiState.calibrationZero?.toString() ?: notAvailable
    val calibrationFactor = uiState.calibrationFactor?.toString() ?: notAvailable

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.section_scale), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.label_distance, uiState.distance?.toString() ?: notAvailable))
            Text(stringResource(R.string.label_weight, uiState.weight?.toString() ?: notAvailable))
            Text(stringResource(R.string.label_stable_weight, uiState.stableWeight?.toString() ?: notAvailable))
            Text(stringResource(R.string.label_calibration_state, calibrationZero, calibrationFactor))

            OutlinedTextField(
                value = uiState.zeroInput,
                onValueChange = onZeroInputChange,
                label = { Text(stringResource(R.string.field_zero)) },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            OutlinedTextField(
                value = uiState.factorInput,
                onValueChange = onFactorInputChange,
                label = { Text(stringResource(R.string.field_factor)) },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onZero) {
                    Text(stringResource(R.string.action_zero))
                }
                Button(onClick = onCalibrate) {
                    Text(stringResource(R.string.action_calibrate))
                }
            }
        }
    }
}
