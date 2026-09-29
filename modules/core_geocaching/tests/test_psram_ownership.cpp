#include "platform/memory/psram_ptr.h"
#include "platform/ui/psram_ui_lease.h"
#include <cassert>
#include <cstdint>

bool lvgl_external_font_load_uses_strict_psram() { return false; }
uint32_t lvgl_primary_caps(size_t) { return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT; }
uint32_t lvgl_secondary_caps(size_t) { return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT; }
// Compile the actual firmware allocator functions, including realloc policy.
#include "geocaching_lvgl_allocator.inc"
struct Object
{
    static inline unsigned alive = 0;
    Object() { ++alive; }
    ~Object() { --alive; }
    char payload[4096]{};
};
int main()
{
    static_assert(sizeof(platform::memory::PsramPtr<Object>) == sizeof(Object*));
    {
        platform::memory::PsramPtr<Object> object(platform::memory::createPsram<Object>());
        assert(object && Object::alive == 1 && esp_ptr_external_ram(object.get()));
    }
    assert(Object::alive == 0 && memory_domains.empty());
    fail_psram = true;
    assert(!platform::memory::createPsram<Object>() && Object::alive == 0);
    fail_psram = false;
    void* text = nullptr;
    {
        platform::ui::PsramUiLease page;
        assert(platform::ui::psramUiRequired());
        {
            platform::ui::PsramUiLease map;
            text = lv_malloc_core(24);
            assert(text && esp_ptr_external_ram(text));
        }
        assert(platform::ui::psramUiRequired());
        text = lv_realloc_core(text, 64);
        assert(text && esp_ptr_external_ram(text));
        fail_psram = true;
        assert(!lv_malloc_core(12));
        assert(!lv_realloc_core(text, 128));
        assert(esp_ptr_external_ram(text)); // Failed realloc retains the source.
        fail_psram = false;
    }
    assert(!platform::ui::psramUiRequired());
    text = lv_realloc_core(text, 16); // Deferred work must not migrate to RAM.
    assert(text && esp_ptr_external_ram(text));
    lv_free_core(text);
    assert(memory_domains.empty() && !internal_allocations && !preferred_allocations);
}
