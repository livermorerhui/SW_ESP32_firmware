package com.sonicwave.sdk

import android.content.Context
import com.sonicwave.protocol.CapabilityResult
import com.sonicwave.protocol.Command
import com.sonicwave.protocol.Event
import com.sonicwave.protocol.ProtocolCodec
import com.sonicwave.protocol.ProtocolMode
import com.sonicwave.transport.BleScanResult
import com.sonicwave.transport.BleTransportConfig
import com.sonicwave.transport.BluetoothGattTransport
import com.sonicwave.transport.ConnectionState
import kotlinx.coroutines.CoroutineStart
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

class SonicWaveClient(
    context: Context,
    private val transportConfig: BleTransportConfig = BleTransportConfig(),
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val transport = BluetoothGattTransport(context = context, config = transportConfig)

    val connection: StateFlow<ConnectionState> = transport.connection
    val scanResults: StateFlow<List<BleScanResult>> = transport.scanResults
    val rawLines: SharedFlow<String> = transport.incomingLines
    val rawLinesWithEpoch = transport.incomingStampedLines
    val rawChunks: SharedFlow<String> = transport.incomingRawChunks
    val transportLogs: SharedFlow<String> = transport.transportLogs
    val currentConnectionEpoch: Long?
        get() = transport.activeConnectionEpoch

    private val _outgoingLines = MutableSharedFlow<String>(extraBufferCapacity = 128)
    val outgoingLines: SharedFlow<String> = _outgoingLines.asSharedFlow()

    private val _events = MutableSharedFlow<Event>(extraBufferCapacity = 128)
    val events: SharedFlow<Event> = _events.asSharedFlow()
    private val _eventsWithEpoch = MutableSharedFlow<StampedEvent>(extraBufferCapacity = 128)
    val eventsWithEpoch: SharedFlow<StampedEvent> = _eventsWithEpoch.asSharedFlow()

    private val _mode = MutableStateFlow(ProtocolMode.UNKNOWN)
    val mode: StateFlow<ProtocolMode> = _mode.asStateFlow()

    init {
        scope.launch {
            transport.incomingStampedLines.collect { incoming ->
                if (!incoming.isFromCurrentRequest(incoming.epoch, currentConnectionEpoch)) return@collect
                val event = ProtocolCodec.decode(incoming.line)
                if (event != null) {
                    if (
                        event is Event.Capabilities ||
                        event is Event.FallStopProtection ||
                        event is Event.LeaveProtection
                    ) {
                        _mode.value = ProtocolMode.PRIMARY
                    }
                    _eventsWithEpoch.emit(StampedEvent(incoming.epoch, event))
                    if (currentConnectionEpoch == incoming.epoch) _events.emit(event)
                }
            }
        }
    }

    fun startScan(preferredNamePrefixes: List<String> = emptyList()) {
        val hints = if (preferredNamePrefixes.isEmpty()) {
            transportConfig.preferredNamePrefixes
        } else {
            preferredNamePrefixes
        }
        transport.startScan(hints)
    }

    fun stopScan() {
        transport.stopScan()
    }

    suspend fun connect(deviceId: String) {
        transport.connect(deviceId)
    }

    fun disconnect() {
        transport.disconnect()
    }

    suspend fun send(command: Command, expectedEpoch: Long? = null) {
        val effectiveCommand = when (_mode.value) {
            ProtocolMode.LEGACY -> command.toLegacyFallback()
            else -> command
        }
        val line = ProtocolCodec.encode(effectiveCommand)
        _outgoingLines.tryEmit(line)
        transport.writeLine(line, expectedEpoch = expectedEpoch)
    }

    private fun requireCurrentConnection(requestEpoch: Long) {
        if (currentConnectionEpoch != requestEpoch) {
            throw IllegalStateException("BLE 连接已变化，请重新读取设备状态")
        }
    }

    suspend fun capabilityProbe(timeoutMs: Long = 1_500): CapabilityResult {
        val requestEpoch = currentConnectionEpoch
            ?: throw IllegalStateException("BLE 尚未建立当前连接")
        _mode.value = ProtocolMode.UNKNOWN
        return coroutineScope {
            val rawObserved = AtomicBoolean(false)
            val semanticResult = CompletableDeferred<Event?>()
            val collector = launch(start = CoroutineStart.UNDISPATCHED) {
                transport.incomingStampedLines.collect { incoming ->
                    if (!incoming.isFromCurrentRequest(requestEpoch, currentConnectionEpoch)) return@collect
                    rawObserved.set(true)
                    val event = ProtocolCodec.decode(incoming.line) ?: return@collect
                    if (!semanticResult.isCompleted &&
                        (event is Event.Capabilities ||
                            event is Event.Nack ||
                            event is Event.Error)
                    ) {
                        semanticResult.complete(event)
                    }
                }
            }

            try {
                val line = ProtocolCodec.encode(Command.CapabilityQuery)
                _outgoingLines.tryEmit(line)
                transport.writeLine(line, expectedEpoch = requestEpoch)

                val semanticEvent = withTimeoutOrNull(timeoutMs) { semanticResult.await() }
                requireCurrentConnection(requestEpoch)
                when (semanticEvent) {
                    is Event.Capabilities -> {
                        _mode.value = ProtocolMode.PRIMARY
                        CapabilityResult(
                            mode = ProtocolMode.PRIMARY,
                            capabilities = semanticEvent,
                            reason = "收到 ACK:CAP，使用 primary 协议",
                        )
                    }

                    is Event.Nack -> {
                        _mode.value = ProtocolMode.UNKNOWN
                        CapabilityResult(
                            mode = ProtocolMode.UNKNOWN,
                            reason = "CAP? 收到 NACK:${semanticEvent.reason}，连接可用但协议不匹配",
                        )
                    }

                    is Event.Error -> {
                        _mode.value = ProtocolMode.UNKNOWN
                        CapabilityResult(
                            mode = ProtocolMode.UNKNOWN,
                            reason = "CAP? 收到 ERR:${semanticEvent.reason}，连接可用但协议不匹配",
                        )
                    }
                    else -> {
                        if (rawObserved.get()) {
                            _mode.value = ProtocolMode.UNKNOWN
                            CapabilityResult(
                                mode = ProtocolMode.UNKNOWN,
                                reason = "CAP? 收到响应但无法识别能力回包，未降级 legacy",
                            )
                        } else {
                            _mode.value = ProtocolMode.LEGACY
                            CapabilityResult(
                                mode = ProtocolMode.LEGACY,
                                reason = "CAP? 在 ${timeoutMs}ms 内无任何响应，降级 legacy",
                            )
                        }
                    }
                }
            } finally {
                collector.cancel()
            }
        }
    }

    suspend fun setFallStopProtectionAndAwaitAck(
        enabled: Boolean,
        timeoutMs: Long = 1_500,
    ): Event.FallStopProtection = coroutineScope {
        val requestEpoch = currentConnectionEpoch
            ?: throw IllegalStateException("BLE 尚未建立当前连接")
        val semanticResult = CompletableDeferred<Event?>()
        val collector = launch(start = CoroutineStart.UNDISPATCHED) {
            transport.incomingStampedLines.collect { incoming ->
                if (!incoming.isFromCurrentRequest(requestEpoch, currentConnectionEpoch)) return@collect
                val event = ProtocolCodec.decode(incoming.line) ?: return@collect
                if (event is Event.FallStopProtection && event.enabled != enabled) return@collect
                if (!semanticResult.isCompleted &&
                    (event is Event.FallStopProtection ||
                        event is Event.Nack ||
                        event is Event.Error)
                ) {
                    semanticResult.complete(event)
                }
            }
        }

        try {
            send(Command.FallStopProtectionSet(enabled), expectedEpoch = requestEpoch)
            val event = withTimeoutOrNull(timeoutMs) { semanticResult.await() }
            requireCurrentConnection(requestEpoch)
            when (event) {
                is Event.FallStopProtection -> {
                    _mode.value = ProtocolMode.PRIMARY
                    event
                }

                is Event.Nack -> {
                    throw IllegalStateException("FALL_STOP 收到 NACK:${event.reason}")
                }

                is Event.Error -> {
                    throw IllegalStateException("FALL_STOP 收到 ERR:${event.reason}")
                }

                else -> {
                    throw IllegalStateException("FALL_STOP 在 ${timeoutMs}ms 内未收到设备确认")
                }
            }
        } finally {
            collector.cancel()
        }
    }

    suspend fun setLeaveProtectionAndAwaitAck(
        enabled: Boolean,
        timeoutMs: Long = 1_500,
    ): Event.LeaveProtection = coroutineScope {
        val requestEpoch = currentConnectionEpoch
            ?: throw IllegalStateException("BLE 尚未建立当前连接")
        val semanticResult = CompletableDeferred<Event?>()
        val collector = launch(start = CoroutineStart.UNDISPATCHED) {
            transport.incomingStampedLines.collect { incoming ->
                if (!incoming.isFromCurrentRequest(requestEpoch, currentConnectionEpoch)) return@collect
                val event = ProtocolCodec.decode(incoming.line) ?: return@collect
                if (event is Event.LeaveProtection && event.enabled != enabled) return@collect
                if (!semanticResult.isCompleted &&
                    (event is Event.LeaveProtection ||
                        event is Event.Nack ||
                        event is Event.Error)
                ) {
                    semanticResult.complete(event)
                }
            }
        }

        try {
            send(Command.LeaveProtectionSet(enabled), expectedEpoch = requestEpoch)
            val event = withTimeoutOrNull(timeoutMs) { semanticResult.await() }
            requireCurrentConnection(requestEpoch)
            when (event) {
                is Event.LeaveProtection -> {
                    _mode.value = ProtocolMode.PRIMARY
                    event
                }

                is Event.Nack -> {
                    throw IllegalStateException("LEAVE_PROTECTION 收到 NACK:${event.reason}")
                }

                is Event.Error -> {
                    throw IllegalStateException("LEAVE_PROTECTION 收到 ERR:${event.reason}")
                }

                else -> {
                    throw IllegalStateException("LEAVE_PROTECTION 在 ${timeoutMs}ms 内未收到设备确认")
                }
            }
        } finally {
            collector.cancel()
        }
    }

    suspend fun setStreamSubscriptionAndAwaitAck(
        enabled: Boolean,
        rateHz: Int = 10,
        timeoutMs: Long = 1_500,
    ): Event.StreamSubscription = coroutineScope {
        val requestEpoch = currentConnectionEpoch
            ?: throw IllegalStateException("BLE 尚未建立当前连接")
        val semanticResult = CompletableDeferred<Event?>()
        val collector = launch(start = CoroutineStart.UNDISPATCHED) {
            transport.incomingStampedLines.collect { incoming ->
                if (!incoming.isFromCurrentRequest(requestEpoch, currentConnectionEpoch)) return@collect
                val event = ProtocolCodec.decode(incoming.line) ?: return@collect
                if (event is Event.StreamSubscription &&
                    (event.enabled != enabled || event.supported != true ||
                        (enabled && event.rateHz != rateHz))
                ) return@collect
                if (!semanticResult.isCompleted &&
                    (event is Event.StreamSubscription ||
                        event is Event.Nack ||
                        event is Event.Error)
                ) {
                    semanticResult.complete(event)
                }
            }
        }

        try {
            send(Command.StreamSet(enabled = enabled, rateHz = rateHz), expectedEpoch = requestEpoch)
            val event = withTimeoutOrNull(timeoutMs) { semanticResult.await() }
            requireCurrentConnection(requestEpoch)
            when (event) {
                is Event.StreamSubscription -> {
                    if (event.enabled != enabled || event.supported != true ||
                        (enabled && event.rateHz != rateHz)
                    ) {
                        throw IllegalStateException("STREAM 确认值与请求不一致")
                    }
                    _mode.value = ProtocolMode.PRIMARY
                    event
                }

                is Event.Nack -> {
                    throw IllegalStateException("STREAM 收到 NACK:${event.reason}")
                }

                is Event.Error -> {
                    throw IllegalStateException("STREAM 收到 ERR:${event.reason}")
                }

                else -> {
                    throw IllegalStateException("STREAM 在 ${timeoutMs}ms 内未收到设备确认")
                }
            }
        } finally {
            collector.cancel()
        }
    }

    fun close() {
        transport.close()
        scope.cancel()
    }

    private fun Command.toLegacyFallback(): Command = when (this) {
        Command.CapabilityQuery -> Command.LegacyRaw("CAP?")
        Command.SnapshotQuery -> Command.LegacyRaw("SNAPSHOT?")
        is Command.DegradedStartSet -> this
        is Command.WaveSet -> Command.LegacyWaveFie(freqHz = freqHz, intensity = intensity)
        Command.WaveStart -> Command.LegacyWaveFie(enable = true)
        Command.WaveStop -> Command.LegacyWaveFie(enable = false)
        Command.ScaleZero -> Command.LegacyZero
        is Command.ScaleCal -> Command.LegacySetPs(
            zeroDistance = zeroDistance,
            scaleFactor = scaleFactor,
        )

        else -> this
    }
}
