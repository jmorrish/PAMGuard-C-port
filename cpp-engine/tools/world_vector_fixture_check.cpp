#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/WorldVectors.h"

namespace {

using Vec3 = std::array<double, 3>;
using pamguard::localisation::ArrayShapeType;

struct VectorRow {
    std::string name;
    int array_type = 0;
    std::size_t axis_count = 0;
    std::size_t vector_count = 0;
    bool cone = false;
    std::array<Vec3, 2> vectors{};
};

struct VectorCase {
    std::vector<Vec3> positions;
    std::vector<double> angles;
    bool drop_axes = false;
};

const std::vector<Vec3> kPlane = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {2.0, 3.0, 0.0}};
const std::vector<Vec3> kVolume = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.5, 1.0, 2.5}};
const std::vector<Vec3> kLineY = {{0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {0.0, 4.0, 0.0}};
const std::vector<Vec3> kLineVertical = {{0.0, 0.0, 0.0}, {0.0, 0.0, 2.0}, {0.0, 0.0, 4.0}};
const std::vector<Vec3> kLineDiagonal = {{0.0, 0.0, 0.0}, {1.0, 1.0, 0.5}, {2.0, 2.0, 1.0}};

std::map<std::string, VectorCase> case_catalogue() {
    constexpr double half_pi = std::numbers::pi / 2.0;
    return {
        {"plane-zero-angles", {kPlane, {0.0, 0.0}, false}},
        {"plane-quarter-turn", {kPlane, {half_pi, 0.0}, false}},
        {"plane-elevated", {kPlane, {0.6, 0.4}, false}},
        {"plane-negative-elevation", {kPlane, {-1.2, -0.35}, false}},
        {"volume-zero-angles", {kVolume, {0.0, 0.0}, false}},
        {"volume-oblique", {kVolume, {2.1, -0.6}, false}},
        {"line-y-axis", {kLineY, {0.7}, false}},
        {"line-y-axis-broadside", {kLineY, {half_pi}, false}},
        {"line-vertical", {kLineVertical, {0.9}, false}},
        {"line-diagonal", {kLineDiagonal, {1.4}, false}},
        {"line-no-axes-fallback", {kLineY, {0.7}, true}},
        {"plane-no-axes-fallback", {kPlane, {0.6, 0.4}, true}},
    };
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> cells;
    std::stringstream stream(text);
    std::string cell;
    while (std::getline(stream, cell, delimiter)) {
        cells.push_back(cell);
    }
    return cells;
}

std::vector<VectorRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::vector<VectorRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("case,arrayType", 0) == 0) {
            continue;
        }
        const auto cells = split(line, ',');
        if (cells.size() != 11) {
            throw std::runtime_error("world vector row must have eleven columns: " + line);
        }
        VectorRow row;
        row.name = cells[0];
        row.array_type = std::stoi(cells[1]);
        row.axis_count = static_cast<std::size_t>(std::stoul(cells[2]));
        row.vector_count = static_cast<std::size_t>(std::stoul(cells[3]));
        row.cone = std::stoi(cells[4]) != 0;
        for (std::size_t i = 0; i < 2; ++i) {
            for (std::size_t e = 0; e < 3; ++e) {
                row.vectors[i][e] = std::stod(cells[5 + i * 3 + e]);
            }
        }
        rows.push_back(row);
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any world vector rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: world_vector_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        const auto catalogue = case_catalogue();
        constexpr double tolerance = 1e-14;
        double max_abs_error = 0.0;

        for (const auto& row : fixture) {
            const auto found = catalogue.find(row.name);
            if (found == catalogue.end()) {
                std::cerr << "Fixture case " << row.name << " has no entry in the check catalogue\n";
                return 1;
            }
            const auto& vector_case = found->second;

            const auto shape = pamguard::localisation::array_shape(vector_case.positions);
            if (static_cast<int>(shape) != row.array_type) {
                std::cerr << "Case " << row.name << " array type mismatch\n";
                return 1;
            }
            auto axes = pamguard::localisation::array_directions(vector_case.positions);
            if (vector_case.drop_axes) {
                axes.clear();
            }
            if (axes.size() != row.axis_count) {
                std::cerr << "Case " << row.name << " axis count mismatch: fixture=" << row.axis_count
                          << " ported=" << axes.size() << "\n";
                return 1;
            }

            const auto vectors = pamguard::localisation::world_vectors(shape, axes, vector_case.angles);
            if (vectors.size() != row.vector_count) {
                std::cerr << "Case " << row.name << " vector count mismatch: fixture=" << row.vector_count
                          << " ported=" << vectors.size() << "\n";
                return 1;
            }
            if (!vectors.empty() && vectors.front().cone != row.cone) {
                std::cerr << "Case " << row.name << " cone flag mismatch\n";
                return 1;
            }
            for (std::size_t i = 0; i < vectors.size(); ++i) {
                for (std::size_t e = 0; e < 3; ++e) {
                    const double error = std::abs(vectors[i].direction[e] - row.vectors[i][e]);
                    max_abs_error = std::max(max_abs_error, error);
                    if (error > tolerance) {
                        std::cerr << "Case " << row.name << " vector " << i << " component " << e
                                  << " mismatch: fixture=" << row.vectors[i][e]
                                  << " ported=" << vectors[i].direction[e] << "\n";
                        return 1;
                    }
                }
            }
        }

        // A point sub-array has no direction to report.
        if (!pamguard::localisation::world_vectors(ArrayShapeType::Point, {{1.0, 0.0, 0.0}}, {0.5}).empty()) {
            std::cerr << "A point sub-array should produce no world vectors\n";
            return 1;
        }
        if (!pamguard::localisation::world_vectors(ArrayShapeType::None, {}, {0.5}).empty()) {
            std::cerr << "An unknown sub-array shape should produce no world vectors\n";
            return 1;
        }
        // No angles means nothing to point at.
        if (!pamguard::localisation::world_vectors(ArrayShapeType::Volume, {{1.0, 0.0, 0.0}}, {}).empty()) {
            std::cerr << "An empty angle set should produce no world vectors\n";
            return 1;
        }

        // planar_unit_vector is the identity round trip of the azimuth and
        // elevation LSQBearingLocaliser derives from its fitted unit vector
        // (azimuth = pi/2 - atan2(x, y), elevation = asin(z)). Feeding those
        // angles back must return the vector it started from, which is why
        // LSQ output needs no array-axis rotation.
        for (const Vec3& fitted : {Vec3{0.3, 0.5, -0.2}, Vec3{-0.7, 0.1, 0.6}, Vec3{0.0, 1.0, 0.0},
                                   Vec3{0.0, 0.0, 1.0}, Vec3{-1.0, 0.0, 0.0}}) {
            const double length = std::sqrt(fitted[0] * fitted[0] + fitted[1] * fitted[1] + fitted[2] * fitted[2]);
            const Vec3 unit = {fitted[0] / length, fitted[1] / length, fitted[2] / length};
            const double azimuth = std::numbers::pi / 2.0 - std::atan2(unit[0], unit[1]);
            const double elevation = std::asin(unit[2]);
            const auto reconstructed = pamguard::localisation::planar_unit_vector(azimuth, elevation);
            for (std::size_t e = 0; e < 3; ++e) {
                if (std::abs(reconstructed[e] - unit[e]) > 1e-12) {
                    std::cerr << "planar_unit_vector did not round trip component " << e << ": expected " << unit[e]
                              << " got " << reconstructed[e] << "\n";
                    return 1;
                }
            }

            // And for a volume sub-array the two treatments agree, because a
            // volume array's principal axes are the Cartesian axes, so
            // getWorldVectors' rotation is the identity. That is what makes
            // skipping it for LSQ safe on the shape LSQ actually runs on.
            const auto rotated = pamguard::localisation::world_vectors(
                ArrayShapeType::Volume, pamguard::localisation::array_directions(kVolume), {azimuth, elevation});
            if (rotated.size() != 1) {
                std::cerr << "Volume world vectors should hold one entry\n";
                return 1;
            }
            for (std::size_t e = 0; e < 3; ++e) {
                if (std::abs(rotated.front().direction[e] - unit[e]) > 1e-12) {
                    std::cerr << "Volume array-axis rotation should be the identity, component " << e
                              << ": expected " << unit[e] << " got " << rotated.front().direction[e] << "\n";
                    return 1;
                }
            }
        }

        std::cout << "World vector parity passed\n";
        std::cout << "cases=" << fixture.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
