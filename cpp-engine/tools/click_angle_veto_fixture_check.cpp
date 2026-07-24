#include "pamguard/detectors/ClickAngleVeto.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    while (fields.size() < 10) {
        fields.emplace_back();
    }
    return fields;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: click_angle_veto_fixture_check <fixture.csv>\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "Cannot open " << argv[1] << "\n";
        return 2;
    }
    std::string line;
    std::getline(input, line);
    std::size_t checked = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split(line);
        const auto veto_count =
            static_cast<std::size_t>(std::stoul(fields.at(3)));
        std::vector<pamguard::detectors::ClickAngleVeto> vetoes;
        for (std::size_t i = 0; i < veto_count; ++i) {
            const auto offset = 4 + i * 3;
            vetoes.push_back({
                static_cast<std::uint32_t>(std::stoul(fields.at(offset))),
                std::stod(fields.at(offset + 1)),
                std::stod(fields.at(offset + 2)),
            });
        }
        const bool expected = fields.at(2) == "true";
        const bool actual =
            pamguard::detectors::ClickAngleVetoes::pass_all(
                vetoes, std::stod(fields.at(1)));
        if (actual != expected) {
            std::cerr << fields.at(0) << ": expected " << expected
                      << ", got " << actual << "\n";
            return 1;
        }
        ++checked;
    }
    std::cout << "Angle-veto Java parity passed for " << checked
              << " cases\n";
    return checked == 11 ? 0 : 1;
}
