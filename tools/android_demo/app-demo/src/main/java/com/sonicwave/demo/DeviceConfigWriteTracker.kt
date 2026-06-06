package com.sonicwave.demo

import com.sonicwave.protocol.PlatformModel

internal data class DeviceConfigWriteRequest(
    val platformModel: PlatformModel,
    val laserInstalled: Boolean,
    val requestedAtMs: Long,
)

internal data class DeviceConfigWriteConfirmation(
    val request: DeviceConfigWriteRequest,
    val observedPlatformModel: PlatformModel,
    val observedLaserInstalled: Boolean,
)

internal class DeviceConfigWriteTracker {
    private var pendingRequest: DeviceConfigWriteRequest? = null

    val isPending: Boolean
        get() = pendingRequest != null

    fun start(
        platformModel: PlatformModel,
        laserInstalled: Boolean,
        requestedAtMs: Long,
    ): DeviceConfigWriteRequest {
        val request = DeviceConfigWriteRequest(
            platformModel = platformModel,
            laserInstalled = laserInstalled,
            requestedAtMs = requestedAtMs,
        )
        pendingRequest = request
        return request
    }

    fun clear(): DeviceConfigWriteRequest? {
        val request = pendingRequest
        pendingRequest = null
        return request
    }

    fun genericAck(): DeviceConfigWriteRequest? = clear()

    fun failure(): DeviceConfigWriteRequest? = clear()

    fun timeout(): DeviceConfigWriteRequest? = clear()

    fun shouldRefreshConfirmation(isConnected: Boolean): Boolean {
        return isPending && isConnected
    }

    fun confirmObserved(
        observedPlatformModel: PlatformModel?,
        observedLaserInstalled: Boolean?,
    ): DeviceConfigWriteConfirmation? {
        val request = pendingRequest ?: return null
        val confirmedPlatformModel = observedPlatformModel ?: return null
        val confirmedLaserInstalled = observedLaserInstalled ?: return null
        if (!doesObservedDeviceConfigMatchRequested(
                requestedPlatformModel = request.platformModel,
                requestedLaserInstalled = request.laserInstalled,
                observedPlatformModel = confirmedPlatformModel,
                observedLaserInstalled = confirmedLaserInstalled,
            )
        ) {
            return null
        }
        pendingRequest = null
        return DeviceConfigWriteConfirmation(
            request = request,
            observedPlatformModel = confirmedPlatformModel,
            observedLaserInstalled = confirmedLaserInstalled,
        )
    }
}

internal fun doesObservedDeviceConfigMatchRequested(
    requestedPlatformModel: PlatformModel?,
    requestedLaserInstalled: Boolean?,
    observedPlatformModel: PlatformModel?,
    observedLaserInstalled: Boolean?,
): Boolean {
    return requestedPlatformModel != null &&
        requestedLaserInstalled != null &&
        observedPlatformModel == requestedPlatformModel &&
        observedLaserInstalled == requestedLaserInstalled
}
