#include "music_state.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/input/cardputer_softkeys.h"
#include "ui/localization.h"
#include "ui/ui_common.h"
#include "ui/ui_theme.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#if !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace ui::assets
{
extern "C" const lv_image_dsc_t music;
}
namespace ui::music_page
{
namespace
{
using ui::fonts::FontScope;

lv_obj_t* label(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                const lv_font_t* font = &lv_font_montserrat_14, bool muted = false, FontScope scope = FontScope::Ui)
{
    auto* object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, muted ? ui::theme::text_muted() : ui::theme::text(), 0);
    if (scope == FontScope::Content) ui::i18n::set_content_label_text_raw(object, text);
    else ui::i18n::set_label_text(object, text);
    return object;
}
lv_obj_t* content_label(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                        const lv_font_t* font = &lv_font_montserrat_14, bool muted = false)
{
    return label(parent, text, x, y, width, font, muted, FontScope::Content);
}
lv_obj_t* card(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    auto* object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 12, 0);
    lv_obj_set_style_bg_color(object, ui::theme::surface(), 0);
    lv_obj_set_style_border_color(object, ui::theme::separator(), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
void changed(lv_obj_t* object, const char* text, FontScope scope = FontScope::Ui)
{
    if (!object || std::strcmp(lv_label_get_text(object), text) == 0) return;
    if (scope == FontScope::Content) ui::i18n::set_content_label_text_raw(object, text);
    else ui::i18n::set_label_text_raw(object, text);
}
void playlist_name_changed(lv_event_t* event)
{
    auto* editor = lv_event_get_target_obj(event);
    ui::fonts::apply_content_font(editor, lv_textarea_get_text(editor), &lv_font_montserrat_14);
}
void spacer(lv_obj_t* parent)
{
    auto* object = lv_obj_create(parent);
    lv_obj_set_size(object, 0, 1);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(object, 1);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}
const char* browse_name(::music::Browse kind)
{
    switch (kind)
    {
    case ::music::Browse::Artists:
        return "Artists";
    case ::music::Browse::Albums:
        return "Albums";
    case ::music::Browse::Folders:
        return "Folders";
    case ::music::Browse::Playlists:
        return "Playlists";
    default:
        return "Songs";
    }
}
void lyrics(lv_obj_t* parent, lv_coord_t w, lv_coord_t h)
{
    auto& self = *state();
    label(parent, "Lyrics", 18, 14, w - 36, &lv_font_montserrat_14, true);
    const lv_coord_t center = h / 2;
    self.lyric[0] = content_label(parent, "", 18, center - 44, w - 36, &lv_font_montserrat_14, true);
    self.lyric[1] = content_label(parent, "", 18, center - 14, w - 36, &lv_font_montserrat_16);
    self.lyric[2] = content_label(parent, "", 18, center + 44, w - 36, &lv_font_montserrat_14, true);
    for (auto* line : self.lyric)
    {
        lv_obj_set_style_text_align(line, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_height(line, line == self.lyric[1] ? 44 : 28);
        lv_label_set_long_mode(line, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_style_text_color(self.lyric[1], lv_color_hex(0xA56818), 0);
}
void cover(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t edge)
{
    auto& self = *state();
    self.artwork_edge = static_cast<uint16_t>(edge);
    auto* frame = card(parent, x, y, edge, edge);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0xF6E7C5), 0);
    lv_obj_set_style_radius(frame, 8, 0);
    auto* fallback = lv_image_create(frame);
    lv_image_set_src(fallback, &ui::assets::music);
    lv_obj_center(fallback);
    self.cover = lv_image_create(frame);
    lv_obj_center(self.cover);
    lv_obj_add_flag(self.cover, LV_OBJ_FLAG_HIDDEN);
}
void now_playing(lv_coord_t width, lv_coord_t height)
{
    auto& self = *state();
    const bool short_screen = !self.wide && height < 150;
    const lv_coord_t margin = self.wide ? std::max<lv_coord_t>(24, (width - 1040) / 2) : 8;
    const lv_coord_t gap = 20;
    const lv_coord_t w = self.wide ? (width - 2 * margin - gap) / 2 : width - 2 * margin;
    const lv_coord_t h = self.wide ? std::min<lv_coord_t>(400, height - 40) : height - 8;
    const lv_coord_t y = (height - h) / 2;
    auto* panel = card(self.body, margin, y, w, h);
    lv_coord_t title_y = short_screen ? 4 : 10, text_x = 14;
    const lv_coord_t controls_y = h - (short_screen ? 28 : 44);
    const lv_coord_t progress_y = controls_y - (short_screen ? 26 : 34);
    // Include the slider knob's painted extent, not just its five-pixel track.
    const lv_coord_t edge = self.wide      ? std::min<lv_coord_t>(200, h - 210)
                            : short_screen ? std::max<lv_coord_t>(16, std::min<lv_coord_t>(48, progress_y - 8))
                                           : std::min<lv_coord_t>(96, h - 96);
    cover(panel, self.wide ? (w - edge) / 2 : 12, self.wide ? 16 : title_y, edge);
    if (self.wide)
    {
        title_y = edge + 32;
    }
    else text_x = edge + 24;
    const lv_coord_t text_w = w - text_x - 14;
    self.title = content_label(panel, self.snapshot.current.title[0] ? self.snapshot.current.title : ui::i18n::tr("Choose a song"),
                               text_x, title_y, text_w, short_screen ? &lv_font_montserrat_14 : &lv_font_montserrat_16);
    lv_label_set_long_mode(self.title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(self.title, short_screen ? 18 : 20);
    self.artist = content_label(panel, self.snapshot.current.artist, text_x, title_y + (short_screen ? 18 : 26), text_w,
                                short_screen ? &lv_font_montserrat_12 : &lv_font_montserrat_14, true);
    lv_label_set_long_mode(self.artist, LV_LABEL_LONG_DOT);
    self.album = content_label(panel, self.snapshot.current.album, text_x, title_y + (short_screen ? 34 : 46), text_w - 44, &lv_font_montserrat_12, true);
    lv_label_set_long_mode(self.album, LV_LABEL_LONG_DOT);
    char volume[16];
    std::snprintf(volume, sizeof(volume), "%u%%", self.snapshot.volume);
    self.volume = label(panel, volume, w - 54, title_y + (short_screen ? 34 : 46), 40, &lv_font_montserrat_12, true);
    self.playback_status = label(panel, "", text_x, title_y + 64, text_w, &lv_font_montserrat_12, true);
    lv_label_set_long_mode(self.playback_status, LV_LABEL_LONG_DOT);
    lv_obj_set_height(self.playback_status, lv_font_get_line_height(&lv_font_montserrat_12));
    self.progress = lv_slider_create(panel);
    lv_obj_set_pos(self.progress, 14, progress_y);
    lv_obj_set_size(self.progress, w - 28, 5);
    lv_slider_set_range(self.progress, 0, 1000);
    lv_obj_set_style_bg_color(self.progress, ui::theme::separator(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(self.progress, ui::theme::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(self.progress, ui::theme::accent(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(self.progress, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(self.progress, action_event, LV_EVENT_RELEASED, reinterpret_cast<void*>(99));
    lv_obj_add_event_cb(self.progress, key_event, LV_EVENT_KEY, nullptr);
    const lv_coord_t time_y = progress_y + (short_screen ? 8 : 12);
    self.elapsed = label(panel, "0:00", 14, time_y, 70, &lv_font_montserrat_12, true);
    self.duration = label(panel, "--:--", w - 84, time_y, 70, &lv_font_montserrat_12, true);
    lv_obj_set_style_text_align(self.duration, LV_TEXT_ALIGN_RIGHT, 0);
    if (!self.wide)
    {
        // Status shares the time row on compact screens, leaving the cover,
        // metadata and the slider's painted knob area independent.
        lv_obj_set_pos(self.playback_status, 92, time_y);
        lv_obj_set_width(self.playback_status, w - 184);
        lv_obj_set_style_text_align(self.playback_status, LV_TEXT_ALIGN_CENTER, 0);
    }
    auto* mode = button(panel, "Repeat all", PlayMode, 92);
    self.mode = lv_obj_get_child(mode, 0);
    auto* previous = button(panel, LV_SYMBOL_PREV, Previous, 36);
    auto* play = button(panel, "Play", Toggle, 64);
    auto* next = button(panel, LV_SYMBOL_NEXT, Next, 36);
    self.play_label = lv_obj_get_child(play, 0);
    lv_obj_set_pos(mode, (w - 256) / 2, controls_y);
    lv_obj_set_pos(previous, (w - 256) / 2 + 104, controls_y);
    lv_obj_set_pos(play, (w - 256) / 2 + 148, controls_y);
    lv_obj_set_pos(next, (w - 256) / 2 + 220, controls_y);
    for (auto* control : {mode, previous, play, next}) lv_obj_set_height(control, short_screen ? 24 : 30);
    lv_obj_set_style_bg_color(play, ui::theme::accent(), 0);
    lv_group_focus_obj(play);
    if (self.wide)
    {
        auto* pane = card(self.body, margin + w + gap, y, w, h);
        lyrics(pane, w, h);
    }
}
void library(lv_coord_t width, lv_coord_t height)
{
    auto& self = *state();
    const lv_coord_t x = self.wide ? 226 : 8;
    const lv_coord_t w = width - x - (self.wide ? 24 : 8);
    self.notice = label(self.body, "", x + 6, 5, w - 12, &lv_font_montserrat_12, true);
    if (self.wide)
    {
        auto* sidebar = card(self.body, 24, 20, 176, height - 40);
        label(sidebar, "YOUR LIBRARY", 14, 16, 148, &lv_font_montserrat_12, true);
        const char* names[] = {"Songs", "Artists", "Albums", "Folders", "Playlists"};
        for (int index = 0; index < 5; index++)
        {
            auto* item = button(sidebar, names[index], Songs + index, 148);
            lv_obj_set_pos(item, 14, 46 + index * 44);
            lv_obj_set_height(item, 34);
        }
    }
    if (!self.service || !self.snapshot.storage_ready || self.snapshot.row_count == 0)
    {
        const char* message = !self.service ? "Music is unavailable on this device." : !self.snapshot.storage_ready                     ? "Insert an SD card"
                                                                                   : self.snapshot.busy                                 ? "Reading music library..."
                                                                                   : self.snapshot.browse == ::music::Browse::Playlists ? "No playlists yet"
                                                                                                                                        : "No songs found";
        auto* empty = label(self.body, message, x + 12, height / 2 - 16, w - 24, &lv_font_montserrat_16);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        if (self.snapshot.browse == ::music::Browse::Playlists)
        {
            auto* create = button(self.body, "New playlist", NewPlaylist, 120);
            lv_obj_set_pos(create, x + (w - 120) / 2, height / 2 + 24);
        }
        return;
    }
    const lv_coord_t row_h = (height - 32) / self.visible_rows - 4;
    for (unsigned index = 0; index < self.snapshot.row_count; index++)
    {
        const auto& data = self.snapshot.rows[index];
        auto* row = lv_button_create(self.body);
        ui::components::bottom_bar::style_button(row);
        lv_obj_set_pos(row, x, 26 + index * (row_h + 4));
        lv_obj_set_size(row, w, row_h);
        lv_obj_set_style_bg_color(row, ui::theme::surface(), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xF6E7C5), LV_STATE_FOCUSED);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_event_cb(row, row_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(row, row_event, LV_EVENT_FOCUSED, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(row, key_event, LV_EVENT_KEY, nullptr);
        auto* title = content_label(row, data.title, 10, 3, w - 48, &lv_font_montserrat_14);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_height(title, 17);
        auto* subtitle = content_label(row, data.subtitle, 10, row_h - 16, w - 48, &lv_font_montserrat_12, true);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
        lv_obj_set_height(subtitle, 14);
        const bool tracks = self.snapshot.browse == ::music::Browse::Songs || self.snapshot.browse >= ::music::Browse::PlaylistTracks;
        if (tracks && self.view != View::ChoosePlaylist)
        {
            auto* add = button(row, "+", AddToPlaylist, 26);
            lv_obj_align(add, LV_ALIGN_RIGHT_MID, -6, 0);
            lv_obj_set_user_data(add, reinterpret_cast<void*>(index + 1));
        }
        if ((self.selected == data.id) || (index == 0 && self.selected == 0))
        {
            self.selected = data.id;
            lv_group_focus_obj(row);
        }
    }
}
void menu_items()
{
    auto& self = *state();
    self.menu_content = lv_obj_create(self.root);
    lv_obj_set_size(self.menu_content, 180, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(self.menu_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(self.menu_content, 4, 0);
    lv_obj_set_style_pad_row(self.menu_content, 2, 0);
    const auto add = [&](const char* text, int action)
    {auto* item=button(self.menu_content,text,action,LV_PCT(100));lv_obj_set_height(item,24); };
    if (self.view == View::Playing)
    {
        add("Track info", Details);
        add("Add to playlist", AddToPlaylist);
        add("Stop playback", Stop);
    }
    else
    {
        add("Songs", Songs);
        add("Artists", Artists);
        add("Albums", Albums);
        if (self.wide) add("Folders", Folders);
        add("Playlists", Playlists);
        add("New playlist", NewPlaylist);
        if (self.snapshot.browse == ::music::Browse::PlaylistTracks) add("Delete playlist", DeletePlaylist);
        add("Scan for music", Rescan);
    }
    lv_obj_add_flag(self.menu_content, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_max_height(self.menu_content, lv_display_get_vertical_resolution(nullptr) - 56, 0);
}
} // namespace
lv_obj_t* button(lv_obj_t* parent, const char* text, intptr_t action, lv_coord_t width)
{
    auto* object = lv_button_create(parent);
    ui::components::bottom_bar::style_button(object);
    lv_obj_set_size(object, width, 24);
    lv_obj_set_style_pad_left(object, 8, 0);
    lv_obj_set_style_pad_right(object, 8, 0);
    lv_obj_set_style_pad_top(object, 2, 0);
    lv_obj_set_style_pad_bottom(object, 2, 0);
    auto* caption = label(object, text, 0, 0, LV_SIZE_CONTENT, &lv_font_montserrat_14);
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, action_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(action));
    lv_obj_add_event_cb(object, key_event, LV_EVENT_KEY, nullptr);
    ::ui::input::cardputer::softkeys::bind_button(parent, object, action == OpenMenu ? 4 : -1);
    return object;
}
void draw()
{
    auto& self = *state();
    self.menu.reset();
    if (self.menu_content)
    {
        lv_obj_delete(self.menu_content);
        self.menu_content = nullptr;
    }
    lv_obj_clean(self.body);
    lv_obj_clean(self.footer);
    lv_image_cache_drop(&self.cover_image);
    if (self.artwork)
    {
        self.artwork->drop();
        self.artwork = nullptr;
    }
    self.cover = self.volume = self.playback_status = self.mode = nullptr;
    self.artwork_edge = 0;
    self.rendered_artwork = UINT32_MAX;
    self.title = self.artist = self.album = self.progress = self.elapsed = self.duration = self.play_label = self.notice = self.editor = self.page_number = nullptr;
    for (auto*& line : self.lyric) line = nullptr;
    lv_obj_update_layout(self.body);
    const auto width = lv_obj_get_content_width(self.body), height = lv_obj_get_content_height(self.body);
    const char* heading = self.view == View::Playing ? "Now Playing" : self.view == View::Lyrics       ? "Lyrics"
                                                                   : self.view == View::Details        ? "Track info"
                                                                   : self.view == View::NewPlaylist    ? "New playlist"
                                                                   : self.view == View::ChoosePlaylist ? "Add to playlist"
                                                                   : self.view == View::ConfirmDelete  ? "Delete playlist"
                                                                                                       : "Music";
    ui::widgets::top_bar_set_title(self.bar, heading);
    if (self.view == View::Playing) now_playing(width, height);
    else if (self.view == View::Library || self.view == View::ChoosePlaylist) library(width, height);
    else if (self.view == View::Lyrics)
    {
        auto* panel = card(self.body, 8, 6, width - 16, height - 12);
        lyrics(panel, width - 16, height - 12);
        lv_group_add_obj(app_g, panel);
        lv_obj_add_event_cb(panel, key_event, LV_EVENT_KEY, nullptr);
        lv_group_focus_obj(panel);
    }
    else if (self.view == View::NewPlaylist)
    {
        const lv_coord_t w = std::min<lv_coord_t>(width - 24, 460);
        auto* panel = card(self.body, (width - w) / 2, 12, w, std::min<lv_coord_t>(height - 24, 160));
        label(panel, "Playlist name", 12, 12, w - 24);
        self.editor = lv_textarea_create(panel);
        lv_obj_set_pos(self.editor, 12, 40);
        lv_obj_set_size(self.editor, w - 24, 36);
        lv_textarea_set_one_line(self.editor, true);
        lv_textarea_set_max_length(self.editor, 48);
        lv_textarea_set_placeholder_text(self.editor, "e.g. Evening trail");
        ui::fonts::apply_content_font(self.editor, "", &lv_font_montserrat_14);
        lv_obj_add_event_cb(self.editor, playlist_name_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(self.editor, editor_ready, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(
            self.editor, [](lv_event_t* event)
            {
                if (lv_event_get_key(event) != LV_KEY_ESC) return;
                lv_event_stop_bubbling(event);
                key_event(event); },
            static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), nullptr);
        ui::widgets::attach_touch_text_editor(self.editor);
        lv_group_focus_obj(self.editor);
        self.notice = label(panel, "Keep a short, clear name", 12, 88, w - 24, &lv_font_montserrat_12, true);
    }
    else if (self.view == View::Details)
    {
        const lv_coord_t edge = self.wide ? 192 : 72;
        auto* art_panel = card(self.body, 12, 8, edge + 16, height - 16);
        cover(art_panel, 8, 8, edge);
        label(art_panel, self.snapshot.current.picture_size ? "ID3 cover" : "No cover", 4, edge + 16, edge + 8, &lv_font_montserrat_12, true);
        auto* panel = card(self.body, edge + 40, 8, width - edge - 52, height - 16);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(panel, 12, 0);
        lv_obj_set_style_pad_row(panel, 8, 0);
        if (self.snapshot.metadata.track_id != self.snapshot.current.id || !self.snapshot.metadata.count)
            label(panel, "Reading track information...", 0, 0, LV_PCT(100));
        for (unsigned index = 0; index < self.snapshot.metadata.count; index++)
        {
            const auto& field = self.snapshot.metadata.fields[index];
            char caption[64];
            std::snprintf(caption, sizeof(caption), "%s%s%s", field.name, field.id[0] ? " / " : "", field.id);
            auto* name = label(panel, caption, 0, 0, LV_PCT(100), &lv_font_montserrat_12, true);
            (void)name;
            content_label(panel, field.value[0] ? field.value : "--", 0, 0, LV_PCT(100));
            if (field.truncated) label(panel, "Text shortened", 0, 0, LV_PCT(100), &lv_font_montserrat_12, true);
        }
        if (self.snapshot.metadata.unsupported_flags) label(panel, "Some compressed or encrypted tags could not be read", 0, 0, LV_PCT(100), &lv_font_montserrat_12, true);
        lv_obj_add_event_cb(panel, key_event, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(app_g, panel);
        lv_group_focus_obj(panel);
    }
    else if (self.view == View::ConfirmDelete)
    {
        label(self.body, "Delete this playlist?", 16, 24, width - 32, &lv_font_montserrat_16);
        content_label(self.body, self.snapshot.category, 16, 60, width - 32);
        label(self.body, "Songs on the SD card are kept.", 16, 90, width - 32, &lv_font_montserrat_12, true);
    }
    if (self.view == View::ConfirmDelete)
    {
        button(self.footer, "Cancel", Cancel);
        spacer(self.footer);
        button(self.footer, "Delete", ConfirmDelete);
    }
    else if (self.view == View::NewPlaylist)
    {
        button(self.footer, "Cancel", Cancel);
        spacer(self.footer);
        button(self.footer, "Create", CreatePlaylist);
    }
    else
    {
        lv_obj_t* anchor = nullptr;
        if (self.view == View::Library) anchor = button(self.footer, "Browse", OpenMenu);
        else if (self.view == View::ChoosePlaylist) button(self.footer, "Cancel", Cancel);
        else button(self.footer, "Library", Library);
        spacer(self.footer);
        if (self.view == View::Library || self.view == View::ChoosePlaylist || self.view == View::Details)
        {
            button(self.footer, LV_SYMBOL_LEFT, PagePrevious, 26);
            self.page_number = label(self.footer, "", 0, 0, LV_SIZE_CONTENT, &lv_font_montserrat_12, true);
            button(self.footer, LV_SYMBOL_RIGHT, PageNext, 26);
            if (self.view != View::Details && ::music::has_session(self.snapshot)) button(self.footer, "Playing", Toggle);
        }
        else if (self.view == View::Playing) button(self.footer, "Lyrics", Lyrics);
        if (!anchor) anchor = button(self.footer, "Menu", OpenMenu);
        menu_items();
        self.menu.init(self.root, anchor, self.menu_content);
    }
    self.rendered_track = self.snapshot.current.id;
    self.rendered_library = self.snapshot.library_revision;
    self.rendered_metadata = self.snapshot.metadata_revision;
    if (self.service)
    {
        self.service->request_artwork(self.artwork_edge);
        self.service->request_lyrics(self.view == View::Lyrics || (self.wide && self.view == View::Playing));
    }
    update();
}
void update()
{
    auto& self = *state();
    // Track changes keep the player controls and their rotary focus alive.
    // Refresh raw ID3/artwork in place instead of rebuilding the whole page.
    if (self.view == View::Playing)
    {
        changed(self.title, self.snapshot.current.id ? self.snapshot.current.title : ui::i18n::tr("Choose a song"), FontScope::Content);
        changed(self.artist, self.snapshot.current.artist, FontScope::Content);
        changed(self.album, self.snapshot.current.album, FontScope::Content);
        self.rendered_track = self.snapshot.current.id;
    }
    char time[32];
    std::snprintf(time, sizeof(time), "%u:%02u", static_cast<unsigned>(self.snapshot.position_ms / 60000), static_cast<unsigned>((self.snapshot.position_ms / 1000) % 60));
    changed(self.elapsed, time);
    if (self.snapshot.current.duration_ms)
        std::snprintf(time, sizeof(time), "%s%u:%02u", self.snapshot.current.estimated_duration ? "~" : "", static_cast<unsigned>(self.snapshot.current.duration_ms / 60000), static_cast<unsigned>((self.snapshot.current.duration_ms / 1000) % 60));
    else std::snprintf(time, sizeof(time), "--:--");
    changed(self.duration, time);
    changed(self.mode, ui::i18n::tr(self.snapshot.mode == ::music::PlayMode::RepeatOne ? "Repeat one" : self.snapshot.mode == ::music::PlayMode::Shuffle ? "Shuffle"
                                                                                                                                                         : "Repeat all"));
    std::snprintf(time, sizeof(time), "%u%%", self.snapshot.volume);
    changed(self.volume, time);
    changed(self.playback_status, self.snapshot.playback == ::music::Playback::Interrupted ? ui::i18n::tr("Paused for voice / radio") : self.snapshot.playback == ::music::Playback::Paused ? ui::i18n::tr("Paused")
                                                                                                                                                                                            : self.snapshot.notice);
    changed(self.play_label, ui::i18n::tr(self.snapshot.playback == ::music::Playback::Playing || self.snapshot.playback == ::music::Playback::Interrupted ? "Pause" : "Play"));
    if (self.progress && !lv_obj_has_state(self.progress, LV_STATE_PRESSED))
        lv_slider_set_value(self.progress, self.snapshot.current.duration_ms ? static_cast<int64_t>(self.snapshot.position_ms) * 1000 / self.snapshot.current.duration_ms : 0, LV_ANIM_OFF);
    if (self.page_number)
    {
        const bool details = self.view == View::Details;
        const uint32_t offset = details ? self.snapshot.metadata.offset : self.snapshot.offset;
        const uint32_t total = details ? self.snapshot.metadata.total : self.snapshot.total;
        const uint32_t size = details ? ::music::kMetadataRows : self.visible_rows;
        std::snprintf(time, sizeof(time), "%u / %u", static_cast<unsigned>(offset / size + 1), static_cast<unsigned>(std::max<uint32_t>(1, (total + size - 1) / size)));
        changed(self.page_number, time);
    }
    if (self.notice)
    {
        if (self.snapshot.notice[0]) changed(self.notice, self.snapshot.notice);
        else if (self.view == View::NewPlaylist) changed(self.notice, ui::i18n::tr("Keep a short, clear name"));
        else
        {
            char caption[160];
            std::snprintf(caption, sizeof(caption), "%s  |  %u", self.snapshot.category[0] ? self.snapshot.category : ui::i18n::tr(browse_name(self.snapshot.browse)), static_cast<unsigned>(self.snapshot.total));
            changed(self.notice, caption, FontScope::Content);
        }
    }
    const char* lines[] = {self.snapshot.lyric_previous, self.snapshot.has_lyrics ? self.snapshot.lyric_current : ui::i18n::tr("No lyrics found"), self.snapshot.has_lyrics ? self.snapshot.lyric_next : ui::i18n::tr("Add a .lrc file beside the song")};
    for (unsigned index = 0; index < 3; index++) changed(self.lyric[index], lines[index], self.snapshot.has_lyrics ? FontScope::Content : FontScope::Ui);
    if (self.cover && self.service && self.rendered_artwork != self.snapshot.artwork_revision)
    {
        // Detach LVGL before releasing the borrowed pixels. Both UI and worker hold references.
        lv_obj_add_flag(self.cover, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(self.cover, nullptr);
        lv_image_cache_drop(&self.cover_image);
        if (self.artwork)
        {
            self.artwork->drop();
            self.artwork = nullptr;
        }
        self.artwork = self.service->acquire_artwork();
        self.rendered_artwork = self.snapshot.artwork_revision;
        if (self.artwork && self.artwork->track_id == self.snapshot.current.id)
        {
            self.cover_image = {};
            self.cover_image.header.magic = LV_IMAGE_HEADER_MAGIC;
            self.cover_image.header.cf = LV_COLOR_FORMAT_RGB565;
            self.cover_image.header.w = self.artwork->width;
            self.cover_image.header.h = self.artwork->height;
            self.cover_image.header.stride = self.artwork->width * 2;
            self.cover_image.data_size = self.artwork->width * self.artwork->height * 2;
            self.cover_image.data = reinterpret_cast<const uint8_t*>(self.artwork->pixels);
            lv_image_set_src(self.cover, &self.cover_image);
            const auto scale = static_cast<uint32_t>(self.artwork_edge) * 256 / std::max(self.artwork->width, self.artwork->height);
            lv_image_set_scale(self.cover, scale);
            lv_obj_center(self.cover);
            lv_obj_remove_flag(self.cover, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
} // namespace ui::music_page
