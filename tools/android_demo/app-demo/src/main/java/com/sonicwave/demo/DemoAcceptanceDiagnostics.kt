package com.sonicwave.demo

import android.util.Log

/** Low-frequency acceptance diagnostics; it never changes device commands or readiness. */
internal object DemoAcceptanceDiagnostics {
    private const val TAG = "DemoAcceptance"

    fun event(name: String, vararg fields: Pair<String, Any?>) {
        val payload = fields
            .filter { it.second != null }
            .joinToString(" ") { (key, value) -> "$key=${sanitize(value.toString())}" }
        Log.i(TAG, if (payload.isEmpty()) "event=$name" else "event=$name $payload")
    }

    private fun sanitize(value: String): String =
        value.replace(Regex("\\s+"), "_").replace('=', '_')
}
