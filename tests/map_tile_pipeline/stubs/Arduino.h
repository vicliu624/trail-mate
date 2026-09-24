#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct DiagnosticSerialStub
{
    std::string output;
    std::size_t write(const uint8_t* data, std::size_t size)
    {
        output.append(reinterpret_cast<const char*>(data), size);
        return size;
    }
};
inline DiagnosticSerialStub Serial;
