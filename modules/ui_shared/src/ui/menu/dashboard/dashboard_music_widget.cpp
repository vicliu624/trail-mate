#include "music/service.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_key_focus.h"
#include "ui/menu/dashboard/dashboard_style.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui/menu/menu_layout.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace ui::assets
{
extern "C" const lv_image_dsc_t music;
}
namespace ui::menu::dashboard
{
namespace
{
struct MusicWidget : WidgetLifetime
{
    DashboardKeyFocus focus{};
    DashboardCardChrome chrome{};
    ::music::IService* source = nullptr;
    ::music::Snapshot snapshot{};
    ::music::Request request{};
    ::music::Artwork* art = nullptr;
    lv_image_dsc_t image{};
    lv_obj_t *cover = nullptr, *title = nullptr, *artist = nullptr, *album = nullptr, *play = nullptr, *mode = nullptr;
    uint32_t art_revision = UINT32_MAX;
    uint16_t edge = 0;
    ~MusicWidget()
    {
        lv_image_cache_drop(&image);
        if (art) art->drop();
        if (source) source->detach();
    }
};
void refresh_music(void* context)
{
    auto& self = *static_cast<MusicWidget*>(context);
    if (self.source) self.source->snapshot(self.snapshot);
    const auto& value = self.snapshot;
    const char* title = value.current.id ? value.current.title : ui::i18n::tr("Choose a song");
    if (std::strcmp(lv_label_get_text(self.title), title)) ui::i18n::set_content_label_text_raw(self.title, title);
    if (std::strcmp(lv_label_get_text(self.artist), value.current.artist)) ui::i18n::set_content_label_text_raw(self.artist, value.current.artist);
    if (std::strcmp(lv_label_get_text(self.album), value.current.album)) ui::i18n::set_content_label_text_raw(self.album, value.current.album);
    const char* play = value.playback == ::music::Playback::Playing || value.playback == ::music::Playback::Interrupted ? "Pause" : "Play";
    const char* mode = value.mode == ::music::PlayMode::RepeatOne ? "Repeat one" : value.mode == ::music::PlayMode::Shuffle ? "Shuffle"
                                                                                                                            : "Repeat all";
    if (std::strcmp(lv_label_get_text(self.play), ui::i18n::tr(play))) ui::i18n::set_label_text(self.play, play);
    if (std::strcmp(lv_label_get_text(self.mode), ui::i18n::tr(mode))) ui::i18n::set_label_text(self.mode, mode);
    const char* status = !self.source ? "Unavailable" : value.playback == ::music::Playback::Interrupted ? "Voice / radio"
                                                    : value.playback == ::music::Playback::Playing       ? "Playing"
                                                    : value.current.id                                   ? "Paused"
                                                                                                         : "Idle";
    if (std::strcmp(lv_label_get_text(self.chrome.status_label), status)) set_status_chip(self.chrome, status, color_soft_amber(), color_text());
    if (self.source && self.art_revision != value.artwork_revision)
    {
        lv_obj_add_flag(self.cover, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(self.cover, nullptr);
        lv_image_cache_drop(&self.image);
        if (self.art)
        {
            self.art->drop();
            self.art = nullptr;
        }
        self.art = self.source->acquire_artwork();
        self.art_revision = value.artwork_revision;
        if (self.art && self.art->track_id == value.current.id)
        {
            self.image = {};
            self.image.header.magic = LV_IMAGE_HEADER_MAGIC;
            self.image.header.cf = LV_COLOR_FORMAT_RGB565;
            self.image.header.w = self.art->width;
            self.image.header.h = self.art->height;
            self.image.header.stride = self.art->width * 2;
            self.image.data_size = self.art->width * self.art->height * 2;
            self.image.data = reinterpret_cast<const uint8_t*>(self.art->pixels);
            lv_image_set_src(self.cover, &self.image);
            lv_image_set_scale(self.cover, self.edge * 256 / std::max(self.art->width, self.art->height));
            lv_obj_center(self.cover);
            lv_obj_remove_flag(self.cover, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
void send(MusicWidget& self, unsigned action)
{
    if (action == 4 || (action == 1 && !self.snapshot.current.id))
    {
        ui::menu_layout::launchAppByStableId("music");
        return;
    }
    if (!self.source) return;
    self.request = {};
    self.request.action = action == 0 ? ::music::Action::Previous : action == 1 ? ::music::Action::TogglePause
                                                                : action == 2   ? ::music::Action::Next
                                                                                : ::music::Action::SetPlayMode;
    if (action == 3) self.request.value = (static_cast<unsigned>(self.snapshot.mode) + 1) % 3;
    self.source->send(self.request);
    refresh_music(&self);
}
void clicked(lv_event_t* event)
{
    send(*static_cast<MusicWidget*>(lv_event_get_user_data(event)), reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
}
bool music_key(void* context, uint32_t key)
{
    auto& self = *static_cast<MusicWidget*>(context);
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    if (key == 'a' || key == 'A' || key == LV_KEY_LEFT || key == 'd' || key == 'D' || key == LV_KEY_RIGHT)
    {
        self.focus.move(key == 'a' || key == 'A' || key == LV_KEY_LEFT ? -1 : 1);
        return true;
    }
#endif
    if (key == ' ')
    {
        send(self, 1);
        return true;
    }
    if (key == 'j' || key == 'J')
    {
        send(self, 0);
        return true;
    }
    if (key == 'k' || key == 'K')
    {
        send(self, 2);
        return true;
    }
    if (key == 'r' || key == 'R')
    {
        send(self, 3);
        return true;
    }
    if (key == LV_KEY_ENTER)
    {
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        send(self, self.focus.selected);
#else
        send(self, 4);
#endif
        return true;
    }
    return false;
}
lv_obj_t* text(lv_obj_t* parent, const char* value, lv_coord_t x, lv_coord_t y, lv_coord_t w, bool muted = false)
{
    auto* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    style_body_label(label, muted ? &lv_font_montserrat_12 : &lv_font_montserrat_14, muted ? color_text_dim() : color_text());
    ui::i18n::set_label_text(label, value);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}
} // namespace
WidgetHandle create_music_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ::music::IService* source)
{
    auto* self = create_widget_state<MusicWidget>();
    if (!self) return {};
    self->source = source;
    self->chrome = create_card_chrome(parent, ui::i18n::tr("Music"), width, height);
    auto* body = self->chrome.body;
    const auto w = lv_obj_get_content_width(body), h = lv_obj_get_content_height(body);
    const bool tall = h > 180;
    self->edge = tall ? std::min<lv_coord_t>(192, h - 140) : std::min<lv_coord_t>(72, h - 36);
    const lv_coord_t content_w = std::min<lv_coord_t>(w, 560), x = (w - content_w) / 2, top = tall ? (h - self->edge - 120) / 2 : 0;
    auto* frame = lv_obj_create(body);
    lv_obj_set_pos(frame, x, top);
    lv_obj_set_size(frame, self->edge, self->edge);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_radius(frame, 8, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    auto* icon = lv_image_create(frame);
    lv_image_set_src(icon, &ui::assets::music);
    lv_obj_center(icon);
    self->cover = lv_image_create(frame);
    lv_obj_center(self->cover);
    lv_obj_add_flag(self->cover, LV_OBJ_FLAG_HIDDEN);
    const lv_coord_t tx = x + self->edge + 16, tw = content_w - self->edge - 16;
    self->title = text(body, "Choose a song", tx, top + (tall ? 30 : 0), tw);
    self->artist = text(body, "", tx, top + (tall ? 60 : 24), tw, true);
    self->album = text(body, "", tx, top + (tall ? 84 : 44), tw, true);
    const char* names[] = {LV_SYMBOL_PREV, "Play", LV_SYMBOL_NEXT, "Repeat all", "Open"};
    const lv_coord_t widths[] = {36, 64, 36, 90, 48};
    const lv_coord_t total = 36 + 64 + 36 + 90 + 48 + 4 * 6;
    // Small TDeck cards expose Open in the footer rather than squeezing five controls.
    const unsigned controls = content_w >= total ? 5 : 4;
    const lv_coord_t used = controls == 5 ? total : 36 + 64 + 36 + 90 + 3 * 6;
    const lv_coord_t by = tall ? top + self->edge + 42 : h - 27;
    lv_coord_t bx = x + (content_w - used) / 2;
    for (unsigned i = 0; i < controls; i++)
    {
        auto* button = lv_button_create(body);
        self->focus.add(button);
        lv_group_remove_obj(button);
        lv_obj_set_pos(button, bx, by);
        lv_obj_set_size(button, widths[i], 24);
        bx += widths[i] + 6;
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, i == 1 ? color_amber() : color_soft_amber(), 0);
        auto* label = text(button, names[i], 0, 0, LV_SIZE_CONTENT);
        lv_obj_center(label);
        if (i == 1) self->play = label;
        if (i == 3) self->mode = label;
        lv_obj_set_user_data(button, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, self);
    }
    auto* footer = text(self->chrome.footer, "Tap to open Music", 0, 0, LV_PCT(100), true);
    lv_obj_add_flag(footer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(footer, reinterpret_cast<void*>(4));
    lv_obj_add_event_cb(footer, clicked, LV_EVENT_CLICKED, self);
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    lv_obj_set_style_bg_opa(self->chrome.footer, LV_OPA_TRANSP, 0);
    ui::i18n::set_label_text(footer, "A/D select  Enter run  W/S wheel widgets  V menu");
    self->focus.selected = 1;
    self->focus.show();
#endif
    // Observe existing playback only. A widget never scans or opens the song library.
    if (source)
    {
        source->attach(0);
        source->request_lyrics(false);
        source->request_artwork(self->edge);
    }
    // Only the visible widget owns this timer. Poll cached state promptly;
    // unchanged labels/styles do not allocate or invalidate LVGL objects.
    auto handle = own_widget(self->chrome.card, self, refresh_music, 100);
    handle.key = music_key;
    refresh_music(self);
    return handle;
}
} // namespace ui::menu::dashboard
