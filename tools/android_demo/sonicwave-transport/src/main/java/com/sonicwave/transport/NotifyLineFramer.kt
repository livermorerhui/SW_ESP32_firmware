package com.sonicwave.transport

internal class NotifyLineFramer {
    private val buffer = StringBuilder()

    fun append(chunk: String): List<String> {
        if (chunk.isEmpty()) return emptyList()

        val completeLines = mutableListOf<String>()
        synchronized(buffer) {
            buffer.append(chunk)
            while (true) {
                val newlineIndex = buffer.indexOf("\n")
                if (newlineIndex < 0) break

                val line = buffer.substring(0, newlineIndex).trimEnd('\r')
                buffer.delete(0, newlineIndex + 1)
                if (line.isNotEmpty()) {
                    completeLines += line
                }
            }
        }
        return completeLines
    }

    fun clear() {
        synchronized(buffer) {
            buffer.clear()
        }
    }
}
