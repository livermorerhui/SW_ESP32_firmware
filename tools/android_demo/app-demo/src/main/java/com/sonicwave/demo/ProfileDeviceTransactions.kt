package com.sonicwave.demo

import com.sonicwave.protocol.Event
import com.sonicwave.protocol.PlatformModel
import com.sonicwave.protocol.SensorProfile

internal data class ProfileConfigConfirmation(
    val platformModel: PlatformModel,
    val sensorProfile: SensorProfile,
    val generation: Long,
)

/** A SET ACK is acceptance, not the device truth shown to the operator. */
internal class ProfileConfigTransaction {
    private data class Pending(
        val deviceId: String,
        val connectionEpoch: Long,
        val requestSerial: Long,
        val model: PlatformModel,
        val profile: SensorProfile,
        val previousGeneration: Long,
        val allowSameGeneration: Boolean,
        var acceptedGeneration: Long? = null,
    )

    private var pending: Pending? = null
    private var nextRequestSerial = 0L
    val isPending: Boolean get() = pending != null

    fun start(
        deviceId: String,
        connectionEpoch: Long,
        model: PlatformModel,
        profile: SensorProfile,
        previousGeneration: Long,
        allowSameGeneration: Boolean = false,
    ): Long {
        require(deviceId.isNotBlank() && connectionEpoch > 0 && previousGeneration >= 0)
        val serial = ++nextRequestSerial
        pending = Pending(deviceId, connectionEpoch, serial, model, profile, previousGeneration, allowSameGeneration)
        return serial
    }

    fun pendingSerialFor(connectionEpoch: Long): Long? =
        pending?.takeIf { it.connectionEpoch == connectionEpoch }?.requestSerial

    fun isPendingRequest(connectionEpoch: Long, requestSerial: Long): Boolean =
        pending?.let { it.connectionEpoch == connectionEpoch && it.requestSerial == requestSerial } == true

    fun clearIfCurrent(connectionEpoch: Long, requestSerial: Long): Boolean {
        if (!isPendingRequest(connectionEpoch, requestSerial)) return false
        pending = null
        return true
    }

    fun onSetAck(event: Event.ProfileConfigResult, deviceId: String?, connectionEpoch: Long): Boolean {
        val request = pending ?: return false
        val generation = event.generation ?: return false
        if (!event.success || event.operation != "SET" || event.schema != 1 ||
            deviceId != request.deviceId || connectionEpoch != request.connectionEpoch ||
            event.platformModel != request.model || event.sensorProfile != request.profile ||
            (if (request.allowSameGeneration) generation < request.previousGeneration
             else generation <= request.previousGeneration)
        ) return false
        request.acceptedGeneration = generation
        return true
    }

    fun onGet(event: Event.ProfileConfigResult, deviceId: String?, connectionEpoch: Long): ProfileConfigConfirmation? {
        val request = pending ?: return null
        if (!event.success || event.operation != "GET" || event.schema != 1 ||
            deviceId != request.deviceId || connectionEpoch != request.connectionEpoch ||
            event.platformModel != request.model || event.sensorProfile != request.profile ||
            event.generation != request.acceptedGeneration || request.acceptedGeneration == null
        ) return null
        pending = null
        return ProfileConfigConfirmation(request.model, request.profile, event.generation!!)
    }

    fun clear() { pending = null }
}

/** A STREAM ACK from before a profile switch must not confirm the new profile's stream. */
internal class ProfileStreamSubscriptionGate {
    private data class Pending(val connectionEpoch: Long, val requestSerial: Long)

    private var nextRequestSerial = 0L
    private var pending: Pending? = null

    fun isPendingFor(connectionEpoch: Long): Boolean = pending?.connectionEpoch == connectionEpoch

    fun start(connectionEpoch: Long): Long {
        require(connectionEpoch > 0)
        val requestSerial = ++nextRequestSerial
        pending = Pending(connectionEpoch, requestSerial)
        return requestSerial
    }

    fun complete(connectionEpoch: Long, requestSerial: Long): Boolean {
        if (pending != Pending(connectionEpoch, requestSerial)) return false
        pending = null
        return true
    }

    fun retire() {
        ++nextRequestSerial
        pending = null
    }
}

/** A profile write must wait for a SNAPSHOT requested after independent GET. */
internal class ProfileSnapshotRefreshGate {
    private enum class Phase { UNREQUESTED, SENDING, AWAITING_RESPONSE }

    private var pendingEpoch: Long? = null
    private var phase = Phase.UNREQUESTED
    private var snapshotSeenDuringSend = false

    fun arm(connectionEpoch: Long) {
        require(connectionEpoch > 0)
        pendingEpoch = connectionEpoch
        phase = Phase.UNREQUESTED
        snapshotSeenDuringSend = false
    }

    fun beginRequest(connectionEpoch: Long): Boolean {
        if (!needsRequestFor(connectionEpoch)) return false
        phase = Phase.SENDING
        return true
    }

    fun needsRequestFor(connectionEpoch: Long): Boolean =
        pendingEpoch == connectionEpoch && phase == Phase.UNREQUESTED

    /** Returns true when the reply arrived before the GATT write returned. */
    fun requestCompleted(connectionEpoch: Long): Boolean {
        if (pendingEpoch != connectionEpoch || phase != Phase.SENDING) return false
        if (snapshotSeenDuringSend) {
            retire()
            return true
        }
        phase = Phase.AWAITING_RESPONSE
        return false
    }

    fun requestFailed(connectionEpoch: Long) {
        if (pendingEpoch != connectionEpoch || phase != Phase.SENDING) return
        phase = Phase.UNREQUESTED
        snapshotSeenDuringSend = false
    }

    fun acceptSnapshot(connectionEpoch: Long): Boolean {
        if (pendingEpoch != connectionEpoch) return false
        if (phase == Phase.SENDING) {
            snapshotSeenDuringSend = true
            return false
        }
        if (phase != Phase.AWAITING_RESPONSE) return false
        retire()
        return true
    }

    fun retire() {
        pendingEpoch = null
        phase = Phase.UNREQUESTED
        snapshotSeenDuringSend = false
    }
}

internal data class ProfileEmptyConfirmation(
    val status: String,
    val distanceMm: Float?,
    val toleranceMm: Float?,
    val revision: Long,
)

/** SET_CURRENT/CLEAR become visible only after a matching independent GET. */
internal class ProfileEmptyTransaction {
    private data class Pending(
        val deviceId: String,
        val connectionEpoch: Long,
        val profile: SensorProfile,
        val generation: Long,
        val operation: String,
        val previousRevision: Long,
        var accepted: Event.ProfileEmptyDistanceResult? = null,
    )

    private var pending: Pending? = null
    val isPending: Boolean get() = pending != null

    fun start(
        deviceId: String,
        connectionEpoch: Long,
        profile: SensorProfile,
        generation: Long,
        operation: String,
        previousRevision: Long,
    ) {
        require(deviceId.isNotBlank() && connectionEpoch > 0 && profile != SensorProfile.NONE)
        require(operation == "SET_CURRENT" || operation == "CLEAR")
        pending = Pending(deviceId, connectionEpoch, profile, generation, operation, previousRevision)
    }

    fun onCommandAck(event: Event.ProfileEmptyDistanceResult, deviceId: String?, connectionEpoch: Long): Boolean {
        val request = pending ?: return false
        val revision = event.emptyRevision ?: return false
        if (!event.success || event.operation != request.operation ||
            deviceId != request.deviceId || connectionEpoch != request.connectionEpoch ||
            event.sensorProfile != request.profile || event.generation != request.generation ||
            revision <= request.previousRevision ||
            (request.operation == "SET_CURRENT" && event.status != "SET") ||
            (request.operation == "CLEAR" && event.status != "UNSET")
        ) return false
        request.accepted = event
        return true
    }

    fun onGet(event: Event.ProfileEmptyDistanceResult, deviceId: String?, connectionEpoch: Long): ProfileEmptyConfirmation? {
        val request = pending ?: return null
        val accepted = request.accepted ?: return null
        if (!event.success || event.operation != "GET" ||
            deviceId != request.deviceId || connectionEpoch != request.connectionEpoch ||
            event.sensorProfile != request.profile || event.generation != request.generation ||
            event.emptyRevision != accepted.emptyRevision || event.status != accepted.status ||
            event.distanceMm != accepted.distanceMm || event.toleranceMm != accepted.toleranceMm
        ) return null
        pending = null
        return ProfileEmptyConfirmation(event.status!!, event.distanceMm, event.toleranceMm, event.emptyRevision!!)
    }

    fun clear() { pending = null }
}

/** Model replies have no empty_revision, so a new EMPTY revision rejects old ready=1 replies. */
internal fun shouldIgnoreModelReplyDuringEmptyRefresh(
    refreshEpoch: Long?,
    modelQuerySent: Boolean,
    invalidatedEpoch: Long?,
    replyEpoch: Long,
    modelReady: Boolean?,
    explicitModelWriteReadback: Boolean,
): Boolean = (refreshEpoch == replyEpoch && !modelQuerySent) ||
    (invalidatedEpoch == replyEpoch && modelReady == true && !explicitModelWriteReadback)

/** A verified config with a new identity/generation owns a fresh model readback on this BLE link. */
internal fun hasVerifiedProfileChanged(
    previousModel: PlatformModel?,
    previousProfile: SensorProfile?,
    previousGeneration: Long?,
    model: PlatformModel,
    profile: SensorProfile,
    generation: Long,
): Boolean = previousModel != null && previousProfile != null && previousGeneration != null &&
    (previousModel != model || previousProfile != profile || previousGeneration != generation)

/** Only the still-pending request may update EMPTY UI after a GATT completion or timeout. */
internal fun isCurrentEmptyMutation(
    requestEpoch: Long,
    requestSerial: Long,
    currentEpoch: Long?,
    currentSerial: Long,
    pending: Boolean,
): Boolean = pending && currentEpoch == requestEpoch && currentSerial == requestSerial

/** An earlier EMPTY:GET failure cannot terminate a later mutation on the same BLE link. */
internal fun isCurrentEmptyVerificationGet(
    requestEpoch: Long,
    requestSerial: Long,
    refreshPendingAtSend: Boolean,
    currentEpoch: Long?,
    currentSerial: Long,
    refreshEpoch: Long?,
): Boolean = refreshPendingAtSend && currentEpoch == requestEpoch &&
    currentSerial == requestSerial && refreshEpoch == requestEpoch

internal fun isEmptyModelReadbackPending(refreshEpoch: Long?, currentEpoch: Long?): Boolean =
    refreshEpoch != null && refreshEpoch == currentEpoch

internal fun shouldFailEmptyModelReadbackOnTimeout(
    refreshEpoch: Long?,
    replyEpoch: Long,
    currentEpoch: Long?,
    querySent: Boolean,
    pending: Boolean,
): Boolean = refreshEpoch == replyEpoch && currentEpoch == replyEpoch && querySent && pending
