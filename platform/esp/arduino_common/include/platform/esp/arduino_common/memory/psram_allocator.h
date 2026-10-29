#pragma once
#include "platform/memory/psram_allocator.h"

namespace platform::esp::arduino_common::memory
{
using ::platform::memory::psramAllocationFailed;
template <typename T>
using PsramAllocator = ::platform::memory::PsramAllocator<T>;
using ::platform::memory::operator==;
using ::platform::memory::operator!=;
} // namespace platform::esp::arduino_common::memory
