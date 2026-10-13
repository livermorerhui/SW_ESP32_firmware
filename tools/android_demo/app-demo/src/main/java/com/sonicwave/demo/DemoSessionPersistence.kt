package com.sonicwave.demo

import android.content.Context
import android.util.AtomicFile
import com.sonicwave.protocol.CalibrationModelType
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileNotFoundException

internal data class DemoSessionSnapshot(
    val calibrationPoints: List<CalibrationPointUi> = emptyList(),
    val preparedModel: PreparedCalibrationModelUi? = null,
    val selectedModel: CalibrationModelType = CalibrationModelType.LINEAR,
    val samplingSession: MotionSamplingSessionUi? = null,
    val emptyDistance: EmptyDistanceState = EmptyDistanceState(),
    val weightModel: WeightModelState = WeightModelState(),
)

internal sealed interface ProfileSessionLoadResult {
    data object Missing : ProfileSessionLoadResult
    data class Loaded(val snapshot: DemoSessionSnapshot) : ProfileSessionLoadResult
    data object Corrupt : ProfileSessionLoadResult
}

/** A missing file and an unreadable existing file must never have the same meaning. */
internal fun classifyProfileSessionRead(
    anyRecordFilePresent: () -> Boolean,
    readText: () -> String,
    parse: (String) -> DemoSessionSnapshot?,
): ProfileSessionLoadResult {
    val text = try {
        readText()
    } catch (_: FileNotFoundException) {
        val recordMayExist = runCatching { anyRecordFilePresent() }.getOrDefault(true)
        return if (recordMayExist) ProfileSessionLoadResult.Corrupt
        else ProfileSessionLoadResult.Missing
    } catch (_: Exception) {
        return ProfileSessionLoadResult.Corrupt
    }
    val snapshot = try {
        parse(text)
    } catch (_: Exception) {
        null
    }
    return snapshot?.let(ProfileSessionLoadResult::Loaded) ?: ProfileSessionLoadResult.Corrupt
}

/**
 * Small, versioned internal-file store for user-created calibration and sampling evidence.
 * It deliberately does not persist BLE pending flags or device truth; those belong to a
 * connection and are rebuilt after reconnect.
 */
internal class DemoSessionPersistence(context: Context) {
    private val legacyFile = File(context.filesDir, LEGACY_FILE_NAME)
    private val profileDirectory = File(context.filesDir, PROFILE_DIRECTORY_NAME)

    /** New evidence is owned by a stable firmware ID and an explicit sensor profile. */
    fun saveForProfile(deviceId: String, sensorProfile: String, snapshot: DemoSessionSnapshot): Unit =
        synchronized(profileIoLock) {
            val target = profileFile(deviceId, sensorProfile)
            if (!profileDirectory.isDirectory && !profileDirectory.mkdirs()) {
                throw IllegalStateException("Cannot create profile session directory")
            }
            // A malformed or foreign existing record is evidence, not an empty record to overwrite.
            if (loadForProfileResult(deviceId, sensorProfile) == ProfileSessionLoadResult.Corrupt) {
                throw IllegalStateException("Existing profile session cannot be verified")
            }
            val atomicFile = AtomicFile(target)

            val root = JSONObject()
                .put("schema", PROFILE_SCHEMA)
                .put("ownerDeviceId", deviceId)
                .put("ownerSensorProfile", sensorProfile)
                .put("selectedModel", snapshot.selectedModel.name)
                .put("calibrationPoints", JSONArray().also { array ->
                    snapshot.calibrationPoints.forEach { array.put(pointToJson(it)) }
                })
            snapshot.preparedModel?.let { root.put("preparedModel", modelToJson(it)) }
            snapshot.samplingSession?.let { root.put("samplingSession", samplingToJson(it)) }
            root.put("emptyDistance", emptyDistanceToJson(snapshot.emptyDistance))
            root.put("weightModel", weightModelToJson(snapshot.weightModel))
            val bytes = root.toString().toByteArray(Charsets.UTF_8)
            val output = atomicFile.startWrite()
            try {
                output.write(bytes)
                atomicFile.finishWrite(output)
            } catch (error: Exception) {
                atomicFile.failWrite(output)
                throw error
            }
        }

    /** Never falls back to an unattributed or different-profile session. */
    fun loadForProfile(deviceId: String, sensorProfile: String): DemoSessionSnapshot? =
        when (val result = loadForProfileResult(deviceId, sensorProfile)) {
            is ProfileSessionLoadResult.Loaded -> result.snapshot
            ProfileSessionLoadResult.Missing, ProfileSessionLoadResult.Corrupt -> null
        }

    /** Distinguishes first use from a damaged record so callers can fail closed. */
    fun loadForProfileResult(deviceId: String, sensorProfile: String): ProfileSessionLoadResult =
        synchronized(profileIoLock) {
            val target = profileFile(deviceId, sensorProfile)
            val file = AtomicFile(target)
            classifyProfileSessionRead(
                anyRecordFilePresent = {
                    target.exists() || File("${target.path}.bak").exists() ||
                        File("${target.path}.new").exists()
                },
                readText = { file.readFully().toString(Charsets.UTF_8) },
                parse = { text ->
                    parseSnapshot(
                        text,
                        expectedSchema = PROFILE_SCHEMA,
                        expectedDeviceId = deviceId,
                        expectedSensorProfile = sensorProfile,
                    )
                },
            )
        }

    /** Old v1/v2 history remains visible for review but is never assigned to a device. */
    @Synchronized
    fun loadLegacyUnattributed(): DemoSessionSnapshot? {
        if (!legacyFile.exists()) return null
        return runCatching { legacyFile.readText() }.getOrNull()?.let { text ->
            parseSnapshot(text, expectedSchema = null)
        }
    }

    @Deprecated("Legacy global history is read-only; use saveForProfile")
    fun save(@Suppress("UNUSED_PARAMETER") snapshot: DemoSessionSnapshot): Nothing =
        throw UnsupportedOperationException("Unattributed session history is read-only")

    @Deprecated("Use loadLegacyUnattributed for read-only legacy history")
    fun load(): DemoSessionSnapshot? = loadLegacyUnattributed()

    @Deprecated("Legacy global history is read-only")
    fun clear(): Nothing =
        throw UnsupportedOperationException("Unattributed session history is read-only")

    private fun profileFile(deviceId: String, sensorProfile: String): File =
        File(profileDirectory, ProfileSessionFileKey.fileName(deviceId, sensorProfile))

    private fun parseSnapshot(
        text: String,
        expectedSchema: String?,
        expectedDeviceId: String? = null,
        expectedSensorProfile: String? = null,
    ): DemoSessionSnapshot? = runCatching {
        val root = JSONObject(text)
        val schema = root.optString("schema")
        val isProfileRecord = expectedSchema != null
        if (!isProfileRecord) {
            if (schema !in setOf(LEGACY_SCHEMA, PREVIOUS_SCHEMA)) return@runCatching null
        } else if (schema != expectedSchema ||
            root.optString("ownerDeviceId") != expectedDeviceId ||
            root.optString("ownerSensorProfile") != expectedSensorProfile
        ) {
            return@runCatching null
        }

        val pointArray = root.optJSONArray("calibrationPoints")
        if (isProfileRecord && pointArray == null) return@runCatching null
        val points = pointArray?.let { array ->
            (0 until array.length()).mapNotNull { pointFromJson(array.optJSONObject(it)) }
        }.orEmpty()
        if (isProfileRecord && points.size != pointArray?.length()) return@runCatching null

        val preparedJson = root.optJSONObject("preparedModel")
        val prepared = modelFromJson(preparedJson)
        if (isProfileRecord && root.has("preparedModel") && prepared == null) return@runCatching null

        val samplingJson = root.optJSONObject("samplingSession")
        val sampling = samplingFromJson(samplingJson)
        if (isProfileRecord && root.has("samplingSession") && sampling == null) return@runCatching null

        val selected = runCatching {
            CalibrationModelType.valueOf(root.optString("selectedModel"))
        }.getOrNull() ?: if (isProfileRecord) return@runCatching null else CalibrationModelType.LINEAR
        val emptyJson = root.optJSONObject("emptyDistance")
        val weightJson = root.optJSONObject("weightModel")
        if (isProfileRecord && (emptyJson == null || weightJson == null)) return@runCatching null

        DemoSessionSnapshot(
            calibrationPoints = points,
            preparedModel = prepared,
            selectedModel = selected,
            samplingSession = sampling,
            // Device truth always requires a fresh GET after reconnect or process restart.
            emptyDistance = emptyDistanceFromJson(emptyJson).withoutDeviceConfirmation(),
            weightModel = weightModelFromJson(weightJson).withoutDeviceConfirmation(),
        )
    }.getOrNull()

    private fun emptyDistanceToJson(value: EmptyDistanceState) = JSONObject().apply {
        put("status", value.status.name)
        putNullable("distanceMm", value.distanceMm?.takeIf { it.isFinite() && it > 0f })
        putNullable("toleranceMm", value.toleranceMm?.takeIf { it.isFinite() && it >= 0f })
        putNullable("backend", value.backend)
        putNullable("source", value.source?.name)
        putNullable("updatedAtMs", value.updatedAtMs)
    }

    private fun emptyDistanceFromJson(json: JSONObject?): EmptyDistanceState {
        if (json == null) return EmptyDistanceState()
        return EmptyDistanceState(
            status = enumOrDefault(json.optString("status"), EmptyDistanceStatusUi.NOT_SET),
            distanceMm = floatOrNull(json, "distanceMm")?.takeIf { it.isFinite() && it > 0f },
            toleranceMm = floatOrNull(json, "toleranceMm")?.takeIf { it.isFinite() && it >= 0f },
            backend = stringOrNull(json, "backend"),
            source = stringOrNull(json, "source")?.let {
                enumOrDefault(it, EmptyDistanceSourceUi.LOCAL_ONLY)
            },
            updatedAtMs = longOrNull(json, "updatedAtMs"),
        )
    }

    private fun weightModelToJson(value: WeightModelState) = JSONObject().apply {
        put("status", value.status.name)
        putNullable("type", value.type?.name)
        putNullable("a", value.a?.takeIf { it.isFinite() })
        putNullable("b", value.b?.takeIf { it.isFinite() })
        putNullable("c", value.c?.takeIf { it.isFinite() })
        putNullable("updatedAtMs", value.updatedAtMs)
    }

    private fun weightModelFromJson(json: JSONObject?): WeightModelState {
        if (json == null) return WeightModelState()
        return WeightModelState(
            status = enumOrDefault(json.optString("status"), WeightModelStatusUi.NOT_SET),
            type = stringOrNull(json, "type")?.let { runCatching { CalibrationModelType.valueOf(it) }.getOrNull() },
            a = floatOrNull(json, "a")?.takeIf { it.isFinite() },
            b = floatOrNull(json, "b")?.takeIf { it.isFinite() },
            c = floatOrNull(json, "c")?.takeIf { it.isFinite() },
            updatedAtMs = longOrNull(json, "updatedAtMs"),
        )
    }

    private inline fun <reified T : Enum<T>> enumOrDefault(raw: String, fallback: T): T =
        runCatching { enumValueOf<T>(raw) }.getOrDefault(fallback)

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

    private companion object {
        // AtomicFile handles interrupted writes; this lock prevents overlapping writers.
        val profileIoLock = Any()
        const val LEGACY_FILE_NAME = "demo_session_state.json"
        const val PROFILE_DIRECTORY_NAME = "demo_sessions"
        const val PROFILE_SCHEMA = "demo_session_v3"
        const val PREVIOUS_SCHEMA = "demo_session_v2"
        const val LEGACY_SCHEMA = "demo_session_v1"
    }
}
