#pragma once
extern const void* discovery_test_psram;
inline bool esp_ptr_external_ram(const void* pointer)
{
    return pointer == discovery_test_psram;
}
