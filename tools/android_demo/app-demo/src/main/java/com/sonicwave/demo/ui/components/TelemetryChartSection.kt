package com.sonicwave.demo.ui.components

import androidx.annotation.StringRes
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
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.sonicwave.demo.R
import com.sonicwave.demo.TelemetryPointUi
import kotlin.math.max

private enum class TelemetryUnit {
    MM,
    KG,
}

private data class TelemetrySeriesDefinition(
    @StringRes val labelRes: Int,
    val color: Color,
    val unit: TelemetryUnit,
    val strokeWidth: Float = 3f,
    val selector: (TelemetryPointUi) -> Float?,
)

private data class TelemetrySummaryDefinition(
    val series: TelemetrySeriesDefinition,
    val latestValue: Float?,
    val showsWindow: Boolean = true,
)

@Composable
fun TelemetryChartSection(
    telemetryPoints: List<TelemetryPointUi>,
    stableWeight: Float?,
    stableWeightActive: Boolean,
    modifier: Modifier = Modifier,
) {
    var showStableWeight by rememberSaveable { mutableStateOf(true) }
    // Test operators mostly focus on the stable baseline plus MA overlays first,
    // so the noisier live rhythm channels start hidden until explicitly enabled.
    var showRhythmDistance by rememberSaveable { mutableStateOf(false) }
    var showRhythmWeight by rememberSaveable { mutableStateOf(false) }
    var showMa3 by rememberSaveable { mutableStateOf(true) }
    var showMa5 by rememberSaveable { mutableStateOf(true) }
    var showMa7 by rememberSaveable { mutableStateOf(true) }

    val stableWeightSeries = TelemetrySeriesDefinition(
        labelRes = R.string.legend_stable_weight,
        color = Color(0xFF15803D),
        unit = TelemetryUnit.KG,
        strokeWidth = 2.8f,
        selector = { it.stableWeight },
    )
    val rhythmDistanceSeries = TelemetrySeriesDefinition(
        labelRes = R.string.legend_rhythm_distance,
        color = Color(0xFF0284C7),
        unit = TelemetryUnit.MM,
        strokeWidth = 3.2f,
        selector = { it.rhythmDistance },
    )
    val rhythmWeightSeries = TelemetrySeriesDefinition(
        labelRes = R.string.legend_rhythm_weight,
        color = Color(0xFFC2410C),
        unit = TelemetryUnit.KG,
        strokeWidth = 3.2f,
        selector = { it.rhythmWeight },
    )
    val ma3Series = TelemetrySeriesDefinition(
        labelRes = R.string.legend_ma3,
        color = Color(0xFFEA580C),
        unit = TelemetryUnit.KG,
        strokeWidth = 2.4f,
        selector = { it.ma3 },
    )
    val ma5Series = TelemetrySeriesDefinition(
        labelRes = R.string.legend_ma5,
        color = Color(0xFFDC2626),
        unit = TelemetryUnit.KG,
        strokeWidth = 2.4f,
        selector = { it.ma5 },
    )
    val ma7Series = TelemetrySeriesDefinition(
        labelRes = R.string.legend_ma7,
        color = Color(0xFF0F766E),
        unit = TelemetryUnit.KG,
        strokeWidth = 2.4f,
        selector = { it.ma7 },
    )

    // Keep the stable-weight box bound to the active baseline latch so it stays
    // visible through vibration, but clears as soon as leave-platform semantics
    // explicitly clear the current baseline in view-model state.
    val rhythmDistanceSummary = TelemetrySummaryDefinition(
        series = rhythmDistanceSeries,
        latestValue = telemetryPoints.lastNotNullOfOrNull(rhythmDistanceSeries.selector),
    )
    val rhythmWeightSummary = TelemetrySummaryDefinition(
        series = rhythmWeightSeries,
        latestValue = telemetryPoints.lastNotNullOfOrNull(rhythmWeightSeries.selector),
    )
    val stableWeightSummary = TelemetrySummaryDefinition(
        series = stableWeightSeries,
        latestValue = stableWeight.takeIf { stableWeightActive },
        showsWindow = false,
    )
    val ma3Summary = TelemetrySummaryDefinition(
        series = ma3Series,
        latestValue = telemetryPoints.lastNotNullOfOrNull(ma3Series.selector),
    )
    val ma5Summary = TelemetrySummaryDefinition(
        series = ma5Series,
        latestValue = telemetryPoints.lastNotNullOfOrNull(ma5Series.selector),
    )
    val ma7Summary = TelemetrySummaryDefinition(
        series = ma7Series,
        latestValue = telemetryPoints.lastNotNullOfOrNull(ma7Series.selector),
    )
    val summaryRows = listOf(
        listOf(rhythmDistanceSummary, rhythmWeightSummary),
        listOf(stableWeightSummary, ma3Summary),
        listOf(ma5Summary, ma7Summary),
    )

    val showDistanceFamily = showRhythmDistance
    val visibleWeightSeries = buildList {
        if (showStableWeight) add(stableWeightSeries)
        if (showRhythmWeight) add(rhythmWeightSeries)
        if (showMa3) add(ma3Series)
        if (showMa5) add(ma5Series)
        if (showMa7) add(ma7Series)
    }

    val gridColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)
    val distanceRange = valueRange(telemetryPoints.mapNotNull(rhythmDistanceSeries.selector))
    val weightRange = valueRange(
        visibleWeightSeries.flatMap { series ->
            telemetryPoints.mapNotNull(series.selector)
        },
    )

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
            Text(
                text = stringResource(R.string.label_chart_ma_source),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            summaryRows.forEach { summaries ->
                SummaryRow(
                    summaries = summaries,
                    telemetryPoints = telemetryPoints,
                )
            }
            Text(
                text = stringResource(R.string.label_chart_overlay_controls),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            // Keep all six toggles visible during live testing instead of relying
            // on a horizontally scrollable selector row.
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OverlayChipRow(
                    chips = listOf(
                        OverlayChipUi(
                            label = stringResource(stableWeightSeries.labelRes),
                            selected = showStableWeight,
                            onClick = { showStableWeight = !showStableWeight },
                        ),
                        OverlayChipUi(
                            label = stringResource(rhythmDistanceSeries.labelRes),
                            selected = showRhythmDistance,
                            onClick = { showRhythmDistance = !showRhythmDistance },
                        ),
                        OverlayChipUi(
                            label = stringResource(rhythmWeightSeries.labelRes),
                            selected = showRhythmWeight,
                            onClick = { showRhythmWeight = !showRhythmWeight },
                        ),
                    ),
                )
                OverlayChipRow(
                    chips = listOf(
                        OverlayChipUi(
                            label = stringResource(ma3Series.labelRes),
                            selected = showMa3,
                            onClick = { showMa3 = !showMa3 },
                        ),
                        OverlayChipUi(
                            label = stringResource(ma5Series.labelRes),
                            selected = showMa5,
                            onClick = { showMa5 = !showMa5 },
                        ),
                        OverlayChipUi(
                            label = stringResource(ma7Series.labelRes),
                            selected = showMa7,
                            onClick = { showMa7 = !showMa7 },
                        ),
                    ),
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

                        // The view model keeps only the recent 20-second window in memory.
                        // Rendering against that same timestamp domain keeps the data spread
                        // across the full width instead of bunching up at the right edge.
                        val domainStartTimestampMs = telemetryPoints.firstOrNull()?.timestampMs ?: 0L
                        val domainEndTimestampMs = max(
                            telemetryPoints.lastOrNull()?.timestampMs ?: 1L,
                            domainStartTimestampMs + 1L,
                        )

                        for (index in 0..4) {
                            val y = height * index / 4f
                            drawLine(
                                color = gridColor,
                                start = Offset(0f, y),
                                end = Offset(width, y),
                                strokeWidth = 1f,
                            )
                        }

                        if (showDistanceFamily) {
                            drawPath(
                                path = buildPath(
                                    points = telemetryPoints,
                                    width = width,
                                    height = height,
                                    domainStartTimestampMs = domainStartTimestampMs,
                                    domainEndTimestampMs = domainEndTimestampMs,
                                    selector = rhythmDistanceSeries.selector,
                                    minValue = distanceRange.first,
                                    maxValue = distanceRange.second,
                                ),
                                color = rhythmDistanceSeries.color,
                                style = Stroke(width = rhythmDistanceSeries.strokeWidth),
                            )
                        }

                        visibleWeightSeries.forEach { series ->
                            drawPath(
                                path = buildPath(
                                    points = telemetryPoints,
                                    width = width,
                                    height = height,
                                    domainStartTimestampMs = domainStartTimestampMs,
                                    domainEndTimestampMs = domainEndTimestampMs,
                                    selector = series.selector,
                                    minValue = weightRange.first,
                                    maxValue = weightRange.second,
                                ),
                                color = series.color,
                                style = Stroke(width = series.strokeWidth),
                            )
                        }
                    }
                }
            }
        }
    }
}

private data class OverlayChipUi(
    val label: String,
    val selected: Boolean,
    val onClick: () -> Unit,
)

@Composable
private fun OverlayChipRow(
    chips: List<OverlayChipUi>,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        chips.forEach { chip ->
            OverlayChip(
                label = chip.label,
                selected = chip.selected,
                onClick = chip.onClick,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun SummaryRow(
    summaries: List<TelemetrySummaryDefinition>,
    telemetryPoints: List<TelemetryPointUi>,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        summaries.forEach { summary ->
            SummaryBox(
                label = formatSummaryLabel(
                    label = stringResource(summary.series.labelRes),
                    unit = summary.series.unit,
                ),
                latestValue = summary.latestValue,
                range = nullableValueRange(telemetryPoints, summary.series.selector),
                showsWindow = summary.showsWindow,
                color = summary.series.color,
                unit = summary.series.unit,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun SummaryBox(
    label: String,
    latestValue: Float?,
    range: Pair<Float, Float>?,
    showsWindow: Boolean,
    color: Color,
    unit: TelemetryUnit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .background(color.copy(alpha = 0.12f), RoundedCornerShape(16.dp))
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(label, color = color, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.labelLarge)
        Text(
            text = stringResource(
                if (showsWindow) {
                    R.string.label_telemetry_summary_latest
                } else {
                    R.string.label_telemetry_summary_baseline
                },
                formatTelemetryValue(latestValue, unit),
            ),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface,
        )
        if (showsWindow) {
            Text(
                text = stringResource(
                    R.string.label_telemetry_summary_range,
                    formatTelemetryRange(range, unit),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun OverlayChip(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    FilterChip(
        modifier = modifier,
        selected = selected,
        onClick = onClick,
        label = {
            Text(
                text = label,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
}

private fun valueRange(values: List<Float>): Pair<Float, Float> {
    val finiteValues = values.filter(Float::isFinite)
    if (finiteValues.isEmpty()) return 0f to 1f
    val min = finiteValues.minOrNull() ?: 0f
    val max = finiteValues.maxOrNull() ?: 1f
    return if (min == max) {
        (min - 1f) to (max + 1f)
    } else {
        min to max
    }
}

private fun nullableValueRange(
    points: List<TelemetryPointUi>,
    selector: (TelemetryPointUi) -> Float?,
): Pair<Float, Float>? {
    val values = points.mapNotNull(selector).filter(Float::isFinite)
    if (values.isEmpty()) return null
    val min = values.minOrNull() ?: return null
    val max = values.maxOrNull() ?: return null
    return min to max
}

private fun buildPath(
    points: List<TelemetryPointUi>,
    width: Float,
    height: Float,
    domainStartTimestampMs: Long,
    domainEndTimestampMs: Long,
    selector: (TelemetryPointUi) -> Float?,
    minValue: Float,
    maxValue: Float,
): Path {
    val valueRange = (maxValue - minValue).takeIf { it > 0f } ?: 1f
    val elapsedRange = (domainEndTimestampMs - domainStartTimestampMs).coerceAtLeast(1L).toFloat()
    val path = Path()
    var hasStarted = false

    points.forEach { point ->
        val value = selector(point) ?: return@forEach
        if (!value.isFinite()) return@forEach

        val x = width * (point.timestampMs - domainStartTimestampMs).toFloat() / elapsedRange
        val normalized = (value - minValue) / valueRange
        val y = height - (normalized * height)
        if (!hasStarted) {
            path.moveTo(x, y)
            hasStarted = true
        } else {
            path.lineTo(x, y)
        }
    }
    return path
}

private fun formatTelemetryValue(
    value: Float?,
    unit: TelemetryUnit,
): String {
    val finiteValue = value?.takeIf(Float::isFinite) ?: return "-"
    return when (unit) {
        TelemetryUnit.MM -> "%.2f".format(finiteValue)
        TelemetryUnit.KG -> "%.2f".format(finiteValue)
    }
}

private fun formatTelemetryRange(
    range: Pair<Float, Float>?,
    unit: TelemetryUnit,
): String {
    val (min, max) = range ?: return "-"
    return when (unit) {
        TelemetryUnit.MM -> "%.2f..%.2f".format(min, max)
        TelemetryUnit.KG -> "%.2f..%.2f".format(min, max)
    }
}

private fun formatSummaryLabel(
    label: String,
    unit: TelemetryUnit,
): String {
    val suffix = when (unit) {
        TelemetryUnit.MM -> "mm"
        TelemetryUnit.KG -> "kg"
    }
    return "$label($suffix)"
}

private fun List<TelemetryPointUi>.lastNotNullOfOrNull(
    selector: (TelemetryPointUi) -> Float?,
): Float? {
    for (index in indices.reversed()) {
        val value = selector(this[index]) ?: continue
        if (value.isFinite()) {
            return value
        }
    }
    return null
}
