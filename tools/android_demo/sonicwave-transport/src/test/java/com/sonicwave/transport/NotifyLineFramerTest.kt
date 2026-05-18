package com.sonicwave.transport

import kotlin.test.Test
import kotlin.test.assertEquals

class NotifyLineFramerTest {
    @Test
    fun retainsHalfLineUntilNextChunk() {
        val framer = NotifyLineFramer()

        assertEquals(emptyList(), framer.append("ACK:CAP fw=SW-HUB"))
        assertEquals(
            listOf("ACK:CAP fw=SW-HUB-1.0.0 proto=2"),
            framer.append("-1.0.0 proto=2\n"),
        )
    }

    @Test
    fun emitsMultipleLinesFromSingleChunk() {
        val framer = NotifyLineFramer()

        assertEquals(
            listOf(
                "SNAPSHOT: top_state=ARMED runtime_ready=1",
                "EVT:STREAM:120.35,66.80",
                "EVT:STABLE:65.20",
            ),
            framer.append(
                "SNAPSHOT: top_state=ARMED runtime_ready=1\r\n" +
                    "EVT:STREAM:120.35,66.80\n" +
                    "EVT:STABLE:65.20\n",
            ),
        )
    }

    @Test
    fun skipsEmptyLinesAndRetainsTrailingPartialLine() {
        val framer = NotifyLineFramer()

        assertEquals(
            listOf("EVT:STATE RUNNING"),
            framer.append("\nEVT:STATE RUNNING\nSNAPSHOT: top_state=IDLE"),
        )
        assertEquals(
            listOf("SNAPSHOT: top_state=IDLE user_present=0"),
            framer.append(" user_present=0\n"),
        )
    }
}
