#pragma once

#include <array>

namespace pamguard::localisation {

using Matrix3 = std::array<std::array<double, 3>, 3>;

/**
 * Jama's LUDecomposition (Crout, partial pivoting) followed by its solve
 * against the identity, which is what Jama's Matrix.inverse() does. Ported
 * rather than replaced with a 3x3 cofactor formula so the rounding matches the
 * reference wherever PAMGuard inverts a 3x3.
 *
 * Returns false when the decomposition is singular, where Jama throws.
 */
[[nodiscard]] bool jama_inverse_3x3(const Matrix3& input, Matrix3& inverse);

/** PamVector.vecProd. */
[[nodiscard]] std::array<double, 3> cross_product(const std::array<double, 3>& a, const std::array<double, 3>& b);

/**
 * Jama's Matrix.times against a column vector: `result_i = sum_j m[i][j] * v[j]`.
 * Note this is **not** the same as PamVector.rotate(Matrix), which makes the
 * vector a single-row matrix and multiplies from the left instead.
 */
[[nodiscard]] std::array<double, 3> matrix_times_column(const Matrix3& matrix, const std::array<double, 3>& vector);

/** PamVector.rotate(Matrix): the vector as a single **row**, times the matrix. */
[[nodiscard]] std::array<double, 3> row_times_matrix(const std::array<double, 3>& vector, const Matrix3& matrix);

} // namespace pamguard::localisation
