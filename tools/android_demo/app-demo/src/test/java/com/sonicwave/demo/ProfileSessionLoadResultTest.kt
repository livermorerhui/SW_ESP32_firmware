package com.sonicwave.demo

import java.io.FileNotFoundException
import java.io.IOException
import kotlin.test.Test
import kotlin.test.assertEquals

class ProfileSessionLoadResultTest {
    @Test
    fun missingFileIsDistinctFromDamagedFile() {
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { false },
            readText = { throw FileNotFoundException("no record") },
            parse = { DemoSessionSnapshot() },
        )

        assertEquals(ProfileSessionLoadResult.Missing, result)
    }

    @Test
    fun existingFileThatCannotBeReadIsCorrupt() {
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { true },
            readText = { throw FileNotFoundException("unreadable record") },
            parse = { DemoSessionSnapshot() },
        )

        assertEquals(ProfileSessionLoadResult.Corrupt, result)
    }

    @Test
    fun otherReadFailureIsCorrupt() {
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { false },
            readText = { throw IOException("read failed") },
            parse = { DemoSessionSnapshot() },
        )

        assertEquals(ProfileSessionLoadResult.Corrupt, result)
    }

    @Test
    fun failedPresenceCheckCannotTurnReadFailureIntoMissing() {
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { throw SecurityException("cannot stat file") },
            readText = { throw FileNotFoundException("cannot read record") },
            parse = { DemoSessionSnapshot() },
        )

        assertEquals(ProfileSessionLoadResult.Corrupt, result)
    }

    @Test
    fun validRecordReturnsItsSnapshot() {
        val snapshot = DemoSessionSnapshot()
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { true },
            readText = { "profile document" },
            parse = { text -> snapshot.takeIf { text == "profile document" } },
        )

        assertEquals(ProfileSessionLoadResult.Loaded(snapshot), result)
    }

    @Test
    fun unparseableRecordIsCorrupt() {
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { true },
            readText = { "invalid profile document" },
            parse = { null },
        )

        assertEquals(ProfileSessionLoadResult.Corrupt, result)
    }

    @Test
    fun parserFailureIsCorrupt() {
        val result = classifyProfileSessionRead(
            anyRecordFilePresent = { true },
            readText = { "profile document" },
            parse = { throw IllegalArgumentException("invalid owner") },
        )

        assertEquals(ProfileSessionLoadResult.Corrupt, result)
    }
}
