package com.sonicwave.demo

import android.content.Context
import com.sonicwave.protocol.CalibrationModelType
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

internal data class DemoSessionSnapshot(
    val calibrationPoints: List<CalibrationPointUi> = emptyList(),
    val preparedModel: PreparedCalibrationModelUi? = null,
    val selectedModel: CalibrationModelType = CalibrationModelType.LINEAR,
    val samplingSession: MotionSamplingSessionUi? = null,
)

/**
 * Small, versioned internal-file store for user-created calibration and sampling evidence.
 * It deliberately does not persist BLE pending flags or device truth; those belong to a
 * connection and are rebuilt after reconnect.
 */
internal class DemoSessionPersistence(context: Context) {
    private val file = File(context.filesDir, FILE_NAME)

    @Synchronized
    fun save(snapshot: DemoSessionSnapshot) {
        val root = JSONObject()
            .put("schema", SCHEMA)
            .put("selectedModel", snapshot.selectedModel.name)
            .put("calibrationPoints", JSONArray().also { array ->
                snapshot.calibrationPoints.forEach { array.put(pointToJson(it)) }
            })
        snapshot.preparedModel?.let { root.put("preparedModel", modelToJson(it)) }
        snapshot.samplingSession?.let { root.put("samplingSession", samplingToJson(it)) }
        file.parentFile?.mkdirs()
        file.writeText(root.toString())
    }

    @Synchronized
    fun load(): DemoSessionSnapshot? {
        if (!file.exists()) return null
        return runCatching {
            val root = JSONObject(file.readText())
            if (root.optString("schema") != SCHEMA) return null
            val points = root.optJSONArray("calibrationPoints")?.let { array ->
                (0 until array.length()).mapNotNull { pointFromJson(array.optJSONObject(it)) }
            }.orEmpty()
            val prepared = modelFromJson(root.optJSONObject("preparedModel"))
            val selected = runCatching {
                CalibrationModelType.valueOf(root.optString("selectedModel"))
            }.getOrDefault(CalibrationModelType.LINEAR)
            DemoSessionSnapshot(
                calibrationPoints = points,
                preparedModel = prepared,
                selectedModel = selected,
                samplingSession = samplingFromJson(root.optJSONObject("samplingSession")),
            )
        }.getOrNull()
    }

    @Synchronized
    fun clear() {
        file.delete()
    }

    private fun pointToJson(point: CalibrationPointUi) = JSONObject().apply {
        putNullable("index", point.index)
        putNullable("timestampMs", point.timestampMs)
        putNullable("distanceMm", point.distanceMm)
        putNullable("referenceWeightKg", point.referenceWeightKg)
        putNullable("predictedWeightKg", point.predictedWeightKg)
        putNullable("liveWeightKg", point.liveWeightKg)
        putNullable("stableWeightKg", point.stableWeightKg)
        put("captureRoute", point.captureRoute.name)
        putNullable("visibleSampleValid", point.visibleSampleValid)
        putNullable("stableFlag", point.stableFlag)
        putNullable("validFlag", point.validFlag)
        put("raw", point.raw)
    }

    private fun pointFromJson(json: JSONObject?): CalibrationPointUi? = json?.let {
        CalibrationPointUi(
            index = intOrNull(it, "index"), timestampMs = longOrNull(it, "timestampMs"),
            distanceMm = floatOrNull(it, "distanceMm"), referenceWeightKg = floatOrNull(it, "referenceWeightKg"),
            predictedWeightKg = floatOrNull(it, "predictedWeightKg"), liveWeightKg = floatOrNull(it, "liveWeightKg"),
            stableWeightKg = floatOrNull(it, "stableWeightKg"),
            captureRoute = runCatching { CalibrationCaptureRouteUi.valueOf(it.optString("captureRoute")) }
                .getOrDefault(CalibrationCaptureRouteUi.APP_LIVE_SNAPSHOT),
            visibleSampleValid = booleanOrNull(it, "visibleSampleValid"),
            stableFlag = booleanOrNull(it, "stableFlag"), validFlag = booleanOrNull(it, "validFlag"),
            raw = it.optString("raw"),
        )
    }

    private fun modelToJson(model: PreparedCalibrationModelUi) = JSONObject().apply {
        put("type", model.type.name); put("referenceDistance", model.referenceDistance)
        put("c0", model.c0); put("c1", model.c1); put("c2", model.c2); put("source", model.source.name)
    }

    private fun modelFromJson(json: JSONObject?): PreparedCalibrationModelUi? = json?.let {
        runCatching {
            PreparedCalibrationModelUi(
                type = CalibrationModelType.valueOf(it.optString("type")),
                referenceDistance = it.getDouble("referenceDistance").toFloat(),
                c0 = it.getDouble("c0").toFloat(), c1 = it.getDouble("c1").toFloat(),
                c2 = it.getDouble("c2").toFloat(),
                source = PreparedCalibrationModelSourceUi.valueOf(it.optString("source")),
            )
        }.getOrNull()
    }

    private fun samplingToJson(session: MotionSamplingSessionUi) = JSONObject().apply {
        put("sessionId", session.sessionId); put("startedAtMs", session.startedAtMs)
        putNullable("endedAtMs", session.endedAtMs); put("schemaVersion", session.schemaVersion)
        putNullable("appVersion", session.appVersion); putNullable("firmwareMetadata", session.firmwareMetadata)
        putNullable("connectedDeviceName", session.connectedDeviceName); putNullable("protocolModeCode", session.protocolModeCode)
        putNullable("waveFrequencyHz", session.waveFrequencyHz); putNullable("waveIntensity", session.waveIntensity)
        putNullable("fallStopEnabled", session.fallStopEnabled); put("samplingModeEnabled", session.samplingModeEnabled)
        put("waveWasRunningAtSessionStart", session.waveWasRunningAtSessionStart)
        putNullable("modelTypeCode", session.modelTypeCode); putNullable("modelReferenceDistance", session.modelReferenceDistance)
        putNullable("modelC0", session.modelC0); putNullable("modelC1", session.modelC1); putNullable("modelC2", session.modelC2)
        putNullable("notes", session.notes)
        put("rows", JSONArray().also { array -> session.rows.forEach { array.put(rowToJson(it)) } })
        putNullable("exportScenarioLabel", session.exportScenarioLabel); putNullable("exportScenarioCategory", session.exportScenarioCategory)
        putNullable("lastExportTimestampMs", session.lastExportTimestampMs); putNullable("lastExportCsvPath", session.lastExportCsvPath)
        putNullable("lastExportJsonPath", session.lastExportJsonPath)
    }

    private fun samplingFromJson(json: JSONObject?): MotionSamplingSessionUi? = json?.let {
        runCatching {
            MotionSamplingSessionUi(
                sessionId = it.getString("sessionId"), startedAtMs = it.getLong("startedAtMs"),
                endedAtMs = longOrNull(it, "endedAtMs"), schemaVersion = it.optString("schemaVersion", "motion_sampling_mvp_v1"),
                appVersion = stringOrNull(it, "appVersion"), firmwareMetadata = stringOrNull(it, "firmwareMetadata"),
                connectedDeviceName = stringOrNull(it, "connectedDeviceName"), protocolModeCode = stringOrNull(it, "protocolModeCode"),
                waveFrequencyHz = intOrNull(it, "waveFrequencyHz"), waveIntensity = intOrNull(it, "waveIntensity"),
                fallStopEnabled = booleanOrNull(it, "fallStopEnabled"), samplingModeEnabled = it.optBoolean("samplingModeEnabled"),
                waveWasRunningAtSessionStart = it.optBoolean("waveWasRunningAtSessionStart"), modelTypeCode = stringOrNull(it, "modelTypeCode"),
                modelReferenceDistance = floatOrNull(it, "modelReferenceDistance"), modelC0 = floatOrNull(it, "modelC0"),
                modelC1 = floatOrNull(it, "modelC1"), modelC2 = floatOrNull(it, "modelC2"), notes = stringOrNull(it, "notes"),
                rows = it.optJSONArray("rows")?.let { array -> (0 until array.length()).mapNotNull { i -> rowFromJson(array.optJSONObject(i)) } }.orEmpty(),
                exportScenarioLabel = stringOrNull(it, "exportScenarioLabel"), exportScenarioCategory = stringOrNull(it, "exportScenarioCategory"),
                lastExportTimestampMs = longOrNull(it, "lastExportTimestampMs"), lastExportCsvPath = stringOrNull(it, "lastExportCsvPath"),
                lastExportJsonPath = stringOrNull(it, "lastExportJsonPath"),
            )
        }.getOrNull()
    }

    private fun rowToJson(row: MotionSamplingRowUi) = JSONObject().apply {
        put("sampleIndex", row.sampleIndex); putNullable("measurementSeq", row.measurementSeq); putNullable("deviceTimestampMs", row.deviceTimestampMs)
        put("timestampMs", row.timestampMs); put("elapsedMs", row.elapsedMs)
        putNullable("distanceMm", row.distanceMm.takeIf { it.isFinite() }); putNullable("liveWeightKg", row.liveWeightKg)
        putNullable("ma12WeightKg", row.ma12WeightKg); putNullable("stableWeightKg", row.stableWeightKg); put("measurementValid", row.measurementValid)
        put("distanceValid", row.distanceValid); put("weightValid", row.weightValid); putNullable("distanceReason", row.distanceReason); putNullable("weightReason", row.weightReason)
        putNullable("sensorBackend", row.sensorBackend); put("stableVisible", row.stableVisible); put("runtimeStateCode", row.runtimeStateCode)
        put("waveStateCode", row.waveStateCode); put("safetyStateCode", row.safetyStateCode); put("safetyReasonCode", row.safetyReasonCode)
        putNullable("safetyCode", row.safetyCode); put("connectionStateCode", row.connectionStateCode); putNullable("modelTypeCode", row.modelTypeCode)
        putNullable("userMarker", row.userMarker); putNullable("motionSafetyState", row.motionSafetyState); putNullable("ddDt", row.ddDt); putNullable("dwDt", row.dwDt)
    }

    private fun rowFromJson(json: JSONObject?): MotionSamplingRowUi? = json?.let {
        runCatching {
            MotionSamplingRowUi(
                sampleIndex = it.getInt("sampleIndex"), measurementSeq = longOrNull(it, "measurementSeq"), deviceTimestampMs = longOrNull(it, "deviceTimestampMs"),
                timestampMs = it.getLong("timestampMs"), elapsedMs = it.getLong("elapsedMs"), distanceMm = floatOrNull(it, "distanceMm") ?: Float.NaN, liveWeightKg = floatOrNull(it, "liveWeightKg"),
                ma12WeightKg = floatOrNull(it, "ma12WeightKg"), stableWeightKg = floatOrNull(it, "stableWeightKg"), measurementValid = it.optBoolean("measurementValid"),
                distanceValid = it.optBoolean("distanceValid", true), weightValid = it.optBoolean("weightValid"), distanceReason = stringOrNull(it, "distanceReason"), weightReason = stringOrNull(it, "weightReason"),
                sensorBackend = stringOrNull(it, "sensorBackend"), stableVisible = it.optBoolean("stableVisible"), runtimeStateCode = it.optString("runtimeStateCode"),
                waveStateCode = it.optString("waveStateCode"), safetyStateCode = it.optString("safetyStateCode"), safetyReasonCode = it.optString("safetyReasonCode"),
                safetyCode = intOrNull(it, "safetyCode"), connectionStateCode = it.optString("connectionStateCode"), modelTypeCode = stringOrNull(it, "modelTypeCode"),
                userMarker = stringOrNull(it, "userMarker"), motionSafetyState = stringOrNull(it, "motionSafetyState"), ddDt = floatOrNull(it, "ddDt"), dwDt = floatOrNull(it, "dwDt"),
            )
        }.getOrNull()
    }

    private fun JSONObject.putNullable(key: String, value: Any?) { put(key, value ?: JSONObject.NULL) }
    private fun stringOrNull(json: JSONObject, key: String) = json.optString(key).takeUnless { it.isEmpty() || it == "null" }
    private fun floatOrNull(json: JSONObject, key: String) = if (json.isNull(key) || !json.has(key)) null else json.optDouble(key).toFloat()
    private fun longOrNull(json: JSONObject, key: String) = if (json.isNull(key) || !json.has(key)) null else json.optLong(key)
    private fun intOrNull(json: JSONObject, key: String) = if (json.isNull(key) || !json.has(key)) null else json.optInt(key)
    private fun booleanOrNull(json: JSONObject, key: String) = if (json.isNull(key) || !json.has(key)) null else json.optBoolean(key)

    private companion object { const val FILE_NAME = "demo_session_state.json"; const val SCHEMA = "demo_session_v1" }
}
