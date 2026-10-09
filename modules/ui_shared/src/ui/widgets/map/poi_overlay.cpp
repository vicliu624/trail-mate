#include "ui/widgets/map/poi_overlay.h"
#include "ui/assets/fonts/font_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ui::widgets::map
{
namespace
{
const char* symbol(const char* category)
{
    if (std::strcmp(category, "water") == 0) return "W";
    if (std::strcmp(category, "camp") == 0) return "C";
    if (std::strcmp(category, "shelter") == 0) return "H";
    if (std::strcmp(category, "peak") == 0) return "^";
    if (std::strcmp(category, "viewpoint") == 0) return "V";
    if (std::strcmp(category, "parking") == 0) return "P";
    if (std::strcmp(category, "toilet") == 0) return "T";
    if (std::strcmp(category, "emergency") == 0) return "+";
    if (std::strcmp(category, "trailhead") == 0) return "h";
    return "i";
}
lv_color_t color(const char* category)
{
    if (std::strcmp(category, "water") == 0) return lv_color_hex(0x246BB2);
    if (std::strcmp(category, "emergency") == 0) return lv_color_hex(0xBC3B35);
    if (std::strcmp(category, "camp") == 0 || std::strcmp(category, "shelter") == 0) return lv_color_hex(0x347544);
    return lv_color_hex(0x705337);
}
void draw_background(lv_layer_t* layer, const lv_area_t& area, lv_color_t fill, bool marker)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = fill;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = marker ? LV_RADIUS_CIRCLE : 2;
    dsc.border_width = marker ? 1 : 0;
    dsc.border_color = lv_color_hex(0xFFFFFF);
    dsc.border_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &dsc, &area);
}
void draw_text(lv_layer_t* layer, const lv_area_t& area, const char* text, const lv_font_t* font, bool marker)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.text = text;
    dsc.align = marker ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
    dsc.color = lv_color_hex(marker ? 0xFFFFFF : 0x3A2A1A);
    dsc.flag = LV_TEXT_FLAG_EXPAND;
    // LVGL owns a short transient copy if a viewport refresh releases the PSRAM snapshot.
    dsc.text_local = !marker;
    dsc.text_static = marker;
    lv_draw_label(layer, &dsc, &area);
}

void draw_annotation_marker(lv_layer_t* layer, const lv_area_t& area, const ui::map::MapPoiItem& item)
{
    draw_background(layer, area, color(item.category.c_str()), true);
    char count[4];
    const char* text = symbol(item.category.c_str());
    if (item.cluster_count > 1)
    {
        std::snprintf(count, sizeof(count), item.cluster_count > 9 ? "9+" : "%u", static_cast<unsigned>(item.cluster_count));
        text = count;
    }
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = &lv_font_montserrat_10;
    dsc.text = text;
    dsc.text_local = 1;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.color = lv_color_hex(0xFFFFFF);
    lv_area_t centered = area;
    centered.y1 += (12 - dsc.font->line_height) / 2;
    lv_draw_label(layer, &dsc, &centered);
}
} // namespace

PoiOverlay::~PoiOverlay()
{
    if (layer_) lv_obj_del(layer_);
}
void PoiOverlay::on_delete(lv_event_t* event)
{
    auto* self = static_cast<PoiOverlay*>(lv_event_get_user_data(event));
    self->layer_ = nullptr;
    self->snapshot_ = nullptr;
}
void PoiOverlay::create(lv_obj_t* parent)
{
    if (layer_) lv_obj_del(layer_);
    layer_ = lv_obj_create(parent);
    lv_obj_remove_style_all(layer_);
    lv_obj_set_size(layer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_clear_flag(layer_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_event_cb(layer_, on_delete, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(layer_, on_draw, LV_EVENT_DRAW_MAIN, this);
    clear();
}
void PoiOverlay::update(const ui::map::MapPoiSnapshot& snapshot)
{
    if (!layer_) return;
    clear();
    lv_obj_set_pos(layer_, 0, 0);
    if (!snapshot.enabled || !snapshot.layout_ready || !snapshot.items || snapshot.item_count == 0) return;
    snapshot_ = &snapshot;
    lv_obj_clear_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(layer_);
}
void PoiOverlay::on_draw(lv_event_t* event)
{
    const auto* self = static_cast<const PoiOverlay*>(lv_event_get_user_data(event));
    if (!self->snapshot_ || !self->snapshot_->items) return;
    const auto& snapshot = *self->snapshot_;
    const auto count = std::min(std::min(snapshot.item_count, snapshot.capacity), ui::map::MapPoiSnapshot::kMaxItems);
    auto* layer = lv_event_get_layer(event);
    lv_area_t origin{};
    lv_obj_get_coords(self->layer_, &origin);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& item = snapshot.items[i];
        if (snapshot.layout_ready)
        {
            if (item.marker)
            {
                lv_area_t marker_area{origin.x1 + item.x - 6, origin.y1 + item.y - 6,
                                      origin.x1 + item.x + 5, origin.y1 + item.y + 5};
                draw_annotation_marker(layer, marker_area, item);
            }
            if (snapshot.labels && !item.label.empty() && self->font_)
            {
                lv_area_t text_area{origin.x1 + item.text_x, origin.y1 + item.text_y,
                                    origin.x1 + item.text_x + item.text_width - 1,
                                    origin.y1 + item.text_y + item.text_height - 1};
                draw_text(layer, text_area, item.label.c_str(), self->font_, false);
            }
            continue;
        }
    }
}
void PoiOverlay::clear()
{
    snapshot_ = nullptr;
    if (layer_) lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);
}
void PoiOverlay::translate(int dx, int dy)
{
    if (layer_) lv_obj_set_pos(layer_, lv_obj_get_x(layer_) + dx, lv_obj_get_y(layer_) + dy);
}

void PoiOverlay::prepare_text(const char* text)
{
    font_ = ui::fonts::content_font(text, &lv_font_montserrat_14);
}

uint64_t PoiOverlay::font_signature() const
{
    uint64_t hash = UINT64_C(14695981039346656037);
    // The shared font manager updates fallback links when deferred resources
    // become available. Inspect metadata only; never load files from this check.
    const lv_font_t* font = font_;
    for (unsigned depth = 0; font && depth < 16; ++depth, font = font->fallback)
    {
        hash ^= reinterpret_cast<std::uintptr_t>(font);
        hash *= UINT64_C(1099511628211);
        hash ^= reinterpret_cast<std::uintptr_t>(font->dsc);
        hash *= UINT64_C(1099511628211);
        hash ^= font->line_height;
    }
    return hash;
}

bool PoiOverlay::measure_text(void* view, const char* text, std::size_t bytes, bool ellipsis, int16_t& width, int16_t& height)
{
    auto* self = static_cast<PoiOverlay*>(view);
    if (!self || !self->font_ || !text || bytes > 76) return false;
    char prefix[80];
    std::memcpy(prefix, text, bytes);
    if (ellipsis)
    {
        std::memcpy(prefix + bytes, "\xE2\x80\xA6", 3);
        bytes += 3;
    }
    prefix[bytes] = '\0';
    // A missing glyph must not be laid out using LVGL's placeholder metrics.
    // Font loading is requested by prepare_text outside the draw callback;
    // the shared font-chain signature schedules another layout once it loads.
    for (std::size_t i = 0; i < bytes;)
    {
        const auto lead = static_cast<uint8_t>(prefix[i++]);
        uint32_t codepoint = lead;
        unsigned remaining = 0;
        if (lead >= 0xF0 && lead <= 0xF4)
        {
            codepoint = lead & 7U;
            remaining = 3;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            codepoint = lead & 15U;
            remaining = 2;
        }
        else if (lead >= 0xC2 && lead <= 0xDF)
        {
            codepoint = lead & 31U;
            remaining = 1;
        }
        else if (lead >= 0x80) return false;
        if (i + remaining > bytes) return false;
        while (remaining--)
        {
            const auto next = static_cast<uint8_t>(prefix[i++]);
            if ((next & 0xC0U) != 0x80U) return false;
            codepoint = (codepoint << 6) | (next & 63U);
        }
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(self->font_, &glyph, codepoint, 0) || glyph.is_placeholder)
        {
            static uint32_t last_missing_ms = 0;
            const auto now = lv_tick_get();
            if (!last_missing_ms || now - last_missing_ms >= 5000U)
            {
                last_missing_ms = now;
                std::printf("[MapViewport][POI][font] missing_glyph=U+%04lX text=%.64s\n",
                            static_cast<unsigned long>(codepoint), prefix);
            }
            return false;
        }
    }
    lv_point_t size{};
    lv_text_get_size(&size, prefix, self->font_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    width = static_cast<int16_t>(size.x);
    height = static_cast<int16_t>(size.y);
    return true;
}
} // namespace ui::widgets::map
