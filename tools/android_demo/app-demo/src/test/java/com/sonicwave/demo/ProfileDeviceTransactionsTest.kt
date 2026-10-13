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

    @Test fun oldProfileSendCompletionCannotClearNewConnectionOrSecondWrite() {
        val tracker = ProfileConfigTransaction()
        val first = tracker.start("sw1_a", 7L, PlatformModel.PLUS, SensorProfile.J100_485, 0L)
        val reconnected = tracker.start("sw1_a", 8L, PlatformModel.PLUS, SensorProfile.J100_485, 0L)
        assertFalse(tracker.clearIfCurrent(7L, first))
        assertTrue(tracker.isPendingRequest(8L, reconnected))

        val second = tracker.start("sw1_a", 8L, PlatformModel.PLUS, SensorProfile.CYD3S1612_IIC, 1L)
        assertFalse(tracker.clearIfCurrent(8L, reconnected))
        assertTrue(tracker.isPendingRequest(8L, second))
        assertTrue(tracker.clearIfCurrent(8L, second))
        assertFalse(tracker.isPending)
    }

    @Test fun oldStreamAcknowledgementCannotConfirmSubscriptionAfterProfileSwitch() {
        val gate = ProfileStreamSubscriptionGate()
        val oldRequest = gate.start(7L)
        assertTrue(gate.isPendingFor(7L))
        gate.retire()
        val newRequest = gate.start(7L)
        assertFalse(gate.complete(7L, oldRequest))
        assertTrue(gate.isPendingFor(7L))
        assertFalse(gate.complete(8L, newRequest))
        assertTrue(gate.complete(7L, newRequest))
        assertFalse(gate.isPendingFor(7L))
    }

    @Test fun profileSnapshotMustBeRequestedAfterWriteBeforeReleasingRuntimeGate() {
        val gate = ProfileSnapshotRefreshGate()
        gate.arm(7L)
        assertFalse(gate.acceptSnapshot(7L)) // queued before the post-GET query
        assertFalse(gate.beginRequest(8L))
        assertTrue(gate.needsRequestFor(7L))
        assertTrue(gate.beginRequest(7L))
        assertFalse(gate.acceptSnapshot(8L))
        assertFalse(gate.requestCompleted(7L))
        assertTrue(gate.acceptSnapshot(7L))
        assertFalse(gate.acceptSnapshot(7L))
    }

    @Test fun earlySnapshotDuringGattWriteIsReleasedOnlyAfterWriteSucceeds() {
        val gate = ProfileSnapshotRefreshGate()
        gate.arm(7L)
        assertTrue(gate.beginRequest(7L))
        assertFalse(gate.acceptSnapshot(7L))
        assertTrue(gate.requestCompleted(7L))
        assertFalse(gate.acceptSnapshot(7L))

        gate.arm(8L)
        assertTrue(gate.beginRequest(8L))
        assertFalse(gate.acceptSnapshot(8L))
        gate.requestFailed(8L)
        assertTrue(gate.needsRequestFor(8L))
        assertFalse(gate.acceptSnapshot(8L))
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

    @Test fun anOldReadyModelCannotReappearAfterAnEmptyMutation() {
        assertTrue(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = 7L, modelQuerySent = false, invalidatedEpoch = null,
            replyEpoch = 7L, modelReady = true, explicitModelWriteReadback = false,
        ))
        assertTrue(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = 7L, modelQuerySent = true, invalidatedEpoch = 7L,
            replyEpoch = 7L, modelReady = true, explicitModelWriteReadback = false,
        ))
        assertFalse(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = 7L, modelQuerySent = true, invalidatedEpoch = 7L,
            replyEpoch = 7L, modelReady = false, explicitModelWriteReadback = false,
        ))
        assertFalse(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = null, modelQuerySent = false, invalidatedEpoch = 7L,
            replyEpoch = 7L, modelReady = true, explicitModelWriteReadback = true,
        ))
        assertFalse(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = 7L, modelQuerySent = false, invalidatedEpoch = 7L,
            replyEpoch = 8L, modelReady = true, explicitModelWriteReadback = false,
        ))
    }

    @Test fun verifiedProfileSwitchRetiresOldEmptyModelGuardOnSameBleLink() {
        val epoch = 7L
        assertTrue(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = null, modelQuerySent = false, invalidatedEpoch = epoch,
            replyEpoch = epoch, modelReady = true, explicitModelWriteReadback = false,
        ))
        assertTrue(hasVerifiedProfileChanged(
            previousModel = PlatformModel.PLUS, previousProfile = SensorProfile.J100_485,
            previousGeneration = 4L, model = PlatformModel.PLUS,
            profile = SensorProfile.CYD3S1612_IIC, generation = 5L,
        ))
        // The verified new profile's CAL:GET_MODEL may legitimately report ready=1.
        assertFalse(shouldIgnoreModelReplyDuringEmptyRefresh(
            refreshEpoch = null, modelQuerySent = false, invalidatedEpoch = null,
            replyEpoch = epoch, modelReady = true, explicitModelWriteReadback = false,
        ))
        assertFalse(hasVerifiedProfileChanged(
            previousModel = PlatformModel.PLUS, previousProfile = SensorProfile.J100_485,
            previousGeneration = 4L, model = PlatformModel.PLUS,
            profile = SensorProfile.J100_485, generation = 4L,
        ))
    }

    @Test fun oldEmptyGattCompletionCannotMutateNewConnectionOrRequest() {
        assertTrue(isCurrentEmptyMutation(7L, 3L, 7L, 3L, pending = true))
        assertFalse(isCurrentEmptyMutation(7L, 3L, 8L, 3L, pending = true))
        assertFalse(isCurrentEmptyMutation(7L, 3L, 7L, 4L, pending = true))
        assertFalse(isCurrentEmptyMutation(7L, 3L, 7L, 3L, pending = false))
    }

    @Test fun oldEmptyGetFailureCannotTerminateLaterMutationOnSameLink() {
        assertTrue(isCurrentEmptyVerificationGet(7L, 3L, true, 7L, 3L, 7L))
        assertFalse(isCurrentEmptyVerificationGet(7L, 3L, false, 7L, 3L, 7L))
        assertFalse(isCurrentEmptyVerificationGet(7L, 3L, true, 7L, 4L, 7L))
        assertFalse(isCurrentEmptyVerificationGet(7L, 3L, true, 8L, 3L, 8L))
    }

    @Test fun modelWriteWaitsForPostEmptyModelReadbackOnCurrentConnection() {
        assertTrue(isEmptyModelReadbackPending(refreshEpoch = 7L, currentEpoch = 7L))
        assertFalse(isEmptyModelReadbackPending(refreshEpoch = null, currentEpoch = 7L))
        assertFalse(isEmptyModelReadbackPending(refreshEpoch = 7L, currentEpoch = 8L))
    }

    @Test fun emptyModelReadbackTimeoutAffectsOnlyTheOutstandingCurrentLinkQuery() {
        assertTrue(shouldFailEmptyModelReadbackOnTimeout(7L, 7L, 7L, querySent = true, pending = true))
        assertFalse(shouldFailEmptyModelReadbackOnTimeout(null, 7L, 7L, querySent = true, pending = true))
        assertFalse(shouldFailEmptyModelReadbackOnTimeout(7L, 7L, 8L, querySent = true, pending = true))
        assertFalse(shouldFailEmptyModelReadbackOnTimeout(7L, 7L, 7L, querySent = false, pending = true))
        assertFalse(shouldFailEmptyModelReadbackOnTimeout(7L, 7L, 7L, querySent = true, pending = false))
    }
}
