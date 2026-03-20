package com.sonicwave.protocol

object ProtocolCodec {
    fun encode(command: Command): String = when (command) {
        Command.CapabilityQuery -> "CAP?"
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
        Command.LegacyZero -> "ZERO"
        is Command.LegacySetPs -> "SET_PS:${command.zeroDistance},${command.scaleFactor}"
        is Command.LegacyWaveFie -> encodeLegacyWaveFie(command)
        is Command.LegacyRaw -> command.raw
    }

    fun decode(line: String): Event? {
        val raw = line.trim()
        if (raw.isEmpty()) return null

        parseCapabilities(raw)?.let { return it }
        parseCalibrationModel(raw)?.let { return it }
        parseCalibrationSetModelResult(raw)?.let { return it }
        parseCalibrationPoint(raw)?.let { return it }
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

    private fun parseFault(raw: String): Event.Fault? {
        val payload = namedPayload(raw, "FAULT") ?: return null
        val code = INTEGER_REGEX.find(payload)?.value?.toIntOrNull()
        val reason = payload.ifBlank { "UNKNOWN" }
        return Event.Fault(code = code, reason = reason)
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
        val namedPayload = namedPayload(raw, "STREAM")
            ?: payloadAfterPrefix(raw, "CSV")
        if (namedPayload != null) {
            return parseCsvPair(namedPayload)?.let { (distance, weight) ->
                Event.StreamSample(distance = distance, weight = weight)
            }
        }

        return parseCsvPair(raw)?.let { (distance, weight) ->
            Event.StreamSample(distance = distance, weight = weight)
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
