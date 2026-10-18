package com.sonicwave.sdk

import com.sonicwave.protocol.Event
import com.sonicwave.transport.StampedBleLine

/** The connection on which the firmware event was received. */
data class StampedEvent(
    val epoch: Long,
    val event: Event,
)

internal fun StampedBleLine.isFromCurrentRequest(
    requestEpoch: Long,
    activeEpoch: Long?,
): Boolean = epoch > 0L && epoch == requestEpoch && activeEpoch == requestEpoch
