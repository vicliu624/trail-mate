#pragma once

#include "hostlink_types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hostlink
{

bool build_status_payload(std::vector<uint8_t>& out, uint8_t link_state, uint32_t last_error);
bool apply_config(const uint8_t* data, size_t len, uint32_t* out_err);
bool set_time_epoch(uint64_t epoch_seconds);

} // namespace hostlink
