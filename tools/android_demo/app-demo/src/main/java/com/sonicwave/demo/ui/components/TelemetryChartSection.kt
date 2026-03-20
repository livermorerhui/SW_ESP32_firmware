package com.sonicwave.demo.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.TelemetryPointUi
import kotlin.math.max

@Composable
fun TelemetryChartSection(
    telemetryPoints: List<TelemetryPointUi>,
    modifier: Modifier = Modifier,
) {
    val distanceColor = Color(0xFF0284C7)
    val weightColor = Color(0xFF7C3AED)
    val gridColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)
    val distanceRange = valueRange(telemetryPoints.map { it.distance })
    val weightRange = valueRange(telemetryPoints.map { it.weight })

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.section_telemetry_chart), style = MaterialTheme.typography.titleMedium)
            Text(
                text = stringResource(R.string.label_chart_window),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = stringResource(R.string.label_chart_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                LegendChip(
                    label = stringResource(R.string.legend_distance),
                    detail = formatRange(distanceRange.first, distanceRange.second),
                    color = distanceColor,
                    modifier = Modifier.weight(1f),
                )
                LegendChip(
                    label = stringResource(R.string.legend_weight),
                    detail = formatRange(weightRange.first, weightRange.second),
                    color = weightColor,
                    modifier = Modifier.weight(1f),
                )
            }
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(220.dp)
                    .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(16.dp))
                    .padding(12.dp),
            ) {
                if (telemetryPoints.size < 2) {
                    Text(
                        text = stringResource(R.string.chart_waiting_data),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                } else {
                    Canvas(modifier = Modifier.fillMaxWidth().height(196.dp)) {
                        val width = size.width
                        val height = size.height
                        val lastElapsed = max(telemetryPoints.last().elapsedMs, 1L)

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
                            path = buildPath(
                                points = telemetryPoints,
                                width = width,
                                height = height,
                                maxElapsed = lastElapsed,
                                selector = { it.distance },
                                minValue = distanceRange.first,
                                maxValue = distanceRange.second,
                            ),
                            color = distanceColor,
                            style = Stroke(width = 3f),
                        )

                        drawPath(
                            path = buildPath(
                                points = telemetryPoints,
                                width = width,
                                height = height,
                                maxElapsed = lastElapsed,
                                selector = { it.weight },
                                minValue = weightRange.first,
                                maxValue = weightRange.second,
                            ),
                            color = weightColor,
                            style = Stroke(width = 3f),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun LegendChip(
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

private fun valueRange(values: List<Float>): Pair<Float, Float> {
    if (values.isEmpty()) return 0f to 1f
    val min = values.minOrNull() ?: 0f
    val max = values.maxOrNull() ?: 1f
    return if (min == max) {
        (min - 1f) to (max + 1f)
    } else {
        min to max
    }
}

private fun buildPath(
    points: List<TelemetryPointUi>,
    width: Float,
    height: Float,
    maxElapsed: Long,
    selector: (TelemetryPointUi) -> Float,
    minValue: Float,
    maxValue: Float,
): Path {
    val range = (maxValue - minValue).takeIf { it > 0f } ?: 1f
    val path = Path()
    points.forEachIndexed { index, point ->
        val x = width * point.elapsedMs.toFloat() / maxElapsed.toFloat()
        val normalized = (selector(point) - minValue) / range
        val y = height - (normalized * height)
        if (index == 0) {
            path.moveTo(x, y)
        } else {
            path.lineTo(x, y)
        }
    }
    return path
}

private fun formatRange(min: Float, max: Float): String {
    return "%.2f..%.2f".format(min, max)
}
