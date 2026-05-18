package com.sonicwave.demo.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.text.KeyboardOptions
import com.sonicwave.demo.MotionSamplingExportRequest
import com.sonicwave.demo.MotionSamplingPrimaryLabel
import com.sonicwave.demo.MotionSamplingRowUi
import com.sonicwave.demo.MotionSamplingSessionUi
import com.sonicwave.demo.MotionSamplingSubLabel
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.demo.displayNameZh
import com.sonicwave.demo.displayName
import com.sonicwave.demo.safetyEffectLabel
import com.sonicwave.demo.safetyReasonLabel
import com.sonicwave.demo.waveStateLabel
import com.sonicwave.protocol.ProtocolMode
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import kotlin.math.max

@Composable
fun MotionSamplingSection(
    uiState: UiState,
    onStartSampling: () -> Unit,
    onStopSampling: () -> Unit,
    onEnableSamplingMode: () -> Unit,
    onDisableSamplingMode: () -> Unit,
    onClearSession: () -> Unit,
    onExportSession: (MotionSamplingExportRequest) -> Unit,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    val session = uiState.motionSamplingSession
    val rows = session?.rows.orEmpty()
    val rowCount = rows.size
    val latestRow = rows.lastOrNull()
    val primaryLabelOptions = listOf(
        MotionSamplingPrimaryLabelOption(
            value = MotionSamplingPrimaryLabel.NORMAL_USE,
            label = MotionSamplingPrimaryLabel.NORMAL_USE.displayNameZh(),
        ),
        MotionSamplingPrimaryLabelOption(
            value = MotionSamplingPrimaryLabel.LEAVE_PLATFORM,
            label = MotionSamplingPrimaryLabel.LEAVE_PLATFORM.displayNameZh(),
        ),
        MotionSamplingPrimaryLabelOption(
            value = MotionSamplingPrimaryLabel.DANGER_STATE,
            label = MotionSamplingPrimaryLabel.DANGER_STATE.displayNameZh(),
        ),
    )
    val subLabelOptions = listOf(
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.NORMAL_VIBRATION,
            label = MotionSamplingSubLabel.NORMAL_VIBRATION.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.LEAVE_PLATFORM,
            label = MotionSamplingSubLabel.LEAVE_PLATFORM.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.PARTIAL_LEAVE,
            label = MotionSamplingSubLabel.PARTIAL_LEAVE.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.FALL_ON_PLATFORM,
            label = MotionSamplingSubLabel.FALL_ON_PLATFORM.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.FALL_OFF_PLATFORM,
            label = MotionSamplingSubLabel.FALL_OFF_PLATFORM.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.LEFT_RIGHT_SWAY,
            label = MotionSamplingSubLabel.LEFT_RIGHT_SWAY.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.SQUAT_STAND,
            label = MotionSamplingSubLabel.SQUAT_STAND.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.RAPID_UNLOAD,
            label = MotionSamplingSubLabel.RAPID_UNLOAD.displayNameZh(),
        ),
        MotionSamplingSubLabelOption(
            value = MotionSamplingSubLabel.OTHER_DISTURBANCE,
            label = MotionSamplingSubLabel.OTHER_DISTURBANCE.displayNameZh(),
        ),
    )
    val hasStoppedUnexportedSession = session != null &&
        !uiState.isMotionSamplingActive &&
        rowCount > 0 &&
        session.lastExportCsvPath == null
    var showExportDialog by rememberSaveable(session?.sessionId) { mutableStateOf(false) }
    var pendingPrimaryLabel by rememberSaveable(session?.sessionId) {
        mutableStateOf(MotionSamplingPrimaryLabel.NORMAL_USE.name)
    }
    var pendingSubLabel by rememberSaveable(session?.sessionId) {
        mutableStateOf(MotionSamplingSubLabel.NORMAL_VIBRATION.name)
    }
    var exportNotes by rememberSaveable(session?.sessionId) { mutableStateOf("") }
    var showClearConfirm by rememberSaveable(session?.sessionId) { mutableStateOf(false) }
    var maPointsInput by rememberSaveable(session?.sessionId) { mutableStateOf(DEFAULT_MOTION_MA_POINTS.toString()) }
    var appliedMaPoints by rememberSaveable(session?.sessionId) { mutableStateOf(DEFAULT_MOTION_MA_POINTS) }
    val selectedPrimaryLabel = MotionSamplingPrimaryLabel.values().firstOrNull {
        it.name == pendingPrimaryLabel
    } ?: MotionSamplingPrimaryLabel.NORMAL_USE
    val selectedSubLabel = MotionSamplingSubLabel.values().firstOrNull {
        it.name == pendingSubLabel
    } ?: MotionSamplingSubLabel.NORMAL_VIBRATION
    val canConfirmExport = session != null
    val exportNotesPreview = exportNotes.trim()
    val distanceAverageSeries = buildMovingAverageSeries(rows, appliedMaPoints) { it.distanceMm }
    val liveWeightAverageSeries = buildMovingAverageSeries(rows, appliedMaPoints) { it.liveWeightKg }
    val latestDistanceAverage = distanceAverageSeries.lastOrNull()?.value
    val latestLiveWeightAverage = liveWeightAverageSeries.lastOrNull()?.value

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = stringResource(R.string.section_motion_sampling_tool),
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                text = stringResource(R.string.label_motion_sampling_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(R.string.label_motion_sampling_ma_research_only),
                style = MaterialTheme.typography.bodySmall,
                color = Color(0xFF9A3412),
            )
            Text(
                text = if (uiState.motionSamplingModeEnabled) {
                    stringResource(R.string.label_motion_sampling_mode_enabled)
                } else {
                    stringResource(R.string.label_motion_sampling_mode_disabled)
                },
                fontWeight = FontWeight.Bold,
                color = if (uiState.motionSamplingModeEnabled) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = onEnableSamplingMode,
                    enabled = uiState.isConnected &&
                        uiState.protocolMode == ProtocolMode.PRIMARY &&
                        !uiState.motionSamplingModeEnabled,
                    modifier = Modifier.weight(1f),
                ) {
                    Text(stringResource(R.string.action_enable_motion_sampling_mode))
                }
                OutlinedButton(
                    onClick = onDisableSamplingMode,
                    enabled = uiState.isConnected &&
                        uiState.protocolMode == ProtocolMode.PRIMARY &&
                        uiState.motionSamplingModeEnabled,
                    modifier = Modifier.weight(1f),
                ) {
                    Text(stringResource(R.string.action_disable_motion_sampling_mode))
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = onStartSampling,
                    enabled = !uiState.isMotionSamplingActive,
                    modifier = Modifier.weight(1f),
                ) {
                    Text(stringResource(R.string.action_start_motion_sampling))
                }
                Button(
                    onClick = onStopSampling,
                    enabled = uiState.isMotionSamplingActive,
                    modifier = Modifier.weight(1f),
                ) {
                    Text(stringResource(R.string.action_stop_motion_sampling))
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(
                    onClick = {
                        if (hasStoppedUnexportedSession) {
                            showClearConfirm = true
                        } else {
                            onClearSession()
                        }
                    },
                    enabled = !uiState.isMotionSamplingActive && session != null,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(
                        contentColor = Color(0xFFB91C1C),
                    ),
                    border = BorderStroke(1.dp, Color(0xFFB91C1C)),
                ) {
                    Text(stringResource(R.string.action_clear_motion_sampling_session))
                }
                Button(
                    onClick = {
                        showExportDialog = true
                    },
                    enabled = !uiState.isMotionSamplingActive && rowCount > 0,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF15803D),
                        contentColor = Color.White,
                        disabledContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                        disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    ),
                ) {
                    Text(stringResource(R.string.action_export_motion_sampling_session))
                }
            }
            if (hasStoppedUnexportedSession) {
                Text(
                    text = stringResource(R.string.label_motion_sampling_export_recommended),
                    style = MaterialTheme.typography.bodySmall,
                    color = Color(0xFFB45309),
                )
            }
            if (session != null && !uiState.isMotionSamplingActive && rowCount > 0) {
                Text(
                    text = stringResource(
                        if (session.lastExportCsvPath == null) {
                            R.string.label_motion_sampling_export_ready_pending
                        } else {
                            R.string.label_motion_sampling_export_ready_exported
                        },
                        rowCount,
                        session.waveFrequencyHz?.toString() ?: notAvailable,
                        session.waveIntensity?.toString() ?: notAvailable,
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (session.lastExportCsvPath == null) {
                        Color(0xFF0F766E)
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                )
            }

            Text(
                text = if (uiState.isMotionSamplingActive) {
                    stringResource(R.string.label_motion_sampling_state_active)
                } else {
                    stringResource(R.string.label_motion_sampling_state_inactive)
                },
                fontWeight = FontWeight.Bold,
                color = if (uiState.isMotionSamplingActive) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
            uiState.motionSamplingStatus?.let { status ->
                Text(
                    text = status,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = stringResource(R.string.label_motion_sampling_live_summary),
                    fontWeight = FontWeight.Bold,
                )
                Text(stringResource(R.string.label_distance, uiState.distance?.format2() ?: notAvailable))
                Text(stringResource(R.string.label_weight, uiState.weight?.format2() ?: notAvailable))
                Text(
                    stringResource(
                        R.string.label_stable_weight,
                        uiState.stableWeight?.takeIf { uiState.stableWeightActive }?.format2() ?: notAvailable,
                    ),
                )
                Text(
                    text = stringResource(
                        R.string.label_motion_sampling_measurement_validity,
                        if (uiState.streamWarning == null) {
                            stringResource(R.string.value_motion_sampling_measurement_valid)
                        } else {
                            stringResource(R.string.value_motion_sampling_measurement_invalid)
                        },
                    ),
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    text = stringResource(
                        R.string.label_motion_sampling_runtime_context,
                        uiState.deviceState.displayName(),
                        waveStateLabel(uiState.safetyStatus.waveCode),
                        safetyReasonLabel(uiState.safetyStatus.reasonCode),
                    ),
                    style = MaterialTheme.typography.bodySmall,
                )
            }

            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = stringResource(R.string.label_motion_sampling_session_summary),
                    fontWeight = FontWeight.Bold,
                )
                if (session == null) {
                    Text(
                        text = stringResource(R.string.label_motion_sampling_no_session),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                } else {
                    Text(stringResource(R.string.label_motion_sampling_session_id, session.sessionId))
                    Text(stringResource(R.string.label_motion_sampling_started_at, formatTimestamp(session.startedAtMs)))
                    Text(
                        stringResource(
                            R.string.label_motion_sampling_ended_at,
                            session.endedAtMs?.let(::formatTimestamp)
                                ?: stringResource(R.string.value_motion_sampling_in_progress),
                        ),
                    )
                    Text(stringResource(R.string.label_motion_sampling_row_count, rowCount))
                    session.waveFrequencyHz?.let { frequency ->
                        Text(
                            stringResource(
                                R.string.label_motion_sampling_wave_context,
                                frequency,
                                session.waveIntensity ?: 0,
                            ),
                        )
                    }
                    Text(
                        stringResource(
                            R.string.label_motion_sampling_session_flags,
                            if (session.samplingModeEnabled) {
                                stringResource(R.string.value_motion_sampling_flag_enabled)
                            } else {
                                stringResource(R.string.value_motion_sampling_flag_disabled)
                            },
                            if (session.waveWasRunningAtSessionStart) {
                                stringResource(R.string.value_motion_sampling_flag_yes)
                            } else {
                                stringResource(R.string.value_motion_sampling_flag_no)
                            },
                        ),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    session.exportScenarioLabel?.let { scenario ->
                        val exportPrimaryLabel = MotionSamplingPrimaryLabel.values()
                            .firstOrNull { it.name == session.exportScenarioCategory }
                            ?.displayNameZh()
                            ?: (session.exportScenarioCategory ?: notAvailable)
                        val exportSubLabel = MotionSamplingSubLabel.values()
                            .firstOrNull { it.name == scenario }
                            ?.displayNameZh()
                            ?: scenario
                        Text(
                            stringResource(
                                R.string.label_motion_sampling_export_scenario,
                                exportPrimaryLabel,
                                exportSubLabel,
                            ),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    session.lastExportCsvPath?.let { path ->
                        Text(
                            text = stringResource(R.string.label_motion_sampling_last_export_csv, path),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    session.lastExportJsonPath?.let { path ->
                        Text(
                            text = stringResource(R.string.label_motion_sampling_last_export_json, path),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }

            Text(
                text = stringResource(R.string.label_motion_sampling_chart_title),
                fontWeight = FontWeight.Bold,
            )
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = stringResource(R.string.label_motion_sampling_ma_controls),
                    fontWeight = FontWeight.Bold,
                )
                OutlinedTextField(
                    value = maPointsInput,
                    onValueChange = { value ->
                        val digitsOnly = value.filter(Char::isDigit)
                        maPointsInput = digitsOnly
                        digitsOnly.toIntOrNull()?.let { parsed ->
                            val clamped = parsed.coerceIn(MOTION_MA_MIN_POINTS, MOTION_MA_MAX_POINTS)
                            appliedMaPoints = clamped
                            if (parsed != clamped) {
                                maPointsInput = clamped.toString()
                            }
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = {
                        Text(stringResource(R.string.field_motion_sampling_ma_points))
                    },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    MOTION_MA_PRESETS.forEach { preset ->
                        FilterChip(
                            selected = appliedMaPoints == preset,
                            onClick = {
                                maPointsInput = preset.toString()
                                appliedMaPoints = preset
                            },
                            label = {
                                Text(stringResource(R.string.label_motion_sampling_ma_preset, preset))
                            },
                        )
                    }
                }
                Text(
                    text = stringResource(
                        R.string.label_motion_sampling_ma_points_hint,
                        MOTION_MA_MIN_POINTS,
                        MOTION_MA_MAX_POINTS,
                        appliedMaPoints,
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            MotionSamplingChart(
                rows = rows,
                appliedMaPoints = appliedMaPoints,
                distanceAverageSeries = distanceAverageSeries,
                liveWeightAverageSeries = liveWeightAverageSeries,
            )

            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = stringResource(R.string.label_motion_sampling_last_values),
                    fontWeight = FontWeight.Bold,
                )
                if (latestRow == null) {
                    Text(
                        text = stringResource(R.string.label_motion_sampling_row_preview_empty),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                } else {
                    Text(
                        text = stringResource(
                            R.string.label_motion_sampling_last_values_summary,
                            latestRow.distanceMm.format2(),
                            latestRow.liveWeightKg.format2(),
                            latestRow.stableWeightKg?.format2() ?: notAvailable,
                            safetyEffectLabel(latestRow.safetyStateCode),
                        ),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    Text(
                        text = stringResource(
                            R.string.label_motion_sampling_last_values_ma_summary,
                            appliedMaPoints,
                            latestDistanceAverage?.format2() ?: notAvailable,
                            latestLiveWeightAverage?.format2() ?: notAvailable,
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = stringResource(R.string.label_motion_sampling_row_preview_title),
                    fontWeight = FontWeight.Bold,
                )
                val previewRows = session?.rows?.takeLast(8).orEmpty()
                if (previewRows.isEmpty()) {
                    Text(
                        text = stringResource(R.string.label_motion_sampling_row_preview_empty),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                } else {
                    previewRows.forEach { row ->
                        Text(
                            text = stringResource(
                                R.string.label_motion_sampling_preview_row,
                                row.elapsedMs / 1000f,
                                row.distanceMm.format2(),
                                row.liveWeightKg.format2(),
                                row.runtimeStateCode,
                                row.safetyReasonCode,
                            ),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }

            Text(
                text = stringResource(R.string.label_motion_sampling_schema_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }

    if (showExportDialog && session != null) {
        AlertDialog(
            onDismissRequest = { showExportDialog = false },
            title = {
                Text(stringResource(R.string.title_motion_sampling_export_dialog))
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = stringResource(R.string.label_motion_sampling_export_dialog_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = stringResource(
                            R.string.label_motion_sampling_export_context,
                            session.waveFrequencyHz?.toString() ?: notAvailable,
                            session.waveIntensity?.toString() ?: notAvailable,
                            formatTimestamp(System.currentTimeMillis()),
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    MotionSamplingSelectionChipGroup(
                        title = stringResource(R.string.field_motion_sampling_export_primary_label),
                        options = primaryLabelOptions.map {
                            MotionSamplingSelectionOption(
                                key = it.value.name,
                                label = it.label,
                            )
                        },
                        selectedKey = selectedPrimaryLabel.name,
                        columns = 2,
                        onSelected = { pendingPrimaryLabel = it },
                    )
                    MotionSamplingSelectionChipGroup(
                        title = stringResource(R.string.field_motion_sampling_export_sub_label),
                        options = subLabelOptions.map {
                            MotionSamplingSelectionOption(
                                key = it.value.name,
                                label = it.label,
                            )
                        },
                        selectedKey = selectedSubLabel.name,
                        columns = 3,
                        onSelected = { pendingSubLabel = it },
                    )
                    OutlinedTextField(
                        value = exportNotes,
                        onValueChange = { value -> exportNotes = value },
                        modifier = Modifier.fillMaxWidth(),
                        label = {
                            Text(stringResource(R.string.field_motion_sampling_export_notes))
                        },
                        supportingText = {
                            Text(
                                stringResource(
                                    R.string.label_motion_sampling_export_notes_hint,
                                    stringResource(R.string.label_motion_sampling_export_notes_examples),
                                ),
                            )
                        },
                        minLines = 2,
                        maxLines = 3,
                    )
                    ExportReviewCard(
                        session = session,
                        rowCount = rowCount,
                        primaryLabel = selectedPrimaryLabel,
                        subLabel = selectedSubLabel,
                        notesPreview = exportNotesPreview,
                        notAvailable = notAvailable,
                    )
                }
            },
            confirmButton = {
                Button(
                    onClick = {
                        // 这里的复核块只帮助操作者确认即将导出的内容，
                        // 不会改动会话行数据、采样时序或运行时判定逻辑。
                        onExportSession(
                            MotionSamplingExportRequest(
                                primaryLabel = selectedPrimaryLabel,
                                subLabel = selectedSubLabel,
                                notes = exportNotes.trim(),
                                exportTimestampMs = System.currentTimeMillis(),
                            ),
                        )
                        showExportDialog = false
                    },
                    enabled = canConfirmExport,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF15803D),
                        contentColor = Color.White,
                    ),
                ) {
                    Text(stringResource(R.string.action_export_motion_sampling_session))
                }
            },
            dismissButton = {
                TextButton(onClick = { showExportDialog = false }) {
                    Text(stringResource(R.string.action_cancel))
                }
            },
        )
    }

    if (showClearConfirm && session != null) {
        AlertDialog(
            onDismissRequest = { showClearConfirm = false },
            title = {
                Text(stringResource(R.string.title_motion_sampling_clear_dialog))
            },
            text = {
                Text(stringResource(R.string.label_motion_sampling_clear_unexported_warning))
            },
            confirmButton = {
                Button(
                    onClick = {
                        onClearSession()
                        showClearConfirm = false
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFFB91C1C),
                        contentColor = Color.White,
                    ),
                ) {
                    Text(stringResource(R.string.action_confirm_clear))
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearConfirm = false }) {
                    Text(stringResource(R.string.action_cancel))
                }
            },
        )
    }
}

private data class MotionSamplingPrimaryLabelOption(
    val value: MotionSamplingPrimaryLabel,
    val label: String,
)

private data class MotionSamplingSubLabelOption(
    val value: MotionSamplingSubLabel,
    val label: String,
)

private data class MotionSamplingSelectionOption(
    val key: String,
    val label: String,
)

@Composable
private fun ExportReviewCard(
    session: MotionSamplingSessionUi,
    rowCount: Int,
    primaryLabel: MotionSamplingPrimaryLabel,
    subLabel: MotionSamplingSubLabel,
    notesPreview: String,
    notAvailable: String,
) {
    val notesLine = if (notesPreview.isBlank()) {
        stringResource(R.string.value_motion_sampling_export_notes_empty)
    } else {
        notesPreview.take(EXPORT_NOTES_PREVIEW_LIMIT)
    }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(14.dp))
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = stringResource(R.string.title_motion_sampling_export_review),
            fontWeight = FontWeight.Bold,
            style = MaterialTheme.typography.bodyMedium,
        )
        Text(
            text = stringResource(R.string.label_motion_sampling_export_review_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            text = stringResource(
                R.string.label_motion_sampling_export_review_labels,
                primaryLabel.displayNameZh(),
                subLabel.displayNameZh(),
            ),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(R.string.label_motion_sampling_export_review_notes, notesLine),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(
                R.string.label_motion_sampling_export_review_session,
                rowCount,
                session.waveFrequencyHz?.toString() ?: notAvailable,
                session.waveIntensity?.toString() ?: notAvailable,
            ),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(
                R.string.label_motion_sampling_export_review_filename,
                buildMotionExportPreviewPrefix(session, primaryLabel, subLabel),
            ),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(R.string.label_motion_sampling_export_review_metadata_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun MotionSamplingSelectionChipGroup(
    title: String,
    options: List<MotionSamplingSelectionOption>,
    selectedKey: String,
    columns: Int,
    onSelected: (String) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(title, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.bodySmall)
        options.chunked(columns).forEach { optionRow ->
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                optionRow.forEach { option ->
                    FilterChip(
                        selected = selectedKey == option.key,
                        onClick = { onSelected(option.key) },
                        label = {
                            Text(
                                text = option.label,
                                textAlign = TextAlign.Center,
                            )
                        },
                        modifier = Modifier.weight(1f),
                    )
                }
                repeat((columns - optionRow.size).coerceAtLeast(0)) {
                    Spacer(modifier = Modifier.weight(1f))
                }
            }
        }
    }
}

@Composable
private fun MotionSamplingChart(
    rows: List<MotionSamplingRowUi>,
    appliedMaPoints: Int,
    distanceAverageSeries: List<MotionChartPoint>,
    liveWeightAverageSeries: List<MotionChartPoint>,
) {
    val distanceColor = Color(0xFF0284C7)
    val weightColor = Color(0xFFC2410C)
    val gridColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)
    val distanceRawSeries = rows.map { MotionChartPoint(elapsedMs = it.elapsedMs, value = it.distanceMm) }
    val liveWeightRawSeries = rows.map { MotionChartPoint(elapsedMs = it.elapsedMs, value = it.liveWeightKg) }
    val distanceRange = motionValueRange((distanceRawSeries + distanceAverageSeries).map { it.value })
    val weightRange = motionValueRange((liveWeightRawSeries + liveWeightAverageSeries).map { it.value })

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MotionLegendChip(
                label = stringResource(R.string.legend_distance),
                detail = formatRange(distanceRange.first, distanceRange.second),
                color = distanceColor,
                modifier = Modifier.weight(1f),
            )
            MotionLegendChip(
                label = stringResource(R.string.legend_weight),
                detail = formatRange(weightRange.first, weightRange.second),
                color = weightColor,
                modifier = Modifier.weight(1f),
            )
        }
        Text(
            text = stringResource(R.string.label_motion_sampling_chart_overlay_hint, appliedMaPoints),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(220.dp)
                .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(16.dp))
                .padding(12.dp),
        ) {
            if (rows.size < 2) {
                Text(
                    text = stringResource(R.string.chart_motion_sampling_waiting_data),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Canvas(modifier = Modifier.fillMaxWidth().height(196.dp)) {
                    val width = size.width
                    val height = size.height
                    val lastElapsed = max(rows.last().elapsedMs, 1L)

                    for (index in 0..4) {
                        val y = height * index / 4f
                        drawLine(
                            color = gridColor,
                            start = Offset(0f, y),
                            end = Offset(width, y),
                            strokeWidth = 1f,
                        )
                    }

                    drawPath(
                        path = buildMotionPath(
                            points = distanceRawSeries,
                            width = width,
                            height = height,
                            maxElapsed = lastElapsed,
                            minValue = distanceRange.first,
                            maxValue = distanceRange.second,
                        ),
                        color = distanceColor.copy(alpha = 0.42f),
                        style = Stroke(width = 2f),
                    )

                    drawPath(
                        path = buildMotionPath(
                            points = distanceAverageSeries,
                            width = width,
                            height = height,
                            maxElapsed = lastElapsed,
                            minValue = distanceRange.first,
                            maxValue = distanceRange.second,
                        ),
                        color = distanceColor,
                        style = Stroke(width = 4f),
                    )

                    drawPath(
                        path = buildMotionPath(
                            points = liveWeightRawSeries,
                            width = width,
                            height = height,
                            maxElapsed = lastElapsed,
                            minValue = weightRange.first,
                            maxValue = weightRange.second,
                        ),
                        color = weightColor.copy(alpha = 0.42f),
                        style = Stroke(width = 2f),
                    )

                    drawPath(
                        path = buildMotionPath(
                            points = liveWeightAverageSeries,
                            width = width,
                            height = height,
                            maxElapsed = lastElapsed,
                            minValue = weightRange.first,
                            maxValue = weightRange.second,
                        ),
                        color = weightColor,
                        style = Stroke(width = 4f),
                    )
                }
            }
        }
    }
}

private data class MotionChartPoint(
    val elapsedMs: Long,
    val value: Float,
)

@Composable
private fun MotionLegendChip(
    label: String,
    detail: String,
    color: Color,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .background(color.copy(alpha = 0.12f), RoundedCornerShape(16.dp))
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(label, color = color, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.labelLarge)
        Text(detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

private fun buildMotionPath(
    points: List<MotionChartPoint>,
    width: Float,
    height: Float,
    maxElapsed: Long,
    minValue: Float,
    maxValue: Float,
): Path {
    val range = (maxValue - minValue).takeIf { it > 0f } ?: 1f
    val path = Path()
    points.forEachIndexed { index, point ->
        val x = width * point.elapsedMs.toFloat() / maxElapsed.toFloat()
        val normalized = (point.value - minValue) / range
        val y = height - (normalized * height)
        if (index == 0) {
            path.moveTo(x, y)
        } else {
            path.lineTo(x, y)
        }
    }
    return path
}

private fun motionValueRange(values: List<Float>): Pair<Float, Float> {
    if (values.isEmpty()) return 0f to 1f
    val min = values.minOrNull() ?: 0f
    val max = values.maxOrNull() ?: 1f
    return if (min == max) {
        (min - 1f) to (max + 1f)
    } else {
        min to max
    }
}

private fun formatRange(min: Float, max: Float): String {
    return "%.2f..%.2f".format(min, max)
}

private fun formatTimestamp(timestampMs: Long): String {
    return Instant.ofEpochMilli(timestampMs)
        .atZone(ZoneId.systemDefault())
        .format(MOTION_TIMESTAMP_FORMATTER)
}

private fun buildMovingAverageSeries(
    rows: List<MotionSamplingRowUi>,
    pointCount: Int,
    selector: (MotionSamplingRowUi) -> Float,
): List<MotionChartPoint> {
    if (rows.isEmpty()) return emptyList()
    val windowSize = pointCount.coerceIn(MOTION_MA_MIN_POINTS, MOTION_MA_MAX_POINTS)
    val window = ArrayDeque<Float>(windowSize)
    var runningSum = 0f
    return rows.map { row ->
        val value = selector(row)
        window.addLast(value)
        runningSum += value
        if (window.size > windowSize) {
            runningSum -= window.removeFirst()
        }
        MotionChartPoint(
            elapsedMs = row.elapsedMs,
            value = runningSum / window.size,
        )
    }
}

private fun Float.format2(): String = "%.2f".format(this)

private fun buildMotionExportPreviewPrefix(
    session: MotionSamplingSessionUi,
    primaryLabel: MotionSamplingPrimaryLabel,
    subLabel: MotionSamplingSubLabel,
): String {
    val primary = sanitizeMotionExportPreviewComponent(primaryLabel.displayNameZh())
    val sub = sanitizeMotionExportPreviewComponent(subLabel.displayNameZh())
    val frequency = "${session.waveFrequencyHz ?: 0}hz"
    val intensity = session.waveIntensity?.toString() ?: "0"
    return "${primary}_${sub}_${frequency}_${intensity}"
}

private fun sanitizeMotionExportPreviewComponent(raw: String): String {
    return raw
        .trim()
        .replace(Regex("[\\\\/:*?\"<>|\\s]+"), "_")
        .replace(Regex("_+"), "_")
        .trim('_')
        .ifBlank { "session" }
}

private val MOTION_TIMESTAMP_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
private const val DEFAULT_MOTION_MA_POINTS = 5
private const val MOTION_MA_MIN_POINTS = 1
private const val MOTION_MA_MAX_POINTS = 50
private val MOTION_MA_PRESETS = listOf(3, 5, 7)
private const val EXPORT_NOTES_PREVIEW_LIMIT = 48
