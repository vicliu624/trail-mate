#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// Small image fixtures must remain bounded even when a test is given a bad path
// or unexpectedly large input. Production route readers have separate owners.
inline std::vector<uint8_t> read_fixture_bytes(const std::filesystem::path& path,
                                               size_t max_bytes = 4 * 1024 * 1024)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open image fixture");
    std::vector<uint8_t> result;
    std::array<char, 4096> chunk{};
    while (input)
    {
        input.read(chunk.data(), chunk.size());
        const auto count = static_cast<size_t>(input.gcount());
        if (count > max_bytes - result.size()) throw std::runtime_error("Image fixture exceeds byte limit");
        result.insert(result.end(), chunk.data(), chunk.data() + count);
    }
    if (!input.eof()) throw std::runtime_error("Cannot read image fixture");
    return result;
}
