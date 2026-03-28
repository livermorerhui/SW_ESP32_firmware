package com.sonicwave.demo.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.TestSessionExportRequest
import com.sonicwave.demo.TestSessionPrimaryLabel
import com.sonicwave.demo.TestSessionSecondaryLabel
import com.sonicwave.demo.TestSessionStatusUi
import com.sonicwave.demo.TestSessionUi
import com.sonicwave.demo.UiState
import com.sonicwave.demo.buildTestSessionExportFileName
import com.sonicwave.demo.buildTestSessionHzIntensityLabel
import com.sonicwave.demo.displayNameZh
import com.sonicwave.demo.formatTestSessionExportTimestamp
import com.sonicwave.demo.resolvedTestSessionExportTimestampMs
import com.sonicwave.demo.testSessionSecondaryLabelsFor
import kotlinx.coroutines.delay
import java.util.Locale

@Composable
fun TestSessionSection(
    uiState: UiState,
    onClearSession: () -> Unit,
    onExportSession: (TestSessionExportRequest) -> Unit,
    modifier: Modifier = Modifier,
) {
    val session = uiState.testSession
    val status = session?.status ?: TestSessionStatusUi.IDLE
    val sampleCount = session?.samples?.size ?: 0
    val canClear = session != null && status != TestSessionStatusUi.RECORDING
    val canExport = session != null &&
        status == TestSessionStatusUi.FINISHED &&
        session.samples.isNotEmpty()
    var showClearConfirm by remember(session?.sessionId) { mutableStateOf(false) }
    var showExportDialog by rememberSaveable(session?.sessionId) { mutableStateOf(false) }
    var pendingPrimaryLabel by rememberSaveable(session?.sessionId) {
        mutableStateOf(TestSessionPrimaryLabel.NORMAL_RHYTHM.name)
    }
    var pendingSecondaryLabel by rememberSaveable(session?.sessionId) {
        mutableStateOf(TestSessionSecondaryLabel.ADJUST_STANCE.name)
    }
    var exportNotes by rememberSaveable(session?.sessionId) { mutableStateOf("") }
    val selectedPrimaryLabel = TestSessionPrimaryLabel.entries.firstOrNull {
        it.name == pendingPrimaryLabel
    } ?: TestSessionPrimaryLabel.NORMAL_RHYTHM
    val availableSecondaryLabels = testSessionSecondaryLabelsFor(selectedPrimaryLabel)
    val selectedSecondaryLabel = availableSecondaryLabels.firstOrNull {
        it.name == pendingSecondaryLabel
    } ?: availableSecondaryLabels.first()
    val exportRequestPreview = session?.let {
        TestSessionExportRequest(
            primaryLabel = selectedPrimaryLabel,
            secondaryLabel = selectedSecondaryLabel,
            notes = exportNotes.trim(),
        )
    }
    val exportTimestampPreview = session?.let {
        formatTestSessionExportTimestamp(resolvedTestSessionExportTimestampMs(it))
    }.orEmpty()
    val hzIntensityPreview = session?.let {
        buildTestSessionHzIntensityLabel(it.summary.freqHz, it.summary.intensity)
    }.orEmpty()
    val csvPreviewName = session?.let { currentSession ->
        exportRequestPreview?.let { request ->
            buildTestSessionExportFileName(currentSession, request, "csv")
        }
    }.orEmpty()
    val jsonPreviewName = session?.let { currentSession ->
        exportRequestPreview?.let { request ->
            buildTestSessionExportFileName(currentSession, request, "json")
        }
    }.orEmpty()
    val durationText by produceState(
        initialValue = formatDuration(currentDurationMs(uiState)),
        status,
        session?.startedAtMs,
        session?.endedAtMs,
        session?.summary?.durationMs,
    ) {
        while (true) {
            value = formatDuration(currentDurationMs(uiState))
            if (status != TestSessionStatusUi.RECORDING || session == null) {
                break
            }
            delay(1000L)
        }
    }
    val statusText = when (status) {
        TestSessionStatusUi.IDLE -> stringResource(R.string.label_test_session_status_idle)
        TestSessionStatusUi.RECORDING -> stringResource(R.string.label_test_session_status_recording)
        TestSessionStatusUi.FINISHED -> stringResource(R.string.label_test_session_status_finished)
    }

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = stringResource(R.string.section_test_session),
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                text = stringResource(R.string.label_test_session_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(R.string.label_test_session_status, statusText),
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(
                text = stringResource(R.string.label_test_session_duration, durationText),
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(
                text = stringResource(R.string.label_test_session_sample_count, sampleCount),
                style = MaterialTheme.typography.bodyMedium,
            )
            session?.lastExportCsvPath?.let { path ->
                Text(
                    text = stringResource(R.string.label_test_session_last_export_csv, path),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            session?.lastExportJsonPath?.let { path ->
                Text(
                    text = stringResource(R.string.label_test_session_last_export_json, path),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            uiState.testSessionNotice?.let { message ->
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(
                    onClick = {
                        if (canClear) {
                            showClearConfirm = true
                        }
                    },
                    enabled = canClear,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFFB91C1C)),
                    border = BorderStroke(1.dp, Color(0xFFB91C1C)),
                ) {
                    Text(stringResource(R.string.action_clear_test_session))
                }
                Button(
                    onClick = {
                        if (canExport) {
                            showExportDialog = true
                        }
                    },
                    enabled = canExport,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF15803D),
                        contentColor = Color.White,
                        disabledContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                        disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    ),
                ) {
                    Text(stringResource(R.string.action_export_test_session))
                }
            }
        }
    }

    if (showExportDialog && session != null && exportRequestPreview != null) {
        var primaryExpanded by remember { mutableStateOf(false) }
        var secondaryExpanded by remember { mutableStateOf(false) }
        AlertDialog(
            onDismissRequest = { showExportDialog = false },
            title = { Text(stringResource(R.string.title_test_session_export_dialog)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = stringResource(R.string.label_test_session_export_dialog_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = stringResource(
                            R.string.label_test_session_export_context,
                            exportTimestampPreview,
                            hzIntensityPreview,
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    SessionLabelDropdown(
                        title = stringResource(R.string.field_test_session_export_primary_label),
                        value = selectedPrimaryLabel.displayNameZh(),
                        expanded = primaryExpanded,
                        onExpandedChange = { primaryExpanded = it },
                        options = TestSessionPrimaryLabel.entries.map { label ->
                            label.displayNameZh() to {
                                pendingPrimaryLabel = label.name
                                pendingSecondaryLabel = testSessionSecondaryLabelsFor(label).first().name
                                primaryExpanded = false
                            }
                        },
                    )
                    SessionLabelDropdown(
                        title = stringResource(R.string.field_test_session_export_secondary_label),
                        value = selectedSecondaryLabel.displayNameZh(),
                        expanded = secondaryExpanded,
                        onExpandedChange = { secondaryExpanded = it },
                        options = availableSecondaryLabels.map { label ->
                            label.displayNameZh() to {
                                pendingSecondaryLabel = label.name
                                secondaryExpanded = false
                            }
                        },
                    )
                    OutlinedTextField(
                        value = exportNotes,
                        onValueChange = { exportNotes = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text(stringResource(R.string.field_test_session_export_notes)) },
                        supportingText = {
                            Text(stringResource(R.string.label_test_session_export_notes_hint))
                        },
                        minLines = 1,
                        maxLines = 2,
                    )
                    TestSessionExportPreviewCard(
                        session = session,
                        primaryLabel = selectedPrimaryLabel,
                        secondaryLabel = selectedSecondaryLabel,
                        exportTimestampPreview = exportTimestampPreview,
                        hzIntensityPreview = hzIntensityPreview,
                        csvPreviewName = csvPreviewName,
                        jsonPreviewName = jsonPreviewName,
                    )
                }
            },
            confirmButton = {
                Button(
                    onClick = {
                        onExportSession(exportRequestPreview)
                        showExportDialog = false
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF15803D),
                        contentColor = Color.White,
                    ),
                ) {
                    Text(stringResource(R.string.action_export_test_session))
                }
            },
            dismissButton = {
                TextButton(onClick = { showExportDialog = false }) {
                    Text(stringResource(R.string.action_cancel))
                }
            },
        )
    }

    if (showClearConfirm) {
        AlertDialog(
            onDismissRequest = { showClearConfirm = false },
            title = { Text(stringResource(R.string.title_clear_test_session)) },
            text = { Text(stringResource(R.string.message_clear_test_session_confirm)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        showClearConfirm = false
                        onClearSession()
                    },
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

@Composable
private fun SessionLabelDropdown(
    title: String,
    value: String,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    options: List<Pair<String, () -> Unit>>,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Bold)
        Box(modifier = Modifier.fillMaxWidth()) {
            OutlinedButton(
                onClick = { onExpandedChange(!expanded) },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(value, modifier = Modifier.fillMaxWidth())
            }
            DropdownMenu(
                expanded = expanded,
                onDismissRequest = { onExpandedChange(false) },
            ) {
                options.forEach { (label, onClick) ->
                    DropdownMenuItem(
                        text = { Text(label) },
                        onClick = onClick,
                    )
                }
            }
        }
    }
}

@Composable
private fun TestSessionExportPreviewCard(
    session: TestSessionUi,
    primaryLabel: TestSessionPrimaryLabel,
    secondaryLabel: TestSessionSecondaryLabel,
    exportTimestampPreview: String,
    hzIntensityPreview: String,
    csvPreviewName: String,
    jsonPreviewName: String,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(14.dp))
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = stringResource(R.string.title_test_session_export_preview),
            fontWeight = FontWeight.Bold,
            style = MaterialTheme.typography.bodyMedium,
        )
        Text(
            text = stringResource(
                R.string.label_test_session_export_preview_labels,
                primaryLabel.displayNameZh(),
                secondaryLabel.displayNameZh(),
            ),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(
                R.string.label_test_session_export_preview_context,
                exportTimestampPreview,
                hzIntensityPreview,
            ),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(
                R.string.label_test_session_export_preview_session,
                session.samples.size,
                session.summary.freqHz?.let { String.format(Locale.US, "%.2f", it) } ?: "--",
                session.summary.intensity?.toString() ?: "--",
            ),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(R.string.label_test_session_export_preview_csv, csvPreviewName),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            text = stringResource(R.string.label_test_session_export_preview_json, jsonPreviewName),
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

private fun currentDurationMs(uiState: UiState): Long {
    val session = uiState.testSession ?: return 0L
    return when (session.status) {
        TestSessionStatusUi.IDLE -> 0L
        TestSessionStatusUi.RECORDING -> (System.currentTimeMillis() - session.startedAtMs).coerceAtLeast(0L)
        TestSessionStatusUi.FINISHED -> session.summary.durationMs
    }
}

private fun formatDuration(elapsedMs: Long): String {
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
