package com.sonicwave.protocol

object ProtocolCodec {
    fun encode(command: Command): String = when (command) {
        Command.CapabilityQuery -> "CAP?"
        Command.SnapshotQuery -> "SNAPSHOT?"
        is Command.DeviceSetConfig -> {
            "DEVICE:SET_CONFIG platform_model=${command.platformModel.name}," +
                "laser_installed=${if (command.laserInstalled) 1 else 0}"
        }
        is Command.WaveSet -> "WAVE:SET f=${command.freqHz},i=${command.intensity}"
        Command.WaveStart -> "WAVE:START"
        Command.WaveStop -> "WAVE:STOP"
        Command.ScaleZero -> "SCALE:ZERO"
        Command.CalibrationZero -> "CAL:ZERO"
        is Command.ScaleCal -> "SCALE:CAL z=${command.zeroDistance},k=${command.scaleFactor}"
        is Command.CalibrationCapture -> "CAL:CAPTURE w=${command.referenceWeightKg}"
        Command.CalibrationGetModel -> "CAL:GET_MODEL"
        is Command.CalibrationSetModel -> {
            "CAL:SET_MODEL type=${command.type.name},ref=${command.referenceDistance}," +
                "c0=${command.c0},c1=${command.c1},c2=${command.c2}"
        }
        is Command.FallStopProtectionSet -> "DEBUG:FALL_STOP enabled=${if (command.enabled) 1 else 0}"
        is Command.MotionSamplingModeSet -> "DEBUG:MOTION_SAMPLING enabled=${if (command.enabled) 1 else 0}"
        Command.LegacyZero -> "ZERO"
        is Command.LegacySetPs -> "SET_PS:${command.zeroDistance},${command.scaleFactor}"
        is Command.LegacyWaveFie -> encodeLegacyWaveFie(command)
        is Command.LegacyRaw -> command.raw
    }

    fun decode(line: String): Event? {
        val raw = line.trim()
        if (raw.isEmpty()) return null

        parseCapabilities(raw)?.let { return it }
        parseDeviceConfig(raw)?.let { return it }
        parseCalibrationModel(raw)?.let { return it }
        parseCalibrationSetModelResult(raw)?.let { return it }
        parseCalibrationPoint(raw)?.let { return it }
        parseSnapshot(raw)?.let { return it }
        parseWaveOutput(raw)?.let { return it }
        parseBaseline(raw)?.let { return it }
        parseStop(raw)?.let { return it }
        parseSafety(raw)?.let { return it }
        parseState(raw)?.let { return it }
        parseFault(raw)?.let { return it }
        parseStable(raw)?.let { return it }
        parseParam(raw)?.let { return it }
        parseStream(raw)?.let { return it }
        parseNack(raw)?.let { return it }
        parseError(raw)?.let { return it }
        parseAck(raw)?.let { return it }
        parseLegacy(raw)?.let { return it }
        return null
    }

    private fun encodeLegacyWaveFie(command: Command.LegacyWaveFie): String {
        val segments = mutableListOf<String>()
        command.freqHz?.let { segments += "F:$it" }
        command.intensity?.let { segments += "I:$it" }
        command.enable?.let { segments += "E:${if (it) 1 else 0}" }
        return segments.joinToString(",")
    }

    private fun parseCapabilities(raw: String): Event.Capabilities? {
        val payload = when {
            raw.equals("CAP", ignoreCase = true) -> ""
            raw.startsWith("CAP:", ignoreCase = true) -> payloadAfterPrefix(raw, "CAP")
            raw.startsWith("ACK:CAP", ignoreCase = true) -> raw.substringAfter(':', "").let { afterAck ->
                payloadAfterPrefix(afterAck, "CAP")
            }

            else -> null
        } ?: return null

        val kv = parseKeyValuePayload(payload)
        val values = when {
            kv.isNotEmpty() -> kv
            payload.isNotBlank() -> mapOf("RAW" to payload)
            else -> emptyMap()
        }
        return Event.Capabilities(values = values, raw = raw)
    }

    private fun parseDeviceConfig(raw: String): Event.DeviceConfig? {
        if (!raw.startsWith("ACK:DEVICE_CONFIG", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:DEVICE_CONFIG", "").trim()
        val kv = parseKeyValuePayload(payload)
        return Event.DeviceConfig(
            platformModel = parsePlatformModel(kv["PLATFORM_MODEL"]),
            laserInstalled = parseBooleanFlag(kv["LASER_INSTALLED"]),
            raw = raw,
        )
    }

    private fun parseCalibrationModel(raw: String): Event.CalibrationModel? {
        if (!raw.startsWith("ACK:CAL_MODEL", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:CAL_MODEL", "").trim()
        val kv = parseKeyValuePayload(payload)
        return Event.CalibrationModel(
            type = parseCalibrationModelType(kv["TYPE"]),
            referenceDistance = kv["REF"]?.toFloatOrNull(),
            c0 = kv["C0"]?.toFloatOrNull(),
            c1 = kv["C1"]?.toFloatOrNull(),
            c2 = kv["C2"]?.toFloatOrNull(),
            raw = raw,
        )
    }

    private fun parseCalibrationPoint(raw: String): Event.CalibrationPoint? {
        if (!raw.startsWith("ACK:CAL_POINT", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:CAL_POINT", "").trim()
        val kv = parseKeyValuePayload(payload)
        return Event.CalibrationPoint(
            index = kv["IDX"]?.toIntOrNull(),
            timestampMs = kv["TS"]?.toLongOrNull(),
            distanceMm = kv["D_MM"]?.toFloatOrNull(),
            referenceWeightKg = kv["REF_KG"]?.toFloatOrNull(),
            predictedWeightKg = kv["PRED_KG"]?.toFloatOrNull(),
            stableFlag = parseBooleanFlag(kv["STABLE"]),
            validFlag = parseBooleanFlag(kv["VALID"]),
            raw = raw,
        )
    }

    private fun parseCalibrationSetModelResult(raw: String): Event.CalibrationSetModelResult? {
        if (raw.startsWith("ACK:CAL_SET_MODEL", ignoreCase = true)) {
            val payload = raw.substringAfter("ACK:CAL_SET_MODEL", "").trim()
            val kv = parseKeyValuePayload(payload)
            return Event.CalibrationSetModelResult(
                success = true,
                type = parseCalibrationModelType(kv["TYPE"]),
                reason = null,
                raw = raw,
            )
        }

        val nackPayload = payloadAfterPrefix(raw, "NACK:CAL_SET_MODEL") ?: return null
        val kv = parseKeyValuePayload(nackPayload)
        return Event.CalibrationSetModelResult(
            success = false,
            type = parseCalibrationModelType(kv["TYPE"]),
            reason = kv["REASON"]?.takeIf { it.isNotBlank() } ?: nackPayload.ifBlank { "UNKNOWN" },
            raw = raw,
        )
    }

    private fun parseState(raw: String): Event.State? {
        val payload = namedPayload(raw, "STATE") ?: return null
        return Event.State(parseDeviceState(payload))
    }

    private fun parseSnapshot(raw: String): Event.Snapshot? {
        if (!raw.startsWith("SNAPSHOT:", ignoreCase = true)) return null
        val payload = raw.substringAfter("SNAPSHOT:", "").trim()
        val kv = parseKeyValuePayload(payload)
        return Event.Snapshot(
            topState = parseDeviceState(kv["TOP_STATE"]),
            userPresent = parseBooleanFlag(kv["USER_PRESENT"]),
            runtimeReady = parseBooleanFlag(kv["RUNTIME_READY"]),
            startReady = parseBooleanFlag(kv["START_READY"]),
            baselineReady = parseBooleanFlag(kv["BASELINE_READY"]),
            waveOutputActive = parseBooleanFlag(kv["WAVE_OUTPUT_ACTIVE"]),
            currentReasonCode = kv["CURRENT_REASON_CODE"],
            currentSafetyEffect = kv["CURRENT_SAFETY_EFFECT"],
            stableWeightKg = kv["STABLE_WEIGHT"]?.toFloatOrNull(),
            currentFrequencyHz = kv["CURRENT_FREQUENCY"]?.toFloatOrNull(),
            currentIntensity = kv["CURRENT_INTENSITY"]?.toIntOrNull(),
            platformModel = parsePlatformModel(kv["PLATFORM_MODEL"]),
            laserInstalled = parseBooleanFlag(kv["LASER_INSTALLED"]),
            laserAvailable = parseBooleanFlag(kv["LASER_AVAILABLE"]),
            protectionDegraded = parseBooleanFlag(kv["PROTECTION_DEGRADED"]),
            raw = raw,
        )
    }

    private fun parseWaveOutput(raw: String): Event.WaveOutput? {
        val payload = namedPayload(raw, "WAVE_OUTPUT") ?: return null
        val kv = parseKeyValuePayload(payload)
        val active = parseBooleanFlag(kv["ACTIVE"]) ?: return null
        return Event.WaveOutput(
            active = active,
            raw = raw,
        )
    }

    private fun parseFault(raw: String): Event.Fault? {
        val payload = namedPayload(raw, "FAULT") ?: return null
        val code = INTEGER_REGEX.find(payload)?.value?.toIntOrNull()
        val reason = payload.ifBlank { "UNKNOWN" }
        return Event.Fault(code = code, reason = reason)
    }

    private fun parseBaseline(raw: String): Event.BaselineMain? {
        val payload = namedPayload(raw, "BASELINE") ?: return null
        val kv = parseKeyValuePayload(payload)
        return Event.BaselineMain(
            baselineReady = parseBooleanFlag(kv["BASELINE_READY"]) ?: false,
            stableWeightKg = kv["STABLE_WEIGHT"]?.toFloatOrNull(),
            ma7WeightKg = kv["MA7"]?.toFloatOrNull(),
            deviationKg = kv["DEVIATION"]?.toFloatOrNull(),
            ratio = kv["RATIO"]?.toFloatOrNull(),
            mainState = kv["MAIN_STATE"] ?: "BASELINE_PENDING",
            abnormalDurationMs = kv["ABNORMAL_DURATION_MS"]?.toLongOrNull(),
            dangerDurationMs = kv["DANGER_DURATION_MS"]?.toLongOrNull(),
            stopReason = kv["STOP_REASON"] ?: "NONE",
            stopSource = kv["STOP_SOURCE"] ?: "NONE",
            raw = raw,
        )
    }

    private fun parseStop(raw: String): Event.Stop? {
        val payload = namedPayload(raw, "STOP") ?: return null
        val kv = parseKeyValuePayload(payload)
        return Event.Stop(
            stopReason = kv["STOP_REASON"] ?: "NONE",
            stopSource = kv["STOP_SOURCE"] ?: "NONE",
            code = kv["CODE"]?.toIntOrNull() ?: INTEGER_REGEX.find(payload)?.value?.toIntOrNull(),
            effect = parseSafetyEffect(kv["EFFECT"]),
            state = parseDeviceState(kv["STATE"]),
            raw = raw,
        )
    }

    private fun parseSafety(raw: String): Event.Safety? {
        val payload = namedPayload(raw, "SAFETY") ?: return null
        val kv = parseKeyValuePayload(payload)
        val reason = kv["REASON"]?.takeIf { it.isNotBlank() } ?: "UNKNOWN"
        val code = kv["CODE"]?.toIntOrNull() ?: INTEGER_REGEX.find(payload)?.value?.toIntOrNull()
        return Event.Safety(
            reason = reason,
            code = code,
            effect = parseSafetyEffect(kv["EFFECT"]),
            state = parseDeviceState(kv["STATE"]),
            wave = parseWaveState(kv["WAVE"]),
            extras = kv,
            raw = raw,
        )
    }

    private fun parseStable(raw: String): Event.Stable? {
        val payload = namedPayload(raw, "STABLE") ?: return null
        val weight = NUMBER_REGEX.find(payload)?.value?.toFloatOrNull()
        return Event.Stable(stableWeightKg = weight, raw = payload.ifBlank { raw })
    }

    private fun parseParam(raw: String): Event.Param? {
        val payload = namedPayload(raw, "PARAM") ?: return null

        val kv = parseKeyValuePayload(payload)
        val zeroByKey = kv["Z"]?.toFloatOrNull() ?: kv["ZERO"]?.toFloatOrNull()
        val factorByKey = kv["K"]?.toFloatOrNull() ?: kv["FACTOR"]?.toFloatOrNull()

        val csv = parseCsvPair(payload)
        val zeroDistance = zeroByKey ?: csv?.first
        val scaleFactor = factorByKey ?: csv?.second

        val extras = if (kv.isNotEmpty()) {
            kv
        } else if (csv != null) {
            mapOf("Z" to csv.first.toString(), "K" to csv.second.toString())
        } else {
            emptyMap()
        }

        return Event.Param(
            zeroDistance = zeroDistance,
            scaleFactor = scaleFactor,
            extras = extras,
        )
    }

    private fun parseStream(raw: String): Event.StreamSample? {
        val evtPayload = namedPayload(raw, "STREAM")
        if (evtPayload != null) {
            val kv = parseKeyValuePayload(evtPayload)
            if (
                kv.containsKey("SEQ") ||
                kv.containsKey("TS_MS") ||
                kv.containsKey("VALID") ||
                kv.containsKey("DISTANCE") ||
                kv.containsKey("WEIGHT") ||
                kv.containsKey("MA12")
            ) {
                val distance = kv["DISTANCE"]?.toFloatOrNull()
                val weight = kv["WEIGHT"]?.toFloatOrNull()
                return Event.StreamSample(
                    carrier = MeasurementCarrier.FORMAL_EVT_STREAM,
                    sequence = kv["SEQ"]?.toLongOrNull(),
                    timestampMs = kv["TS_MS"]?.toLongOrNull(),
                    distance = distance,
                    weight = weight,
                    ma12 = kv["MA12"]?.toFloatOrNull(),
                    ma12Ready = parseBooleanFlag(kv["MA12_READY"]) ?: kv["MA12"]?.toFloatOrNull()?.let { true } ?: false,
                    valid = parseBooleanFlag(kv["VALID"]) ?: (distance != null && weight != null),
                    reason = kv["REASON"],
                    raw = raw,
                )
            }
        }

        val fallbackCsv = payloadAfterPrefix(raw, "CSV") ?: evtPayload ?: raw
        return parseCsvPair(fallbackCsv)?.let { (distance, weight) ->
            Event.StreamSample(
                carrier = MeasurementCarrier.LEGACY_CSV_FALLBACK,
                sequence = null,
                timestampMs = null,
                distance = distance,
                weight = weight,
                ma12 = null,
                ma12Ready = false,
                valid = true,
                reason = null,
                raw = raw,
            )
        }
    }

    private fun parseNack(raw: String): Event.Nack? {
        val payload = payloadAfterPrefix(raw, "NACK") ?: return null
        return Event.Nack(payload.ifBlank { "UNKNOWN" })
    }

    private fun parseError(raw: String): Event.Error? {
        val payload = payloadAfterPrefix(raw, "ERR") ?: return null
        return Event.Error(payload.ifBlank { "UNKNOWN" })
    }

    private fun parseAck(raw: String): Event.Ack? {
        if (!raw.startsWith("ACK", ignoreCase = true)) return null
        return Event.Ack(raw)
    }

    private fun parseLegacy(raw: String): Event? {
        if (raw.startsWith("F:", ignoreCase = true) ||
            raw.startsWith("I:", ignoreCase = true) ||
            raw.startsWith("E:", ignoreCase = true)
        ) {
            return Event.LegacyInfo(raw)
        }

        if (raw.equals("ZERO", ignoreCase = true) || raw.startsWith("SET_PS:", ignoreCase = true)) {
            return Event.Ack(raw)
        }

        return null
    }

    private fun namedPayload(raw: String, keyword: String): String? {
        payloadAfterPrefix(raw, "EVT:$keyword")?.let { return it }
        payloadAfterPrefix(raw, keyword)?.let { return it }
        return null
    }

    private fun payloadAfterPrefix(raw: String, prefix: String): String? {
        if (!raw.startsWith(prefix, ignoreCase = true)) return null
        return raw
            .substring(prefix.length)
            .trimStart(' ', ':', ',')
            .trim()
    }

    private fun parseCsvPair(input: String): Pair<Float, Float>? {
        val parts = input
            .trim()
            .split(',')
            .map { it.trim() }
            .filter { it.isNotEmpty() }
        if (parts.size < 2) return null

        val distance = parts[0].toFloatOrNull() ?: return null
        val weight = parts[1].toFloatOrNull() ?: return null
        return distance to weight
    }

    private fun parseKeyValuePayload(payload: String): Map<String, String> {
        if (payload.isBlank()) return emptyMap()

        val result = linkedMapOf<String, String>()
        KEY_VALUE_REGEX.findAll(payload).forEach {
            val key = it.groupValues[1].uppercase()
            val value = it.groupValues[2].trim()
            if (value.isNotEmpty()) {
                result[key] = value
            }
        }
        if (result.isNotEmpty()) return result

        payload
            .split(',', ';', ' ')
            .map { it.trim() }
            .filter { it.contains('=') }
            .forEach { token ->
                val key = token.substringBefore('=').trim().uppercase()
                val value = token.substringAfter('=', "").trim()
                if (key.isNotEmpty() && value.isNotEmpty()) {
                    result[key] = value
                }
            }
        return result
    }

    private fun parseCalibrationModelType(raw: String?): CalibrationModelType? {
        return when (raw?.uppercase()) {
            "LINEAR", "1" -> CalibrationModelType.LINEAR
            "QUADRATIC", "2" -> CalibrationModelType.QUADRATIC
            else -> null
        }
    }

    private fun parsePlatformModel(raw: String?): PlatformModel? {
        return when (raw?.uppercase()) {
            "BASE" -> PlatformModel.BASE
            "PLUS" -> PlatformModel.PLUS
            "PRO" -> PlatformModel.PRO
            "ULTRA" -> PlatformModel.ULTRA
            else -> null
        }
    }

    private fun parseDeviceState(raw: String?): DeviceState {
        return when (raw?.uppercase()) {
            "IDLE" -> DeviceState.IDLE
            "ARMED" -> DeviceState.ARMED
            "RUNNING" -> DeviceState.RUNNING
            "FAULT_STOP" -> DeviceState.FAULT_STOP
            else -> DeviceState.UNKNOWN
        }
    }

    private fun parseSafetyEffect(raw: String?): SafetyEffect {
        return when (raw?.uppercase()) {
            "WARNING_ONLY" -> SafetyEffect.WARNING_ONLY
            "RECOVERABLE_PAUSE" -> SafetyEffect.RECOVERABLE_PAUSE
            "ABNORMAL_STOP" -> SafetyEffect.ABNORMAL_STOP
            else -> SafetyEffect.UNKNOWN
        }
    }

    private fun parseWaveState(raw: String?): WaveState {
        return when (raw?.uppercase()) {
            "STOPPED" -> WaveState.STOPPED
            "RUNNING" -> WaveState.RUNNING
            else -> WaveState.UNKNOWN
        }
    }

    private fun parseBooleanFlag(raw: String?): Boolean? {
        return when (raw?.trim()) {
            "1" -> true
            "0" -> false
            else -> null
        }
    }

    private val KEY_VALUE_REGEX = Regex("([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^,;\\s]+)")
    private val NUMBER_REGEX = Regex("-?\\d+(?:\\.\\d+)?")
    private val INTEGER_REGEX = Regex("-?\\d+")
}
