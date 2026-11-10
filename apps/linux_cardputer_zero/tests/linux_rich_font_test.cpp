#include "ui/assets/fonts/bundled_font_root.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"

namespace
{
namespace fs = std::filesystem;

std::size_t descriptor_count()
{
    return static_cast<std::size_t>(std::distance(fs::directory_iterator("/proc/self/fd"), fs::directory_iterator{}));
}

void check_font_descriptors(bool closed = false)
{
    for (const auto& entry : fs::directory_iterator("/proc/self/fd"))
    {
        std::error_code error;
        const auto target = fs::read_symlink(entry.path(), error).string();
        if (!error && (target.find("/font.otf") != std::string::npos || target.find("/font.ttf") != std::string::npos))
        {
            assert(!closed);
            const int descriptor = std::stoi(entry.path().filename().string());
            assert((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0);
        }
    }
}

void check_glyph(const lv_font_t* font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph{};
    if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder)
    {
        std::cerr << "Missing actual glyph U+" << std::hex << codepoint << std::dec << '\n';
        std::abort();
    }
}

void check_translation_glyphs(const fs::path& root)
{
    std::ifstream strings(root / "locales/zh-Hans/strings.tsv");
    assert(strings);
    std::string line;
    std::size_t checked = 0;
    while (std::getline(strings, line))
    {
        const auto tab = line.find('\t');
        if (line.empty() || line.front() == '#' || tab == std::string::npos) continue;
        const auto text = line.substr(tab + 1);
        const auto* cursor = reinterpret_cast<const unsigned char*>(text.c_str());
        while (*cursor)
        {
            uint32_t codepoint = *cursor++;
            unsigned remaining = 0;
            if ((codepoint & 0xE0U) == 0xC0U)
            {
                remaining = 1;
                codepoint &= 0x1FU;
            }
            else if ((codepoint & 0xF0U) == 0xE0U)
            {
                remaining = 2;
                codepoint &= 0x0FU;
            }
            else if ((codepoint & 0xF8U) == 0xF0U)
            {
                remaining = 3;
                codepoint &= 0x07U;
            }
            while (remaining--)
            {
                assert((*cursor & 0xC0U) == 0x80U);
                codepoint = (codepoint << 6U) | (*cursor++ & 0x3FU);
            }
            if (codepoint < 0x80U || codepoint == 0xFE0FU || codepoint == 0x200DU) continue;
            check_glyph(ui::i18n::active_ui_font_fallback(), codepoint);
            ++checked;
        }
    }
    assert(checked > 1000);
    std::cout << "Validated " << checked << " translated Unicode glyph occurrences\n";
}

struct Capture
{
    std::vector<uint16_t> pixels = std::vector<uint16_t>(320 * 170);
    std::vector<uint8_t> drawing = std::vector<uint8_t>(320 * 30 * 2);
};

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels)
{
    auto* capture = static_cast<Capture*>(lv_display_get_user_data(display));
    const auto width = static_cast<std::size_t>(area->x2 - area->x1 + 1);
    for (int y = area->y1; y <= area->y2; ++y)
        std::memcpy(capture->pixels.data() + y * 320 + area->x1,
                    pixels + static_cast<std::size_t>(y - area->y1) * width * 2, width * 2);
    lv_display_flush_ready(display);
}

void render(const fs::path& output)
{
    Capture capture;
    auto* display = lv_display_create(320, 170);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display, &capture);
    lv_display_set_buffers(display, capture.drawing.data(), nullptr, capture.drawing.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    const char* rows[] = {"TrailMate / Linux fonts", "设置 发送 路线 地图 联系人", "扩展汉字：龘 𠮷", "日本語・한국어", "😀 🥾 🗺️ 🌍 📍 👍", "English 123 / café / Łódź"};
    for (std::size_t index = 0; index < std::size(rows); ++index)
    {
        auto* label = lv_label_create(screen);
        lv_obj_set_pos(label, 6, 4 + static_cast<int>(index) * 26);
        lv_obj_set_width(label, 308);
        lv_obj_set_style_text_font(label, ui::fonts::content_font(rows[index], &lv_font_montserrat_14), 0);
        lv_label_set_text(label, rows[index]);
    }
    lv_refr_now(display);
    std::vector<uint8_t> rgba(320 * 170 * 4, 255);
    std::size_t colored = 0;
    for (std::size_t index = 0; index < capture.pixels.size(); ++index)
    {
        const auto pixel = capture.pixels[index];
        const int red = ((pixel >> 11) & 31) * 255 / 31;
        const int green = ((pixel >> 5) & 63) * 255 / 63;
        const int blue = (pixel & 31) * 255 / 31;
        rgba[index * 4] = static_cast<uint8_t>(red);
        rgba[index * 4 + 1] = static_cast<uint8_t>(green);
        rgba[index * 4 + 2] = static_cast<uint8_t>(blue);
        if (std::max({red, green, blue}) - std::min({red, green, blue}) > 40) ++colored;
    }
    assert(colored > 50); // White background and black text cannot satisfy this.
    unsigned char* encoded = nullptr;
    std::size_t size = 0;
    assert(lodepng_encode32(&encoded, &size, rgba.data(), 320, 170) == 0);
    std::ofstream stream(output, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(encoded), static_cast<std::streamsize>(size));
    free(encoded);
    assert(stream.good());
    lv_display_delete(display);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4);
    const fs::path bundled = argv[1];
    const std::string mode = argc == 4 ? argv[3] : "";
    const bool override = mode == "--user-override";
    const bool corrupt = mode == "--corrupt-font";
    assert(mode.empty() || override || corrupt);
    char temporary[] = "/tmp/trailmate-rich-font-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path private_root = temporary;
    const auto user = private_root / "user";
    fs::create_directories(user);
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_SD_ROOT", (private_root / "data").c_str(), 1);
    if (override)
    {
        fs::create_directories(user / "fonts/linux-color-emoji");
        std::ofstream(user / "fonts/linux-color-emoji/manifest.ini")
            << "kind=font\nid=linux-color-emoji\nusage=both\nsource=builtin\nbuiltin_id=builtin-latin-ui\n";
    }
    if (corrupt)
    {
        const auto broken = user / "fonts/broken";
        fs::create_directories(broken);
        std::ofstream(broken / "manifest.ini")
            << "kind=font\nid=broken\nsource=freetype\nusage=both\nfile=font.ttf\npixel_size=16\nestimated_ram_bytes=1024\nranges=ranges.txt\n";
        std::ofstream(broken / "ranges.txt") << "0x20BB7\n";
        std::ofstream(broken / "font.ttf") << "not-a-font";
    }
    lv_init();
    assert(ui::i18n::configure_external_pack_root(user.string()));
    assert(ui::i18n::configure_bundled_font_root(bundled.string()));
    assert(ui::i18n::locale_count() >= 2);
    const char* sample = "你好 龘 𠮷 日本語 한국어 😀 🥾 🗺️";
    const bool covered = ui::i18n::prepare_content_font_for_text(sample);
    if (!override) assert(covered);
    lv_font_glyph_dsc_t emoji{};
    const bool found = lv_font_get_glyph_dsc(ui::i18n::active_content_font_fallback(), &emoji, 0x1F600, 0);
    assert(override ? (!found || emoji.format != LV_FONT_GLYPH_FORMAT_IMAGE)
                    : (found && emoji.format == LV_FONT_GLYPH_FORMAT_IMAGE));
    if (override)
    {
        fs::remove(user / "fonts/linux-color-emoji/manifest.ini");
        fs::remove(user / "fonts/linux-color-emoji");
        fs::remove(user / "fonts");
        ui::i18n::reload_language();
        assert(ui::i18n::prepare_content_font_for_text(sample));
    }
    const auto baseline = descriptor_count();
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        assert(ui::i18n::set_locale("zh-Hans", false));
        assert(std::strcmp(ui::i18n::tr("Settings"), "设置") == 0);
        assert(std::strcmp(ui::i18n::tr("Send"), "发送") == 0);
        check_glyph(ui::i18n::active_ui_font_fallback(), 0x20BB7);
        assert(ui::i18n::set_locale("en", false));
        ui::i18n::reload_language();
        assert(ui::i18n::prepare_content_font_for_text(sample));
        assert(descriptor_count() == baseline);
        check_font_descriptors();
    }
    assert(ui::i18n::set_locale("zh-Hans", false));
    check_translation_glyphs(bundled);
    const auto* mixed = ui::fonts::content_font(sample, &lv_font_montserrat_14);
    lv_font_glyph_dsc_t latin{};
    assert(lv_font_get_glyph_dsc(mixed, &latin, 'A', 0));
    assert(latin.resolved_font->dsc == lv_font_montserrat_14.dsc);
    assert(lv_font_get_glyph_dsc(mixed, &emoji, 0x1F600, 0));
    assert(emoji.format == LV_FONT_GLYPH_FORMAT_IMAGE && !emoji.is_placeholder);
    assert(emoji.box_h <= 18 && emoji.box_w <= 20);
    render(argv[2]);
    if (corrupt) fs::remove_all(user / "fonts");
    assert(fs::is_empty(user));
    ui::i18n::shutdown_resource_packs();
    check_font_descriptors(true);
    lv_deinit();
    // The application owns fresh fonts and bindings after a second LVGL start.
    lv_init();
    assert(ui::i18n::configure_external_pack_root(user.string()));
    assert(ui::i18n::configure_bundled_font_root(bundled.string()));
    assert(ui::i18n::prepare_content_font_for_text(sample));
    ui::i18n::shutdown_resource_packs();
    check_font_descriptors(true);
    lv_deinit();
    fs::remove_all(private_root);
    std::cout << "Full CJK, color pixels, locale switching, user precedence and descriptor stability passed\n";
}
