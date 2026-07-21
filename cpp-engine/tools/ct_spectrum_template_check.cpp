#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/CtSpectrumTemplates.h"

namespace {

struct TemplateRow {
    std::string name;
    double sample_rate_hz = 0.0;
    std::vector<double> values;
};

std::vector<TemplateRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<TemplateRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("name,sampleRate,nBins,values", 0) == 0) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 4) {
            throw std::runtime_error("template row must have four columns: " + line);
        }

        TemplateRow row;
        row.name = cells[0];
        row.sample_rate_hz = std::stod(cells[1]);
        const auto expected_bins = static_cast<std::size_t>(std::stoull(cells[2]));
        std::stringstream values(cells[3]);
        std::string value;
        while (values >> value) {
            row.values.push_back(std::stod(value));
        }
        if (row.values.size() != expected_bins) {
            throw std::runtime_error("template bin count mismatch for " + row.name);
        }
        rows.push_back(std::move(row));
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any templates");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ct_spectrum_template_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        const auto templates = pamguard::detectors::ct_default_spectrum_templates();
        if (fixture.size() != templates.size()) {
            std::cerr << "Template count mismatch: fixture=" << fixture.size()
                      << " ported=" << templates.size() << "\n";
            return 1;
        }

        for (std::size_t i = 0; i < templates.size(); ++i) {
            const auto& expected = fixture[i];
            const auto& actual = templates[i];
            if (expected.name != actual.name) {
                std::cerr << "Template order/name mismatch at " << i << ": fixture=" << expected.name
                          << " ported=" << actual.name << "\n";
                return 1;
            }
            if (expected.sample_rate_hz != actual.sample_rate_hz) {
                std::cerr << "Template sample rate mismatch for " << actual.name << "\n";
                return 1;
            }
            if (expected.values.size() != actual.values.size()) {
                std::cerr << "Template bin count mismatch for " << actual.name << "\n";
                return 1;
            }
            for (std::size_t bin = 0; bin < actual.values.size(); ++bin) {
                if (expected.values[bin] != actual.values[bin]) {
                    std::cerr << "Template value mismatch for " << actual.name << " bin " << bin
                              << ": fixture=" << expected.values[bin] << " ported=" << actual.values[bin] << "\n";
                    return 1;
                }
            }
        }

        std::cout << "CT spectrum template parity passed\n";
        std::cout << "templates=" << templates.size() << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
