package com.sonicwave.demo

import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.ArrayDeque

internal data class RawConsoleAppendResult(
    val state: RawConsoleUiState,
    val forcePublish: Boolean,
)

internal class RawConsoleStore(
    private val maxLines: Int,
    private val timeProvider: () -> LocalTime = { LocalTime.now() },
) {
    private val rawLogLines = ArrayDeque<String>()

    fun reset(): RawConsoleUiState {
        rawLogLines.clear()
        return currentState()
    }

    fun currentState(): RawConsoleUiState {
        return RawConsoleUiState(rawLogLines = rawLogLines.toList())
    }

    fun append(
        direction: String,
        payload: String,
        forcePublish: Boolean = false,
        trackTestSessions: Boolean = false,
    ): RawConsoleAppendResult {
        val line = "${LOG_TIME_FORMATTER.format(timeProvider())} [$direction] $payload"
        if (rawLogLines.size >= maxLines) {
            rawLogLines.removeFirst()
        }
        rawLogLines.addLast(line)
        return RawConsoleAppendResult(
            state = currentState(),
            forcePublish = forcePublish || isHighPriorityLog(line, trackTestSessions),
        )
    }

    fun shouldAppendIncomingRawLine(
        line: String,
        verboseStreamLogsEnabled: Boolean,
    ): Boolean {
        if (verboseStreamLogsEnabled) return true
        if (line.startsWith("EVT:STREAM", ignoreCase = true)) return false
        return !CSV_STREAM_REGEX.matches(line.trim())
    }

    fun isHighPriorityLog(
        line: String,
        trackTestSessions: Boolean,
    ): Boolean {
        return line.contains("EVT:FAULT") ||
            line.contains("EVT:SAFETY") ||
            line.contains("[FAULT]") ||
            (trackTestSessions && line.contains("[TEST_SESSION]")) ||
            line.contains("[DEVICE_CONFIG]") ||
            line.contains("[LAYER:MEASUREMENT_CONSUME]") ||
            line.contains("[MEASUREMENT_CONSUME_SUMMARY]") ||
            line.contains("[CAL_CAPTURE_ATTEMPT]") ||
            line.contains("[CAL_CAPTURE_RESULT]") ||
            line.contains("[STREAM_SUBSCRIPTION_RESULT]")
    }

    private companion object {
        private val CSV_STREAM_REGEX = Regex("""^-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?$""")
        private val LOG_TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")
    }
}
