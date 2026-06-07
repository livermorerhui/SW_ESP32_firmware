package com.sonicwave.demo

internal data class PendingWaveStartRequest(
    val freq: Int,
    val intensity: Int,
    val requestedAtMs: Long,
)

internal data class PendingWaveStopRequest(
    val requestedAtMs: Long,
)

internal data class PendingWaveStopCompletion(
    val result: String,
    val stopReason: String,
    val stopSource: String,
)

internal data class PendingWaveStopConsumption(
    val request: PendingWaveStopRequest?,
    val completion: PendingWaveStopCompletion?,
)

internal class WavePendingLifecycleStore(
    private val nowProvider: () -> Long,
) {
    var startRequest: PendingWaveStartRequest? = null
        private set
    var stopRequest: PendingWaveStopRequest? = null
        private set
    var stopCompletion: PendingWaveStopCompletion? = null
        private set

    val hasPendingStart: Boolean
        get() = startRequest != null

    val hasPendingStop: Boolean
        get() = stopRequest != null

    val hasPendingStopCompletion: Boolean
        get() = stopCompletion != null

    val hasPendingLifecycle: Boolean
        get() = hasPendingStart || hasPendingStop || hasPendingStopCompletion

    fun clearAll() {
        startRequest = null
        stopRequest = null
        stopCompletion = null
    }

    fun beginStart(
        freq: Int,
        intensity: Int,
    ): PendingWaveStartRequest {
        stopRequest = null
        stopCompletion = null
        val request = PendingWaveStartRequest(
            freq = freq,
            intensity = intensity,
            requestedAtMs = nowProvider(),
        )
        startRequest = request
        return request
    }

    fun clearStart(): PendingWaveStartRequest? {
        val request = startRequest
        startRequest = null
        return request
    }

    fun beginStop(): PendingWaveStopRequest {
        startRequest = null
        val request = PendingWaveStopRequest(requestedAtMs = nowProvider())
        stopRequest = request
        return request
    }

    fun ensureStopRequestIf(condition: Boolean): PendingWaveStopRequest? {
        if (!condition) return stopRequest
        if (stopRequest == null) {
            stopRequest = PendingWaveStopRequest(requestedAtMs = nowProvider())
        }
        return stopRequest
    }

    fun stageStopCompletion(
        result: String,
        stopReason: String,
        stopSource: String,
    ): PendingWaveStopCompletion {
        val completion = PendingWaveStopCompletion(
            result = result,
            stopReason = stopReason,
            stopSource = stopSource,
        )
        stopCompletion = completion
        return completion
    }

    fun consumeStop(): PendingWaveStopConsumption {
        val consumption = PendingWaveStopConsumption(
            request = stopRequest,
            completion = stopCompletion,
        )
        stopRequest = null
        stopCompletion = null
        return consumption
    }

    fun clearStop(): PendingWaveStopRequest? {
        val request = stopRequest
        stopRequest = null
        stopCompletion = null
        return request
    }
}
