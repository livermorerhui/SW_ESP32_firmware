package com.sonicwave.protocol

import kotlin.math.abs

/**
 * The firmware model uses the historical runtime distance unit (millimetres
 * divided by 100). The APP keeps calibration points in physical millimetres
 * and performs this one conversion at the model boundary.
 */
const val CALIBRATION_DISTANCE_RUNTIME_DIVISOR: Float = 100.0f
const val CALIBRATION_VALID_MIN_DISTANCE_MM: Float = 20.0f
const val CALIBRATION_VALID_MAX_DISTANCE_MM: Float = 5000.0f
const val CALIBRATION_WANG_MIN_DISTANCE_MM: Float = 64.3f
const val CALIBRATION_WANG_MAX_DISTANCE_MM: Float = 135.7f
const val CALIBRATION_EMPTY_WEIGHT_EPSILON_KG: Float = 0.0005f
const val CALIBRATION_EMPTY_DISTANCE_TOLERANCE_MM: Float = 0.10f
const val CALIBRATION_CYD_EMPTY_DISTANCE_TOLERANCE_MM: Float = 1.0f

fun calibrationDistanceRangeForBackend(sensorBackend: String?): ClosedFloatingPointRange<Float> {
    val backend = sensorBackend.orEmpty().uppercase()
    return if (backend.contains("485") || backend.contains("WANG")) {
        CALIBRATION_WANG_MIN_DISTANCE_MM..CALIBRATION_WANG_MAX_DISTANCE_MM
    } else {
        CALIBRATION_VALID_MIN_DISTANCE_MM..CALIBRATION_VALID_MAX_DISTANCE_MM
    }
}

fun calibrationEmptyDistanceToleranceForBackend(sensorBackend: String?): Float {
    return if (sensorBackend.orEmpty().uppercase().contains("CYD")) {
        CALIBRATION_CYD_EMPTY_DISTANCE_TOLERANCE_MM
    } else {
        CALIBRATION_EMPTY_DISTANCE_TOLERANCE_MM
    }
}

data class CalibrationFitSample(
    val distanceMm: Float,
    val referenceWeightKg: Float,
)

data class CalibrationFitCoefficients(
    /** Physical empty distance expressed in firmware runtime units. */
    val referenceDistance: Float,
    val c0: Float,
    val c1: Float,
    /** Kept for wire compatibility; formal constrained fits always set c2=0. */
    val c2: Float,
)

data class CalibrationCurvePoint(
    val distanceMm: Float,
    val predictedWeightKg: Float,
)

data class CalibrationFitMetrics(
    val pointCount: Int,
    val meanAbsoluteErrorKg: Float,
    val maxAbsoluteErrorKg: Float,
    val emptyPointErrorKg: Float = 0.0f,
)

data class CalibrationFitResult(
    val type: CalibrationModelType,
    val sampleCount: Int,
    val requiredPointCount: Int,
    val coefficients: CalibrationFitCoefficients? = null,
    val metrics: CalibrationFitMetrics? = null,
    val monotonic: Boolean? = null,
    val curvePoints: List<CalibrationCurvePoint> = emptyList(),
    val emptyDistanceMm: Float? = null,
    val failureReason: String? = null,
) {
    /** A curve is deployable only when every hard gate has passed. */
    val isAvailable: Boolean
        get() = coefficients != null &&
            metrics != null &&
            monotonic == true &&
            emptyDistanceMm != null &&
            failureReason == null
}

data class CalibrationComparisonResult(
    val sampleCount: Int,
    val linear: CalibrationFitResult,
    val quadratic: CalibrationFitResult,
    val emptyDistanceMm: Float? = null,
    val emptyAnchorPresent: Boolean = emptyDistanceMm != null,
    val emptyAnchorConflict: Boolean = false,
)

fun calibrationDistanceMmToRuntime(distanceMm: Float): Float =
    distanceMm / CALIBRATION_DISTANCE_RUNTIME_DIVISOR

/**
 * Compare the two supported calibration models while enforcing the product's
 * physical anchor: the point recorded with reference weight 0 kg must be the
 * exact model origin. Thus c2 is never estimated from the data.
 */
object CalibrationComparisonEngine {
    fun compare(
        samples: List<CalibrationFitSample>,
        emptyDistanceMm: Float? = null,
        emptyDistanceToleranceMm: Float = CALIBRATION_EMPTY_DISTANCE_TOLERANCE_MM,
        validationDistanceRangeMm: ClosedFloatingPointRange<Float> =
            CALIBRATION_VALID_MIN_DISTANCE_MM..CALIBRATION_VALID_MAX_DISTANCE_MM,
    ): CalibrationComparisonResult {
        val sanitized = samples
            .filter { it.distanceMm.isFinite() && it.referenceWeightKg.isFinite() }
            .sortedBy { it.distanceMm }

        val anchorResolution = resolveEmptyAnchor(
            samples = sanitized,
            requestedDistanceMm = emptyDistanceMm,
            toleranceMm = emptyDistanceToleranceMm,
        )
        val anchor = anchorResolution.distanceMm
        val reason = anchorResolution.failureReason
        val sampleCount = sanitized.size
        val rangeFailure = sanitized.firstOrNull {
            it.distanceMm < validationDistanceRangeMm.start ||
                it.distanceMm > validationDistanceRangeMm.endInclusive
        }?.let { "DISTANCE_OUT_OF_RANGE" }
        val finalReason = reason ?: rangeFailure

        return CalibrationComparisonResult(
            sampleCount = sampleCount,
            linear = fitCandidate(
                type = CalibrationModelType.LINEAR,
                samples = sanitized,
                requiredPointCount = 2,
                emptyDistanceMm = anchor,
                failureReason = finalReason,
                fitBlock = ::fitLinear,
                validationDistanceRangeMm = validationDistanceRangeMm,
            ),
            quadratic = fitCandidate(
                type = CalibrationModelType.QUADRATIC,
                samples = sanitized,
                requiredPointCount = 3,
                emptyDistanceMm = anchor,
                failureReason = finalReason,
                fitBlock = ::fitQuadratic,
                validationDistanceRangeMm = validationDistanceRangeMm,
            ),
            emptyDistanceMm = anchor,
            emptyAnchorPresent = anchor != null,
            emptyAnchorConflict = reason == "EMPTY_ANCHOR_CONFLICT",
        )
    }

    private data class AnchorResolution(
        val distanceMm: Float?,
        val failureReason: String?,
    )

    private fun resolveEmptyAnchor(
        samples: List<CalibrationFitSample>,
        requestedDistanceMm: Float?,
        toleranceMm: Float,
    ): AnchorResolution {
        if (requestedDistanceMm != null &&
            (!requestedDistanceMm.isFinite() || requestedDistanceMm <= 0.0f)
        ) {
            return AnchorResolution(null, "INVALID_EMPTY_DISTANCE")
        }

        val zeroSamples = samples.filter {
            abs(it.referenceWeightKg) <= CALIBRATION_EMPTY_WEIGHT_EPSILON_KG
        }
        if (zeroSamples.isEmpty()) {
            return AnchorResolution(null, "MISSING_EMPTY_ANCHOR")
        }

        val measuredAnchor = zeroSamples.map { it.distanceMm }.average().toFloat()
        val anchor = requestedDistanceMm ?: measuredAnchor
        val tolerance = toleranceMm.coerceAtLeast(0.0f)
        val hasConflict = zeroSamples.any { abs(it.distanceMm - anchor) > tolerance }
        if (hasConflict) {
            return AnchorResolution(null, "EMPTY_ANCHOR_CONFLICT")
        }
        return AnchorResolution(anchor, null)
    }

    private fun fitCandidate(
        type: CalibrationModelType,
        samples: List<CalibrationFitSample>,
        requiredPointCount: Int,
        emptyDistanceMm: Float?,
        failureReason: String?,
        fitBlock: (List<Pair<Double, Double>>) -> DoubleArray,
        validationDistanceRangeMm: ClosedFloatingPointRange<Float>,
    ): CalibrationFitResult {
        val base = CalibrationFitResult(
            type = type,
            sampleCount = samples.size,
            requiredPointCount = requiredPointCount,
            emptyDistanceMm = emptyDistanceMm,
            failureReason = failureReason,
        )
        if (failureReason != null) return base
        if (emptyDistanceMm == null) return base.copy(failureReason = "MISSING_EMPTY_ANCHOR")
        if (samples.size < requiredPointCount) return base.copy(failureReason = "INSUFFICIENT_POINTS")

        val nonZeroSamples = samples.filter {
            abs(it.referenceWeightKg) > CALIBRATION_EMPTY_WEIGHT_EPSILON_KG
        }
        if (nonZeroSamples.size < requiredPointCount - 1) {
            return base.copy(failureReason = "INSUFFICIENT_NONZERO_POINTS")
        }

        val referenceRuntime = calibrationDistanceMmToRuntime(emptyDistanceMm).toDouble()
        val shifted = samples.map { sample ->
            val runtimeDistance = calibrationDistanceMmToRuntime(sample.distanceMm).toDouble()
            (runtimeDistance - referenceRuntime) to sample.referenceWeightKg.toDouble()
        }

        val coefficients = try {
            val fitted = fitBlock(shifted)
            CalibrationFitCoefficients(
                referenceDistance = referenceRuntime.toFloat(),
                c0 = fitted[0].toFloat(),
                c1 = fitted[1].toFloat(),
                // The fixed 0 kg point is the model origin.
                c2 = 0.0f,
            )
        } catch (_: IllegalArgumentException) {
            return base.copy(failureReason = "SINGULAR_FIT")
        }

        if (!coefficients.c0.isFinite() || !coefficients.c1.isFinite()) {
            return base.copy(failureReason = "NON_FINITE_FIT")
        }

        val metrics = calculateMetrics(samples, coefficients)
        val monotonic = isMonotonic(
            coefficients,
            validationDistanceRangeMm.start,
            validationDistanceRangeMm.endInclusive,
        )
        val curve = buildCurve(samples, coefficients)
        return base.copy(
            coefficients = coefficients,
            metrics = metrics,
            monotonic = monotonic,
            curvePoints = curve,
            failureReason = if (monotonic) null else "NON_MONOTONIC",
        )
    }

    /** Fit y = c1*u with its intercept constrained to zero. */
    private fun fitLinear(points: List<Pair<Double, Double>>): DoubleArray {
        val denominator = points.sumOf { it.first * it.first }
        require(abs(denominator) >= 1e-12) { "linear fit is singular" }
        val slope = points.sumOf { it.first * it.second } / denominator
        return doubleArrayOf(0.0, slope)
    }

    /** Fit y = c0*u² + c1*u with its intercept constrained to zero. */
    private fun fitQuadratic(points: List<Pair<Double, Double>>): DoubleArray {
        val su2 = points.sumOf { it.first * it.first }
        val su3 = points.sumOf { it.first * it.first * it.first }
        val su4 = points.sumOf { it.first * it.first * it.first * it.first }
        val suy = points.sumOf { it.first * it.second }
        val su2y = points.sumOf { it.first * it.first * it.second }
        val determinant = su4 * su2 - su3 * su3
        require(abs(determinant) >= 1e-12) { "quadratic fit is singular" }
        val c0 = (su2 * su2y - su3 * suy) / determinant
        val c1 = (su4 * suy - su3 * su2y) / determinant
        return doubleArrayOf(c0, c1)
    }

    private fun calculateMetrics(
        samples: List<CalibrationFitSample>,
        coefficients: CalibrationFitCoefficients,
    ): CalibrationFitMetrics {
        val absoluteErrors = samples.map { sample ->
            abs(
                predictWeightKg(
                    coefficients = coefficients,
                    runtimeDistance = calibrationDistanceMmToRuntime(sample.distanceMm),
                ) - sample.referenceWeightKg,
            )
        }
        return CalibrationFitMetrics(
            pointCount = samples.size,
            meanAbsoluteErrorKg = absoluteErrors.average().toFloat(),
            maxAbsoluteErrorKg = absoluteErrors.maxOrNull() ?: 0.0f,
            emptyPointErrorKg = samples
                .filter { abs(it.referenceWeightKg) <= CALIBRATION_EMPTY_WEIGHT_EPSILON_KG }
                .map { abs(predictWeightKg(coefficients, calibrationDistanceMmToRuntime(it.distanceMm))) }
                .maxOrNull() ?: Float.POSITIVE_INFINITY,
        )
    }

    private fun buildCurve(
        samples: List<CalibrationFitSample>,
        coefficients: CalibrationFitCoefficients,
        steps: Int = 32,
    ): List<CalibrationCurvePoint> {
        val minDistance = samples.minOf { it.distanceMm }
        val maxDistance = samples.maxOf { it.distanceMm }
        val span = (maxDistance - minDistance).takeIf { it > 0.0f } ?: 1.0f
        return (0..steps).map { step ->
            val ratio = step.toFloat() / steps.toFloat()
            val distanceMm = minDistance + span * ratio
            CalibrationCurvePoint(
                distanceMm = distanceMm,
                predictedWeightKg = predictWeightKg(
                    coefficients,
                    calibrationDistanceMmToRuntime(distanceMm),
                ),
            )
        }
    }

    private fun predictWeightKg(
        coefficients: CalibrationFitCoefficients,
        runtimeDistance: Float,
    ): Float {
        val shifted = runtimeDistance - coefficients.referenceDistance
        return coefficients.c0 * shifted * shifted + coefficients.c1 * shifted + coefficients.c2
    }

    private fun isMonotonic(
        coefficients: CalibrationFitCoefficients,
        minDistanceMm: Float,
        maxDistanceMm: Float,
    ): Boolean {
        val minDistance = calibrationDistanceMmToRuntime(minDistanceMm)
        val maxDistance = calibrationDistanceMmToRuntime(maxDistanceMm)
        val xMin = minDistance - coefficients.referenceDistance
        val xMax = maxDistance - coefficients.referenceDistance
        val tolerance = 0.0001f

        return if (abs(coefficients.c0) < 1e-6f) {
            abs(coefficients.c1) >= tolerance
        } else {
            val dMin = 2.0f * coefficients.c0 * xMin + coefficients.c1
            val dMax = 2.0f * coefficients.c0 * xMax + coefficients.c1
            (dMin >= tolerance && dMax >= tolerance) ||
                (dMin <= -tolerance && dMax <= -tolerance)
        }
    }
}
