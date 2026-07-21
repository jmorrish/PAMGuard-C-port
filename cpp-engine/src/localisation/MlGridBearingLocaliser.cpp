#include "pamguard/localisation/MlGridBearingLocaliser.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pamguard::localisation {

namespace {

constexpr double kPi = std::numbers::pi;

using Vec3 = std::array<double, 3>;

double dot3(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 sub3(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

/** PamVector.vecProd. */
Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {
        a[1] * b[2] - a[2] * b[1],
        -a[0] * b[2] + a[2] * b[0],
        a[0] * b[1] - a[1] * b[0],
    };
}

/** PamVector.rotate(double): a rotation about z, applied in the xy plane. */
Vec3 rotate_about_z(const Vec3& v, double angle) {
    const double s = std::sin(angle);
    const double c = std::cos(angle);
    return {v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2]};
}

/**
 * PamVector.getPerpendicularVector. The first branch returns for any vector
 * lying in the xy plane, which makes the later `vector[2] == 0` branch
 * unreachable; the port keeps the reference's structure rather than tidying it,
 * because the two branches return different vectors and the dead one is not
 * the one that runs.
 */
Vec3 perpendicular_vector(const Vec3& v) {
    if (v[2] == 0.0) {
        return rotate_about_z(v, kPi / 2.0);
    }
    if (v[0] == 0.0) {
        return {1.0, 0.0, 0.0};
    }
    if (v[1] == 0.0) {
        return {0.0, 1.0, 0.0};
    }
    return rotate_about_z({v[0], v[1], 0.0}, kPi / 2.0);
}

/** PamVector.fromHeadAndSlantR. */
Vec3 from_head_and_slant(double heading, double slant_angle) {
    const double z = std::sin(slant_angle);
    const double r = std::cos(slant_angle);
    return {r * std::sin(heading), r * std::cos(heading), z};
}

/** PamVector.sumComponentsSquared: the root of the summed squared products. */
double sum_components_squared(const Vec3& a, const Vec3& b) {
    double sum = 0.0;
    for (std::size_t dim = 0; dim < 3; ++dim) {
        const double term = a[dim] * b[dim];
        sum += term * term;
    }
    return std::sqrt(sum);
}

/**
 * Jama LUDecomposition (Crout, partial pivoting) followed by its solve against
 * the identity, which is what Jama's Matrix.inverse() does. Ported rather than
 * replaced with a 3x3 cofactor formula so the rounding matches the reference.
 * Returns false when the decomposition is singular, where Jama throws and
 * PAMGuard's `prepare` catches and returns with an empty table.
 */
bool jama_inverse_3x3(const std::array<Vec3, 3>& input, std::array<Vec3, 3>& inverse) {
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

/**
 * PamVector.rotate(Matrix) makes the vector a **single-row** matrix and
 * right-multiplies, so the result is the row vector times the matrix, not the
 * matrix times a column.
 */
Vec3 rotate_by_matrix(const Vec3& v, const std::array<Vec3, 3>& matrix) {
    Vec3 out{};
    for (std::size_t j = 0; j < 3; ++j) {
        double sum = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            sum += v[i] * matrix[i][j];
        }
        out[j] = sum;
    }
    return out;
}

/** PamVector.addQuadrature over two error vectors, component by component. */
Vec3 add_quadrature(const Vec3& a, const Vec3& b) {
    Vec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = std::sqrt(a[i] * a[i] + b[i] * b[i]);
    }
    return out;
}

/** PeakSearch.parabolInterpolate: the sub-bin offset from three heights. */
double parabolic_offset(double y1, double y2, double y3) {
    const double bottom = 2.0 * y2 - y1 - y3;
    if (bottom == 0.0) {
        return 0.0;
    }
    return 0.5 * (y3 - y1) / bottom;
}

/** MLGridBearingLocaliser2.getCurvature. */
double curvature(double v1, double v2, double v3) {
    const double a2 = 1.0 / (2.0 * v2 - v1 - v3);
    if (a2 >= 0.0) {
        return std::sqrt(a2);
    }
    return 0.0;
}

} // namespace

MlGridBearingLocaliser::MlGridBearingLocaliser(MlGridBearingConfig config)
    : config_(std::move(config)) {
    const std::size_t phone_count = config_.hydrophones.size();
    if (phone_count < 2 || config_.speed_of_sound_mps <= 0.0 || config_.theta_step_radians <= 0.0 ||
        config_.phi_step_radians <= 0.0) {
        return;
    }
    pair_count_ = phone_count * (phone_count - 1) / 2;

    std::vector<Vec3> positions;
    std::vector<int> streamer_ids;
    positions.reserve(phone_count);
    streamer_ids.reserve(phone_count);
    for (const auto& hydrophone : config_.hydrophones) {
        positions.push_back(hydrophone.position_m);
        streamer_ids.push_back(hydrophone.streamer_id);
    }
    array_type_ = array_shape(positions, streamer_ids);
    const auto axes = array_directions(positions, streamer_ids);

    // prepare(): grid extents and peak-search wrapping follow the shape.
    theta_range_ = {-kPi, kPi};
    phi_range_ = {0.0, 0.0};
    switch (array_type_) {
    case ArrayShapeType::Line:
        theta_range_[0] = 0.0;
        phi_range_ = {0.0, 0.0};
        wrap_theta_ = false;
        wrap_theta_step_ = 1;
        break;
    case ArrayShapeType::Plane:
        // The reference searches the upper hemisphere only for a plane.
        phi_range_ = {0.0, kPi / 2.0};
        wrap_theta_ = true;
        wrap_theta_step_ = 2;
        break;
    case ArrayShapeType::Volume:
        phi_range_ = {-kPi / 2.0, kPi / 2.0};
        wrap_theta_ = true;
        wrap_theta_step_ = 2;
        break;
    default:
        return;
    }

    theta_bins_ = static_cast<std::size_t>(std::floor((theta_range_[1] - theta_range_[0]) / config_.theta_step_radians)) + 1;
    phi_bins_ = phi_range_[1] == phi_range_[0]
                    ? 1
                    : static_cast<std::size_t>(std::floor((phi_range_[1] - phi_range_[0]) / config_.phi_step_radians)) + 1;

    // The rotation frame: the array axes as matrix rows, inverted. Missing
    // axes are filled in as the reference does — a perpendicular to the first
    // for a line, and the cross product of the first two otherwise.
    std::array<Vec3, 3> rotation_rows{};
    for (std::size_t i = 0; i < 3; ++i) {
        rotation_rows[i] = i < axes.size() ? axes[i] : Vec3{0.0, 0.0, 0.0};
    }
    if (axes.size() < 2) {
        if (axes.empty()) {
            return;
        }
        rotation_rows[1] = perpendicular_vector(rotation_rows[0]);
    }
    if (axes.size() < 3) {
        rotation_rows[2] = cross3(rotation_rows[0], rotation_rows[1]);
    }
    std::array<Vec3, 3> rotation{};
    if (!jama_inverse_3x3(rotation_rows, rotation)) {
        return;
    }

    delay_grid_.assign(theta_bins_ * phi_bins_ * pair_count_, 0.0);
    delay_error_grid_.assign(theta_bins_ * phi_bins_ * pair_count_, 0.0);

    const double sos_error_factor =
        config_.speed_of_sound_error_mps / config_.speed_of_sound_mps / config_.speed_of_sound_mps;

    std::size_t pair_index = 0;
    for (std::size_t i = 0; i < phone_count; ++i) {
        for (std::size_t j = i + 1; j < phone_count; ++j, ++pair_index) {
            const auto& first = config_.hydrophones[i];
            const auto& second = config_.hydrophones[j];
            Vec3 pair_vector = sub3(second.position_m, first.position_m);
            // getSeparationErrorVector, for the second phone against the
            // first; streamer-level error vectors are not modelled.
            Vec3 pair_error_vector = add_quadrature(second.position_error_m, first.position_error_m);
            pair_vector = rotate_by_matrix(pair_vector, rotation);
            pair_error_vector = rotate_by_matrix(pair_error_vector, rotation);

            for (std::size_t theta_bin = 0; theta_bin < theta_bins_; ++theta_bin) {
                for (std::size_t phi_bin = 0; phi_bin < phi_bins_; ++phi_bin) {
                    const double theta = theta_bin_to_angle(static_cast<double>(theta_bin));
                    const double phi = phi_bin_to_angle(static_cast<double>(phi_bin));
                    const Vec3 bearing_vector = from_head_and_slant(kPi / 2.0 - theta, phi);
                    const std::size_t index = (theta_bin * phi_bins_ + phi_bin) * pair_count_ + pair_index;
                    delay_grid_[index] = -dot3(bearing_vector, pair_vector) / config_.speed_of_sound_mps;

                    const double e1 = sum_components_squared(pair_error_vector, bearing_vector) / config_.speed_of_sound_mps;
                    const double e2 = dot3(pair_vector, bearing_vector) * sos_error_factor;
                    const double e3 = config_.timing_error_seconds;
                    delay_error_grid_[index] = std::sqrt(e1 * e1 + e2 * e2 + e3 * e3);
                }
            }
        }
    }

    prepared_ = true;
}

double MlGridBearingLocaliser::theta_bin_to_angle(double bin) const {
    return kPi / 2.0 - (theta_range_[0] + bin * config_.theta_step_radians);
}

double MlGridBearingLocaliser::phi_bin_to_angle(double bin) const {
    return phi_range_[0] + bin * config_.phi_step_radians;
}

double MlGridBearingLocaliser::likelihood_at(const std::vector<double>& delays_seconds, std::size_t theta_bin,
                                             std::size_t phi_bin) const {
    double value = 0.0;
    const std::size_t base = (theta_bin * phi_bins_ + phi_bin) * pair_count_;
    for (std::size_t pair = 0; pair < pair_count_; ++pair) {
        const double residual = (delays_seconds[pair] - delay_grid_[base + pair]) / delay_error_grid_[base + pair];
        value -= 0.5 * residual * residual;
    }
    return value;
}

std::vector<std::vector<double>> MlGridBearingLocaliser::likelihood_surface(
    const std::vector<double>& delays_seconds) const {
    std::vector<std::vector<double>> surface;
    if (!prepared_ || delays_seconds.size() != pair_count_) {
        return surface;
    }
    surface.assign(theta_bins_, std::vector<double>(phi_bins_, 0.0));
    for (std::size_t theta_bin = 0; theta_bin < theta_bins_; ++theta_bin) {
        for (std::size_t phi_bin = 0; phi_bin < phi_bins_; ++phi_bin) {
            surface[theta_bin][phi_bin] = likelihood_at(delays_seconds, theta_bin, phi_bin);
        }
    }
    return surface;
}

std::array<double, 2> MlGridBearingLocaliser::errors_at(const std::vector<double>& delays_seconds, long theta_bin,
                                                        long phi_bin) const {
    std::array<double, 2> errors{0.0, 0.0};
    const auto theta_count = static_cast<long>(theta_bins_);
    const auto phi_count = static_cast<long>(phi_bins_);
    if (theta_count >= 3) {
        theta_bin = std::min(std::max(1L, theta_bin), theta_count - 2);
        const auto bin = static_cast<std::size_t>(theta_bin);
        const auto phi = static_cast<std::size_t>(std::min(std::max(0L, phi_bin), phi_count - 1));
        errors[0] = curvature(likelihood_at(delays_seconds, bin - 1, phi), likelihood_at(delays_seconds, bin, phi),
                              likelihood_at(delays_seconds, bin + 1, phi)) *
                    config_.theta_step_radians;
    }
    if (phi_count >= 3) {
        phi_bin = std::min(std::max(1L, phi_bin), phi_count - 2);
        const auto bin = static_cast<std::size_t>(phi_bin);
        const auto theta = static_cast<std::size_t>(std::min(std::max(0L, theta_bin), theta_count - 1));
        errors[1] = curvature(likelihood_at(delays_seconds, theta, bin - 1), likelihood_at(delays_seconds, theta, bin),
                              likelihood_at(delays_seconds, theta, bin + 1)) *
                    config_.phi_step_radians;
    }
    return errors;
}

std::optional<MlGridBearingResult> MlGridBearingLocaliser::localise(const std::vector<double>& delays_seconds) const {
    if (!prepared_ || delays_seconds.size() != pair_count_) {
        return std::nullopt;
    }
    for (const double error : delay_error_grid_) {
        if (error == 0.0) {
            return std::nullopt;
        }
    }

    const auto surface = likelihood_surface(delays_seconds);

    // PeakSearch.simplePeakSearch(double[][]): the running maximum starts at
    // data[0][0] and only a strictly greater value displaces it, so the first
    // bin of a tied ridge wins.
    double peak_value = surface[0][0];
    std::size_t peak_theta = 0;
    std::size_t peak_phi = 0;
    for (std::size_t theta_bin = 0; theta_bin < theta_bins_; ++theta_bin) {
        for (std::size_t phi_bin = 0; phi_bin < phi_bins_; ++phi_bin) {
            if (surface[theta_bin][phi_bin] > peak_value) {
                peak_value = surface[theta_bin][phi_bin];
                peak_theta = theta_bin;
                peak_phi = phi_bin;
            }
        }
    }

    // PeakSearch.interpolatedPeakSearch(double[][]): both interpolations index
    // the surface with the **integer** peak bins, so the theta offset does not
    // move the row the phi interpolation reads.
    const std::size_t integer_theta = peak_theta;
    const std::size_t integer_phi = peak_phi;
    double theta_bin_position = static_cast<double>(peak_theta);
    double phi_bin_position = static_cast<double>(peak_phi);

    if ((peak_theta > 0 && peak_theta < theta_bins_ - 1) || wrap_theta_) {
        std::size_t before = peak_theta == 0 ? theta_bins_ - static_cast<std::size_t>(wrap_theta_step_) : peak_theta - 1;
        std::size_t after = peak_theta + 1 == theta_bins_ ? static_cast<std::size_t>(wrap_theta_step_ - 1) : peak_theta + 1;
        theta_bin_position += parabolic_offset(surface[before][integer_phi], surface[peak_theta][integer_phi],
                                               surface[after][integer_phi]);
    }
    // wrapDim1 is never set by the reference, so phi interpolates only away
    // from the grid edges.
    if (peak_phi > 0 && peak_phi < phi_bins_ - 1) {
        phi_bin_position += parabolic_offset(surface[integer_theta][peak_phi - 1], surface[integer_theta][peak_phi],
                                             surface[integer_theta][peak_phi + 1]);
    }

    const auto errors = errors_at(delays_seconds, static_cast<long>(std::lround(theta_bin_position)),
                                  static_cast<long>(std::lround(phi_bin_position)));

    MlGridBearingResult result;
    result.theta_radians = theta_bin_to_angle(theta_bin_position);
    result.theta_error_radians = errors[0];
    if (array_type_ != ArrayShapeType::Line) {
        result.phi_radians = phi_bin_to_angle(phi_bin_position);
        result.phi_error_radians = errors[1];
        result.has_phi = true;
    }
    return result;
}

} // namespace pamguard::localisation
