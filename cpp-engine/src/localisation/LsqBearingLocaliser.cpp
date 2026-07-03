#include "pamguard/localisation/LsqBearingLocaliser.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace pamguard::localisation {

namespace {

/** Replica of Jama's Maths.hypot (scaled form, not std::hypot). */
double jama_hypot(double a, double b) {
    double r;
    if (std::abs(a) > std::abs(b)) {
        r = b / a;
        r = std::abs(a) * std::sqrt(1 + r * r);
    }
    else if (b != 0) {
        r = a / b;
        r = std::abs(b) * std::sqrt(1 + r * r);
    }
    else {
        r = 0.0;
    }
    return r;
}

double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double norm3(const std::array<double, 3>& a) {
    return std::sqrt(dot3(a, a));
}

} // namespace

LsqBearingLocaliser::LsqBearingLocaliser(LsqBearingConfig config)
    : config_(std::move(config)) {
    if (config_.speed_of_sound_mps <= 0.0) {
        throw std::invalid_argument("lsq bearing speed_of_sound_mps must be positive");
    }
    if (config_.pairs.empty()) {
        throw std::invalid_argument("lsq bearing needs at least one hydrophone pair");
    }

    const double c = config_.speed_of_sound_mps;
    const auto pair_count = config_.pairs.size();
    fit_weights_.resize(pair_count);
    hydrophone_vectors_.resize(pair_count);
    hydrophone_error_vectors_.resize(pair_count);
    qr_.resize(pair_count);
    for (std::size_t row = 0; row < pair_count; ++row) {
        const auto& pair = config_.pairs[row];
        const double baseline_norm = norm3(pair.baseline_m);
        std::array<double, 3> unit{};
        if (baseline_norm != 0.0) {
            unit = {pair.baseline_m[0] / baseline_norm, pair.baseline_m[1] / baseline_norm,
                    pair.baseline_m[2] / baseline_norm};
        }
        const double error_component = dot3(unit, pair.error_m);
        fit_weights_[row] = std::pow(baseline_norm / error_component, 2.0);
        for (std::size_t e = 0; e < 3; ++e) {
            qr_[row][e] = pair.baseline_m[e] / c * fit_weights_[row];
            hydrophone_vectors_[row][e] = pair.baseline_m[e] / c;
            hydrophone_error_vectors_[row][e] = pair.error_m[e] / c;
        }
    }

    // Jama QRDecomposition constructor (Householder, no pivoting).
    const auto m = pair_count;
    for (std::size_t k = 0; k < 3; ++k) {
        double nrm = 0.0;
        for (std::size_t i = k; i < m; ++i) {
            nrm = jama_hypot(nrm, qr_[i][k]);
        }
        if (nrm != 0.0) {
            if (qr_[k][k] < 0.0) {
                nrm = -nrm;
            }
            for (std::size_t i = k; i < m; ++i) {
                qr_[i][k] /= nrm;
            }
            qr_[k][k] += 1.0;
            for (std::size_t j = k + 1; j < 3; ++j) {
                double s = 0.0;
                for (std::size_t i = k; i < m; ++i) {
                    s += qr_[i][k] * qr_[i][j];
                }
                s = -s / qr_[k][k];
                for (std::size_t i = k; i < m; ++i) {
                    qr_[i][j] += s * qr_[i][k];
                }
            }
        }
        r_diag_[k] = -nrm;
    }
}

const LsqBearingConfig& LsqBearingLocaliser::config() const noexcept {
    return config_;
}

std::optional<std::array<double, 3>> LsqBearingLocaliser::qr_solve(std::vector<double> rhs) const {
    // Jama solve: rank-deficient decompositions throw; we report no result.
    for (std::size_t k = 0; k < 3; ++k) {
        if (r_diag_[k] == 0.0) {
            return std::nullopt;
        }
    }

    const auto m = qr_.size();
    for (std::size_t k = 0; k < 3; ++k) {
        double s = 0.0;
        for (std::size_t i = k; i < m; ++i) {
            s += qr_[i][k] * rhs[i];
        }
        s = -s / qr_[k][k];
        for (std::size_t i = k; i < m; ++i) {
            rhs[i] += s * qr_[i][k];
        }
    }
    for (std::size_t k = 3; k-- > 0;) {
        rhs[k] /= r_diag_[k];
        for (std::size_t i = 0; i < k; ++i) {
            rhs[i] -= rhs[k] * qr_[i][k];
        }
    }
    return std::array<double, 3>{rhs[0], rhs[1], rhs[2]};
}

std::optional<LsqBearingResult> LsqBearingLocaliser::localise(const std::vector<double>& delays_seconds) const {
    if (delays_seconds.size() != config_.pairs.size()) {
        throw std::invalid_argument("lsq bearing delay count must match configured pair count");
    }

    std::vector<double> norm_delays(delays_seconds.size(), 0.0);
    for (std::size_t i = 0; i < delays_seconds.size(); ++i) {
        norm_delays[i] = -delays_seconds[i] * fit_weights_[i];
    }
    const auto solution = qr_solve(std::move(norm_delays));
    if (!solution.has_value()) {
        return std::nullopt;
    }

    auto v = *solution;
    const double m = norm3(v);
    if (m != 0.0) {
        v = {v[0] / m, v[1] / m, v[2] / m};
    }

    LsqBearingResult result;
    result.azimuth_radians = std::numbers::pi / 2.0 - std::atan2(v[0], v[1]);
    result.elevation_radians = std::asin(v[2]);

    const double one_deg = std::numbers::pi / 180.0;
    for (int i = 0; i < 20; ++i) {
        const double test_deg = 1.0 + i;
        const double a_diff = test_deg * one_deg;
        const double l1 = log_likelihood(delays_seconds, result.azimuth_radians - a_diff, result.elevation_radians);
        const double l2 = log_likelihood(delays_seconds, result.azimuth_radians, result.elevation_radians);
        const double l3 = log_likelihood(delays_seconds, result.azimuth_radians + a_diff, result.elevation_radians);
        result.azimuth_error_radians = std::sqrt(1.0 / (l1 + l3 - 2.0 * l2)) * a_diff;
        const double l1a = log_likelihood(delays_seconds, result.azimuth_radians, result.elevation_radians - a_diff);
        const double l3a = log_likelihood(delays_seconds, result.azimuth_radians, result.elevation_radians + a_diff);
        result.elevation_error_radians = std::sqrt(1.0 / (l1a + l3a - 2.0 * l2)) * a_diff;
    }

    return result;
}

double LsqBearingLocaliser::log_likelihood(const std::vector<double>& delays_seconds, double angle0, double angle1) const {
    const std::array<double, 3> whale{
        std::cos(angle1) * std::cos(angle0),
        std::cos(angle1) * std::sin(angle0),
        std::sin(angle1),
    };

    const double c = config_.speed_of_sound_mps;
    const double dc = config_.speed_of_sound_error_mps;
    double chi = 0.0;
    for (std::size_t i = 0; i < hydrophone_vectors_.size(); ++i) {
        const double time = dot3(hydrophone_vectors_[i], whale);
        const double time_error = dot3(hydrophone_error_vectors_[i], whale);
        const double time_error2 = time * (dc / c / c);
        const double expected_variance = std::pow(time_error, 2.0) + std::pow(time_error2, 2.0)
            + std::pow(config_.timing_error_seconds, 2.0);
        // PAMGuard's logLikelihood overwrites chi for each pair rather than
        // accumulating; reproduced faithfully.
        chi = std::pow(time + delays_seconds[i], 2.0) / expected_variance;
    }
    return chi / 2.0;
}

} // namespace pamguard::localisation
