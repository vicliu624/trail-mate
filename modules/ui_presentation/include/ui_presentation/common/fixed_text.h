#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ui
{

template <size_t N>
struct FixedText
{
    char data[N]{};

    const char* c_str() const
    {
        return data;
    }

    void clear()
    {
        data[0] = '\0';
    }

    bool empty() const
    {
        return data[0] == '\0';
    }
};

template <size_t N>
inline void copyText(FixedText<N>& out, const char* src)
{
    if (!src)
    {
        out.clear();
        return;
    }

    size_t index = 0;
    for (; index + 1 < N && src[index] != '\0'; ++index)
    {
        out.data[index] = src[index];
    }
    out.data[index] = '\0';
}

} // namespace ui
