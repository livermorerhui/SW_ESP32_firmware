package com.sonicwave.transport

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ConnectionEpochGateTest {
    @Test
    fun queuedNotificationFromPreviousConnectionIsRejectedAfterReconnect() {
        val gate = ConnectionEpochGate()
        val firstConnection = gate.open()
        val queuedDelivery = { gate.accepts(firstConnection) }

        assertEquals(firstConnection, gate.activeEpoch())
        assertTrue(queuedDelivery())
        gate.invalidate()
        assertNull(gate.activeEpoch())
        val secondConnection = gate.open()

        assertFalse(queuedDelivery())
        assertTrue(gate.accepts(secondConnection))
    }

    @Test
    fun disconnectRejectsLateCallbackEvenBeforeAnotherConnectionOpens() {
        val gate = ConnectionEpochGate()
        val connection = gate.open()

        gate.invalidate()

        assertFalse(gate.accepts(connection))
        assertFalse(gate.accepts(0L))
    }

    @Test
    fun lateFragmentCannotJoinNextConnectionsLine() {
        val gate = ConnectionEpochGate()
        val framer = NotifyLineFramer()
        val firstConnection = gate.open()

        gate.withCurrent(firstConnection) { framer.append("ACK:DEVICE_CONFIG op=SET") }
        gate.invalidate()
        framer.clear()
        val secondConnection = gate.open()

        assertNull(gate.withCurrent(firstConnection) { framer.append(" generation=1\n") })
        assertEquals(
            listOf("ACK:DEVICE_CONFIG op=GET generation=2"),
            gate.withCurrent(secondConnection) { framer.append("ACK:DEVICE_CONFIG op=GET generation=2\n") },
        )
    }
}
