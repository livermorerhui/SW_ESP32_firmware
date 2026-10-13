package com.sonicwave.protocol

object ProtocolCodec {
    fun encode(command: Command): String = when (command) {
        Command.CapabilityQuery -> "CAP?"
        Command.SnapshotQuery -> "SNAPSHOT?"
        Command.DeviceIdentityQuery -> "DEVICE:IDENTITY?"
        Command.DeviceGetConfig -> "DEVICE:GET_CONFIG"
        is Command.DeviceSetProfileConfig -> {
            require(command.platformModel == PlatformModel.BASE || command.platformModel == PlatformModel.PLUS)
            require(command.platformModel != PlatformModel.BASE || command.sensorProfile == SensorProfile.NONE)
            "DEVICE:SET_CONFIG platform_model=${command.platformModel.name}," +
                "sensor_profile=${command.sensorProfile.name}"
        }
        is Command.DeviceSetConfig -> {
            "DEVICE:SET_CONFIG platform_model=${command.platformModel.name}," +
                "laser_installed=${if (command.laserInstalled) 1 else 0}"
        }
        is Command.DegradedStartSet -> "DEBUG:DEGRADED_START enabled=${if (command.enabled) 1 else 0}"
        is Command.WaveSet -> "WAVE:SET f=${command.freqHz},i=${command.intensity}"
        Command.WaveStart -> "WAVE:START"
        Command.WaveStop -> "WAVE:STOP"
        Command.ScaleZero -> "SCALE:ZERO"
        Command.CalibrationZero -> "CAL:ZERO"
        is Command.ScaleCal -> "SCALE:CAL z=${command.zeroDistance},k=${command.scaleFactor}"
        is Command.CalibrationCapture -> "CAL:CAPTURE w=${command.referenceWeightKg}"
        Command.CalibrationGetModel -> "CAL:GET_MODEL"
        Command.EmptyDistanceGet -> "EMPTY:GET"
        Command.EmptyDistanceSetCurrent -> "EMPTY:SET_CURRENT"
        is Command.EmptyDistanceSet -> "EMPTY:SET distance_mm=${command.distanceMm}"
        Command.EmptyDistanceClear -> "EMPTY:CLEAR"
        Command.CalibrationClearWeightModel -> "CAL:CLEAR_WEIGHT_MODEL"
        is Command.CalibrationSetModel -> {
            "CAL:SET_MODEL type=${command.type.name},ref=${command.referenceDistance}," +
                "c0=${command.c0},c1=${command.c1},c2=${command.c2}"
        }
        is Command.FallStopProtectionSet -> "DEBUG:FALL_STOP enabled=${if (command.enabled) 1 else 0}"
        is Command.LeaveProtectionSet -> "SAFETY:LEAVE_PROTECTION enabled=${if (command.enabled) 1 else 0}"
        is Command.MotionSamplingModeSet -> "DEBUG:MOTION_SAMPLING enabled=${if (command.enabled) 1 else 0}"
        is Command.StreamSet -> "STREAM:SET enabled=${if (command.enabled) 1 else 0},rate_hz=${command.rateHz}"
        Command.LegacyZero -> "ZERO"
        is Command.LegacySetPs -> "SET_PS:${command.zeroDistance},${command.scaleFactor}"
        is Command.LegacyWaveFie -> encodeLegacyWaveFie(command)
        is Command.LegacyRaw -> command.raw
    }

    fun decode(line: String): Event? {
        val raw = line.trim()
        if (raw.isEmpty()) return null

        parseCapabilities(raw)?.let { return it }
        parseDeviceIdentity(raw)?.let { return it }
        parseProfileConfig(raw)?.let { return it }
        parseDeviceConfig(raw)?.let { return it }
        parseFallStopProtection(raw)?.let { return it }
        parseLeaveProtection(raw)?.let { return it }
        parseDegradedStart(raw)?.let { return it }
        parseStreamSubscription(raw)?.let { return it }
        parseProfileEmptyDistance(raw)?.let { return it }
        parseEmptyDistance(raw)?.let { return it }
        parseWeightModelClear(raw)?.let { return it }
        parseCalibrationModel(raw)?.let { return it }
        parseCalibrationSetModelResult(raw)?.let { return it }
        parseCalibrationZeroResult(raw)?.let { return it }
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

    private fun parseDeviceIdentity(raw: String): Event? {
        val payload = strictPayloadAfterPrefix(raw, "ACK:DEVICE_ID") ?: return null
        val kv = parseStrictFields(payload, setOf("ID", "VERSION"))
            ?: return Event.Error("MALFORMED_DEVICE_ID")
        val id = kv["ID"]
        val version = kv["VERSION"]?.toIntOrNull()
        if (id == null || !DEVICE_ID_REGEX.matches(id) || version != 1) {
            return Event.Error("MALFORMED_DEVICE_ID")
        }
        return Event.DeviceIdentity(id = id, version = version, raw = raw)
    }

    private fun parseProfileConfig(raw: String): Event? {
        val isAck = strictPayloadAfterPrefix(raw, "ACK:DEVICE_CONFIG")
        val isNack = strictPayloadAfterPrefix(raw, "NACK:DEVICE_CONFIG")
        val payload = isAck ?: isNack ?: return null
        if (isNack != null) {
            val kv = parseStrictFields(payload, setOf("OP", "REASON"))
                ?: return Event.Error("MALFORMED_PROFILE_CONFIG_NACK")
            val operation = kv["OP"]
            val reason = kv["REASON"]
            if (operation !in setOf("GET", "SET") || reason.isNullOrBlank()) {
                return Event.Error("MALFORMED_PROFILE_CONFIG_NACK")
            }
            return Event.ProfileConfigResult(
                operation = operation!!, success = false, schema = null,
                platformModel = null, sensorProfile = null, laserInstalled = null,
                generation = null, reason = reason, raw = raw,
            )
        }

        // The old ACK uses the same prefix. It is deliberately left for the
        // legacy parser unless the frame claims to be a schema-1 response.
        val loose = parseKeyValuePayload(payload)
        if (loose.keys.none { it in PROFILE_CONFIG_MARKERS }) return null
        val kv = parseStrictFields(
            payload,
            setOf("OP", "SCHEMA", "PLATFORM_MODEL", "SENSOR_PROFILE", "LASER_INSTALLED", "GENERATION"),
        ) ?: return Event.Error("MALFORMED_PROFILE_CONFIG_ACK")
        val operation = kv["OP"]
        val model = parsePlatformModel(kv["PLATFORM_MODEL"])
        val profile = parseSensorProfile(kv["SENSOR_PROFILE"])
        val laserInstalled = parseBooleanFlag(kv["LASER_INSTALLED"])
        val generation = parseUint32(kv["GENERATION"])
        if (operation !in setOf("GET", "SET") || kv["SCHEMA"] != "1" ||
            model !in setOf(PlatformModel.BASE, PlatformModel.PLUS) || profile == null ||
            laserInstalled == null || generation == null ||
            laserInstalled != (model == PlatformModel.PLUS) ||
            (model == PlatformModel.BASE && profile != SensorProfile.NONE)
        ) {
            return Event.Error("MALFORMED_PROFILE_CONFIG_ACK")
        }
        return Event.ProfileConfigResult(
            operation = operation!!, success = true, schema = 1,
            platformModel = model, sensorProfile = profile,
            laserInstalled = laserInstalled, generation = generation,
            reason = null, raw = raw,
        )
    }

    private fun parseDeviceConfig(raw: String): Event.DeviceConfig? {
        val payload = strictPayloadAfterPrefix(raw, "ACK:DEVICE_CONFIG") ?: return null
        val kv = parseKeyValuePayload(payload)
        if (kv.keys.any { it in PROFILE_CONFIG_MARKERS }) return null
        return Event.DeviceConfig(
            platformModel = parsePlatformModel(kv["PLATFORM_MODEL"]),
            laserInstalled = parseBooleanFlag(kv["LASER_INSTALLED"]),
            raw = raw,
        )
    }

    private fun parseFallStopProtection(raw: String): Event.FallStopProtection? {
        if (!raw.startsWith("ACK:FALL_STOP", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:FALL_STOP", "").trim()
        val kv = parseKeyValuePayload(payload)
        val enabled = parseBooleanFlag(kv["ENABLED"]) ?: return null
        return Event.FallStopProtection(
            enabled = enabled,
            mode = kv["MODE"],
            raw = raw,
        )
    }

    private fun parseLeaveProtection(raw: String): Event.LeaveProtection? {
        if (!raw.startsWith("ACK:LEAVE_PROTECTION", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:LEAVE_PROTECTION", "").trim()
        val kv = parseKeyValuePayload(payload)
        val enabled = parseBooleanFlag(kv["ENABLED"]) ?: return null
        return Event.LeaveProtection(
            enabled = enabled,
            supported = parseBooleanFlag(kv["SUPPORTED"]) ?: true,
            effect = kv["EFFECT"],
            raw = raw,
        )
    }

    private fun parseDegradedStart(raw: String): Event.DegradedStart? {
        if (!raw.startsWith("ACK:DEGRADED_START", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:DEGRADED_START", "").trim()
        val kv = parseKeyValuePayload(payload)
        val enabled = parseBooleanFlag(kv["ENABLED"]) ?: return null
        val available = parseBooleanFlag(kv["AVAILABLE"]) ?: false
        return Event.DegradedStart(
            enabled = enabled,
            available = available,
            raw = raw,
        )
    }

    private fun parseStreamSubscription(raw: String): Event.StreamSubscription? {
        if (!raw.startsWith("ACK:STREAM", ignoreCase = true)) return null
        val payload = raw.substringAfter("ACK:STREAM", "").trim()
        val kv = parseKeyValuePayload(payload)
        val enabled = parseBooleanFlag(kv["ENABLED"]) ?: return null
        return Event.StreamSubscription(
            enabled = enabled,
            supported = parseBooleanFlag(kv["SUPPORTED"]) ?: true,
            rateHz = kv["RATE_HZ"]?.toIntOrNull(),
            raw = raw,
        )
    }

    private fun parseProfileEmptyDistance(raw: String): Event? {
        val ack = strictPayloadAfterPrefix(raw, "ACK:EMPTY")
        val nack = strictPayloadAfterPrefix(raw, "NACK:EMPTY")
        val payload = ack ?: nack ?: return null
        val loose = parseKeyValuePayload(payload)
        // Profile-aware GET/CLEAR failures have the same wire shape as the
        // legacy failures. Treat those NACKs as profile failures so a pending
        // read/write is resolved, while legacy ACKs still require profile,
        // generation and revision before they can confirm new device state.
        val profileReply = loose.keys.any { it in PROFILE_EMPTY_MARKERS } ||
            loose["OP"] == "SET_CURRENT" ||
            (nack != null && loose["OP"] in setOf("GET", "CLEAR"))
        if (!profileReply) return null
        if (nack != null) {
            val kv = parseStrictFields(payload, setOf("OP", "REASON", "SUPPORTED"))
                ?: return Event.Error("MALFORMED_PROFILE_EMPTY_NACK")
            val operation = kv["OP"]
            val reason = kv["REASON"]
            val supported = kv["SUPPORTED"]?.let(::parseBooleanFlag)
            if (operation !in setOf("GET", "SET_CURRENT", "CLEAR") || reason.isNullOrBlank() ||
                (kv.containsKey("SUPPORTED") && supported == null)
            ) {
                return Event.Error("MALFORMED_PROFILE_EMPTY_NACK")
            }
            return Event.ProfileEmptyDistanceResult(
                operation = operation!!, success = false, status = null,
                distanceMm = null, toleranceMm = null, sensorProfile = null,
                generation = null, emptyRevision = null, supported = supported,
                reason = reason, raw = raw,
            )
        }

        val kv = parseStrictFields(
            payload,
            setOf("OP", "STATUS", "DISTANCE_MM", "TOLERANCE_MM", "SENSOR_PROFILE", "GENERATION", "EMPTY_REVISION"),
        ) ?: return Event.Error("MALFORMED_PROFILE_EMPTY_ACK")
        val operation = kv["OP"]
        val status = kv["STATUS"]
        val profile = parseSensorProfile(kv["SENSOR_PROFILE"])
        val generation = parseUint32(kv["GENERATION"])
        val revision = parseUint32(kv["EMPTY_REVISION"])
        val distance = kv["DISTANCE_MM"]?.toFloatOrNull()
        val tolerance = kv["TOLERANCE_MM"]?.toFloatOrNull()
        val hasValues = kv.containsKey("DISTANCE_MM") || kv.containsKey("TOLERANCE_MM")
        if (operation !in setOf("GET", "SET_CURRENT", "CLEAR") ||
            status !in setOf("SET", "UNSET") || profile == null ||
            generation == null || revision == null ||
            (operation == "SET_CURRENT" && status != "SET") ||
            (operation == "CLEAR" && status != "UNSET") ||
            (status == "SET" && (profile == SensorProfile.NONE || revision == 0L)) ||
            (status == "SET" && (distance == null || !distance.isFinite() || distance <= 0f ||
                tolerance == null || !tolerance.isFinite() || tolerance < 0f)) ||
            (status == "UNSET" && hasValues)
        ) {
            return Event.Error("MALFORMED_PROFILE_EMPTY_ACK")
        }
        return Event.ProfileEmptyDistanceResult(
            operation = operation!!, success = true, status = status,
            distanceMm = distance, toleranceMm = tolerance, sensorProfile = profile,
            generation = generation, emptyRevision = revision, supported = null,
            reason = null, raw = raw,
        )
    }

    private fun parseEmptyDistance(raw: String): Event.EmptyDistanceResult? {
        val isAck = raw.startsWith("ACK:EMPTY", ignoreCase = true)
        val isNack = raw.startsWith("NACK:EMPTY", ignoreCase = true)
        if (!isAck && !isNack) return null
        val prefix = if (isAck) "ACK:EMPTY" else "NACK:EMPTY"
        val payload = raw.substringAfter(prefix, "").trim()
        val kv = parseKeyValuePayload(payload)
        return Event.EmptyDistanceResult(
            operation = kv["OP"],
            success = isAck && (parseBooleanFlag(kv["SUCCESS"]) ?: true),
            supported = parseBooleanFlag(kv["SUPPORTED"]) ?: isAck,
            status = kv["STATUS"],
            distanceMm = kv["DISTANCE_MM"]?.toFloatOrNull(),
            toleranceMm = kv["TOLERANCE_MM"]?.toFloatOrNull(),
            backend = kv["BACKEND"] ?: kv["SENSOR_BACKEND"],
            reason = kv["REASON"]?.takeIf { it.isNotBlank() }
                ?: if (isNack) payload.ifBlank { "UNKNOWN" } else null,
            raw = raw,
        )
    }

    private fun parseWeightModelClear(raw: String): Event.WeightModelClearResult? {
        val isAck = raw.startsWith("ACK:CAL_CLEAR_WEIGHT_MODEL", ignoreCase = true)
        val isNack = raw.startsWith("NACK:CAL_CLEAR_WEIGHT_MODEL", ignoreCase = true)
        if (!isAck && !isNack) return null
        val prefix = if (isAck) "ACK:CAL_CLEAR_WEIGHT_MODEL" else "NACK:CAL_CLEAR_WEIGHT_MODEL"
        val payload = raw.substringAfter(prefix, "").trim()
        val kv = parseKeyValuePayload(payload)
        return Event.WeightModelClearResult(
            success = isAck && (parseBooleanFlag(kv["SUCCESS"]) ?: true),
            supported = parseBooleanFlag(kv["SUPPORTED"]) ?: isAck,
            ready = parseBooleanFlag(kv["READY"]),
            reason = kv["REASON"]?.takeIf { it.isNotBlank() }
                ?: if (isNack) payload.ifBlank { "UNKNOWN" } else null,
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
            ready = parseBooleanFlag(kv["READY"]),
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
                ready = parseBooleanFlag(kv["READY"]),
                reason = null,
                raw = raw,
            )
        }

        val nackPayload = payloadAfterPrefix(raw, "NACK:CAL_SET_MODEL") ?: return null
        val kv = parseKeyValuePayload(nackPayload)
        return Event.CalibrationSetModelResult(
            success = false,
            type = parseCalibrationModelType(kv["TYPE"]),
            ready = parseBooleanFlag(kv["READY"]),
            reason = kv["REASON"]?.takeIf { it.isNotBlank() } ?: nackPayload.ifBlank { "UNKNOWN" },
            raw = raw,
        )
    }

    private fun parseCalibrationZeroResult(raw: String): Event.CalibrationZeroResult? {
        if (raw.startsWith("ACK:CAL_ZERO", ignoreCase = true)) {
            return Event.CalibrationZeroResult(
                success = true,
                reason = null,
                raw = raw,
            )
        }

        val nackPayload = payloadAfterPrefix(raw, "NACK:CAL_ZERO") ?: return null
        val kv = parseKeyValuePayload(nackPayload)
        return Event.CalibrationZeroResult(
            success = false,
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
            degradedStartAvailable = parseBooleanFlag(kv["DEGRADED_START_AVAILABLE"]),
            degradedStartEnabled = parseBooleanFlag(kv["DEGRADED_START_ENABLED"]),
            leaveStopEnabled = parseBooleanFlag(kv["LEAVE_STOP_ENABLED"]),
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
            startReady = parseBooleanFlag(kv["START_READY"]),
            baselineReady = parseBooleanFlag(kv["BASELINE_READY"]) ?: false,
            stableWeightKg = kv["STABLE_WEIGHT"]?.toFloatOrNull(),
            ma12WeightKg = kv["MA12"]?.toFloatOrNull() ?: kv["MA7"]?.toFloatOrNull(),
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
                kv.containsKey("DISTANCE_MM") ||
                kv.containsKey("WEIGHT") ||
                kv.containsKey("MA12")
            ) {
                // The firmware is the only unit-conversion owner. Demo APP
                // consumes the explicit physical-mm field as-is and never
                // guesses a sensor-specific conversion from legacy distance.
                val distance = kv["DISTANCE_MM"]?.toFloatOrNull()
                val weight = kv["WEIGHT"]?.toFloatOrNull()
                val combinedValid = parseBooleanFlag(kv["VALID"])
                // New fields are authoritative.  For old frames, a present
                // finite value is the best quality evidence available; this
                // lets a partial frame retain its distance without treating
                // a missing weight as zero.
                val distanceValid = parseBooleanFlag(kv["DISTANCE_VALID"])
                    ?: (distance?.isFinite() == true)
                val weightValid = parseBooleanFlag(kv["WEIGHT_VALID"])
                    ?: (weight?.isFinite() == true)
                val valid = combinedValid ?: (distanceValid && weightValid)
                return Event.StreamSample(
                    carrier = MeasurementCarrier.FORMAL_EVT_STREAM,
                    sequence = kv["SEQ"]?.toLongOrNull(),
                    timestampMs = kv["TS_MS"]?.toLongOrNull(),
                    distance = distance,
                    weight = weight,
                    ma12 = kv["MA12"]?.toFloatOrNull(),
                    ma12Ready = parseBooleanFlag(kv["MA12_READY"]) ?: kv["MA12"]?.toFloatOrNull()?.let { true } ?: false,
                    valid = valid,
                    reason = kv["REASON"],
                    raw = raw,
                    distanceValid = distanceValid,
                    weightValid = weightValid,
                    distanceReason = kv["DISTANCE_REASON"] ?: kv["REASON"].takeIf { !distanceValid },
                    weightReason = kv["WEIGHT_REASON"] ?: kv["REASON"].takeIf { !weightValid },
                    sensorBackend = kv["SENSOR_BACKEND"] ?: kv["BACKEND"],
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
                distanceValid = true,
                weightValid = true,
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

    /** Unlike the older permissive parser, a configuration ACK is a write receipt. */
    private fun parseStrictFields(payload: String, allowed: Set<String>): Map<String, String>? {
        if (payload.isBlank()) return null
        val fields = linkedMapOf<String, String>()
        for (token in payload.split(STRICT_FIELD_SEPARATOR).filter { it.isNotEmpty() }) {
            if (token.count { it == '=' } != 1) return null
            val key = token.substringBefore('=').uppercase()
            val value = token.substringAfter('=')
            if (!FIELD_KEY_REGEX.matches(key) || key !in allowed || value.isBlank() ||
                fields.put(key, value) != null
            ) return null
        }
        return fields.takeIf { it.isNotEmpty() }
    }

    private fun strictPayloadAfterPrefix(raw: String, prefix: String): String? {
        if (!raw.startsWith(prefix, ignoreCase = true)) return null
        if (raw.length > prefix.length && raw[prefix.length] != ' ') return null
        return raw.substring(prefix.length).trim()
    }

    private fun parseUint32(raw: String?): Long? {
        if (raw == null || !UNSIGNED_DECIMAL_REGEX.matches(raw)) return null
        return raw.toLongOrNull()?.takeIf { it in 0L..UINT32_MAX }
    }

    private fun parseSensorProfile(raw: String?): SensorProfile? = when (raw) {
        SensorProfile.NONE.name -> SensorProfile.NONE
        SensorProfile.J100_485.name -> SensorProfile.J100_485
        SensorProfile.CYD3S1612_IIC.name -> SensorProfile.CYD3S1612_IIC
        else -> null
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
    private val STRICT_FIELD_SEPARATOR = Regex("[,;\\s]+")
    private val FIELD_KEY_REGEX = Regex("[A-Z_][A-Z0-9_]*")
    private val UNSIGNED_DECIMAL_REGEX = Regex("0|[1-9][0-9]*")
    private val DEVICE_ID_REGEX = Regex("sw1_[0-9a-f]{32}")
    private val PROFILE_CONFIG_MARKERS = setOf("OP", "SCHEMA", "SENSOR_PROFILE", "GENERATION")
    private val PROFILE_EMPTY_MARKERS = setOf("SENSOR_PROFILE", "GENERATION", "EMPTY_REVISION")
    private const val UINT32_MAX = 4_294_967_295L
    private val NUMBER_REGEX = Regex("-?\\d+(?:\\.\\d+)?")
    private val INTEGER_REGEX = Regex("-?\\d+")
}
