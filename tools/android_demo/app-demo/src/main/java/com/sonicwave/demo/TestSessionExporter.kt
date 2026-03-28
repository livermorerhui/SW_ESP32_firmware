package com.sonicwave.demo

import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.OutputStreamWriter
import java.math.BigDecimal
import java.math.RoundingMode
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

data class TestSessionExportResult(
    val csvFileName: String,
    val csvDestinationLabel: String,
    val jsonFileName: String,
    val jsonDestinationLabel: String,
)

fun resolvedTestSessionExportTimestampMs(session: TestSessionUi): Long {
    return session.summary.endTimeMs ?: session.endedAtMs ?: session.startedAtMs
}

fun buildTestSessionExportBaseName(
    session: TestSessionUi,
    request: TestSessionExportRequest,
): String {
    val parts = buildList {
        add(sanitizeTestSessionFileNameComponent(request.primaryLabel.displayNameZh()))
        add(sanitizeTestSessionFileNameComponent(request.secondaryLabel.displayNameZh()))
        add(formatTestSessionExportTimestamp(resolvedTestSessionExportTimestampMs(session)))
        add(buildTestSessionHzIntensityLabel(session.summary.freqHz, session.summary.intensity))
        request.notes.trim()
            .takeIf { it.isNotEmpty() }
            ?.let(::sanitizeTestSessionFileNameComponent)
            ?.takeIf { it.isNotEmpty() }
            ?.let(::add)
    }
    return parts.joinToString("_")
}

fun buildTestSessionExportFileName(
    session: TestSessionUi,
    request: TestSessionExportRequest,
    extension: String,
): String {
    return "${buildTestSessionExportBaseName(session, request)}.$extension"
}

fun buildTestSessionHzIntensityLabel(
    freqHz: Float?,
    intensity: Int?,
): String {
    val frequencyText = formatTestSessionFrequencyForName(freqHz)
    val intensityText = intensity?.toString() ?: "0"
    return "${frequencyText}Hz$intensityText"
}

fun formatTestSessionExportTimestamp(timestampMs: Long): String {
    return Instant.ofEpochMilli(timestampMs)
        .atZone(ZoneId.systemDefault())
        .format(TEST_SESSION_EXPORT_FILE_TIME_FORMATTER)
}

private fun formatTestSessionFrequencyForName(freqHz: Float?): String {
    val value = freqHz ?: 0f
    return BigDecimal.valueOf(value.toDouble())
        .setScale(2, RoundingMode.HALF_UP)
        .stripTrailingZeros()
        .toPlainString()
}

private fun sanitizeTestSessionFileNameComponent(raw: String): String {
    return raw
        .trim()
        .replace(Regex("[\\\\/:*?\"<>|]+"), "_")
        .replace(Regex("\\s+"), "_")
        .replace(Regex("_+"), "_")
        .trim('_')
}

class TestSessionExporter(private val context: Context) {
    fun exportSession(
        session: TestSessionUi,
        request: TestSessionExportRequest,
    ): TestSessionExportResult {
        val csvFileName = buildTestSessionExportFileName(session, request, "csv")
        val jsonFileName = buildTestSessionExportFileName(session, request, "json")
        val csvContent = buildCsvContent(session, request)
        val jsonContent = buildJsonContent(session, request)
        val csvDestination = writeTextFile(csvFileName, "text/csv", csvContent)
        val jsonDestination = writeTextFile(jsonFileName, "application/json", jsonContent)
        return TestSessionExportResult(
            csvFileName = csvFileName,
            csvDestinationLabel = csvDestination,
            jsonFileName = jsonFileName,
            jsonDestinationLabel = jsonDestination,
        )
    }

    private fun buildCsvContent(
        session: TestSessionUi,
        request: TestSessionExportRequest,
    ): String {
        val summary = session.summary
        val exportedSampleCount = session.samples.size
        val timestamp = formatTestSessionExportTimestamp(resolvedTestSessionExportTimestampMs(session))
        val hzIntensity = buildTestSessionHzIntensityLabel(summary.freqHz, summary.intensity)
        val notes = request.notes.trim()

        val headerLines = listOf(
            "# 一级标签: ${request.primaryLabel.displayNameZh()}",
            "# 二级标签: ${request.secondaryLabel.displayNameZh()}",
            "# 时间戳: $timestamp",
            "# 赫兹强度: $hzIntensity",
            "# 备注: $notes",
            "# test_id: ${summary.testId?.toString().orEmpty()}",
            "# freq_hz: ${summary.freqHz?.let(::formatFrequencyValue).orEmpty()}",
            "# intensity: ${summary.intensity?.toString().orEmpty()}",
            "# result: ${summary.result.orEmpty()}",
            "# stop_reason: ${summary.stopReason.orEmpty()}",
            "# stop_source: ${summary.stopSource.orEmpty()}",
            "# fall_stop_enabled: ${summary.fallStopEnabled?.toString().orEmpty()}",
            "# baseline_ready: ${summary.baselineReady?.toString().orEmpty()}",
            "# stable_weight: ${summary.stableWeight?.let(::formatMetricValue).orEmpty()}",
            "# duration_ms: ${summary.durationMs}",
            "# ratio_max: ${summary.ratioMax?.let(::formatRatioValue).orEmpty()}",
            "# final_main_state: ${summary.finalMainState.orEmpty()}",
            "# final_abnormal_duration_ms: ${summary.finalAbnormalDurationMs?.toString().orEmpty()}",
            "# final_danger_duration_ms: ${summary.finalDangerDurationMs?.toString().orEmpty()}",
            "# sample_count: $exportedSampleCount",
        )

        return buildString {
            headerLines.forEach { line ->
                append(line)
                append('\n')
            }
            append(
                "timestamp_ms,baseline_ready,stable_weight,weight,distance,ma3,ma5,ma7,deviation,ratio," +
                    "main_state,abnormal_duration_ms,danger_duration_ms,stop_reason,stop_source,event_aux,risk_advisory\n",
            )
            session.samples.forEach { sample ->
                append(
                    listOf(
                        sample.timestampMs.toString(),
                        if (sample.baselineReady) "1" else "0",
                        sample.stableWeight?.let(::formatMetricValue),
                        formatMetricValue(sample.weight),
                        sample.distance?.let(::formatMetricValue),
                        sample.ma3?.let(::formatMetricValue),
                        sample.ma5?.let(::formatMetricValue),
                        sample.ma7?.let(::formatMetricValue),
                        sample.deviation?.let(::formatMetricValue),
                        sample.ratio?.let(::formatRatioValue),
                        sample.mainState,
                        sample.abnormalDurationMs?.toString(),
                        sample.dangerDurationMs?.toString(),
                        sample.stopReason,
                        sample.stopSource,
                        sample.eventAux,
                        sample.riskAdvisory,
                    ).joinToString(",") { csvEscape(it) },
                )
                append('\n')
            }
        }
    }

    private fun buildJsonContent(
        session: TestSessionUi,
        request: TestSessionExportRequest,
    ): String {
        val summary = session.summary
        val exportedSampleCount = session.samples.size
        val timestamp = formatTestSessionExportTimestamp(resolvedTestSessionExportTimestampMs(session))
        val hzIntensity = buildTestSessionHzIntensityLabel(summary.freqHz, summary.intensity)
        val meta = JSONObject()
            .put("一级标签", request.primaryLabel.displayNameZh())
            .put("二级标签", request.secondaryLabel.displayNameZh())
            .put("时间戳", timestamp)
            .put("赫兹强度", hzIntensity)
            .put("备注", request.notes.trim())
            .put("test_id", summary.testId ?: JSONObject.NULL)
            .put("freq_hz", summary.freqHz?.let(::roundFrequencyValue) ?: JSONObject.NULL)
            .put("intensity", summary.intensity ?: JSONObject.NULL)
            .put("result", summary.result ?: JSONObject.NULL)
            .put("stop_reason", summary.stopReason ?: JSONObject.NULL)
            .put("stop_source", summary.stopSource ?: JSONObject.NULL)
            .put("fall_stop_enabled", summary.fallStopEnabled ?: JSONObject.NULL)
            .put("baseline_ready", summary.baselineReady ?: JSONObject.NULL)
            .put("stable_weight", summary.stableWeight?.let(::roundMetricValue) ?: JSONObject.NULL)
            .put("duration_ms", summary.durationMs)
            .put("ratio_max", summary.ratioMax?.let(::roundRatioValue) ?: JSONObject.NULL)
            .put("final_main_state", summary.finalMainState ?: JSONObject.NULL)
            .put("final_abnormal_duration_ms", summary.finalAbnormalDurationMs ?: JSONObject.NULL)
            .put("final_danger_duration_ms", summary.finalDangerDurationMs ?: JSONObject.NULL)
            .put("sample_count", exportedSampleCount)

        val samples = JSONArray()
        session.samples.forEach { sample ->
            samples.put(
                JSONObject()
                    .put("timestamp_ms", sample.timestampMs)
                    .put("baseline_ready", sample.baselineReady)
                    .put("stable_weight", sample.stableWeight?.let(::roundMetricValue) ?: JSONObject.NULL)
                    .put("weight", roundMetricValue(sample.weight))
                    .put("distance", sample.distance?.let(::roundMetricValue) ?: JSONObject.NULL)
                    .put("ma3", sample.ma3?.let(::roundMetricValue) ?: JSONObject.NULL)
                    .put("ma5", sample.ma5?.let(::roundMetricValue) ?: JSONObject.NULL)
                    .put("ma7", sample.ma7?.let(::roundMetricValue) ?: JSONObject.NULL)
                    .put("deviation", sample.deviation?.let(::roundMetricValue) ?: JSONObject.NULL)
                    .put("ratio", sample.ratio?.let(::roundRatioValue) ?: JSONObject.NULL)
                    .put("main_state", sample.mainState)
                    .put("abnormal_duration_ms", sample.abnormalDurationMs ?: JSONObject.NULL)
                    .put("danger_duration_ms", sample.dangerDurationMs ?: JSONObject.NULL)
                    .put("stop_reason", sample.stopReason)
                    .put("stop_source", sample.stopSource)
                    .put("event_aux", sample.eventAux)
                    .put("risk_advisory", sample.riskAdvisory),
            )
        }

        return JSONObject()
            .put("meta", meta)
            .put("samples", samples)
            .toString(2)
    }

    private fun writeTextFile(
        fileName: String,
        mimeType: String,
        content: String,
    ): String {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            writeMediaStoreFile(fileName, mimeType, content)
        } else {
            writeLegacyFile(fileName, content)
        }
    }

    private fun writeMediaStoreFile(
        fileName: String,
        mimeType: String,
        content: String,
    ): String {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
            put(MediaStore.MediaColumns.MIME_TYPE, mimeType)
            put(MediaStore.MediaColumns.RELATIVE_PATH, "${Environment.DIRECTORY_DOWNLOADS}/SonicWave")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            ?: error("Failed to create Downloads/SonicWave entry")
        resolver.openOutputStream(uri)?.use { stream ->
            OutputStreamWriter(stream).use { writer ->
                writer.write(content)
                writer.flush()
            }
        } ?: error("Failed to open output stream for $uri")

        resolver.update(
            uri,
            ContentValues().apply {
                put(MediaStore.MediaColumns.IS_PENDING, 0)
            },
            null,
            null,
        )
        return "Downloads/SonicWave/$fileName"
    }

    private fun writeLegacyFile(
        fileName: String,
        content: String,
    ): String {
        val downloads = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        val dir = File(downloads, "SonicWave")
        if (!dir.exists() && !dir.mkdirs()) {
            error("Failed to create ${dir.absolutePath}")
        }
        val file = File(dir, fileName)
        file.writeText(content)
        return file.absolutePath
    }

    private fun csvEscape(value: String?): String {
        if (value == null) return ""
        if (!value.contains(',') && !value.contains('"') && !value.contains('\n')) {
            return value
        }
        return "\"${value.replace("\"", "\"\"")}\""
    }
}

private fun formatFrequencyValue(value: Float): String {
    return String.format(Locale.US, "%.2f", value)
}

private fun formatMetricValue(value: Float): String {
    return String.format(Locale.US, "%.2f", value)
}

private fun formatRatioValue(value: Float): String {
    return String.format(Locale.US, "%.4f", value)
}

private fun roundFrequencyValue(value: Float): Double {
    return BigDecimal.valueOf(value.toDouble()).setScale(2, RoundingMode.HALF_UP).toDouble()
}

private fun roundMetricValue(value: Float): Double {
    return BigDecimal.valueOf(value.toDouble()).setScale(2, RoundingMode.HALF_UP).toDouble()
}

private fun roundRatioValue(value: Float): Double {
    return BigDecimal.valueOf(value.toDouble()).setScale(4, RoundingMode.HALF_UP).toDouble()
}

private val TEST_SESSION_EXPORT_FILE_TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern(
    "yyyyMMdd_HHmmss",
    Locale.US,
)
