#include "pamguard/localisation/FarFieldBearingLocaliser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

namespace pamguard::localisation {

namespace {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

std::optional<Vec3> find_position(const std::vector<HydrophonePosition>& hydrophones, std::size_t channel) {
    const auto found = std::find_if(hydrophones.begin(), hydrophones.end(), [&](const auto& hydrophone) {
        return hydrophone.channel == channel;
    });
    if (found == hydrophones.end()) {
        return std::nullopt;
    }
    return Vec3{found->x_m, found->y_m, found->z_m};
}

double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double norm(Vec3 value) {
    return std::sqrt(dot(value, value));
}

Vec3 normalise(Vec3 value) {
    const double length = norm(value);
    if (length == 0.0) {
        return {};
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

bool solve_3x3(std::array<std::array<double, 3>, 3> a, std::array<double, 3> b, Vec3& out) {
    for (std::size_t pivot = 0; pivot < 3; ++pivot) {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1; row < 3; ++row) {
            if (std::abs(a[row][pivot]) > std::abs(a[best][pivot])) {
                best = row;
            }
        }
        if (std::abs(a[best][pivot]) < 1e-18) {
            return false;
        }
        if (best != pivot) {
            std::swap(a[best], a[pivot]);
            std::swap(b[best], b[pivot]);
        }
        const double scale = a[pivot][pivot];
        for (std::size_t col = pivot; col < 3; ++col) {
            a[pivot][col] /= scale;
        }
        b[pivot] /= scale;
        for (std::size_t row = 0; row < 3; ++row) {
            if (row == pivot) {
                continue;
            }
            const double factor = a[row][pivot];
            for (std::size_t col = pivot; col < 3; ++col) {
                a[row][col] -= factor * a[pivot][col];
            }
            b[row] -= factor * b[pivot];
        }
    }
    out = Vec3{b[0], b[1], b[2]};
    return true;
}

double degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

} // namespace

FarFieldBearingLocaliser::FarFieldBearingLocaliser(FarFieldBearingConfig config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("far-field bearing sample_rate_hz must be positive");
    }
    if (config_.speed_of_sound_mps <= 0.0) {
        throw std::invalid_argument("speed_of_sound_mps must be positive");
    }
}

const FarFieldBearingConfig& FarFieldBearingLocaliser::config() const noexcept {
    return config_;
}

FarFieldBearingResult FarFieldBearingLocaliser::estimate(
    const std::vector<ChannelPairDelay>& delays,
    const std::vector<std::size_t>& click_channels,
    std::size_t click_index,
    std::int64_t click_start_sample) const {
    struct Row {
        Vec3 baseline_seconds;
        double delay_seconds = 0.0;
    };

    FarFieldBearingResult result;
    result.click_index = click_index;
    result.click_start_sample = click_start_sample;

    std::vector<Row> rows;
    rows.reserve(delays.size());
    for (const auto& delay : delays) {
        if (delay.channel_a >= click_channels.size() || delay.channel_b >= click_channels.size()) {
            continue;
        }
        const auto channel_a = click_channels[delay.channel_a];
        const auto channel_b = click_channels[delay.channel_b];
        const auto pos_a = find_position(config_.hydrophones, channel_a);
        const auto pos_b = find_position(config_.hydrophones, channel_b);
        if (!pos_a || !pos_b) {
            continue;
        }
        rows.push_back(Row{
            Vec3{
                (pos_b->x - pos_a->x) / config_.speed_of_sound_mps,
                (pos_b->y - pos_a->y) / config_.speed_of_sound_mps,
                (pos_b->z - pos_a->z) / config_.speed_of_sound_mps,
            },
            delay.delay.delay_samples / config_.sample_rate_hz,
        });
    }

    result.used_pairs = rows.size();
    if (rows.empty()) {
        return result;
    }

    Vec3 estimate;
    if (rows.size() == 1) {
        const double denom = dot(rows[0].baseline_seconds, rows[0].baseline_seconds);
        if (denom == 0.0) {
            return result;
        }
        estimate = Vec3{
            rows[0].baseline_seconds.x * rows[0].delay_seconds / denom,
            rows[0].baseline_seconds.y * rows[0].delay_seconds / denom,
            rows[0].baseline_seconds.z * rows[0].delay_seconds / denom,
        };
    }
    else {
        std::array<std::array<double, 3>, 3> normal{};
        std::array<double, 3> rhs{};
        for (const auto& row : rows) {
            const std::array<double, 3> a{row.baseline_seconds.x, row.baseline_seconds.y, row.baseline_seconds.z};
            for (std::size_t i = 0; i < 3; ++i) {
                rhs[i] += a[i] * row.delay_seconds;
                for (std::size_t j = 0; j < 3; ++j) {
                    normal[i][j] += a[i] * a[j];
                }
            }
        }
        for (std::size_t i = 0; i < 3; ++i) {
            normal[i][i] += 1e-12;
        }
        if (!solve_3x3(normal, rhs, estimate)) {
            return result;
        }
    }

    estimate = normalise(estimate);
    if (norm(estimate) == 0.0) {
        return result;
    }

    double residual_sum = 0.0;
    for (const auto& row : rows) {
        const double model = dot(row.baseline_seconds, estimate);
        const double residual = model - row.delay_seconds;
        residual_sum += residual * residual;
    }

    result.valid = true;
    result.unit_x = estimate.x;
    result.unit_y = estimate.y;
    result.unit_z = estimate.z;
    result.azimuth_degrees = degrees(std::atan2(estimate.y, estimate.x));
    if (result.azimuth_degrees < 0.0) {
        result.azimuth_degrees += 360.0;
    }
    result.elevation_degrees = degrees(std::asin(std::clamp(estimate.z, -1.0, 1.0)));
    result.residual_rms_seconds = std::sqrt(residual_sum / static_cast<double>(rows.size()));
    return result;
}

} // namespace pamguard::localisation
