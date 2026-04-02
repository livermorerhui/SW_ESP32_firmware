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
    val rawChunks: SharedFlow<String> = transport.incomingRawChunks
    val transportLogs: SharedFlow<String> = transport.transportLogs

    private val _outgoingLines = MutableSharedFlow<String>(extraBufferCapacity = 128)
    val outgoingLines: SharedFlow<String> = _outgoingLines.asSharedFlow()

    private val _events = MutableSharedFlow<Event>(extraBufferCapacity = 128)
    val events: SharedFlow<Event> = _events.asSharedFlow()

    private val _mode = MutableStateFlow(ProtocolMode.UNKNOWN)
    val mode: StateFlow<ProtocolMode> = _mode.asStateFlow()

    init {
        scope.launch {
            transport.incomingLines.collect { line ->
                val event = ProtocolCodec.decode(line)
                if (event != null) {
                    if (event is Event.Capabilities) {
                        _mode.value = ProtocolMode.PRIMARY
                    }
                    _events.emit(event)
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

    suspend fun send(command: Command) {
        val effectiveCommand = when (_mode.value) {
            ProtocolMode.LEGACY -> command.toLegacyFallback()
            else -> command
        }
        val line = ProtocolCodec.encode(effectiveCommand)
        _outgoingLines.tryEmit(line)
        transport.writeLine(line)
    }

    suspend fun capabilityProbe(timeoutMs: Long = 1_500): CapabilityResult {
        _mode.value = ProtocolMode.UNKNOWN
        return coroutineScope {
            val rawObserved = AtomicBoolean(false)
            val semanticResult = CompletableDeferred<Event?>()
            val collector = launch {
                transport.incomingLines.collect { line ->
                    rawObserved.set(true)
                    val event = ProtocolCodec.decode(line) ?: return@collect
                    if (!semanticResult.isCompleted &&
                        (event is Event.Capabilities ||
                            event is Event.Nack ||
                            event is Event.Error ||
                            event is Event.Ack)
                    ) {
                        semanticResult.complete(event)
                    }
                }
            }

            try {
                val line = ProtocolCodec.encode(Command.CapabilityQuery)
                _outgoingLines.tryEmit(line)
                transport.writeLine(line)

                when (val semanticEvent = withTimeoutOrNull(timeoutMs) { semanticResult.await() }) {
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

                    is Event.Ack -> {
                        _mode.value = ProtocolMode.UNKNOWN
                        CapabilityResult(
                            mode = ProtocolMode.UNKNOWN,
                            reason = "CAP? 收到 ACK 但非能力回包: ${semanticEvent.raw}",
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

    fun close() {
        transport.close()
        scope.cancel()
    }

    private fun Command.toLegacyFallback(): Command = when (this) {
        Command.CapabilityQuery -> Command.LegacyRaw("CAP?")
        Command.SnapshotQuery -> Command.LegacyRaw("SNAPSHOT?")
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
