package com.sonicwave.protocol

import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class CalibrationComparisonEngineTest {
    @Test
    fun distanceConversionUsesFirmwareRuntimeDivisor() {
        assertEquals(1.25f, calibrationDistanceMmToRuntime(125.0f))
    }

    @Test
    fun linearFitRecoversSimpleLine() {
        val result = CalibrationComparisonEngine.compare(
            samples = listOf(
                sample(distanceRuntime = 0.5f, weightKg = 6.0f),
                sample(distanceRuntime = 1.5f, weightKg = 10.0f),
                sample(distanceRuntime = 2.5f, weightKg = 14.0f),
            ),
        )

        val linear = result.linear
        assertTrue(linear.isAvailable)
        val coefficients = assertNotNull(linear.coefficients)
        val metrics = assertNotNull(linear.metrics)
        assertNear(1.5f, coefficients.referenceDistance)
        assertNear(0.0f, coefficients.c0)
        assertNear(4.0f, coefficients.c1)
        assertNear(10.0f, coefficients.c2)
        assertNear(0.0f, metrics.meanAbsoluteErrorKg)
        assertNear(0.0f, metrics.maxAbsoluteErrorKg)
    }

    @Test
    fun quadraticFitRecoversSimpleParabola() {
        val result = CalibrationComparisonEngine.compare(
            samples = listOf(
                sample(distanceRuntime = 0.0f, weightKg = 1.0f),
                sample(distanceRuntime = 1.0f, weightKg = 2.5f),
                sample(distanceRuntime = 2.0f, weightKg = 5.0f),
                sample(distanceRuntime = 3.0f, weightKg = 8.5f),
                sample(distanceRuntime = 4.0f, weightKg = 13.0f),
            ),
        )

        val quadratic = result.quadratic
        assertTrue(quadratic.isAvailable)
        val coefficients = assertNotNull(quadratic.coefficients)
        assertNear(2.0f, coefficients.referenceDistance)
        assertNear(0.5f, coefficients.c0)
        assertNear(3.0f, coefficients.c1)
        assertNear(5.0f, coefficients.c2)
    }

    @Test
    fun metricsAreComputedAgainstRecordedReferenceWeight() {
        val result = CalibrationComparisonEngine.compare(
            samples = listOf(
                sample(distanceRuntime = -1.0f, weightKg = 1.0f),
                sample(distanceRuntime = 0.0f, weightKg = 3.0f),
                sample(distanceRuntime = 1.0f, weightKg = 5.0f),
                sample(distanceRuntime = 2.0f, weightKg = 7.0f),
            ),
        )

        val linearMetrics = assertNotNull(result.linear.metrics)
        assertEquals(4, linearMetrics.pointCount)
        assertNear(0.0f, linearMetrics.meanAbsoluteErrorKg)
        assertNear(0.0f, linearMetrics.maxAbsoluteErrorKg)
    }

    @Test
    fun monotonicCheckFlagsNonMonotonicQuadraticCandidate() {
        val result = CalibrationComparisonEngine.compare(
            samples = listOf(
                sample(distanceRuntime = -2.0f, weightKg = 6.0f),
                sample(distanceRuntime = -1.0f, weightKg = 9.0f),
                sample(distanceRuntime = 0.0f, weightKg = 10.0f),
                sample(distanceRuntime = 1.0f, weightKg = 9.0f),
                sample(distanceRuntime = 2.0f, weightKg = 6.0f),
            ),
        )

        assertEquals(false, result.quadratic.monotonic)
    }

    @Test
    fun insufficientPointsDoNotCrashAndReportUnavailableFits() {
        val singlePoint = CalibrationComparisonEngine.compare(
            samples = listOf(sample(distanceRuntime = 1.0f, weightKg = 5.0f)),
        )
        assertFalse(singlePoint.linear.isAvailable)
        assertEquals(2, singlePoint.linear.requiredPointCount)
        assertFalse(singlePoint.quadratic.isAvailable)
        assertEquals(3, singlePoint.quadratic.requiredPointCount)

        val twoPoints = CalibrationComparisonEngine.compare(
            samples = listOf(
                sample(distanceRuntime = 1.0f, weightKg = 5.0f),
                sample(distanceRuntime = 2.0f, weightKg = 7.0f),
            ),
        )
        assertTrue(twoPoints.linear.isAvailable)
        assertFalse(twoPoints.quadratic.isAvailable)
    }

    private fun sample(distanceRuntime: Float, weightKg: Float): CalibrationFitSample {
        return CalibrationFitSample(
            distanceMm = distanceRuntime * CALIBRATION_DISTANCE_RUNTIME_DIVISOR,
            referenceWeightKg = weightKg,
        )
    }

    private fun assertNear(expected: Float, actual: Float, tolerance: Float = 1e-4f) {
        assertTrue(abs(expected - actual) <= tolerance, "Expected $expected, actual $actual")
    }
}
