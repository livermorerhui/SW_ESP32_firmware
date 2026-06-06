package com.sonicwave.demo

import java.time.LocalTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class RawConsoleStoreTest {
    @Test
    fun appendFormatsLineWithDirectionAndTimestamp() {
        val store = storeAt(hour = 12, minute = 34, second = 56, nano = 789_000_000)

        val result = store.append(direction = "SYS", payload = "boot")

        assertEquals(listOf("12:34:56.789 [SYS] boot"), result.state.rawLogLines)
        assertFalse(result.forcePublish)
    }

    @Test
    fun appendTrimsOldestLinesWhenMaxLineCountIsReached() {
        val store = RawConsoleStore(maxLines = 2, timeProvider = { LocalTime.NOON })

        store.append(direction = "SYS", payload = "first")
        store.append(direction = "SYS", payload = "second")
        val result = store.append(direction = "SYS", payload = "third")

        assertEquals(
            listOf(
                "12:00:00.000 [SYS] second",
                "12:00:00.000 [SYS] third",
            ),
            result.state.rawLogLines,
        )
    }

    @Test
    fun incomingStreamLinesAreHiddenUnlessVerboseIsEnabled() {
        val store = RawConsoleStore(maxLines = 20)

        assertFalse(store.shouldAppendIncomingRawLine("EVT:STREAM seq=1 distance=100 weight=70", false))
        assertTrue(store.shouldAppendIncomingRawLine("EVT:STREAM seq=1 distance=100 weight=70", true))
    }

    @Test
    fun incomingLegacyCsvFallbackLinesAreHiddenUnlessVerboseIsEnabled() {
        val store = RawConsoleStore(maxLines = 20)

        assertFalse(store.shouldAppendIncomingRawLine("123.4,56.7", false))
        assertFalse(store.shouldAppendIncomingRawLine(" -123.4,56.7 ".trim(), false))
        assertTrue(store.shouldAppendIncomingRawLine("123.4,56.7", true))
    }

    @Test
    fun normalIncomingLinesRemainVisibleWhenVerboseIsDisabled() {
        val store = RawConsoleStore(maxLines = 20)

        assertTrue(store.shouldAppendIncomingRawLine("EVT:STATE RUNNING", false))
        assertTrue(store.shouldAppendIncomingRawLine("ACK:OK", false))
    }

    @Test
    fun captureAndMeasurementLogsForcePublish() {
        val store = RawConsoleStore(maxLines = 20, timeProvider = { LocalTime.NOON })

        assertTrue(store.append("SYS", "[MEASUREMENT_CONSUME_SUMMARY] valid_count=1").forcePublish)
        assertTrue(store.append("SYS", "[CAL_CAPTURE_RESULT] result=success").forcePublish)
        assertTrue(store.append("SYS", "[STREAM_SUBSCRIPTION_RESULT] enabled=true").forcePublish)
    }

    @Test
    fun testSessionLogsOnlyForcePublishWhenTrackingIsEnabled() {
        val store = RawConsoleStore(maxLines = 20, timeProvider = { LocalTime.NOON })

        assertFalse(store.append("SYS", "[TEST_SESSION] started", trackTestSessions = false).forcePublish)
        assertTrue(store.append("SYS", "[TEST_SESSION] stopped", trackTestSessions = true).forcePublish)
    }

    @Test
    fun explicitForcePublishIsPreservedForNormalLines() {
        val store = RawConsoleStore(maxLines = 20, timeProvider = { LocalTime.NOON })

        assertTrue(store.append("SYS", "normal", forcePublish = true).forcePublish)
    }

    @Test
    fun resetClearsRawConsoleState() {
        val store = RawConsoleStore(maxLines = 20, timeProvider = { LocalTime.NOON })
        store.append("SYS", "line")

        val state = store.reset()

        assertTrue(state.rawLogLines.isEmpty())
    }

    private fun storeAt(
        hour: Int,
        minute: Int,
        second: Int,
        nano: Int,
    ): RawConsoleStore {
        return RawConsoleStore(
            maxLines = 20,
            timeProvider = { LocalTime.of(hour, minute, second, nano) },
        )
    }
}
