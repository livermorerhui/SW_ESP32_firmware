package com.sonicwave.demo

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import java.io.BufferedWriter
import java.io.File
import java.io.OutputStreamWriter

data class RecordingSession(
    val displayName: String,
    val destinationLabel: String,
    val writer: BufferedWriter,
    val uri: Uri? = null,
)

class TelemetryRecorder(private val context: Context) {
    fun startSession(): RecordingSession {
        val timestamp = System.currentTimeMillis()
        val fileName = "sonicwave_${timestamp}.csv"
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startMediaStoreSession(fileName)
        } else {
            startLegacyFileSession(fileName)
        }
    }

    fun appendRow(session: RecordingSession, point: TelemetryPointUi) {
        session.writer.append(
            "${point.timestampMs},${point.distance},${point.weight},${if (point.stableFlag) 1 else 0}\n",
        )
        session.writer.flush()
    }

    fun stopSession(session: RecordingSession) {
        session.writer.flush()
        session.writer.close()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && session.uri != null) {
            context.contentResolver.update(
                session.uri,
                ContentValues().apply {
                    put(MediaStore.MediaColumns.IS_PENDING, 0)
                },
                null,
                null,
            )
        }
    }

    private fun startMediaStoreSession(fileName: String): RecordingSession {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
            put(MediaStore.MediaColumns.MIME_TYPE, "text/csv")
            put(MediaStore.MediaColumns.RELATIVE_PATH, "${Environment.DIRECTORY_DOWNLOADS}/SonicWave")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            ?: error("Failed to create Downloads/SonicWave entry")
        val stream = resolver.openOutputStream(uri)
            ?: error("Failed to open output stream for $uri")
        val writer = BufferedWriter(OutputStreamWriter(stream))
        writer.append("timestamp,distance,weight,stable\n")
        writer.flush()
        return RecordingSession(
            displayName = fileName,
            destinationLabel = "Downloads/SonicWave/$fileName",
            writer = writer,
            uri = uri,
        )
    }

    private fun startLegacyFileSession(fileName: String): RecordingSession {
        val downloads = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        val dir = File(downloads, "SonicWave")
        if (!dir.exists() && !dir.mkdirs()) {
            error("Failed to create ${dir.absolutePath}")
        }
        val file = File(dir, fileName)
        val writer = file.bufferedWriter()
        writer.append("timestamp,distance,weight,stable\n")
        writer.flush()
        return RecordingSession(
            displayName = fileName,
            destinationLabel = file.absolutePath,
            writer = writer,
        )
    }
}
