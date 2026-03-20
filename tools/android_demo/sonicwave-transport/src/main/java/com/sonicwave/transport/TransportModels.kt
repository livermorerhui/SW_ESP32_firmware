package com.sonicwave.transport

sealed interface ConnectionState {
    data object Disconnected : ConnectionState
    data object Connecting : ConnectionState
    data object DiscoveringServices : ConnectionState
    data object Subscribing : ConnectionState
    data object Connected : ConnectionState
    data class Error(val message: String) : ConnectionState
}

data class BleScanResult(
    val id: String,
    val address: String,
    val name: String?,
    val rssi: Int,
    val advertisesUartService: Boolean = false,
)

data class BleTransportConfig(
    val preferredNamePrefixes: List<String> = listOf("SonicWave", "SW", "SONIC"),
    val connectTimeoutMs: Long = 10_000,
    val writeTimeoutMs: Long = 2_000,
)
