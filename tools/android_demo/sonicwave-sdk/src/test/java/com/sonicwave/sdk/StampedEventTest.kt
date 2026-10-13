package com.sonicwave.sdk

import com.sonicwave.transport.StampedBleLine
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class StampedEventTest {
    @Test
    fun oldQueuedAckCannotSatisfyNewConnectionRequest() {
        val oldAck = StampedBleLine(epoch = 2L, line = "ACK:STREAM enabled=1")
        val newAck = StampedBleLine(epoch = 4L, line = "ACK:STREAM enabled=1")

        assertFalse(oldAck.isFromCurrentRequest(requestEpoch = 4L, activeEpoch = 4L))
        assertTrue(newAck.isFromCurrentRequest(requestEpoch = 4L, activeEpoch = 4L))
    }

    @Test
    fun disconnectedRequestCannotConsumePreviouslyBufferedAck() {
        val queuedAck = StampedBleLine(epoch = 2L, line = "ACK:CAP proto=2")

        assertFalse(queuedAck.isFromCurrentRequest(requestEpoch = 2L, activeEpoch = null))
        assertFalse(queuedAck.isFromCurrentRequest(requestEpoch = 2L, activeEpoch = 4L))
    }
}
