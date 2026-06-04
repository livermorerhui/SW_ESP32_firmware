package com.sonicwave.demo

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class WaveLifecycleCommandGateTest {
    @Test
    fun stopInvalidatesInFlightStart() {
        val gate = WaveLifecycleCommandGate()
        val startToken = gate.beginStart()

        gate.invalidateForStop()

        assertFalse(
            gate.canContinueStart(
                token = startToken,
                hasPendingStart = false,
                hasPendingStop = true,
            ),
        )
    }

    @Test
    fun currentStartCanContinueWhenNoStopIsPending() {
        val gate = WaveLifecycleCommandGate()
        val startToken = gate.beginStart()

        assertTrue(
            gate.canContinueStart(
                token = startToken,
                hasPendingStart = true,
                hasPendingStop = false,
            ),
        )
    }

    @Test
    fun newStartAfterStopGetsFreshToken() {
        val gate = WaveLifecycleCommandGate()
        val oldStartToken = gate.beginStart()
        gate.invalidateForStop()
        val newStartToken = gate.beginStart()

        assertFalse(
            gate.canContinueStart(
                token = oldStartToken,
                hasPendingStart = true,
                hasPendingStop = false,
            ),
        )
        assertTrue(
            gate.canContinueStart(
                token = newStartToken,
                hasPendingStart = true,
                hasPendingStop = false,
            ),
        )
    }
}
