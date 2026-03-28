package com.sonicwave.demo.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonColors
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.demo.WaveStartAvailabilityUi
import com.sonicwave.demo.canStartWave
import com.sonicwave.demo.waveStartAvailability
import com.sonicwave.protocol.DeviceState
import kotlinx.coroutines.delay
import java.util.Locale

private val SelectedPresetOrange = Color(0xFFF59E0B)
private val SelectedPresetText = Color(0xFF402100)
private const val InputFieldWeight = 1.08f
private const val PresetWeight = 0.86f
private const val ActionWeight = 1.02f
private val CompactInputHeight = 44.dp

@Composable
fun WaveControlBottomBar(
    uiState: UiState,
    onFreqInputChange: (String) -> Unit,
    onIntensityInputChange: (String) -> Unit,
    onFreqInputCommit: () -> Unit,
    onIntensityInputCommit: () -> Unit,
    onFreqPresetSelected: (Int) -> Unit,
    onIntensityPresetSelected: (Int) -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val presetColors = FilterChipDefaults.filterChipColors(
        selectedContainerColor = SelectedPresetOrange,
        selectedLabelColor = SelectedPresetText,
    )

    val isRunning = uiState.deviceState == DeviceState.RUNNING
    val startAvailability = uiState.waveStartAvailability(hasPendingStartRequest = uiState.isWaveStartPending)
    val startEnabled = uiState.canStartWave(hasPendingStartRequest = uiState.isWaveStartPending)
    val stopEnabled = uiState.isConnected && isRunning
    val runtimeText by produceState(
        initialValue = formatWaveRuntime(currentWaveRuntimeElapsedMs(uiState, System.currentTimeMillis())),
        uiState.deviceState,
        uiState.waveRuntimeStartMs,
        uiState.waveRuntimeElapsedMs,
    ) {
        while (true) {
            val elapsedMs = currentWaveRuntimeElapsedMs(uiState, System.currentTimeMillis())
            value = formatWaveRuntime(elapsedMs)
            if (!isRunning || uiState.waveRuntimeStartMs == null) break
            val remainderMs = elapsedMs % RUNTIME_UPDATE_INTERVAL_MS
            delay(
                if (remainderMs == 0L) {
                    RUNTIME_UPDATE_INTERVAL_MS
                } else {
                    RUNTIME_UPDATE_INTERVAL_MS - remainderMs
                },
            )
        }
    }
    val hint = when (startAvailability) {
        WaveStartAvailabilityUi.DISCONNECTED -> stringResource(R.string.wave_bar_hint_disconnected)
        WaveStartAvailabilityUi.START_PENDING -> stringResource(R.string.wave_bar_hint_start_pending)
        WaveStartAvailabilityUi.RUNNING -> stringResource(R.string.wave_bar_hint_running)
        WaveStartAvailabilityUi.INVALID_PARAMETERS -> stringResource(R.string.wave_bar_hint_invalid_values)
        WaveStartAvailabilityUi.LEFT_PLATFORM_BLOCKED -> stringResource(R.string.wave_bar_hint_left_platform)
        WaveStartAvailabilityUi.ABNORMAL_STOP_BLOCKED -> stringResource(R.string.wave_bar_hint_abnormal_stop)
        WaveStartAvailabilityUi.SAFETY_BLOCKED ->
            stringResource(R.string.wave_bar_hint_recoverable_pause, uiState.safetyStatus.reason)
        WaveStartAvailabilityUi.NOT_READY -> stringResource(R.string.wave_bar_hint_not_ready)
        WaveStartAvailabilityUi.READY -> stringResource(R.string.wave_bar_hint_ready)
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .navigationBarsPadding(),
        tonalElevation = 4.dp,
        shadowElevation = 8.dp,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.surface)
                .padding(horizontal = 12.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = hint,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Text(
                text = stringResource(R.string.wave_bar_elapsed_runtime, runtimeText),
                style = MaterialTheme.typography.labelMedium,
                color = if (isRunning) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
                modifier = Modifier.align(Alignment.End),
            )

            WaveControlRow(
                value = uiState.freqInput,
                compactLabel = stringResource(R.string.wave_bar_compact_frequency),
                presets = listOf(20, 30, 40),
                selectedValue = uiState.freqInput.toIntOrNull(),
                onValueChange = onFreqInputChange,
                onValueCommit = onFreqInputCommit,
                onPresetSelected = onFreqPresetSelected,
                actionLabel = stringResource(R.string.action_start),
                actionEnabled = startEnabled,
                presetColors = presetColors,
                actionColors = ButtonDefaults.buttonColors(
                    containerColor = Color(0xFF15803D),
                    contentColor = Color.White,
                    disabledContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                    disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                ),
                onAction = onStart,
            )

            WaveControlRow(
                value = uiState.intensityInput,
                compactLabel = stringResource(R.string.wave_bar_compact_intensity),
                presets = listOf(60, 80, 100),
                selectedValue = uiState.intensityInput.toIntOrNull(),
                onValueChange = onIntensityInputChange,
                onValueCommit = onIntensityInputCommit,
                onPresetSelected = onIntensityPresetSelected,
                actionLabel = stringResource(R.string.action_stop),
                actionEnabled = stopEnabled,
                presetColors = presetColors,
                actionColors = ButtonDefaults.buttonColors(
                    containerColor = Color(0xFFB91C1C),
                    contentColor = Color.White,
                    disabledContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                    disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                ),
                onAction = onStop,
            )
        }
    }
}

@Composable
private fun WaveControlRow(
    value: String,
    compactLabel: String,
    presets: List<Int>,
    selectedValue: Int?,
    onValueChange: (String) -> Unit,
    onValueCommit: () -> Unit,
    onPresetSelected: (Int) -> Unit,
    actionLabel: String,
    actionEnabled: Boolean,
    presetColors: androidx.compose.material3.SelectableChipColors,
    actionColors: ButtonColors,
    onAction: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(modifier = Modifier.weight(InputFieldWeight)) {
            CompactWaveInputField(
                value = value,
                onValueChange = onValueChange,
                onValueCommit = onValueCommit,
                compactLabel = compactLabel,
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Number,
                    imeAction = ImeAction.Done,
                ),
                modifier = Modifier.fillMaxWidth(),
            )
        }

        presets.forEach { preset ->
            Box(modifier = Modifier.weight(PresetWeight)) {
                FilterChip(
                    selected = selectedValue == preset,
                    onClick = { onPresetSelected(preset) },
                    label = { Text(preset.toString()) },
                    colors = presetColors,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(CompactInputHeight),
                )
            }
        }

        Box(modifier = Modifier.weight(ActionWeight)) {
            Button(
                onClick = onAction,
                enabled = actionEnabled,
                colors = actionColors,
                modifier = Modifier.fillMaxWidth(),
                contentPadding = PaddingValues(horizontal = 10.dp, vertical = 14.dp),
            ) {
                Text(actionLabel)
            }
        }
    }
}

@Composable
private fun CompactWaveInputField(
    value: String,
    onValueChange: (String) -> Unit,
    onValueCommit: () -> Unit,
    compactLabel: String,
    keyboardOptions: KeyboardOptions,
    modifier: Modifier = Modifier,
    height: Dp = CompactInputHeight,
) {
    val colors = MaterialTheme.colorScheme
    val focusManager = LocalFocusManager.current
    val textStyle = MaterialTheme.typography.bodyMedium.merge(
        TextStyle(color = colors.onSurface),
    )
    var hadFocus by remember { mutableStateOf(false) }

    Surface(
        modifier = modifier.height(height),
        shape = MaterialTheme.shapes.medium,
        color = colors.surface,
        border = BorderStroke(1.dp, colors.outlineVariant),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(height),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .width(28.dp)
                    .background(colors.surfaceContainerHighest),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = compactVerticalLabel(compactLabel),
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.onSurfaceVariant,
                )
            }

            Box(
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 10.dp),
                contentAlignment = Alignment.CenterStart,
            ) {
                BasicTextField(
                    value = value,
                    onValueChange = onValueChange,
                    singleLine = true,
                    keyboardOptions = keyboardOptions,
                    keyboardActions = KeyboardActions(
                        onDone = {
                            onValueCommit()
                            focusManager.clearFocus()
                        },
                    ),
                    textStyle = textStyle,
                    cursorBrush = SolidColor(colors.primary),
                    modifier = Modifier
                        .fillMaxWidth()
                        // Commit on Done/focus loss so the view model can normalize if needed
                        // without forcing every keystroke into a final send-time range.
                        .onFocusChanged { focusState ->
                            if (focusState.isFocused) {
                                hadFocus = true
                            } else if (hadFocus) {
                                hadFocus = false
                                onValueCommit()
                            }
                        },
                )
            }
        }
    }
}

private fun compactVerticalLabel(label: String): String {
    return label.replace("|", "\n")
}

private fun currentWaveRuntimeElapsedMs(uiState: UiState, nowMs: Long): Long {
    val startMs = uiState.waveRuntimeStartMs
    return if (uiState.deviceState == DeviceState.RUNNING && startMs != null) {
        (nowMs - startMs).coerceAtLeast(0L)
    } else {
        uiState.waveRuntimeElapsedMs
    }
}

private fun formatWaveRuntime(elapsedMs: Long): String {
    val totalSeconds = (elapsedMs / 1000L).coerceAtLeast(0L)
    val hours = totalSeconds / 3600L
    val minutes = (totalSeconds % 3600L) / 60L
    val seconds = totalSeconds % 60L
    return if (hours > 0L) {
        String.format(Locale.US, "%02d:%02d:%02d", hours, minutes, seconds)
    } else {
        String.format(Locale.US, "%02d:%02d", minutes, seconds)
    }
}

private const val RUNTIME_UPDATE_INTERVAL_MS = 1000L
