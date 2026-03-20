package com.sonicwave.protocol

import kotlin.math.abs

const val CALIBRATION_DISTANCE_RUNTIME_DIVISOR: Float = 100.0f
const val CALIBRATION_VALID_MIN_DISTANCE_MM: Float = -3570.0f
const val CALIBRATION_VALID_MAX_DISTANCE_MM: Float = 3570.0f

data class CalibrationFitSample(
    val distanceMm: Float,
    val referenceWeightKg: Float,
)

data class CalibrationFitCoefficients(
    val referenceDistance: Float,
    val c0: Float,
    val c1: Float,
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
)

data class CalibrationFitResult(
    val type: CalibrationModelType,
    val sampleCount: Int,
    val requiredPointCount: Int,
    val coefficients: CalibrationFitCoefficients? = null,
    val metrics: CalibrationFitMetrics? = null,
    val monotonic: Boolean? = null,
    val curvePoints: List<CalibrationCurvePoint> = emptyList(),
) {
    val isAvailable: Boolean
        get() = coefficients != null && metrics != null && monotonic != null
}

data class CalibrationComparisonResult(
    val sampleCount: Int,
    val linear: CalibrationFitResult,
    val quadratic: CalibrationFitResult,
)

fun calibrationDistanceMmToRuntime(distanceMm: Float): Float = distanceMm / CALIBRATION_DISTANCE_RUNTIME_DIVISOR

object CalibrationComparisonEngine {
    fun compare(samples: List<CalibrationFitSample>): CalibrationComparisonResult {
        val sanitized = samples
            .filter { it.distanceMm.isFinite() && it.referenceWeightKg.isFinite() }
            .sortedBy { it.distanceMm }

        return CalibrationComparisonResult(
            sampleCount = sanitized.size,
            linear = fitCandidate(
                type = CalibrationModelType.LINEAR,
                samples = sanitized,
                requiredPointCount = 2,
            ) { shifted -> fitLinear(shifted) },
            quadratic = fitCandidate(
                type = CalibrationModelType.QUADRATIC,
                samples = sanitized,
                requiredPointCount = 3,
            ) { shifted -> fitQuadratic(shifted) },
        )
    }

    private fun fitCandidate(
        type: CalibrationModelType,
        samples: List<CalibrationFitSample>,
        requiredPointCount: Int,
        fitBlock: (List<Pair<Double, Double>>) -> DoubleArray,
    ): CalibrationFitResult {
        if (samples.size < requiredPointCount) {
            return CalibrationFitResult(
                type = type,
                sampleCount = samples.size,
                requiredPointCount = requiredPointCount,
            )
        }

        val referenceDistance = samples
            .map { calibrationDistanceMmToRuntime(it.distanceMm).toDouble() }
            .average()
            .toFloat()
        val shifted = samples.map { sample ->
            val runtimeDistance = calibrationDistanceMmToRuntime(sample.distanceMm).toDouble()
            (runtimeDistance - referenceDistance.toDouble()) to sample.referenceWeightKg.toDouble()
        }
        val coefficients = fitBlock(shifted)
        val model = CalibrationFitCoefficients(
            referenceDistance = referenceDistance,
            c0 = coefficients[0].toFloat(),
            c1 = coefficients[1].toFloat(),
            c2 = coefficients[2].toFloat(),
        )

        return CalibrationFitResult(
            type = type,
            sampleCount = samples.size,
            requiredPointCount = requiredPointCount,
            coefficients = model,
            metrics = calculateMetrics(samples, model),
            monotonic = isMonotonic(model),
            curvePoints = buildCurve(samples, model),
        )
    }

    private fun fitLinear(points: List<Pair<Double, Double>>): DoubleArray {
        val count = points.size.toDouble()
        val sx = points.sumOf { it.first }
        val sy = points.sumOf { it.second }
        val sxx = points.sumOf { it.first * it.first }
        val sxy = points.sumOf { it.first * it.second }
        val denominator = count * sxx - sx * sx
        require(abs(denominator) >= 1e-12) { "linear fit is singular" }
        val slope = (count * sxy - sx * sy) / denominator
        val intercept = (sy - slope * sx) / count
        return doubleArrayOf(0.0, slope, intercept)
    }

    private fun fitQuadratic(points: List<Pair<Double, Double>>): DoubleArray {
        val sx = points.sumOf { it.first }
        val sx2 = points.sumOf { it.first * it.first }
        val sx3 = points.sumOf { it.first * it.first * it.first }
        val sx4 = points.sumOf { it.first * it.first * it.first * it.first }
        val sy = points.sumOf { it.second }
        val sxy = points.sumOf { it.first * it.second }
        val sx2y = points.sumOf { it.first * it.first * it.second }
        val count = points.size.toDouble()

        return gaussianSolve(
            matrix = arrayOf(
                doubleArrayOf(sx4, sx3, sx2),
                doubleArrayOf(sx3, sx2, sx),
                doubleArrayOf(sx2, sx, count),
            ),
            vector = doubleArrayOf(sx2y, sxy, sy),
        )
    }

    private fun gaussianSolve(matrix: Array<DoubleArray>, vector: DoubleArray): DoubleArray {
        val augmented = Array(vector.size) { row ->
            DoubleArray(vector.size + 1) { col ->
                if (col == vector.size) {
                    vector[row]
                } else {
                    matrix[row][col]
                }
            }
        }

        for (pivot in vector.indices) {
            var best = pivot
            for (row in pivot until vector.size) {
                if (abs(augmented[row][pivot]) > abs(augmented[best][pivot])) {
                    best = row
                }
            }
            require(abs(augmented[best][pivot]) >= 1e-12) { "quadratic fit is singular" }
            if (best != pivot) {
                val tmp = augmented[pivot]
                augmented[pivot] = augmented[best]
                augmented[best] = tmp
            }

            val pivotValue = augmented[pivot][pivot]
            for (col in pivot until vector.size + 1) {
                augmented[pivot][col] /= pivotValue
            }

            for (row in vector.indices) {
                if (row == pivot) continue
                val factor = augmented[row][pivot]
                if (factor == 0.0) continue
                for (col in pivot until vector.size + 1) {
                    augmented[row][col] -= factor * augmented[pivot][col]
                }
            }
        }

        return DoubleArray(vector.size) { index -> augmented[index][vector.size] }
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
        val meanAbsoluteError = absoluteErrors.average().toFloat()
        val maxAbsoluteError = absoluteErrors.maxOrNull() ?: 0.0f
        return CalibrationFitMetrics(
            pointCount = samples.size,
            meanAbsoluteErrorKg = meanAbsoluteError,
            maxAbsoluteErrorKg = maxAbsoluteError,
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
                    coefficients = coefficients,
                    runtimeDistance = calibrationDistanceMmToRuntime(distanceMm),
                ),
            )
        }
    }

    private fun predictWeightKg(
        coefficients: CalibrationFitCoefficients,
        runtimeDistance: Float,
    ): Float {
        val shifted = runtimeDistance - coefficients.referenceDistance
        return coefficients.c0 * shifted * shifted +
            coefficients.c1 * shifted +
            coefficients.c2
    }

    private fun isMonotonic(coefficients: CalibrationFitCoefficients): Boolean {
        val minDistance = calibrationDistanceMmToRuntime(CALIBRATION_VALID_MIN_DISTANCE_MM)
        val maxDistance = calibrationDistanceMmToRuntime(CALIBRATION_VALID_MAX_DISTANCE_MM)
        val xMin = minDistance - coefficients.referenceDistance
        val xMax = maxDistance - coefficients.referenceDistance
        val tolerance = -0.0001f

        return if (abs(coefficients.c0) < 1e-6f) {
            coefficients.c1 >= tolerance
        } else {
            val dMin = 2.0f * coefficients.c0 * xMin + coefficients.c1
            val dMax = 2.0f * coefficients.c0 * xMax + coefficients.c1
            dMin >= tolerance && dMax >= tolerance
        }
    }
}
