package com.sonicwave.demo

import com.sonicwave.protocol.Event
import com.sonicwave.protocol.PlatformModel
import com.sonicwave.protocol.SensorProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileDeviceTransactionsTest {
    private fun config(op: String, generation: Long, profile: SensorProfile = SensorProfile.J100_485) =
        Event.ProfileConfigResult(op, true, 1, PlatformModel.PLUS, profile, true, generation, null, "ACK")

    private fun empty(op: String, revision: Long, distance: Float? = 112.56f) =
        Event.ProfileEmptyDistanceResult(
            op, true, if (distance == null) "UNSET" else "SET", distance,
            if (distance == null) null else 0.10f, SensorProfile.J100_485, 4L,
            revision, null, null, "ACK",
        )

    @Test fun configNeedsAckThenIndependentGetFromSameIdentityAndEpoch() {
        val tracker = ProfileConfigTransaction()
        tracker.start("sw1_a", 7L, PlatformModel.PLUS, SensorProfile.J100_485, 3L)
        assertNull(tracker.onGet(config("GET", 4L), "sw1_a", 7L))
        assertFalse(tracker.onSetAck(config("SET", 4L), "sw1_b", 7L))
        assertFalse(tracker.onSetAck(config("SET", 3L), "sw1_a", 7L))
        assertTrue(tracker.onSetAck(config("SET", 4L), "sw1_a", 7L))
        assertNull(tracker.onGet(config("GET", 4L), "sw1_a", 8L))
        assertNull(tracker.onGet(config("GET", 5L), "sw1_a", 7L))
        assertNull(tracker.onGet(config("GET", 4L, SensorProfile.CYD3S1612_IIC), "sw1_a", 7L))
        assertNotNull(tracker.onGet(config("GET", 4L), "sw1_a", 7L))
        assertFalse(tracker.isPending)
    }

    @Test fun explicitSameBaseCanConfirmWithoutGenerationChange() {
        val tracker = ProfileConfigTransaction()
        val base = Event.ProfileConfigResult("SET", true, 1, PlatformModel.BASE, SensorProfile.NONE, false, 0L, null, "ACK")
        val get = base.copy(operation = "GET")
        tracker.start("sw1_a", 1L, PlatformModel.BASE, SensorProfile.NONE, 0L, allowSameGeneration = true)
        assertTrue(tracker.onSetAck(base, "sw1_a", 1L))
        assertEquals(0L, tracker.onGet(get, "sw1_a", 1L)?.generation)
    }

    @Test fun emptySetCurrentNeedsExactRevisionAndValueReadback() {
        val tracker = ProfileEmptyTransaction()
        tracker.start("sw1_a", 7L, SensorProfile.J100_485, 4L, "SET_CURRENT", 8L)
        assertNull(tracker.onGet(empty("GET", 9L), "sw1_a", 7L))
        assertFalse(tracker.onCommandAck(empty("SET_CURRENT", 8L), "sw1_a", 7L))
        assertTrue(tracker.onCommandAck(empty("SET_CURRENT", 9L), "sw1_a", 7L))
        assertNull(tracker.onGet(empty("GET", 9L), "sw1_a", 8L))
        assertNull(tracker.onGet(empty("GET", 10L), "sw1_a", 7L))
        assertNull(tracker.onGet(empty("GET", 9L, 112.55f), "sw1_a", 7L))
        assertEquals(112.56f, tracker.onGet(empty("GET", 9L), "sw1_a", 7L)?.distanceMm)
        assertFalse(tracker.isPending)
    }

    @Test fun clearRequiresUnsetReadback() {
        val tracker = ProfileEmptyTransaction()
        tracker.start("sw1_a", 1L, SensorProfile.J100_485, 4L, "CLEAR", 9L)
        assertTrue(tracker.onCommandAck(empty("CLEAR", 10L, null), "sw1_a", 1L))
        assertNull(tracker.onGet(empty("GET", 10L), "sw1_a", 1L))
        assertEquals("UNSET", tracker.onGet(empty("GET", 10L, null), "sw1_a", 1L)?.status)
    }
}
