package com.sonicwave.transport

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
import java.nio.charset.StandardCharsets
import java.util.UUID

class BluetoothGattTransport(
    context: Context,
    private val config: BleTransportConfig = BleTransportConfig(),
) {
    private val appContext = context.applicationContext
    private val bluetoothManager = appContext.getSystemService(BluetoothManager::class.java)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _scanResults = MutableStateFlow<List<BleScanResult>>(emptyList())
    val scanResults: StateFlow<List<BleScanResult>> = _scanResults.asStateFlow()

    private val _incomingLines = MutableSharedFlow<String>(extraBufferCapacity = 128)
    val incomingLines: SharedFlow<String> = _incomingLines.asSharedFlow()
    private val _incomingRawChunks = MutableSharedFlow<String>(extraBufferCapacity = 128)
    val incomingRawChunks: SharedFlow<String> = _incomingRawChunks.asSharedFlow()
    private val _transportLogs = MutableSharedFlow<String>(extraBufferCapacity = 128)
    val transportLogs: SharedFlow<String> = _transportLogs.asSharedFlow()

    private val _connection = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connection: StateFlow<ConnectionState> = _connection.asStateFlow()

    private val connectMutex = Mutex()
    private val writeMutex = Mutex()

    private var scanner: BluetoothLeScanner? = null
    private var scanCallback: ScanCallback? = null
    private val scanCache = linkedMapOf<String, BleScanResult>()

    @Volatile
    private var gatt: BluetoothGatt? = null

    @Volatile
    private var rxCharacteristic: BluetoothGattCharacteristic? = null

    @Volatile
    private var txCharacteristic: BluetoothGattCharacteristic? = null

    private var connectDeferred: CompletableDeferred<Unit>? = null
    private var writeDeferred: CompletableDeferred<Unit>? = null

    private var manualDisconnect = false
    private val lineFramer = NotifyLineFramer()
    private val notifySetupLock = Any()

    @Volatile
    private var notifySetupStarted = false

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS && newState != BluetoothGatt.STATE_CONNECTED) {
                failConnection("连接失败(status=$status)")
                closeGatt(gatt)
                clearConnectionRefs()
                return
            }

            when (newState) {
                BluetoothGatt.STATE_CONNECTED -> {
                    _connection.value = ConnectionState.DiscoveringServices
                    if (!gatt.discoverServices()) {
                        failConnection("discoverServices 调用失败")
                        closeGatt(gatt)
                    }
                }

                BluetoothGatt.STATE_DISCONNECTED -> {
                    val pendingConnect = connectDeferred
                    val message = if (manualDisconnect) {
                        null
                    } else {
                        "设备断开(status=$status)"
                    }
                    clearConnectionRefs()
                    if (message == null) {
                        _connection.value = ConnectionState.Disconnected
                    } else {
                        _connection.value = ConnectionState.Error(message)
                        pendingConnect?.completeExceptionally(IllegalStateException(message))
                    }
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                failConnection("服务发现失败(status=$status)")
                closeGatt(gatt)
                return
            }

            val uartService = gatt.getService(UART_SERVICE_UUID)
            val rx = uartService?.getCharacteristic(UART_RX_UUID)
            val tx = uartService?.getCharacteristic(UART_TX_UUID)
            if (uartService == null || rx == null || tx == null) {
                failConnection("未发现 Nordic UART UUID")
                closeGatt(gatt)
                return
            }

            rxCharacteristic = rx
            txCharacteristic = tx
            _connection.value = ConnectionState.Subscribing

            if (requestPreferredMtu(gatt)) {
                emitSystemLog("MTU request sent, wait callback for notify setup")
            } else {
                emitSystemLog("MTU request not sent, continue notify setup directly")
                continueToNotifySetup(gatt, "mtu-request-not-sent")
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (descriptor.uuid != CCCD_UUID) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                _connection.value = ConnectionState.Connected
                connectDeferred?.complete(Unit)
            } else {
                failConnection("CCCD 写入返回错误(status=$status)")
                closeGatt(gatt)
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            val success = status == BluetoothGatt.GATT_SUCCESS
            emitSystemLog("MTU changed mtu=$mtu status=$status success=$success")
            continueToNotifySetup(gatt, "mtu-callback")
        }

        @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            handleNotifyBytes(characteristic.value)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            handleNotifyBytes(value)
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            val deferred = writeDeferred ?: return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                deferred.complete(Unit)
            } else {
                deferred.completeExceptionally(IllegalStateException("写入失败(status=$status)"))
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan(preferredNamePrefixes: List<String> = config.preferredNamePrefixes) {
        if (!hasScanPermission()) {
            _connection.value = ConnectionState.Error("缺少扫描权限")
            return
        }
        val adapter = bluetoothManager?.adapter
        if (adapter == null || !adapter.isEnabled) {
            _connection.value = ConnectionState.Error("蓝牙不可用")
            return
        }

        stopScan()
        scanCache.clear()
        _scanResults.value = emptyList()

        scanner = adapter.bluetoothLeScanner
        val callback = object : ScanCallback() {
            @SuppressLint("MissingPermission")
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val device = result.device ?: return
                val name = result.device.name ?: result.scanRecord?.deviceName
                val advertisesUart = result.scanRecord?.serviceUuids
                    ?.any { it.uuid == UART_SERVICE_UUID }
                    ?: false
                val matchesName = name?.startsWith("SonicWave", ignoreCase = true) == true
                val scanAddr = device.address
                val scanLabel = listOfNotNull(name, scanAddr).joinToString("/")
                val scanFlags = "uart=${if (advertisesUart) 1 else 0} name_match=${if (matchesName) 1 else 0}"
                emitSystemLog("SCAN_RESULT $scanLabel $scanFlags rssi=${result.rssi}")
                // Require SonicWave name prefix to avoid listing unrelated UART devices.
                if (!matchesName) return
                val item = BleScanResult(
                    id = device.address,
                    address = device.address,
                    name = name,
                    rssi = result.rssi,
                    advertisesUartService = advertisesUart,
                )
                scanCache[item.address] = item
                _scanResults.value = sortScanResults(scanCache.values.toList(), preferredNamePrefixes)
            }

            override fun onScanFailed(errorCode: Int) {
                _connection.value = ConnectionState.Error("扫描失败(errorCode=$errorCode)")
            }
        }

        scanCallback = callback
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner?.startScan(null, settings, callback)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        val localScanner = scanner
        val callback = scanCallback
        if (localScanner != null && callback != null) {
            try {
                localScanner.stopScan(callback)
            } catch (_: SecurityException) {
                _connection.value = ConnectionState.Error("停止扫描时权限不足")
            }
        }
        scanCallback = null
        scanner = null
    }

    @SuppressLint("MissingPermission")
    suspend fun connect(deviceId: String, timeoutMs: Long = config.connectTimeoutMs) {
        connectMutex.withLock {
            if (!hasConnectPermission()) {
                _connection.value = ConnectionState.Error("缺少连接权限")
                return
            }

            stopScan()
            internalDisconnect(closeOnly = true)

            val adapter: BluetoothAdapter = bluetoothManager?.adapter
                ?: run {
                    _connection.value = ConnectionState.Error("蓝牙适配器不可用")
                    return
                }

            val device = try {
                adapter.getRemoteDevice(deviceId)
            } catch (_: IllegalArgumentException) {
                _connection.value = ConnectionState.Error("deviceId 无效: $deviceId")
                return
            }

            _connection.value = ConnectionState.Connecting
            manualDisconnect = false
            connectDeferred = CompletableDeferred()

            val localGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            } else {
                @Suppress("DEPRECATION")
                device.connectGatt(appContext, false, gattCallback)
            }
            gatt = localGatt

            try {
                withTimeout(timeoutMs) {
                    connectDeferred?.await()
                }
            } catch (e: TimeoutCancellationException) {
                failConnection("连接超时(${timeoutMs}ms)")
                internalDisconnect(closeOnly = true)
                throw e
            } catch (e: Exception) {
                failConnection("连接过程异常: ${e.message}")
                internalDisconnect(closeOnly = true)
                throw e
            }
        }
    }

    fun disconnect() {
        scope.launch {
            connectMutex.withLock {
                internalDisconnect(closeOnly = false)
            }
        }
    }

    @SuppressLint("MissingPermission")
    suspend fun writeLine(line: String, timeoutMs: Long = config.writeTimeoutMs) {
        writeMutex.withLock {
            if (_connection.value !is ConnectionState.Connected) {
                throw IllegalStateException("尚未连接")
            }
            val targetGatt = gatt ?: throw IllegalStateException("Gatt 不可用")
            val rx = rxCharacteristic ?: throw IllegalStateException("RX characteristic 不可用")

            val payload = if (line.endsWith("\n")) line else "$line\n"
            val bytes = payload.toByteArray(StandardCharsets.UTF_8)
            val deferred = CompletableDeferred<Unit>()
            writeDeferred = deferred

            val ok = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                targetGatt.writeCharacteristic(rx, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) ==
                    BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                run {
                    rx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    rx.value = bytes
                    targetGatt.writeCharacteristic(rx)
                }
            }

            if (!ok) {
                writeDeferred = null
                throw IllegalStateException("写入调用失败")
            }

            try {
                withTimeout(timeoutMs) {
                    deferred.await()
                }
            } finally {
                writeDeferred = null
            }
        }
    }

    fun close() {
        stopScan()
        internalDisconnect(closeOnly = false)
        scope.cancel()
    }

    @SuppressLint("MissingPermission")
    private fun internalDisconnect(closeOnly: Boolean) {
        val localGatt = gatt
        if (localGatt == null) {
            _connection.value = ConnectionState.Disconnected
            return
        }

        manualDisconnect = true
        txCharacteristic?.let { tx ->
            try {
                localGatt.setCharacteristicNotification(tx, false)
                tx.getDescriptor(CCCD_UUID)?.let { descriptor ->
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        localGatt.writeDescriptor(descriptor, BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE)
                    } else {
                        @Suppress("DEPRECATION")
                        run {
                            descriptor.value = BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE
                            localGatt.writeDescriptor(descriptor)
                        }
                    }
                }
            } catch (_: Exception) {
                // Ignore cleanup errors.
            }
        }

        if (!closeOnly) {
            try {
                localGatt.disconnect()
            } catch (_: Exception) {
                // Ignore
            }
        }
        closeGatt(localGatt)
        clearConnectionRefs()
        _connection.value = ConnectionState.Disconnected
        manualDisconnect = false
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt(localGatt: BluetoothGatt) {
        try {
            localGatt.close()
        } catch (_: Exception) {
            // Ignore
        }
    }

    private fun clearConnectionRefs() {
        gatt = null
        rxCharacteristic = null
        txCharacteristic = null
        connectDeferred = null
        writeDeferred = null
        synchronized(notifySetupLock) {
            notifySetupStarted = false
        }
        lineFramer.clear()
    }

    private fun failConnection(message: String) {
        _connection.value = ConnectionState.Error(message)
        connectDeferred?.completeExceptionally(IllegalStateException(message))
    }

    private fun handleNotifyBytes(bytes: ByteArray?) {
        if (bytes == null || bytes.isEmpty()) return

        val chunk = bytes.toString(StandardCharsets.UTF_8)
        val chunkForLog = escapeForLog(chunk)
        Log.d(TAG, "RX chunk len=${bytes.size} payload=$chunkForLog")
        scope.launch { _incomingRawChunks.emit(chunkForLog) }

        val completeLines = lineFramer.append(chunk)
        completeLines.forEach { line ->
            Log.d(TAG, "RX line emitted=${escapeForLog(line)}")
            scope.launch { _incomingLines.emit(line) }
        }
    }

    @SuppressLint("MissingPermission")
    private fun requestPreferredMtu(gatt: BluetoothGatt): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            emitSystemLog("MTU request skipped (api<21)")
            return false
        }

        val requestSent = try {
            gatt.requestMtu(PREFERRED_MTU)
        } catch (_: Exception) {
            false
        }
        emitSystemLog("MTU request target=$PREFERRED_MTU sent=$requestSent")
        return requestSent
    }

    @SuppressLint("MissingPermission")
    private fun continueToNotifySetup(gatt: BluetoothGatt, trigger: String) {
        val shouldSetup = synchronized(notifySetupLock) {
            if (notifySetupStarted) {
                false
            } else {
                notifySetupStarted = true
                true
            }
        }
        if (!shouldSetup) {
            emitSystemLog("Notify setup already started, skip duplicate trigger=$trigger")
            return
        }

        emitSystemLog("Notify setup start trigger=$trigger")
        val uartService = gatt.getService(UART_SERVICE_UUID)
        if (uartService == null) {
            emitSystemLog("Notify setup failed: UART service missing")
            failConnection("未发现 Nordic UART UUID")
            closeGatt(gatt)
            return
        }

        val tx = uartService.getCharacteristic(UART_TX_UUID)
        if (tx == null) {
            emitSystemLog("Notify setup failed: TX characteristic missing")
            failConnection("未发现 Nordic UART TX UUID")
            closeGatt(gatt)
            return
        }

        txCharacteristic = tx
        enableTxNotifications(gatt, tx)
    }

    @SuppressLint("MissingPermission")
    private fun enableTxNotifications(gatt: BluetoothGatt, tx: BluetoothGattCharacteristic) {
        if (!gatt.setCharacteristicNotification(tx, true)) {
            failConnection("setCharacteristicNotification 失败")
            closeGatt(gatt)
            return
        }

        val cccd = tx.getDescriptor(CCCD_UUID)
        if (cccd == null) {
            failConnection("TX 缺少 CCCD 描述符")
            closeGatt(gatt)
            return
        }

        val writeOk = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            run {
                cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(cccd)
            }
        }

        if (!writeOk) {
            failConnection("CCCD 写入失败")
            closeGatt(gatt)
        }
    }

    private fun emitSystemLog(message: String) {
        Log.i(TAG, message)
        scope.launch { _transportLogs.emit(message) }
    }

    private fun escapeForLog(input: String): String {
        if (input.isEmpty()) return "<empty>"
        val out = StringBuilder(input.length)
        input.forEach { ch ->
            when (ch) {
                '\n' -> out.append("\\n")
                '\r' -> out.append("\\r")
                else -> out.append(ch)
            }
        }
        return out.toString()
    }

    private fun sortScanResults(results: List<BleScanResult>, preferredNamePrefixes: List<String>): List<BleScanResult> {
        fun rank(name: String?): Int {
            if (name.isNullOrBlank()) return Int.MAX_VALUE
            val index = preferredNamePrefixes.indexOfFirst { prefix ->
                name.startsWith(prefix, ignoreCase = true)
            }
            return if (index == -1) Int.MAX_VALUE else index
        }

        return results.sortedWith(
            compareByDescending<BleScanResult> { it.advertisesUartService }
                .thenBy { rank(it.name) }
                .thenByDescending { it.rssi },
        )
    }

    private fun hasScanPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            hasPermission(Manifest.permission.BLUETOOTH_SCAN)
        } else {
            hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    private fun hasConnectPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            hasPermission(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            hasPermission(Manifest.permission.BLUETOOTH)
        }
    }

    private fun hasPermission(permission: String): Boolean {
        return ContextCompat.checkSelfPermission(appContext, permission) == PackageManager.PERMISSION_GRANTED
    }

    companion object {
        private const val TAG = "SonicWaveTransport"
        private const val PREFERRED_MTU = 185
        private val UART_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        private val UART_RX_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        private val UART_TX_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
