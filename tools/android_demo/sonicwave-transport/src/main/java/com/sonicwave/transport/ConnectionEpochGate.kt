package com.sonicwave.transport

/** Invalidates callbacks and queued notifications from an earlier GATT connection. */
internal class ConnectionEpochGate {
    private val lock = Any()
    private var current = 0L
    private var active = false

    fun open(): Long = synchronized(lock) {
        active = true
        ++current
    }

    fun invalidate() {
        synchronized(lock) {
            active = false
            ++current
        }
    }

    fun activeEpoch(): Long? = synchronized(lock) { current.takeIf { active } }

    fun accepts(epoch: Long): Boolean = synchronized(lock) { active && epoch > 0L && current == epoch }

    /** Prevent an old connection's late fragment from entering a new line frame. */
    fun <T> withCurrent(epoch: Long, block: () -> T): T? = synchronized(lock) {
        if (active && epoch > 0L && current == epoch) block() else null
    }
}
