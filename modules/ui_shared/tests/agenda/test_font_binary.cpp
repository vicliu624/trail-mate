#include "lvgl.h"
#ifdef AGENDA_FONT_MEMORY_PROBE
#include "font_memory_probe.h"
#endif

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

// Optional artifact test: load a generated pack with the real firmware LVGL
// loader and verify actual glyph mappings, not just advertised ranges.txt.
int main(int argc, char** argv)
{
    assert(argc == 3 || (argc == 4 && std::strcmp(argv[3], "--with-base") == 0));
    lv_init();
    lv_fs_drv_t driver;
    lv_fs_drv_init(&driver);
    driver.letter = 'T';
    driver.user_data = argv[1];
    driver.open_cb = [](lv_fs_drv_t* drv, const char*, lv_fs_mode_t mode) -> void*
    { return mode == LV_FS_MODE_RD ? std::fopen(static_cast<const char*>(drv->user_data), "rb") : nullptr; };
    driver.close_cb = [](lv_fs_drv_t*, void* file)
    { return std::fclose(static_cast<FILE*>(file)) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR; };
    driver.read_cb = [](lv_fs_drv_t*, void* file, void* buffer, uint32_t bytes, uint32_t* read)
    {
        *read = static_cast<uint32_t>(std::fread(buffer, 1, bytes, static_cast<FILE*>(file)));
        return std::ferror(static_cast<FILE*>(file)) ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
    };
    driver.seek_cb = [](lv_fs_drv_t*, void* file, uint32_t offset, lv_fs_whence_t origin)
    {
        const int whence = origin == LV_FS_SEEK_SET ? SEEK_SET : origin == LV_FS_SEEK_CUR ? SEEK_CUR
                                                                                          : SEEK_END;
        return std::fseek(static_cast<FILE*>(file), static_cast<long>(offset), whence) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
    };
    driver.tell_cb = [](lv_fs_drv_t*, void* file, uint32_t* offset)
    {
        const long value = std::ftell(static_cast<FILE*>(file));
        if (value < 0) return LV_FS_RES_FS_ERR;
        *offset = static_cast<uint32_t>(value);
        return LV_FS_RES_OK;
    };
    lv_fs_drv_register(&driver);
#ifdef AGENDA_FONT_MEMORY_PROBE
    agenda_test_fonts::beginMemoryProbe();
#endif
    auto* font = lv_binfont_create("T:font.bin");
    assert(font);
    // Match font_utils: the built-in base is tried before the external pack.
    lv_font_t composed = lv_font_montserrat_16;
    composed.fallback = font;
    const lv_font_t* tested = argc == 4 ? &composed : font;
    std::ifstream input(argv[2], std::ios::binary);
    assert(input);
    const std::string charset{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    unsigned checked = 0;
    for (std::size_t index = 0; index < charset.size();)
    {
        const auto first = static_cast<uint8_t>(charset[index++]);
        uint32_t cp = first;
        unsigned following = 0;
        if (first >= 0xF0)
        {
            cp &= 0x07;
            following = 3;
        }
        else if (first >= 0xE0)
        {
            cp &= 0x0F;
            following = 2;
        }
        else if (first >= 0xC0)
        {
            cp &= 0x1F;
            following = 1;
        }
        else assert(first < 0x80);
        while (following--)
        {
            assert(index < charset.size());
            const auto byte = static_cast<uint8_t>(charset[index++]);
            assert((byte & 0xC0) == 0x80);
            cp = (cp << 6) | (byte & 0x3F);
        }
        if (cp == '\r' || cp == '\n' || cp == 0xFEFF) continue;
        lv_font_glyph_dsc_t glyph{};
        const bool found = lv_font_get_glyph_dsc(tested, &glyph, cp, 0);
        if (!found || glyph.is_placeholder) std::fprintf(stderr, "Missing binary glyph U+%04lX\n", static_cast<unsigned long>(cp));
        assert(found && !glyph.is_placeholder);
        ++checked;
    }
    assert(checked > 0);
    std::printf("Verified %u glyph mappings (%s): %s\n", checked, argc == 4 ? "base + binary pack" : "binary pack only", argv[1]);
#ifdef AGENDA_FONT_MEMORY_PROBE
    const auto memory = agenda_test_fonts::memoryProbe();
    assert(memory.bytes > 0 && memory.allocations > 0);
    std::printf("Font LVGL payload: retained=%lu allocations=%lu load_peak=%lu pointer_bits=%u\n",
                static_cast<unsigned long>(memory.bytes), static_cast<unsigned long>(memory.allocations),
                static_cast<unsigned long>(memory.peak_bytes), unsigned(sizeof(void*) * 8));
#endif
    lv_binfont_destroy(font);
#ifdef AGENDA_FONT_MEMORY_PROBE
    agenda_test_fonts::endMemoryProbe();
#endif
}
