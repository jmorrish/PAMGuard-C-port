#include "pamguard/localisation/JamaMatrix.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pamguard::localisation {

bool jama_inverse_3x3(const Matrix3& input, Matrix3& inverse) {
    constexpr int n = 3;
    double lu[n][n];
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            lu[i][j] = input[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        }
    }
    int piv[n];
    for (int i = 0; i < n; ++i) {
        piv[i] = i;
    }

    double lu_col_j[n];
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            lu_col_j[i] = lu[i][j];
        }
        for (int i = 0; i < n; ++i) {
            const int kmax = std::min(i, j);
            double s = 0.0;
            for (int k = 0; k < kmax; ++k) {
                s += lu[i][k] * lu_col_j[k];
            }
            lu_col_j[i] -= s;
            lu[i][j] = lu_col_j[i];
        }
        int p = j;
        for (int i = j + 1; i < n; ++i) {
            if (std::abs(lu_col_j[i]) > std::abs(lu_col_j[p])) {
                p = i;
            }
        }
        if (p != j) {
            for (int k = 0; k < n; ++k) {
                std::swap(lu[p][k], lu[j][k]);
            }
            std::swap(piv[p], piv[j]);
        }
        if (lu[j][j] != 0.0) {
            for (int i = j + 1; i < n; ++i) {
                lu[i][j] /= lu[j][j];
            }
        }
    }

    for (int j = 0; j < n; ++j) {
        if (lu[j][j] == 0.0) {
            return false;
        }
    }

    // solve(identity), with the right-hand side permuted by piv first.
    double x[n][n];
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            x[i][j] = piv[i] == j ? 1.0 : 0.0;
        }
    }
    for (int k = 0; k < n; ++k) {
        for (int i = k + 1; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                x[i][j] -= x[k][j] * lu[i][k];
            }
        }
    }
    for (int k = n - 1; k >= 0; --k) {
        for (int j = 0; j < n; ++j) {
            x[k][j] /= lu[k][k];
        }
        for (int i = 0; i < k; ++i) {
            for (int j = 0; j < n; ++j) {
                x[i][j] -= x[k][j] * lu[i][k];
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            inverse[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = x[i][j];
        }
    }
    return true;
}

std::array<double, 3> cross_product(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return {
        a[1] * b[2] - a[2] * b[1],
        -a[0] * b[2] + a[2] * b[0],
        a[0] * b[1] - a[1] * b[0],
    };
}

std::array<double, 3> matrix_times_column(const Matrix3& matrix, const std::array<double, 3>& vector) {
    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = matrix[i][0] * vector[0] + matrix[i][1] * vector[1] + matrix[i][2] * vector[2];
    }
    return out;
}

std::array<double, 3> row_times_matrix(const std::array<double, 3>& vector, const Matrix3& matrix) {
    std::array<double, 3> out{};
    for (std::size_t j = 0; j < 3; ++j) {
        out[j] = vector[0] * matrix[0][j] + vector[1] * matrix[1][j] + vector[2] * matrix[2][j];
    }
    return out;
}

} // namespace pamguard::localisation
