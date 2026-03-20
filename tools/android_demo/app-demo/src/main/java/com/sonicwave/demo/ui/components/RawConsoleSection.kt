package com.sonicwave.demo.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R

@Composable
fun RawConsoleSection(
    rawLogLines: List<String>,
    onClear: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var expanded by rememberSaveable { mutableStateOf(false) }
    val clipboard = LocalClipboardManager.current
    val logText = rawLogLines.joinToString(separator = "\n")

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(stringResource(R.string.section_raw_console), style = MaterialTheme.typography.titleMedium)
                Button(onClick = { expanded = !expanded }) {
                    Text(
                        if (expanded) {
                            stringResource(R.string.action_collapse)
                        } else {
                            stringResource(R.string.action_expand)
                        },
                    )
                }
            }

            if (expanded) {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = { clipboard.setText(AnnotatedString(logText)) },
                    ) {
                        Text(stringResource(R.string.action_copy))
                    }
                    Button(onClick = onClear) {
                        Text(stringResource(R.string.action_clear))
                    }
                }

                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 280.dp)
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .padding(8.dp)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    if (rawLogLines.isEmpty()) {
                        Text(
                            text = stringResource(R.string.raw_console_empty),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    } else {
                        rawLogLines.forEach { line ->
                            Text(
                                text = line,
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace,
                                fontWeight = if (
                                    line.contains("[FAULT]") ||
                                    line.contains("EVT:FAULT") ||
                                    line.contains("EVT:SAFETY")
                                ) {
                                    FontWeight.Bold
                                } else {
                                    FontWeight.Normal
                                },
                                color = consoleLineColor(line),
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun consoleLineColor(line: String) = when {
    "[TX]" in line -> MaterialTheme.colorScheme.primary
    "EVT:FAULT" in line || "[FAULT]" in line -> MaterialTheme.colorScheme.error
    "EVT:SAFETY" in line -> MaterialTheme.colorScheme.tertiary
    "EVT:STATE" in line -> MaterialTheme.colorScheme.tertiary
    "[SYS]" in line -> MaterialTheme.colorScheme.secondary
    "[RX]" in line || "[RX-RAW]" in line -> MaterialTheme.colorScheme.onSurface
    else -> MaterialTheme.colorScheme.onSurfaceVariant
}
