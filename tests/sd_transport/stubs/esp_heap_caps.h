#pragma once
#include <cstddef>
constexpr int MALLOC_CAP_INTERNAL = 1;
constexpr int MALLOC_CAP_DMA = 2;
void* heap_caps_malloc(size_t, unsigned);
void heap_caps_free(void*);
