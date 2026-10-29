#pragma once
#include <cstdint>
#include <string>
namespace platform::linux_runtime
{
// Persists a public node number in the selected state database. Returns zero
// on invalid override/storage failure; never falls back to a shared device ID.
uint32_t load_or_create_node_id(std::string& error);
} // namespace platform::linux_runtime
