#pragma once
#include "runtime_environment.h"
constexpr unsigned MALLOC_CAP_SPIRAM = 1U, MALLOC_CAP_8BIT = 2U;
inline void* heap_caps_malloc(size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return platform::esp::common::memory::allocatePreferred("geocaching.object", size, false);
}
