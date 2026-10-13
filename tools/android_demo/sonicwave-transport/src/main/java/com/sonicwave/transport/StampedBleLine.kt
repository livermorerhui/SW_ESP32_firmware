package com.sonicwave.transport

/** Receive-time connection identity; needed to reject events queued across reconnects. */
data class StampedBleLine(
    val epoch: Long,
    val line: String,
)
