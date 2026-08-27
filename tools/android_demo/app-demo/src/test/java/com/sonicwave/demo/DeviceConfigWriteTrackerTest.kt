package com.sonicwave.demo

import com.sonicwave.protocol.PlatformModel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceConfigWriteTrackerTest {
    @Test
    fun startCreatesPendingRequest() {
        val tracker = DeviceConfigWriteTracker()

        val request = tracker.start(
            platformModel = PlatformModel.PLUS,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        assertTrue(tracker.isPending)
        assertEquals(PlatformModel.PLUS, request.platformModel)
        assertEquals(true, request.laserInstalled)
        assertEquals(100L, request.requestedAtMs)
    }

    @Test
    fun observedConfigMatchConfirmsPendingWrite() {
        val tracker = DeviceConfigWriteTracker()
        val request = tracker.start(
            platformModel = PlatformModel.PRO,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        val confirmation = tracker.confirmObserved(
            observedPlatformModel = PlatformModel.PRO,
            observedLaserInstalled = true,
        )

        assertNotNull(confirmation)
        assertEquals(request, confirmation?.request)
        assertEquals(PlatformModel.PRO, confirmation?.observedPlatformModel)
        assertEquals(true, confirmation?.observedLaserInstalled)
        assertFalse(tracker.isPending)
    }

    @Test
    fun observedConfigMismatchKeepsPendingWrite() {
        val tracker = DeviceConfigWriteTracker()
        tracker.start(
            platformModel = PlatformModel.PRO,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        val confirmation = tracker.confirmObserved(
            observedPlatformModel = PlatformModel.PLUS,
            observedLaserInstalled = true,
        )

        assertNull(confirmation)
        assertTrue(tracker.isPending)
    }

    @Test
    fun missingObservedConfigKeepsPendingWrite() {
        val tracker = DeviceConfigWriteTracker()
        tracker.start(
            platformModel = PlatformModel.BASE,
            laserInstalled = false,
            requestedAtMs = 100L,
        )

        val confirmation = tracker.confirmObserved(
            observedPlatformModel = null,
            observedLaserInstalled = false,
        )

        assertNull(confirmation)
        assertTrue(tracker.isPending)
    }

    @Test
    fun genericAckClearsPendingWriteAsFallback() {
        val tracker = DeviceConfigWriteTracker()
        val request = tracker.start(
            platformModel = PlatformModel.PLUS,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        val cleared = tracker.genericAck()

        assertEquals(request, cleared)
        assertFalse(tracker.isPending)
    }

    @Test
    fun failureClearsPendingWrite() {
        val tracker = DeviceConfigWriteTracker()
        val request = tracker.start(
            platformModel = PlatformModel.PLUS,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        val cleared = tracker.failure()

        assertEquals(request, cleared)
        assertFalse(tracker.isPending)
    }

    @Test
    fun timeoutClearsPendingWrite() {
        val tracker = DeviceConfigWriteTracker()
        val request = tracker.start(
            platformModel = PlatformModel.PLUS,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        val cleared = tracker.timeout()

        assertEquals(request, cleared)
        assertFalse(tracker.isPending)
    }

    @Test
    fun confirmationRefreshRequiresPendingAndConnection() {
        val tracker = DeviceConfigWriteTracker()

        assertFalse(tracker.shouldRefreshConfirmation(isConnected = true))

        tracker.start(
            platformModel = PlatformModel.PLUS,
            laserInstalled = true,
            requestedAtMs = 100L,
        )

        assertFalse(tracker.shouldRefreshConfirmation(isConnected = false))
        assertTrue(tracker.shouldRefreshConfirmation(isConnected = true))
    }

    @Test
    fun observedConfigMatchHelperRequiresBothFields() {
        assertTrue(
            doesObservedDeviceConfigMatchRequested(
                requestedPlatformModel = PlatformModel.PRO,
                requestedLaserInstalled = true,
                observedPlatformModel = PlatformModel.PRO,
                observedLaserInstalled = true,
            ),
        )
        assertFalse(
            doesObservedDeviceConfigMatchRequested(
                requestedPlatformModel = PlatformModel.PRO,
                requestedLaserInstalled = true,
                observedPlatformModel = PlatformModel.PRO,
                observedLaserInstalled = null,
            ),
        )
    }
}
