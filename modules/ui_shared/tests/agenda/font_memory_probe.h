#pragma once

#include <cstddef>

namespace agenda_test_fonts
{
struct FontMemory
{
    std::size_t bytes;
    std::size_t allocations;
    std::size_t peak_bytes;
};
void beginMemoryProbe();
FontMemory memoryProbe();
void endMemoryProbe();
} // namespace agenda_test_fonts
