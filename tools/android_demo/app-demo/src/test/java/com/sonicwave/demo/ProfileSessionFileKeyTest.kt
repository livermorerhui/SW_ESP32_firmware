package com.sonicwave.demo

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class ProfileSessionFileKeyTest {
    private val deviceA = "sw1_0123456789abcdef0123456789abcdef"
    private val deviceB = "sw1_fedcba9876543210fedcba9876543210"

    @Test
    fun stableKeyIsBoundToBothDeviceAndProfile() {
        val j100 = ProfileSessionFileKey.fileName(deviceA, "J100_485")
        assertEquals(j100, ProfileSessionFileKey.fileName(deviceA, "J100_485"))
        assertNotEquals(j100, ProfileSessionFileKey.fileName(deviceA, "CYD3S1612_IIC"))
        assertNotEquals(j100, ProfileSessionFileKey.fileName(deviceB, "J100_485"))
        assertTrue(Regex("demo_session_v3_[0-9a-f]{64}\\.json").matches(j100))
    }

    @Test
    fun rejectsPathSegmentsAndNoncanonicalIdentity() {
        assertFailsWith<IllegalArgumentException> {
            ProfileSessionFileKey.fileName("../$deviceA", "J100_485")
        }
        assertFailsWith<IllegalArgumentException> {
            ProfileSessionFileKey.fileName(deviceA, "../J100_485")
        }
        assertFailsWith<IllegalArgumentException> {
            ProfileSessionFileKey.fileName(deviceA.uppercase(), "J100_485")
        }
        assertFailsWith<IllegalArgumentException> {
            ProfileSessionFileKey.fileName(deviceA, "j100_485")
        }
    }
}
