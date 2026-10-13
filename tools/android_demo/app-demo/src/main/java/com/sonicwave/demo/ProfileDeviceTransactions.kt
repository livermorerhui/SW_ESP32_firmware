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
        val model: PlatformModel,
        val profile: SensorProfile,
        val previousGeneration: Long,
        val allowSameGeneration: Boolean,
        var acceptedGeneration: Long? = null,
    )

    private var pending: Pending? = null
    val isPending: Boolean get() = pending != null

    fun start(
        deviceId: String,
        connectionEpoch: Long,
        model: PlatformModel,
        profile: SensorProfile,
        previousGeneration: Long,
        allowSameGeneration: Boolean = false,
    ) {
        require(deviceId.isNotBlank() && connectionEpoch > 0 && previousGeneration >= 0)
        pending = Pending(deviceId, connectionEpoch, model, profile, previousGeneration, allowSameGeneration)
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
