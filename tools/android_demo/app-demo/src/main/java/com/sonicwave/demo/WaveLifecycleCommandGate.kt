package com.sonicwave.demo

internal class WaveLifecycleCommandGate {
    private var epoch: Long = 0L

    fun beginStart(): Long {
        epoch += 1L
        return epoch
    }

    fun invalidateForStop(): Long {
        epoch += 1L
        return epoch
    }

    fun canContinueStart(
        token: Long,
        hasPendingStart: Boolean,
        hasPendingStop: Boolean,
    ): Boolean = token == epoch && hasPendingStart && !hasPendingStop
}
