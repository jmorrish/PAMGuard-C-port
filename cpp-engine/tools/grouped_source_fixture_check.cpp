#include "pamguard/core/GroupedSource.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> split(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    return fields;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        return 2;
    }
    std::string line;
    std::string active_case;
    std::vector<std::vector<std::size_t>> actual;
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.rfind("case,", 0) == 0) {
            continue;
        }
        const auto fields = split(line, ',');
        if (fields.at(0) != active_case) {
            active_case = fields.at(0);
            std::vector<int> groups;
            for (const auto& group : split(fields.at(2), ';')) {
                groups.push_back(std::stoi(group));
            }
            actual = pamguard::core::grouped_source_channels(
                static_cast<std::uint32_t>(std::stoul(fields.at(1))),
                groups, groups.size());
        }
        const auto index = static_cast<std::size_t>(
            std::stoul(fields.at(3)));
        std::uint32_t bitmap = 0;
        for (const auto channel : actual.at(index)) {
            bitmap |= std::uint32_t{1} << channel;
        }
        if (bitmap != static_cast<std::uint32_t>(
                          std::stoul(fields.at(4)))) {
            std::cerr << "Grouped-source mismatch in " << active_case
                      << " group " << index << "\n";
            return 1;
        }
        ++rows;
    }
    if (rows != 8) {
        std::cerr << "Unexpected grouped-source fixture row count\n";
        return 1;
    }
    std::cout << "Grouped source Java parity: rows=" << rows << "\n";
    return 0;
}
