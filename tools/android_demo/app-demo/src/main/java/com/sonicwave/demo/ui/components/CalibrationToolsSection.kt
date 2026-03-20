package com.sonicwave.demo.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.CaptureFeedbackKind
import com.sonicwave.demo.CaptureFeedbackUi
import com.sonicwave.demo.CalibrationModelOptionUi
import com.sonicwave.demo.PreparedCalibrationModelSourceUi
import com.sonicwave.demo.PreparedCalibrationModelUi
import com.sonicwave.demo.R
import com.sonicwave.demo.UiState
import com.sonicwave.demo.WriteModelFeedbackKind
import com.sonicwave.demo.WriteModelFeedbackUi
import com.sonicwave.protocol.CalibrationComparisonResult
import com.sonicwave.protocol.CalibrationCurvePoint
import com.sonicwave.protocol.CalibrationFitResult
import com.sonicwave.protocol.CalibrationModelType
import java.util.Locale
import kotlin.math.max

@Composable
fun CalibrationToolsSection(
    uiState: UiState,
    onZeroInputChange: (String) -> Unit,
    onFactorInputChange: (String) -> Unit,
    onCaptureReferenceChange: (String) -> Unit,
    onModelReferenceChange: (String) -> Unit,
    onModelC0Change: (String) -> Unit,
    onModelC1Change: (String) -> Unit,
    onModelC2Change: (String) -> Unit,
    onModelTypeChange: (CalibrationModelType) -> Unit,
    onZero: () -> Unit,
    onCalibrate: () -> Unit,
    onCapturePoint: () -> Unit,
    onStartRecording: () -> Unit,
    onStopRecording: () -> Unit,
    onGetModel: () -> Unit,
    onSetModel: () -> Unit,
    onCalibrationZero: () -> Unit,
    onToggleEngineeringSection: () -> Unit,
    onToggleVerboseStreamLogs: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val notAvailable = stringResource(R.string.common_not_available)
    val filteredPoints = uiState.calibrationPoints.filter {
        it.distanceMm != null &&
            it.referenceWeightKg != null &&
            it.validFlag != false
    }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.section_calibration_tools), style = MaterialTheme.typography.titleMedium)
            Text(
                text = stringResource(R.string.label_calibration_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(R.string.label_calibration_workflow),
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = stringResource(R.string.label_calibration_workflow_steps),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(stringResource(R.string.label_distance, uiState.distance?.format2() ?: notAvailable))
            Text(stringResource(R.string.label_weight, uiState.weight?.format2() ?: notAvailable))
            Text(stringResource(R.string.label_stable_weight, uiState.stableWeight?.format2() ?: notAvailable))
            Text(
                stringResource(
                    R.string.label_calibration_state,
                    uiState.calibrationZero?.format2() ?: notAvailable,
                    uiState.calibrationFactor?.format4() ?: notAvailable,
                ),
            )

            SectionTitle(stringResource(R.string.label_calibration_step_zero))
            Button(onClick = onZero) {
                Text(stringResource(R.string.action_zero))
            }
            Text(
                text = stringResource(R.string.label_zero_explanation),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            SectionTitle(stringResource(R.string.label_calibration_step_recording))
            Button(
                onClick = onStartRecording,
                enabled = uiState.isConnected && !uiState.isRecording,
            ) {
                Text(stringResource(R.string.action_start_recording))
            }
            Text(
                text = if (uiState.isRecording) {
                    stringResource(R.string.label_recording_state_active)
                } else {
                    stringResource(R.string.label_recording_state_inactive)
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (uiState.isRecording) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
            uiState.recordingStatus?.let { status ->
                Text(
                    text = status,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            uiState.recordingDestination?.let { destination ->
                Text(
                    text = stringResource(R.string.label_recording_destination, destination),
                    style = MaterialTheme.typography.bodySmall,
                )
            }

            SectionTitle(stringResource(R.string.label_calibration_step_capture))
            CapturePreconditionChecklist(uiState = uiState)
            CaptureResultSummary(uiState = uiState)
            OutlinedTextField(
                value = uiState.captureReferenceInput,
                onValueChange = onCaptureReferenceChange,
                label = { Text(stringResource(R.string.field_reference_weight)) },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            Text(
                text = stringResource(
                    R.string.label_capture_live_distance,
                    uiState.distance?.takeIf { it.isFinite() }?.let { "${it.format2()} mm" } ?: notAvailable,
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Button(
                onClick = onCapturePoint,
                enabled = uiState.canCaptureCalibrationPoint,
            ) {
                Text(stringResource(R.string.action_capture_cal_point))
            }
            Text(
                text = if (uiState.canCaptureCalibrationPoint) {
                    stringResource(R.string.label_capture_hint_active)
                } else {
                    stringResource(R.string.label_capture_hint_requires_recording)
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(R.string.label_capture_semantics_explanation),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CaptureResultCard(feedback = uiState.captureStatus)

            CalibrationPointSummary(uiState = uiState)
            CalibrationPointTable(points = uiState.calibrationPoints)

            SectionDivider()
            SectionTitle(stringResource(R.string.label_calibration_step_compare))
            Text(
                text = stringResource(R.string.label_model_comparison_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CalibrationComparisonChart(
                comparisonResult = uiState.comparisonResult,
                points = filteredPoints,
                selectedModel = uiState.selectedComparisonModel,
            )
            ModelComparisonSummary(
                comparisonResult = uiState.comparisonResult,
                selectedModel = uiState.selectedComparisonModel,
            )

            SectionDivider()
            SectionTitle(stringResource(R.string.label_calibration_step_stop_recording))
            Text(
                text = stringResource(R.string.label_stop_recording_recommended_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Button(
                onClick = onStopRecording,
                enabled = uiState.isRecording,
            ) {
                Text(stringResource(R.string.action_stop_recording))
            }
            Text(
                text = if (uiState.isRecording) {
                    stringResource(R.string.label_stop_recording_active_hint)
                } else {
                    stringResource(R.string.label_stop_recording_inactive_hint)
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (uiState.isRecording) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )

            SectionDivider()
            SectionTitle(stringResource(R.string.label_calibration_step_select_model))
            Text(
                text = stringResource(R.string.label_model_selection_behavior),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            ModelSelectionOptions(
                options = uiState.modelOptions,
                onModelTypeChange = onModelTypeChange,
            )
            PreparedModelSummaryCard(
                preparedModel = uiState.preparedModel,
                comparisonResult = uiState.comparisonResult,
                selectedModel = uiState.selectedComparisonModel,
            )

            SectionDivider()
            SectionTitle(stringResource(R.string.label_calibration_step_apply))
            Text(
                text = stringResource(R.string.label_apply_step_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = if (uiState.isRecording) {
                    stringResource(R.string.label_apply_step_recording_warning)
                } else {
                    stringResource(R.string.label_apply_step_ready_after_stop)
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (uiState.isRecording) {
                    Color(0xFFB45309)
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
            Text(
                text = stringResource(R.string.label_set_model_primary_explanation),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            WriteModelStatusSummary(feedback = uiState.writeModelStatus)
            val writeButtonColors = when (uiState.writeModelStatus?.kind) {
                WriteModelFeedbackKind.SUCCESS -> ButtonDefaults.buttonColors(containerColor = Color(0xFF0F766E))
                WriteModelFeedbackKind.FAILURE -> ButtonDefaults.buttonColors(containerColor = Color(0xFFB91C1C))
                WriteModelFeedbackKind.PENDING -> ButtonDefaults.buttonColors(containerColor = Color(0xFFB45309))
                null -> ButtonDefaults.buttonColors()
            }
            Button(
                onClick = onSetModel,
                enabled = uiState.preparedModel != null,
                colors = writeButtonColors,
            ) {
                Text(
                    when (uiState.writeModelStatus?.kind) {
                        WriteModelFeedbackKind.PENDING -> stringResource(R.string.action_set_model_pending)
                        else -> stringResource(R.string.action_set_model)
                    },
                )
            }
            Text(
                text = stringResource(R.string.label_apply_boundary_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            SectionDivider()
            SectionTitle(stringResource(R.string.label_advanced_engineering_title))
            Text(
                text = stringResource(R.string.label_advanced_engineering_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Button(onClick = onToggleEngineeringSection) {
                Text(
                    stringResource(
                        if (uiState.isEngineeringSectionExpanded) {
                            R.string.action_hide_advanced_engineering
                        } else {
                            R.string.action_show_advanced_engineering
                        },
                    ),
                )
            }
            if (uiState.isEngineeringSectionExpanded) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onGetModel) {
                        Text(stringResource(R.string.action_get_model))
                    }
                    Button(onClick = onCalibrationZero) {
                        Text(stringResource(R.string.action_cal_zero))
                    }
                }
                Text(
                    text = stringResource(R.string.label_model_buttons_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_get_model_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_cal_zero_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                OutlinedTextField(
                    value = uiState.modelRefInput,
                    onValueChange = onModelReferenceChange,
                    label = { Text(stringResource(R.string.field_model_reference)) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                )
                Text(
                    text = stringResource(R.string.label_model_reference_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                OutlinedTextField(
                    value = uiState.modelC0Input,
                    onValueChange = onModelC0Change,
                    label = { Text(stringResource(R.string.field_model_c0)) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                )
                Text(
                    text = stringResource(R.string.label_model_c0_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                OutlinedTextField(
                    value = uiState.modelC1Input,
                    onValueChange = onModelC1Change,
                    label = { Text(stringResource(R.string.field_model_c1)) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                )
                Text(
                    text = stringResource(R.string.label_model_c1_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                OutlinedTextField(
                    value = uiState.modelC2Input,
                    onValueChange = onModelC2Change,
                    label = { Text(stringResource(R.string.field_model_c2)) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                )
                Text(
                    text = stringResource(R.string.label_model_c2_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = stringResource(R.string.label_manual_override_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                FilterChip(
                    selected = uiState.verboseStreamLogsEnabled,
                    onClick = onToggleVerboseStreamLogs,
                    label = {
                        Text(stringResource(R.string.label_verbose_stream_logs_chip))
                    },
                )
                Text(
                    text = if (uiState.verboseStreamLogsEnabled) {
                        stringResource(R.string.label_verbose_stream_logs_enabled_hint)
                    } else {
                        stringResource(R.string.label_verbose_stream_logs_disabled_hint)
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                uiState.latestModel?.let { model ->
                    val modelTypeLabel = when (model.type) {
                        CalibrationModelType.LINEAR -> stringResource(R.string.model_type_linear)
                        CalibrationModelType.QUADRATIC -> stringResource(R.string.model_type_quadratic)
                    }
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(
                            text = stringResource(R.string.label_current_model),
                            fontWeight = FontWeight.Bold,
                        )
                        Text(
                            text = stringResource(
                                R.string.label_model_summary,
                                "$modelTypeLabel (${model.type.name})",
                                model.referenceDistance.format2(),
                                model.c0.format6(),
                                model.c1.format6(),
                                model.c2.format6(),
                            ),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
                SectionDivider()
                Text(
                    text = stringResource(R.string.label_legacy_calibration_title),
                    fontWeight = FontWeight.Bold,
                )
                Text(
                    text = stringResource(R.string.label_calibrate_explanation),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
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
                Button(onClick = onCalibrate) {
                    Text(stringResource(R.string.action_calibrate))
                }
            }
        }
    }
}

@Composable
private fun WriteModelStatusSummary(feedback: WriteModelFeedbackUi?) {
    if (feedback == null) return

    val highlightColor = when (feedback.kind) {
        WriteModelFeedbackKind.SUCCESS -> Color(0xFF0F766E)
        WriteModelFeedbackKind.FAILURE -> Color(0xFFB91C1C)
        WriteModelFeedbackKind.PENDING -> Color(0xFFB45309)
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .border(1.dp, highlightColor.copy(alpha = 0.35f), RoundedCornerShape(14.dp))
            .background(highlightColor.copy(alpha = 0.08f), RoundedCornerShape(14.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = when (feedback.kind) {
                WriteModelFeedbackKind.SUCCESS -> stringResource(R.string.label_write_model_result_success_title)
                WriteModelFeedbackKind.FAILURE -> stringResource(R.string.label_write_model_result_failure_title)
                WriteModelFeedbackKind.PENDING -> stringResource(R.string.label_write_model_result_pending_title)
            },
            fontWeight = FontWeight.Bold,
            color = highlightColor,
        )
        Text(
            text = feedback.message,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface,
        )
        feedback.rawReason?.let { rawReason ->
            Text(
                text = stringResource(R.string.label_write_model_raw_reason, rawReason),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun ModelSelectionOptions(
    options: List<CalibrationModelOptionUi>,
    onModelTypeChange: (CalibrationModelType) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEach { option ->
            ModelSelectionCard(
                option = option,
                onClick = { onModelTypeChange(option.type) },
            )
        }
    }
}

@Composable
private fun ModelSelectionCard(
    option: CalibrationModelOptionUi,
    onClick: () -> Unit,
) {
    val label = when (option.type) {
        CalibrationModelType.LINEAR -> stringResource(R.string.model_type_linear)
        CalibrationModelType.QUADRATIC -> stringResource(R.string.model_type_quadratic)
    }
    val borderColor = when {
        option.selected -> MaterialTheme.colorScheme.primary
        option.available -> MaterialTheme.colorScheme.outline
        else -> MaterialTheme.colorScheme.outline.copy(alpha = 0.45f)
    }
    val backgroundColor = when {
        option.selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)
        option.available -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
        else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.20f)
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .border(1.5.dp, borderColor, RoundedCornerShape(14.dp))
            .background(backgroundColor, RoundedCornerShape(14.dp))
            .clickable(onClick = onClick)
            .padding(12.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                text = label,
                fontWeight = FontWeight.Bold,
                color = if (option.selected) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
            )
            Text(
                text = if (option.available) {
                    stringResource(R.string.label_model_option_ready)
                } else {
                    stringResource(R.string.label_model_option_not_ready)
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Column(
            horizontalAlignment = androidx.compose.ui.Alignment.End,
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            if (option.selected) {
                Text(
                    text = stringResource(R.string.label_model_option_selected),
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary,
                )
            }
            if (option.prepared) {
                Text(
                    text = stringResource(R.string.label_model_option_prepared),
                    style = MaterialTheme.typography.bodySmall,
                    color = Color(0xFF0F766E),
                )
            }
        }
    }
}

@Composable
private fun PreparedModelSummaryCard(
    preparedModel: PreparedCalibrationModelUi?,
    comparisonResult: CalibrationComparisonResult?,
    selectedModel: CalibrationModelType,
) {
    val selectedFit = when (selectedModel) {
        CalibrationModelType.LINEAR -> comparisonResult?.linear
        CalibrationModelType.QUADRATIC -> comparisonResult?.quadratic
    }
    val waitingText = if (selectedFit == null || selectedFit.isAvailable) {
        null
    } else {
        stringResource(
            R.string.label_model_fit_insufficient,
            selectedFit.sampleCount,
            selectedFit.requiredPointCount,
        )
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.3f), RoundedCornerShape(14.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.30f), RoundedCornerShape(14.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(
            text = stringResource(R.string.label_prepared_model_title),
            fontWeight = FontWeight.Bold,
        )
        if (preparedModel == null) {
            Text(
                text = waitingText ?: stringResource(R.string.label_prepared_model_unavailable),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        } else {
            val modelTypeLabel = when (preparedModel.type) {
                CalibrationModelType.LINEAR -> stringResource(R.string.model_type_linear)
                CalibrationModelType.QUADRATIC -> stringResource(R.string.model_type_quadratic)
            }
            val sourceLabel = when (preparedModel.source) {
                PreparedCalibrationModelSourceUi.AUTO_SELECTED_FIT -> stringResource(R.string.label_prepared_model_source_auto)
                PreparedCalibrationModelSourceUi.MANUAL_OVERRIDE -> stringResource(R.string.label_prepared_model_source_manual)
            }
            Text(
                text = stringResource(R.string.label_prepared_model_summary, modelTypeLabel, sourceLabel),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface,
            )
            Text(
                text = stringResource(
                    R.string.label_model_fit_coefficients,
                    preparedModel.referenceDistance.format2(),
                    preparedModel.c0.format6(),
                    preparedModel.c1.format6(),
                    preparedModel.c2.format6(),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun CapturePreconditionChecklist(uiState: UiState) {
    val referenceWeight = uiState.captureReferenceInput.toFloatOrNull()
    val referenceValid = referenceWeight != null && referenceWeight >= 0.0f
    val liveDistanceAvailable = uiState.distance != null && uiState.distance.isFinite()
    val liveSampleQualityVisible = uiState.distance != null &&
        uiState.weight != null &&
        uiState.distance.isFinite() &&
        uiState.weight.isFinite() &&
        uiState.streamWarning == null
    val requiredItems = listOf(
        stringResource(R.string.label_capture_condition_recording) to uiState.isRecording,
        stringResource(R.string.label_capture_condition_connected) to uiState.isConnected,
        stringResource(R.string.label_capture_condition_reference) to referenceValid,
        stringResource(R.string.label_capture_condition_live_distance) to liveDistanceAvailable,
    )
    val advisoryItems = listOf(
        stringResource(R.string.label_capture_condition_stable_advisory) to uiState.stableWeightActive,
        stringResource(R.string.label_capture_condition_live_sample_advisory) to liveSampleQualityVisible,
    )

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.25f), RoundedCornerShape(14.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f), RoundedCornerShape(14.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(
            text = stringResource(R.string.label_capture_preconditions_title),
            fontWeight = FontWeight.Bold,
        )
        Text(
            text = stringResource(
                R.string.label_capture_button_state,
                if (uiState.canCaptureCalibrationPoint) {
                    stringResource(R.string.value_capture_button_enabled)
                } else {
                    stringResource(R.string.value_capture_button_disabled)
                },
            ),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            text = stringResource(R.string.label_capture_required_conditions),
            fontWeight = FontWeight.Bold,
            style = MaterialTheme.typography.bodySmall,
        )
        requiredItems.forEach { (label, satisfied) ->
            CaptureConditionRow(label = label, satisfied = satisfied)
        }
        Text(
            text = stringResource(R.string.label_capture_advisory_conditions),
            fontWeight = FontWeight.Bold,
            style = MaterialTheme.typography.bodySmall,
        )
        advisoryItems.forEach { (label, satisfied) ->
            CaptureConditionRow(label = label, satisfied = satisfied)
        }
        Text(
            text = stringResource(R.string.label_capture_preconditions_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun CaptureConditionRow(
    label: String,
    satisfied: Boolean,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(
            text = if (satisfied) "[\u2713] $label" else "[ ] $label",
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = if (satisfied) {
                stringResource(R.string.value_check_ok)
            } else {
                stringResource(R.string.value_check_missing)
            },
            style = MaterialTheme.typography.bodySmall,
            color = if (satisfied) {
                Color(0xFF0F766E)
            } else {
                MaterialTheme.colorScheme.onSurfaceVariant
            },
        )
    }
}

@Composable
private fun CaptureResultSummary(uiState: UiState) {
    val resultLabel = when (uiState.captureStatus?.kind) {
        CaptureFeedbackKind.PENDING -> stringResource(R.string.value_capture_result_pending)
        CaptureFeedbackKind.SUCCESS -> stringResource(R.string.value_capture_result_success)
        CaptureFeedbackKind.FAILURE -> stringResource(R.string.value_capture_result_failure)
        else -> stringResource(R.string.value_capture_result_none)
    }
    val currentLabel = if (uiState.canCaptureCalibrationPoint) {
        stringResource(R.string.value_capture_button_enabled)
    } else {
        stringResource(R.string.value_capture_button_disabled)
    }

    Column(
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = stringResource(R.string.label_capture_summary_title),
            fontWeight = FontWeight.Bold,
        )
        Text(
            text = stringResource(R.string.label_capture_summary_result, resultLabel),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            text = stringResource(R.string.label_capture_summary_current, currentLabel),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun CaptureResultCard(feedback: CaptureFeedbackUi?) {
    if (feedback == null) return

    val highlightColor = when (feedback.kind) {
        CaptureFeedbackKind.SUCCESS -> Color(0xFF0F766E)
        CaptureFeedbackKind.FAILURE -> Color(0xFFB91C1C)
        CaptureFeedbackKind.PENDING -> Color(0xFFB45309)
        CaptureFeedbackKind.INFO -> MaterialTheme.colorScheme.outline
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .border(1.dp, highlightColor.copy(alpha = 0.35f), RoundedCornerShape(14.dp))
            .background(highlightColor.copy(alpha = 0.08f), RoundedCornerShape(14.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = stringResource(R.string.label_capture_result_title),
            fontWeight = FontWeight.Bold,
            color = highlightColor,
        )
        Text(
            text = feedback.message,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface,
        )
        feedback.rawReason?.let { rawReason ->
            Text(
                text = stringResource(R.string.label_capture_raw_reason, rawReason),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun SectionTitle(title: String) {
    Text(
        text = title,
        style = MaterialTheme.typography.titleSmall,
        fontWeight = FontWeight.Bold,
    )
}

@Composable
private fun SectionDivider() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)),
    )
}

@Composable
private fun CalibrationPointSummary(uiState: UiState) {
    val count = uiState.calibrationPoints.size
    Column(
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = stringResource(R.string.label_recorded_point_count, count),
            fontWeight = FontWeight.Bold,
        )
        uiState.latestCalibrationPoint?.let { point ->
            Text(
                text = stringResource(
                    R.string.label_latest_point_summary,
                    (point.index ?: count).toString(),
                    point.distanceMm?.format2() ?: "-",
                    point.referenceWeightKg?.format2() ?: "-",
                    point.predictedWeightKg?.format2() ?: "-",
                    point.stableFlag.toDisplayFlag(),
                    point.validFlag.toDisplayFlag(),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        } ?: Text(
            text = stringResource(R.string.label_no_calibration_points),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun CalibrationPointTable(points: List<com.sonicwave.demo.CalibrationPointUi>) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            TableHeaderCell(stringResource(R.string.label_point_index), Modifier.width(36.dp))
            TableHeaderCell(stringResource(R.string.label_point_distance_mm), Modifier.weight(1f))
            TableHeaderCell(stringResource(R.string.label_point_reference_weight), Modifier.weight(1f))
            TableHeaderCell(stringResource(R.string.label_point_predicted_weight), Modifier.weight(1f))
            TableHeaderCell(stringResource(R.string.label_point_flags), Modifier.width(68.dp))
        }
        if (points.isEmpty()) {
            Text(
                text = stringResource(R.string.label_no_calibration_points),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        } else {
            points.forEach { point ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(
                            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f),
                            RoundedCornerShape(10.dp),
                        )
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    TableBodyCell((point.index ?: 0).toString(), Modifier.width(36.dp))
                    TableBodyCell(point.distanceMm?.format2() ?: "-", Modifier.weight(1f))
                    TableBodyCell(point.referenceWeightKg?.format2() ?: "-", Modifier.weight(1f))
                    TableBodyCell(point.predictedWeightKg?.format2() ?: "-", Modifier.weight(1f))
                    TableBodyCell(
                        text = "${point.stableFlag.toDisplayFlag()}/${point.validFlag.toDisplayFlag()}",
                        modifier = Modifier.width(68.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun TableHeaderCell(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        modifier = modifier,
        style = MaterialTheme.typography.labelSmall,
        fontWeight = FontWeight.Bold,
    )
}

@Composable
private fun TableBodyCell(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        modifier = modifier,
        style = MaterialTheme.typography.bodySmall,
    )
}

@Composable
private fun CalibrationComparisonChart(
    comparisonResult: CalibrationComparisonResult?,
    points: List<com.sonicwave.demo.CalibrationPointUi>,
    selectedModel: CalibrationModelType,
) {
    val linearColor = Color(0xFF0F766E)
    val quadraticColor = Color(0xFFB45309)
    val pointColor = Color(0xFF2563EB)
    val gridColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.25f)
    val chartPoints = points.mapNotNull { point ->
        val x = point.distanceMm
        val y = point.referenceWeightKg
        if (x == null || y == null) {
            null
        } else {
            Offset(x, y)
        }
    }
    val linearCurve = comparisonResult?.linear?.curvePoints.orEmpty()
    val quadraticCurve = comparisonResult?.quadratic?.curvePoints.orEmpty()
    val allX = buildList {
        addAll(chartPoints.map { it.x })
        addAll(linearCurve.map { it.distanceMm })
        addAll(quadraticCurve.map { it.distanceMm })
    }
    val allY = buildList {
        addAll(chartPoints.map { it.y })
        addAll(linearCurve.map { it.predictedWeightKg })
        addAll(quadraticCurve.map { it.predictedWeightKg })
    }
    val xRange = floatRange(allX)
    val yRange = floatRange(allY)

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            LegendPill(
                label = stringResource(R.string.legend_calibration_points),
                color = pointColor,
                modifier = Modifier.weight(1f),
            )
            LegendPill(
                label = stringResource(R.string.model_type_linear),
                color = linearColor,
                modifier = Modifier.weight(1f),
            )
            LegendPill(
                label = stringResource(R.string.model_type_quadratic),
                color = quadraticColor,
                modifier = Modifier.weight(1f),
            )
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(240.dp)
                .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(16.dp))
                .padding(12.dp),
        ) {
            if (chartPoints.isEmpty()) {
                Text(
                    text = stringResource(R.string.chart_waiting_calibration_points),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Canvas(modifier = Modifier.fillMaxWidth().height(216.dp)) {
                    val width = size.width
                    val height = size.height

                    for (index in 0..4) {
                        val x = width * index / 4f
                        val y = height * index / 4f
                        drawLine(
                            color = gridColor,
                            start = Offset(x, 0f),
                            end = Offset(x, height),
                            strokeWidth = 1f,
                        )
                        drawLine(
                            color = gridColor,
                            start = Offset(0f, y),
                            end = Offset(width, y),
                            strokeWidth = 1f,
                        )
                    }

                    if (linearCurve.isNotEmpty()) {
                        drawPath(
                            path = buildCurvePath(linearCurve, width, height, xRange, yRange),
                            color = linearColor.copy(alpha = if (selectedModel == CalibrationModelType.LINEAR) 1f else 0.45f),
                            style = Stroke(width = if (selectedModel == CalibrationModelType.LINEAR) 4f else 2f),
                        )
                    }
                    if (quadraticCurve.isNotEmpty()) {
                        drawPath(
                            path = buildCurvePath(quadraticCurve, width, height, xRange, yRange),
                            color = quadraticColor.copy(alpha = if (selectedModel == CalibrationModelType.QUADRATIC) 1f else 0.45f),
                            style = Stroke(width = if (selectedModel == CalibrationModelType.QUADRATIC) 4f else 2f),
                        )
                    }

                    chartPoints.forEach { point ->
                        drawCircle(
                            color = pointColor,
                            radius = 7f,
                            center = Offset(
                                x = normalize(point.x, xRange.first, xRange.second, width),
                                y = invertNormalize(point.y, yRange.first, yRange.second, height),
                            ),
                        )
                    }
                }
            }
        }

        Text(
            text = stringResource(
                R.string.label_scatter_axis_summary,
                xRange.first.format2(),
                xRange.second.format2(),
                yRange.first.format2(),
                yRange.second.format2(),
            ),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun LegendPill(
    label: String,
    color: Color,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .background(color.copy(alpha = 0.12f), RoundedCornerShape(14.dp))
            .padding(horizontal = 10.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(
            modifier = Modifier
                .width(12.dp)
                .height(12.dp)
                .background(color, RoundedCornerShape(6.dp)),
        )
        Text(
            text = label,
            color = color,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.Bold,
        )
    }
}

@Composable
private fun ModelComparisonSummary(
    comparisonResult: CalibrationComparisonResult?,
    selectedModel: CalibrationModelType,
) {
    if (comparisonResult == null) {
        Text(
            text = stringResource(R.string.label_comparison_waiting_points),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        return
    }

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        ModelSummaryCard(
            fit = comparisonResult.linear,
            selected = selectedModel == CalibrationModelType.LINEAR,
            title = stringResource(R.string.model_type_linear),
        )
        ModelSummaryCard(
            fit = comparisonResult.quadratic,
            selected = selectedModel == CalibrationModelType.QUADRATIC,
            title = stringResource(R.string.model_type_quadratic),
        )
    }
}

@Composable
private fun ModelSummaryCard(
    fit: CalibrationFitResult,
    selected: Boolean,
    title: String,
) {
    val highlightColor = if (fit.type == CalibrationModelType.LINEAR) {
        Color(0xFF0F766E)
    } else {
        Color(0xFFB45309)
    }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .border(
                width = if (selected) 2.dp else 1.dp,
                color = highlightColor.copy(alpha = if (selected) 0.75f else 0.3f),
                shape = RoundedCornerShape(14.dp),
            )
            .background(
                highlightColor.copy(alpha = if (selected) 0.12f else 0.06f),
                RoundedCornerShape(14.dp),
            )
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = if (selected) {
                stringResource(R.string.label_model_summary_selected, title)
            } else {
                title
            },
            fontWeight = FontWeight.Bold,
            color = highlightColor,
        )
        if (fit.isAvailable) {
            val coefficients = fit.coefficients!!
            val metrics = fit.metrics!!
            Text(
                text = stringResource(R.string.label_model_fit_points, metrics.pointCount),
                style = MaterialTheme.typography.bodySmall,
            )
            Text(
                text = stringResource(R.string.label_model_fit_mae, metrics.meanAbsoluteErrorKg.format3()),
                style = MaterialTheme.typography.bodySmall,
            )
            Text(
                text = stringResource(R.string.label_model_fit_max, metrics.maxAbsoluteErrorKg.format3()),
                style = MaterialTheme.typography.bodySmall,
            )
            Text(
                text = stringResource(
                    R.string.label_model_fit_monotonic,
                    if (fit.monotonic == true) {
                        stringResource(R.string.value_monotonic_yes)
                    } else {
                        stringResource(R.string.value_monotonic_no)
                    },
                ),
                style = MaterialTheme.typography.bodySmall,
            )
            Text(
                text = stringResource(
                    R.string.label_model_fit_coefficients,
                    coefficients.referenceDistance.format4(),
                    coefficients.c0.format6(),
                    coefficients.c1.format6(),
                    coefficients.c2.format6(),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        } else {
            Text(
                text = stringResource(
                    R.string.label_model_fit_insufficient,
                    fit.sampleCount,
                    fit.requiredPointCount,
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private fun buildCurvePath(
    curvePoints: List<CalibrationCurvePoint>,
    width: Float,
    height: Float,
    xRange: Pair<Float, Float>,
    yRange: Pair<Float, Float>,
): Path {
    val path = Path()
    curvePoints.forEachIndexed { index, point ->
        val x = normalize(point.distanceMm, xRange.first, xRange.second, width)
        val y = invertNormalize(point.predictedWeightKg, yRange.first, yRange.second, height)
        if (index == 0) {
            path.moveTo(x, y)
        } else {
            path.lineTo(x, y)
        }
    }
    return path
}

private fun floatRange(values: List<Float>): Pair<Float, Float> {
    if (values.isEmpty()) return 0f to 1f
    val min = values.minOrNull() ?: 0f
    val max = values.maxOrNull() ?: 1f
    return if (min == max) {
        (min - 1f) to (max + 1f)
    } else {
        min to max
    }
}

private fun normalize(value: Float, min: Float, max: Float, size: Float): Float {
    val range = (max - min).takeIf { it > 0f } ?: 1f
    return ((value - min) / range) * size
}

private fun invertNormalize(value: Float, min: Float, max: Float, size: Float): Float {
    return size - normalize(value, min, max, size)
}

private fun Boolean?.toDisplayFlag(): String = if (this == true) "Y" else "N"

private fun Float.format2(): String = String.format(Locale.US, "%.2f", this)

private fun Float.format3(): String = String.format(Locale.US, "%.3f", this)

private fun Float.format4(): String = String.format(Locale.US, "%.4f", this)

private fun Float.format6(): String = String.format(Locale.US, "%.6f", this)
