#pragma once
// Only the host compatibility target includes this synthetic placement port.
inline bool ifac_test_external_memory = true;
inline bool esp_ptr_external_ram(const void*) { return ifac_test_external_memory; }
