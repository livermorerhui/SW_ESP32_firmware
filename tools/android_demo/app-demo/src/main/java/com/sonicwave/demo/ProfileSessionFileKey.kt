package com.sonicwave.demo

import java.security.MessageDigest

/** File names never contain a caller-provided ID or profile path segment. */
internal object ProfileSessionFileKey {
    private val deviceIdPattern = Regex("sw1_[0-9a-f]{32}")
    private val sensorProfilePattern = Regex("[A-Z][A-Z0-9_]{0,63}")
    private const val HEX_DIGITS = "0123456789abcdef"

    fun fileName(deviceId: String, sensorProfile: String): String {
        require(deviceIdPattern.matches(deviceId)) { "Invalid stable device ID" }
        require(sensorProfilePattern.matches(sensorProfile)) { "Invalid sensor profile ID" }
        val digest = MessageDigest.getInstance("SHA-256")
            .digest("$deviceId\u0000$sensorProfile".toByteArray(Charsets.UTF_8))
        return buildString(85) {
            append("demo_session_v3_")
            digest.forEach { byte ->
                val value = byte.toInt() and 0xff
                append(HEX_DIGITS[value ushr 4])
                append(HEX_DIGITS[value and 0x0f])
            }
            append(".json")
        }
    }
}
