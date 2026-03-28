package com.sonicwave.demo

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.OutputStreamWriter
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

data class MotionSamplingExportResult(
    val csvFileName: String,
    val csvDestinationLabel: String,
    val jsonFileName: String?,
    val jsonDestinationLabel: String?,
)

class MotionSamplingExporter(private val context: Context) {
    fun exportSession(
        session: MotionSamplingSessionUi,
        request: MotionSamplingExportRequest,
    ): MotionSamplingExportResult {
        val baseName = buildBaseName(session, request)
        val csvName = "${baseName}.csv"
        val jsonName = "${baseName}.json"

        val csvText = buildCsv(session)
        val jsonText = buildJsonMetadata(session, request)

        val csvDestination = writeTextFile(csvName, "text/csv", csvText)
        val jsonDestination = writeTextFile(jsonName, "application/json", jsonText)

        return MotionSamplingExportResult(
            csvFileName = csvName,
            csvDestinationLabel = csvDestination,
            jsonFileName = jsonName,
            jsonDestinationLabel = jsonDestination,
        )
    }

    private fun buildCsv(session: MotionSamplingSessionUi): String {
        val header = listOf(
            "sessionId",
            "sampleIndex",
            "timestampMs",
            "elapsedMs",
            "distanceMm",
            "liveWeightKg",
            "stableWeightKg",
            "measurementValid",
            "stableVisible",
            "runtimeStateCode",
            "waveStateCode",
            "safetyStateCode",
            "safetyReasonCode",
            "safetyCode",
            "connectionStateCode",
            "modelTypeCode",
            "userMarker",
            "motionSafetyState",
            "ddDt",
            "dwDt",
        )

        return buildString {
            append("# fall_stop_enabled: ${session.fallStopEnabled?.toString().orEmpty()}")
            append('\n')
            append(header.joinToString(","))
            append('\n')
            session.rows.forEach { row ->
                append(
                    listOf(
                        session.sessionId,
                        row.sampleIndex.toString(),
                        row.timestampMs.toString(),
                        row.elapsedMs.toString(),
                        row.distanceMm.toString(),
                        row.liveWeightKg.toString(),
                        row.stableWeightKg?.toString(),
                        if (row.measurementValid) "1" else "0",
                        if (row.stableVisible) "1" else "0",
                        row.runtimeStateCode,
                        row.waveStateCode,
                        row.safetyStateCode,
                        row.safetyReasonCode,
                        row.safetyCode?.toString(),
                        row.connectionStateCode,
                        row.modelTypeCode,
                        row.userMarker,
                        row.motionSafetyState,
                        row.ddDt?.toString(),
                        row.dwDt?.toString(),
                    ).joinToString(",") { csvEscape(it) },
                )
                append('\n')
            }
        }
    }

    private fun buildJsonMetadata(
        session: MotionSamplingSessionUi,
        request: MotionSamplingExportRequest,
    ): String {
        val modelCoefficients = JSONObject()
            .put("c0", session.modelC0 ?: JSONObject.NULL)
            .put("c1", session.modelC1 ?: JSONObject.NULL)
            .put("c2", session.modelC2 ?: JSONObject.NULL)

        val model = JSONObject()
            .put("type", session.modelTypeCode ?: JSONObject.NULL)
            .put("referenceDistance", session.modelReferenceDistance ?: JSONObject.NULL)
            .put("coefficients", modelCoefficients)

        val extensibility = JSONObject()
            .put("segmentLabels", "reserved")
            .put("eventMarkers", "reserved")
            .put("ddDt", "recorded_nullable")
            .put("dwDt", "recorded_nullable")
            .put("motionSafetyState", "recorded_nullable")

        val columns = JSONArray().apply {
            listOf(
                "sessionId",
                "sampleIndex",
                "timestampMs",
                "elapsedMs",
                "distanceMm",
                "liveWeightKg",
                "stableWeightKg",
                "measurementValid",
                "stableVisible",
                "runtimeStateCode",
                "waveStateCode",
                "safetyStateCode",
                "safetyReasonCode",
                "safetyCode",
                "connectionStateCode",
                "modelTypeCode",
                "userMarker",
                "motionSafetyState",
                "ddDt",
                "dwDt",
            ).forEach(::put)
        }

        val exportedAt = Instant.ofEpochMilli(request.exportTimestampMs)
            .atZone(ZoneId.systemDefault())
            .format(EXPORT_METADATA_TIME_FORMAT)
        val exportNotes = request.notes.trim().takeIf { it.isNotEmpty() }

        return JSONObject()
            .put("schemaVersion", session.schemaVersion)
            .put("sessionId", session.sessionId)
            .put("originalSessionId", session.sessionId)
            .put("startedAtMs", session.startedAtMs)
            .put("endedAtMs", session.endedAtMs ?: JSONObject.NULL)
            .put("rowCount", session.rows.size)
            .put("appVersion", session.appVersion ?: JSONObject.NULL)
            .put("firmwareMetadata", session.firmwareMetadata ?: JSONObject.NULL)
            .put("connectedDeviceName", session.connectedDeviceName ?: JSONObject.NULL)
            .put("protocolModeCode", session.protocolModeCode ?: JSONObject.NULL)
            .put("scenarioLabel", request.scenarioLabel)
            .put("scenarioCategory", request.scenarioCategory)
            // metadata 继续保留英文内部值，便于脚本和旧消费方稳定读取；
            // 这次中文化只影响 UI 显示与导出文件名，不改运行时逻辑。
            .put("primaryLabel", request.primaryLabel.name)
            .put("subLabel", request.subLabel.name)
            .put("notes", exportNotes ?: JSONObject.NULL)
            .put("frequencyHz", session.waveFrequencyHz ?: JSONObject.NULL)
            .put("intensity", session.waveIntensity ?: JSONObject.NULL)
            .put("exportedAt", exportedAt)
            .put("waveFrequencyHz", session.waveFrequencyHz ?: JSONObject.NULL)
            .put("waveIntensity", session.waveIntensity ?: JSONObject.NULL)
            .put("fall_stop_enabled", session.fallStopEnabled ?: JSONObject.NULL)
            .put("samplingModeEnabled", session.samplingModeEnabled)
            .put("waveWasRunningAtSessionStart", session.waveWasRunningAtSessionStart)
            .put("modelType", session.modelTypeCode ?: JSONObject.NULL)
            .put("modelCoefficients", modelCoefficients)
            .put("exportTimestampMs", request.exportTimestampMs)
            .put("sessionNotes", session.notes ?: JSONObject.NULL)
            .put("model", model)
            .put("exportedColumns", columns)
            .put("extensibility", extensibility)
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

    private fun buildBaseName(
        session: MotionSamplingSessionUi,
        request: MotionSamplingExportRequest,
    ): String {
        // 文件名改为中文可读，便于采样现场和后续交接直接识别样本语义；
        // 但 JSON metadata 内部标签仍保持英文，避免破坏已有脚本兼容性。
        val primaryLabel = sanitizeFileComponent(request.primaryLabel.displayNameZh())
        val subLabel = sanitizeFileComponent(request.subLabel.displayNameZh())
        val frequency = "${session.waveFrequencyHz ?: 0}hz"
        val intensity = session.waveIntensity?.toString() ?: "0"
        val timestamp = EXPORT_FILE_TIME_FORMAT.format(
            Instant.ofEpochMilli(request.exportTimestampMs).atZone(ZoneId.systemDefault()),
        )
        return "${primaryLabel}_${subLabel}_${frequency}_${intensity}_${timestamp}"
    }

    private fun sanitizeFileComponent(raw: String): String {
        return raw
            .trim()
            .replace(Regex("[\\\\/:*?\"<>|\\s]+"), "_")
            .replace(Regex("_+"), "_")
            .trim('_')
            .ifBlank { "session" }
    }

    private companion object {
        val EXPORT_FILE_TIME_FORMAT: DateTimeFormatter = DateTimeFormatter.ofPattern(
            "yyyyMMddHHmm",
            Locale.US,
        )
        val EXPORT_METADATA_TIME_FORMAT: DateTimeFormatter = DateTimeFormatter.ofPattern(
            "yyyy-MM-dd'T'HH:mm:ssXXX",
            Locale.US,
        )
    }
}
