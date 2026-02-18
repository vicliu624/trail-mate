#include "ui_gnss_skyplot.h"

#include "../gps/gps_service_api.h"
#include "ui_common.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
constexpr int kScreenW = 480;
constexpr int kScreenH = 222;
constexpr int kTopBarH = 30;
constexpr int kCompactMaxWidth = 320;

constexpr int kSkyPanelX = 8;
constexpr int kSkyPanelY = 38;
constexpr int kSkyPanelW = 277;
constexpr int kSkyPanelH = 176;

constexpr int kStatusPanelX = 293;
constexpr int kStatusPanelY = 38;
constexpr int kStatusPanelW = 179;
constexpr int kStatusPanelH = 176;
constexpr int kStatusPanelRightMargin = 8;
constexpr int kStatusToggleBtnW = 82;
constexpr int kStatusToggleBtnH = 24;
constexpr int kStatusToggleBtnBottomMargin = 6;

constexpr int kSkyAreaX = 10;
constexpr int kSkyAreaY = 2;
constexpr int kSkyAreaSize = 170;
constexpr int kSkyCenter = 85;
constexpr int kSkyRadius = 82;
constexpr int kSkyRadius60 = 55;
constexpr int kSkyRadius30 = 27;

constexpr int kDotRadius = 10;
constexpr int kDotSize = kDotRadius * 2;

constexpr int kMaxSats = 32;
constexpr int kTableRows = 7;

constexpr uint32_t kColorAmber = 0xEBA341;
constexpr uint32_t kColorAmberDark = 0xC98118;
constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorPanelBg = 0xFAF0D8;
constexpr uint32_t kColorLine = 0xE7C98F;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;
constexpr uint32_t kColorWarn = 0xB94A2C;
constexpr uint32_t kColorOk = 0x3E7D3E;

constexpr uint32_t kColorSysGps = 0xE3B11F;
constexpr uint32_t kColorSysGln = 0x2D6FB6;
constexpr uint32_t kColorSysGal = 0x3E7D3E;
constexpr uint32_t kColorSysBd = 0xB94A2C;

constexpr uint32_t kColorSnrGood = 0x3E7D3E;
constexpr uint32_t kColorSnrFair = 0x8FBF4D;
constexpr uint32_t kColorSnrWeak = 0xC18B2C;
constexpr uint32_t kColorSnrNotUsed = 0xB94A2C;
constexpr uint32_t kColorSnrInView = 0x6E6E6E;

struct SatDot
{
    lv_obj_t* dot = nullptr;
    lv_obj_t* label = nullptr;
    lv_obj_t* use_tag = nullptr;
    lv_obj_t* use_label = nullptr;
};

struct TableRow
{
    lv_obj_t* row = nullptr;
    lv_obj_t* cells[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
};

struct SkyPlotUi
{
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    ::ui::widgets::TopBar top_bar{};
    bool compact_layout = false;
    bool status_overlay_visible = false;
    lv_obj_t* status_toggle_btn = nullptr;
    lv_obj_t* status_toggle_label = nullptr;

    lv_obj_t* panel_sky = nullptr;
    lv_obj_t* sky_area = nullptr;
    lv_obj_t* label_n = nullptr;
    lv_obj_t* label_e = nullptr;
    lv_obj_t* label_w = nullptr;
    lv_obj_t* label_90 = nullptr;
    lv_obj_t* label_60 = nullptr;
    lv_obj_t* label_30 = nullptr;
    lv_obj_t* label_horizon = nullptr;

    lv_obj_t* panel_status = nullptr;
    lv_obj_t* status_header = nullptr;
    lv_obj_t* status_header_label = nullptr;
    lv_obj_t* table_header = nullptr;
    lv_obj_t* table_header_cells[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    TableRow table_rows[kTableRows];

    SatDot sats[kMaxSats];
    lv_point_precise_t ns_points[2]{};
    lv_point_precise_t ew_points[2]{};
};

static SkyPlotUi s_ui{};
static lv_timer_t* s_refresh_timer = nullptr;

static SatInfo s_cached_sats[kMaxSats];
static int s_cached_sat_count = 0;
static GnssStatus s_cached_status{};
static bool s_cached_status_valid = false;

lv_color_t sys_color(SatInfo::Sys sys)
{
    switch (sys)
    {
    case SatInfo::GPS:
        return lv_color_hex(kColorSysGps);
    case SatInfo::GLN:
        return lv_color_hex(kColorSysGln);
    case SatInfo::GAL:
        return lv_color_hex(kColorSysGal);
    case SatInfo::BD:
    default:
        return lv_color_hex(kColorSysBd);
    }
}

SatInfo::Sys map_sys(gps::GnssSystem sys)
{
    switch (sys)
    {
    case gps::GnssSystem::GPS:
        return SatInfo::GPS;
    case gps::GnssSystem::GLN:
        return SatInfo::GLN;
    case gps::GnssSystem::GAL:
        return SatInfo::GAL;
    case gps::GnssSystem::BD:
        return SatInfo::BD;
    default:
        return SatInfo::GPS;
    }
}

const char* sys_text(SatInfo::Sys sys)
{
    switch (sys)
    {
    case SatInfo::GPS:
        return "GPS";
    case SatInfo::GLN:
        return "GLN";
    case SatInfo::GAL:
        return "GAL";
    case SatInfo::BD:
    default:
        return "BD";
    }
}

lv_color_t snr_color(SatInfo::SNRState state)
{
    switch (state)
    {
    case SatInfo::GOOD:
        return lv_color_hex(kColorSnrGood);
    case SatInfo::FAIR:
        return lv_color_hex(kColorSnrFair);
    case SatInfo::WEAK:
        return lv_color_hex(kColorSnrWeak);
    case SatInfo::NOT_USED:
        return lv_color_hex(kColorSnrNotUsed);
    case SatInfo::IN_VIEW:
    default:
        return lv_color_hex(kColorSnrInView);
    }
}

lv_color_t text_on_color(lv_color_t bg)
{
    const uint8_t lum = lv_color_luminance(bg);
    return lum > 160 ? lv_color_hex(kColorText) : lv_color_white();
}

void place_label_center(lv_obj_t* label, int center_x, int center_y)
{
    if (!label)
    {
        return;
    }
    lv_obj_update_layout(label);
    int w = lv_obj_get_width(label);
    int h = lv_obj_get_height(label);
    lv_obj_set_pos(label, center_x - w / 2, center_y - h / 2);
}

void place_label_center_x(lv_obj_t* label, int center_x, int top_y)
{
    if (!label)
    {
        return;
    }
    lv_obj_update_layout(label);
    int w = lv_obj_get_width(label);
    lv_obj_set_pos(label, center_x - w / 2, top_y);
}

void place_label_diagonal_1030(lv_obj_t* label, int center_x, int center_y, int radius)
{
    if (!label)
    {
        return;
    }
    constexpr float kDiag = 0.70710678f;
    int x = static_cast<int>(std::round(center_x - radius * kDiag));
    int y = static_cast<int>(std::round(center_y - radius * kDiag));
    place_label_center(label, x, y);
}

void apply_common_container_style(lv_obj_t* obj, lv_color_t bg, lv_color_t border, int radius, int border_w)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_ring(lv_obj_t* parent, int radius, int thickness)
{
    lv_obj_t* ring = lv_obj_create(parent);
    const int size = radius * 2;
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, thickness, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    return ring;
}

lv_obj_t* create_axis_line(lv_obj_t* parent, const lv_point_precise_t* pts, uint16_t count)
{
    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, pts, count);
    lv_obj_set_style_line_color(line, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_rounded(line, false, 0);
    return line;
}

void ensure_sat_dot(int index)
{
    if (index < 0 || index >= kMaxSats)
    {
        return;
    }
    if (s_ui.sats[index].dot)
    {
        return;
    }
    SatDot& dot = s_ui.sats[index];
    dot.dot = lv_obj_create(s_ui.panel_sky);
    lv_obj_set_size(dot.dot, kDotSize, kDotSize);
    lv_obj_set_style_radius(dot.dot, kDotRadius, 0);
    lv_obj_set_style_bg_opa(dot.dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot.dot, 2, 0);
    lv_obj_set_style_pad_all(dot.dot, 0, 0);
    lv_obj_clear_flag(dot.dot, LV_OBJ_FLAG_SCROLLABLE);

    dot.label = lv_label_create(dot.dot);
    lv_obj_set_style_text_font(dot.label, &lv_font_montserrat_12, 0);
    lv_obj_center(dot.label);

    dot.use_tag = lv_obj_create(s_ui.panel_sky);
    lv_obj_set_size(dot.use_tag, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(dot.use_tag, lv_color_hex(kColorOk), 0);
    lv_obj_set_style_bg_opa(dot.use_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot.use_tag, 6, 0);
    lv_obj_set_style_border_width(dot.use_tag, 0, 0);
    lv_obj_set_style_pad_left(dot.use_tag, 4, 0);
    lv_obj_set_style_pad_right(dot.use_tag, 4, 0);
    lv_obj_set_style_pad_top(dot.use_tag, 1, 0);
    lv_obj_set_style_pad_bottom(dot.use_tag, 1, 0);
    lv_obj_clear_flag(dot.use_tag, LV_OBJ_FLAG_SCROLLABLE);

    dot.use_label = lv_label_create(dot.use_tag);
    lv_label_set_text(dot.use_label, "USE");
    lv_obj_set_style_text_color(dot.use_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(dot.use_label, &lv_font_montserrat_12, 0);
    lv_obj_center(dot.use_label);

    lv_obj_add_flag(dot.use_tag, LV_OBJ_FLAG_HIDDEN);
}

void hide_sat_dot(int index)
{
    if (index < 0 || index >= kMaxSats)
    {
        return;
    }
    SatDot& dot = s_ui.sats[index];
    if (dot.dot)
    {
        lv_obj_add_flag(dot.dot, LV_OBJ_FLAG_HIDDEN);
    }
    if (dot.use_tag)
    {
        lv_obj_add_flag(dot.use_tag, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_sat_dot(int index, const SatInfo& sat)
{
    ensure_sat_dot(index);
    SatDot& dot = s_ui.sats[index];
    if (!dot.dot)
    {
        return;
    }

    float az = std::fmod(sat.azimuth, 360.0f);
    if (az < 0.0f)
    {
        az += 360.0f;
    }
    float el = std::max(0.0f, std::min(90.0f, sat.elevation));
    float r = kSkyRadius * (1.0f - el / 90.0f);
    float rad = az * 3.1415926535f / 180.0f;

    float sx = (kSkyAreaX + kSkyCenter) + r * std::sin(rad);
    float sy = (kSkyAreaY + kSkyCenter) - r * std::cos(rad);

    int dot_x = static_cast<int>(std::round(sx)) - kDotRadius;
    int dot_y = static_cast<int>(std::round(sy)) - kDotRadius;

    lv_obj_set_pos(dot.dot, dot_x, dot_y);
    lv_obj_clear_flag(dot.dot, LV_OBJ_FLAG_HIDDEN);

    lv_color_t fill = sys_color(sat.sys);
    lv_color_t border = snr_color(sat.snr_state);
    lv_obj_set_style_bg_color(dot.dot, fill, 0);
    lv_obj_set_style_border_color(dot.dot, border, 0);

    char id_buf[8];
    snprintf(id_buf, sizeof(id_buf), "%d", sat.id);
    lv_label_set_text(dot.label, id_buf);
    lv_obj_set_style_text_color(dot.label, text_on_color(fill), 0);
    lv_obj_center(dot.label);

    if (dot.use_tag)
    {
        if (sat.used)
        {
            int tag_x = dot_x + kDotRadius - 12;
            int tag_y = dot_y + kDotRadius + 12;
            lv_obj_set_pos(dot.use_tag, tag_x, tag_y);
            lv_obj_clear_flag(dot.use_tag, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(dot.use_tag, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void update_table_rows()
{
    const int count_all = std::min(s_cached_sat_count, kMaxSats);
    SatInfo sorted[kMaxSats];
    for (int i = 0; i < count_all; ++i)
    {
        sorted[i] = s_cached_sats[i];
    }
    std::sort(sorted, sorted + count_all, [](const SatInfo& a, const SatInfo& b)
              {
        if (a.used != b.used)
        {
            return a.used > b.used;
        }
        if (a.snr != b.snr)
        {
            return a.snr > b.snr;
        }
        if (a.elevation != b.elevation)
        {
            return a.elevation > b.elevation;
        }
        return a.id < b.id; });
    const int count = std::min(count_all, kTableRows);
    for (int row = 0; row < kTableRows; ++row)
    {
        TableRow& r = s_ui.table_rows[row];
        if (!r.row)
        {
            continue;
        }
        if (row < count)
        {
            const SatInfo& sat = sorted[row];
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", sat.id);
            lv_label_set_text(r.cells[0], buf);
            lv_label_set_text(r.cells[1], sys_text(sat.sys));
            lv_obj_set_style_text_color(r.cells[1], sys_color(sat.sys), 0);
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(sat.elevation)));
            lv_label_set_text(r.cells[2], buf);
            snprintf(buf, sizeof(buf), "%d", sat.snr);
            lv_label_set_text(r.cells[3], buf);
            lv_label_set_text(r.cells[4], sat.used ? "YES" : "NO");
            lv_obj_set_style_text_color(r.cells[4], lv_color_hex(sat.used ? kColorOk : kColorWarn), 0);
        }
        else
        {
            for (auto* cell : r.cells)
            {
                if (cell)
                {
                    lv_label_set_text(cell, "");
                    lv_obj_set_style_text_color(cell, lv_color_hex(kColorText), 0);
                }
            }
        }
    }
}

void apply_cached_sats()
{
    if (!s_ui.panel_sky)
    {
        return;
    }
    int count = std::min(s_cached_sat_count, kMaxSats);
    for (int i = 0; i < count; ++i)
    {
        update_sat_dot(i, s_cached_sats[i]);
    }
    for (int i = count; i < kMaxSats; ++i)
    {
        hide_sat_dot(i);
    }
    update_table_rows();
}

void apply_topbar_summary(const GnssStatus& st)
{
    if (!s_ui.top_bar.title_label)
    {
        return;
    }
    const char* fix_text = "NO FIX";
    if (st.fix == GnssStatus::FIX2D)
    {
        fix_text = "2D";
    }
    else if (st.fix == GnssStatus::FIX3D)
    {
        fix_text = "3D";
    }
    const uint32_t use_color = kColorText;
    const uint32_t hdop_color = kColorAmberDark;
    const uint32_t fix_color = (st.fix == GnssStatus::NOFIX) ? kColorWarn : kColorOk;

    char buf[128];
    snprintf(buf,
             sizeof(buf),
             "#%06X USE: %d/%d#|#%06X HDOP: %.1f#|#%06X FIX: %s#",
             use_color,
             st.sats_in_use,
             st.sats_in_view,
             hdop_color,
             st.hdop,
             fix_color,
             fix_text);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, buf);
}

void apply_status(const GnssStatus& st)
{
    apply_topbar_summary(st);
}

void set_status_overlay_visible(bool visible)
{
    if (!s_ui.compact_layout || !s_ui.panel_status)
    {
        return;
    }

    s_ui.status_overlay_visible = visible;
    if (visible)
    {
        lv_obj_clear_flag(s_ui.panel_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.panel_status);
    }
    else
    {
        lv_obj_add_flag(s_ui.panel_status, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_ui.status_toggle_label)
    {
        lv_label_set_text(s_ui.status_toggle_label, visible ? "Hide" : "Status");
        lv_obj_center(s_ui.status_toggle_label);
    }
    if (s_ui.status_toggle_btn)
    {
        lv_obj_move_foreground(s_ui.status_toggle_btn);
    }
}

void on_status_toggle_clicked(lv_event_t* e)
{
    (void)e;
    set_status_overlay_visible(!s_ui.status_overlay_visible);
}

void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE)
    {
        return;
    }
    if (s_ui.compact_layout && s_ui.status_overlay_visible)
    {
        set_status_overlay_visible(false);
        return;
    }
    if (s_ui.top_bar.back_cb)
    {
        s_ui.top_bar.back_cb(s_ui.top_bar.back_user_data);
    }
    else
    {
        ui_request_exit_to_menu();
    }
}

void back_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE)
    {
        return;
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_send_event(s_ui.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
    }
}

SatInfo::SNRState map_snr_state(int snr, bool used)
{
    if (snr < 0)
    {
        return SatInfo::IN_VIEW;
    }
    if (!used)
    {
        return SatInfo::NOT_USED;
    }
    if (snr >= 35)
    {
        return SatInfo::GOOD;
    }
    if (snr >= 25)
    {
        return SatInfo::FAIR;
    }
    return SatInfo::WEAK;
}

void refresh_gnss_data()
{
    gps::GnssSatInfo sats[gps::kMaxGnssSats]{};
    size_t count = 0;
    gps::GnssStatus status{};
    if (!gps::gps_get_gnss_snapshot(sats, gps::kMaxGnssSats, &count, &status))
    {
        return;
    }

    SatInfo ui_sats[kMaxSats]{};
    int ui_count = 0;
    int used_count = 0;
    for (size_t i = 0; i < count && ui_count < kMaxSats; ++i)
    {
        SatInfo sat{};
        sat.id = static_cast<int>(sats[i].id);
        sat.sys = map_sys(sats[i].sys);
        sat.azimuth = static_cast<float>(sats[i].azimuth);
        sat.elevation = static_cast<float>(sats[i].elevation);
        sat.snr = static_cast<int>(sats[i].snr);
        sat.used = sats[i].used;
        if (sat.used)
        {
            used_count++;
        }
        sat.snr_state = map_snr_state(sat.snr, sat.used);
        ui_sats[ui_count++] = sat;
    }

    ui_gnss_skyplot_set_sats(ui_sats, ui_count);

    GnssStatus ui_status{};
    ui_status.sats_in_view = status.sats_in_view > 0 ? status.sats_in_view : static_cast<int>(ui_count);
    ui_status.sats_in_use = status.sats_in_use > 0 ? status.sats_in_use : used_count;
    ui_status.hdop = status.hdop;
    switch (status.fix)
    {
    case gps::GnssFix::FIX2D:
        ui_status.fix = GnssStatus::FIX2D;
        break;
    case gps::GnssFix::FIX3D:
        ui_status.fix = GnssStatus::FIX3D;
        break;
    case gps::GnssFix::NOFIX:
    default:
        ui_status.fix = GnssStatus::NOFIX;
        break;
    }
    ui_gnss_skyplot_set_status(ui_status);
}

void refresh_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!s_ui.root)
    {
        return;
    }
    ui_update_top_bar_battery(s_ui.top_bar);
    refresh_gnss_data();
}
} // namespace

lv_obj_t* ui_gnss_skyplot_create(lv_obj_t* parent)
{
    s_ui = {};
    if (parent)
    {
        lv_obj_update_layout(parent);
    }
    lv_coord_t parent_w = parent ? lv_obj_get_width(parent) : 0;
    lv_coord_t parent_h = parent ? lv_obj_get_height(parent) : 0;
    const bool compact_layout = (parent_w > 0 && parent_w <= kCompactMaxWidth);
    const lv_coord_t screen_w = compact_layout ? (parent_w > 0 ? parent_w : kCompactMaxWidth) : kScreenW;
    const lv_coord_t screen_h = compact_layout ? (parent_h > 0 ? parent_h : 240) : kScreenH;
    const lv_coord_t status_panel_x = compact_layout
                                          ? std::max<lv_coord_t>(0, screen_w - kStatusPanelW - kStatusPanelRightMargin)
                                          : kStatusPanelX;
    s_ui.compact_layout = compact_layout;

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, screen_w, screen_h);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 8, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.header = lv_obj_create(s_ui.root);
    lv_obj_set_pos(s_ui.header, 0, 0);
    lv_obj_set_size(s_ui.header, screen_w, kTopBarH);
    lv_obj_set_style_bg_opa(s_ui.header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.header, 0, 0);
    lv_obj_set_style_pad_all(s_ui.header, 0, 0);
    lv_obj_clear_flag(s_ui.header, LV_OBJ_FLAG_SCROLLABLE);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(s_ui.top_bar, s_ui.header, cfg);
    if (s_ui.top_bar.title_label)
    {
        lv_label_set_recolor(s_ui.top_bar.title_label, true);
        lv_label_set_long_mode(s_ui.top_bar.title_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_ui.top_bar.title_label, &lv_font_montserrat_14, 0);
    }
    char title_buf[96];
    snprintf(title_buf,
             sizeof(title_buf),
             "#%06X USE: --/--#|#%06X HDOP: --#|#%06X FIX: --#",
             kColorText,
             kColorAmberDark,
             kColorTextDim);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, title_buf);
    ::ui::widgets::top_bar_set_back_callback(
        s_ui.top_bar,
        [](void*)
        { ui_request_exit_to_menu(); },
        nullptr);
    ui_update_top_bar_battery(s_ui.top_bar);
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, back_btn_key_event_cb, LV_EVENT_KEY, nullptr);
    }

    s_ui.panel_sky = lv_obj_create(s_ui.root);
    lv_obj_set_pos(s_ui.panel_sky, kSkyPanelX, kSkyPanelY);
    lv_obj_set_size(s_ui.panel_sky, kSkyPanelW, kSkyPanelH);
    apply_common_container_style(s_ui.panel_sky,
                                 lv_color_hex(kColorPanelBg),
                                 lv_color_hex(kColorAmberDark),
                                 10,
                                 2);

    s_ui.sky_area = lv_obj_create(s_ui.panel_sky);
    lv_obj_set_pos(s_ui.sky_area, kSkyAreaX, kSkyAreaY);
    lv_obj_set_size(s_ui.sky_area, kSkyAreaSize, kSkyAreaSize);
    lv_obj_set_style_bg_opa(s_ui.sky_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.sky_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.sky_area, 0, 0);
    lv_obj_clear_flag(s_ui.sky_area, LV_OBJ_FLAG_SCROLLABLE);

    const int center_x = kSkyAreaX + kSkyCenter;
    const int center_y = kSkyAreaY + kSkyCenter;

    lv_obj_t* outer_ring = create_ring(s_ui.panel_sky, kSkyRadius, 2);
    lv_obj_set_pos(outer_ring, center_x - kSkyRadius, center_y - kSkyRadius);
    lv_obj_move_background(outer_ring);

    lv_obj_t* mid_ring = create_ring(s_ui.panel_sky, kSkyRadius60, 1);
    lv_obj_set_pos(mid_ring, center_x - kSkyRadius60, center_y - kSkyRadius60);
    lv_obj_move_background(mid_ring);

    lv_obj_t* inner_ring = create_ring(s_ui.panel_sky, kSkyRadius30, 1);
    lv_obj_set_pos(inner_ring, center_x - kSkyRadius30, center_y - kSkyRadius30);
    lv_obj_move_background(inner_ring);

    s_ui.ns_points[0] = {static_cast<lv_value_precise_t>(center_x), static_cast<lv_value_precise_t>(center_y - kSkyRadius)};
    s_ui.ns_points[1] = {static_cast<lv_value_precise_t>(center_x), static_cast<lv_value_precise_t>(center_y + kSkyRadius)};
    lv_obj_t* ns_line = create_axis_line(s_ui.panel_sky, s_ui.ns_points, 2);
    lv_obj_set_pos(ns_line, 0, 0);
    lv_obj_move_background(ns_line);

    s_ui.ew_points[0] = {static_cast<lv_value_precise_t>(center_x - kSkyRadius), static_cast<lv_value_precise_t>(center_y)};
    s_ui.ew_points[1] = {static_cast<lv_value_precise_t>(center_x + kSkyRadius), static_cast<lv_value_precise_t>(center_y)};
    lv_obj_t* ew_line = create_axis_line(s_ui.panel_sky, s_ui.ew_points, 2);
    lv_obj_set_pos(ew_line, 0, 0);
    lv_obj_move_background(ew_line);

    lv_obj_t* center_dot = lv_obj_create(s_ui.panel_sky);
    lv_obj_set_size(center_dot, 4, 4);
    lv_obj_set_pos(center_dot, center_x - 2, center_y - 2);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_set_style_radius(center_dot, 2, 0);
    lv_obj_clear_flag(center_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(center_dot);

    s_ui.label_n = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_n, "N");
    lv_obj_set_style_text_color(s_ui.label_n, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(s_ui.label_n, &lv_font_montserrat_18, 0);
    place_label_center_x(s_ui.label_n, kSkyAreaX + kSkyCenter, kSkyAreaY - 2);

    s_ui.label_e = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_e, "E");
    lv_obj_set_style_text_color(s_ui.label_e, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(s_ui.label_e, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_ui.label_e, kSkyAreaX + kSkyAreaSize + 8, kSkyAreaY + kSkyCenter - 10);

    s_ui.label_w = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_w, "W");
    lv_obj_set_style_text_color(s_ui.label_w, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(s_ui.label_w, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_ui.label_w, 2, kSkyAreaY + kSkyCenter - 10);

    s_ui.label_90 = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_90, "90°");
    lv_obj_set_style_text_color(s_ui.label_90, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_style_text_font(s_ui.label_90, &lv_font_montserrat_16, 0);
    place_label_diagonal_1030(s_ui.label_90, center_x, center_y, kSkyRadius);

    s_ui.label_60 = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_60, "60°");
    lv_obj_set_style_text_color(s_ui.label_60, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_style_text_font(s_ui.label_60, &lv_font_montserrat_16, 0);
    place_label_diagonal_1030(s_ui.label_60, center_x, center_y, kSkyRadius60);

    s_ui.label_30 = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_30, "30°");
    lv_obj_set_style_text_color(s_ui.label_30, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_style_text_font(s_ui.label_30, &lv_font_montserrat_16, 0);
    place_label_diagonal_1030(s_ui.label_30, center_x, center_y, kSkyRadius30);

    s_ui.label_horizon = lv_label_create(s_ui.panel_sky);
    lv_label_set_text(s_ui.label_horizon, "0° Horizon");
    lv_obj_set_style_text_color(s_ui.label_horizon, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_style_text_font(s_ui.label_horizon, &lv_font_montserrat_12, 0);
    place_label_center(s_ui.label_horizon, kSkyAreaX + kSkyCenter, kSkyAreaY + kSkyCenter + 12);

    struct LegendSys
    {
        const char* text;
        uint32_t color;
    };
    const int legend_sys_x = 190;
    const int legend_sys_y = 105;
    const int legend_sys_row = 15;
    const LegendSys sys_legend[] = {
        {"GPS", kColorSysGps},
        {"GLONASS", kColorSysGln},
        {"Galileo", kColorSysGal},
        {"BeiDou", kColorSysBd},
    };
    for (int i = 0; i < 4; ++i)
    {
        int x = legend_sys_x;
        int y = legend_sys_y + i * legend_sys_row;
        lv_obj_t* block = lv_obj_create(s_ui.panel_sky);
        lv_obj_set_pos(block, x, y + 4);
        lv_obj_set_size(block, 10, 10);
        lv_obj_set_style_bg_color(block, lv_color_hex(sys_legend[i].color), 0);
        lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(block, 0, 0);
        lv_obj_set_style_radius(block, 2, 0);
        lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* label = lv_label_create(s_ui.panel_sky);
        lv_label_set_text(label, sys_legend[i].text);
        lv_obj_set_style_text_color(label, lv_color_hex(kColorText), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label, x + 14, y);
    }

    struct LegendSnr
    {
        const char* text;
        uint32_t color;
    };
    const LegendSnr snr_legend[] = {
        {"SNR Good", kColorSnrGood},
        {"SNR Weak", kColorSnrWeak},
        {"Not Used", kColorSnrNotUsed},
        {"In View", kColorSnrInView},
    };
    const int snr_row = legend_sys_row;
    const int snr_gap = 6;
    const int snr_start_y = legend_sys_y - (4 * snr_row) - snr_gap - 30;
    for (int i = 0; i < 4; ++i)
    {
        int x = legend_sys_x;
        int y = snr_start_y + i * snr_row;
        lv_obj_t* dot = lv_obj_create(s_ui.panel_sky);
        lv_obj_set_pos(dot, x, y + 4);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_bg_color(dot, lv_color_hex(snr_legend[i].color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* label = lv_label_create(s_ui.panel_sky);
        lv_label_set_text(label, snr_legend[i].text);
        lv_obj_set_style_text_color(label, lv_color_hex(kColorTextDim), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label, x + 14, y);
    }

    s_ui.panel_status = lv_obj_create(s_ui.root);
    lv_obj_set_pos(s_ui.panel_status, status_panel_x, kStatusPanelY);
    lv_obj_set_size(s_ui.panel_status, kStatusPanelW, kStatusPanelH);
    apply_common_container_style(s_ui.panel_status,
                                 lv_color_hex(kColorPanelBg),
                                 lv_color_hex(kColorAmberDark),
                                 10,
                                 2);

    s_ui.status_header = lv_obj_create(s_ui.panel_status);
    lv_obj_set_pos(s_ui.status_header, 0, 0);
    lv_obj_set_size(s_ui.status_header, kStatusPanelW, 26);
    lv_obj_set_style_bg_color(s_ui.status_header, lv_color_hex(kColorAmber), 0);
    lv_obj_set_style_bg_opa(s_ui.status_header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.status_header, 0, 0);
    lv_obj_set_style_radius(s_ui.status_header, 10, 0);
    lv_obj_set_style_pad_all(s_ui.status_header, 0, 0);
    lv_obj_clear_flag(s_ui.status_header, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.status_header_label = lv_label_create(s_ui.status_header);
    lv_label_set_text(s_ui.status_header_label, "SATELLITE STATUS");
    lv_obj_set_style_text_color(s_ui.status_header_label, lv_color_hex(0x2A1A05), 0);
    lv_obj_set_style_text_font(s_ui.status_header_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_ui.status_header_label);

    s_ui.table_header = lv_obj_create(s_ui.panel_status);
    lv_obj_set_pos(s_ui.table_header, 0, 26);
    lv_obj_set_size(s_ui.table_header, kStatusPanelW, 22);
    lv_obj_set_style_bg_color(s_ui.table_header, lv_color_hex(0xF2D9A5), 0);
    lv_obj_set_style_bg_opa(s_ui.table_header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.table_header, 0, 0);
    lv_obj_set_style_pad_all(s_ui.table_header, 0, 0);
    lv_obj_clear_flag(s_ui.table_header, LV_OBJ_FLAG_SCROLLABLE);

    const char* header_texts[5] = {"ID", "SYS", "ELEV", "SNR", "USE"};
    const int col_w[5] = {24, 38, 39, 38, 39};
    int col_x = 0;
    for (int i = 0; i < 5; ++i)
    {
        s_ui.table_header_cells[i] = lv_label_create(s_ui.table_header);
        lv_label_set_text(s_ui.table_header_cells[i], header_texts[i]);
        lv_label_set_long_mode(s_ui.table_header_cells[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(s_ui.table_header_cells[i], lv_color_hex(kColorTextDim), 0);
        lv_obj_set_style_text_font(s_ui.table_header_cells[i], &lv_font_montserrat_14, 0);
        lv_obj_set_size(s_ui.table_header_cells[i], col_w[i], 22);
        lv_obj_set_style_text_align(s_ui.table_header_cells[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_ui.table_header_cells[i], col_x, 0);
        col_x += col_w[i];
    }

    for (int row = 0; row < kTableRows; ++row)
    {
        TableRow& r = s_ui.table_rows[row];
        r.row = lv_obj_create(s_ui.panel_status);
        lv_obj_set_pos(r.row, 0, 48 + row * 17);
        lv_obj_set_size(r.row, kStatusPanelW, 17);
        lv_obj_set_style_bg_opa(r.row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(r.row, 1, 0);
        lv_obj_set_style_border_color(r.row, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_border_side(r.row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_all(r.row, 0, 0);
        lv_obj_clear_flag(r.row, LV_OBJ_FLAG_SCROLLABLE);

        col_x = 0;
        for (int i = 0; i < 5; ++i)
        {
            r.cells[i] = lv_label_create(r.row);
            lv_label_set_text(r.cells[i], "");
            lv_label_set_long_mode(r.cells[i], LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(r.cells[i], lv_color_hex(kColorText), 0);
            lv_obj_set_style_text_font(r.cells[i], &lv_font_montserrat_16, 0);
            lv_obj_set_size(r.cells[i], col_w[i], 17);
            lv_obj_set_style_text_align(r.cells[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(r.cells[i], col_x, 0);
            col_x += col_w[i];
        }
    }

    apply_cached_sats();
    if (s_cached_status_valid)
    {
        apply_status(s_cached_status);
    }

    if (s_ui.compact_layout)
    {
        s_ui.status_toggle_btn = lv_btn_create(s_ui.root);
        lv_obj_set_size(s_ui.status_toggle_btn, kStatusToggleBtnW, kStatusToggleBtnH);
        lv_obj_set_pos(s_ui.status_toggle_btn,
                       screen_w - kStatusToggleBtnW - kStatusPanelRightMargin,
                       screen_h - kStatusToggleBtnH - kStatusToggleBtnBottomMargin);
        lv_obj_set_style_bg_color(s_ui.status_toggle_btn, lv_color_hex(kColorAmber), 0);
        lv_obj_set_style_bg_opa(s_ui.status_toggle_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_ui.status_toggle_btn, 1, 0);
        lv_obj_set_style_border_color(s_ui.status_toggle_btn, lv_color_hex(kColorAmberDark), 0);
        lv_obj_set_style_radius(s_ui.status_toggle_btn, 6, 0);
        lv_obj_set_style_pad_all(s_ui.status_toggle_btn, 0, 0);
        lv_obj_clear_flag(s_ui.status_toggle_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_ui.status_toggle_btn, on_status_toggle_clicked, LV_EVENT_CLICKED, nullptr);

        s_ui.status_toggle_label = lv_label_create(s_ui.status_toggle_btn);
        lv_obj_set_style_text_color(s_ui.status_toggle_label, lv_color_hex(0x2A1A05), 0);
        lv_obj_set_style_text_font(s_ui.status_toggle_label, &lv_font_montserrat_14, 0);

        set_status_overlay_visible(false);
    }

    return s_ui.root;
}

void ui_gnss_skyplot_set_sats(const SatInfo* sats, int count)
{
    if (count < 0)
    {
        count = 0;
    }
    if (!sats || count == 0)
    {
        s_cached_sat_count = 0;
        if (s_ui.root)
        {
            apply_cached_sats();
        }
        return;
    }
    count = std::min(count, kMaxSats);
    s_cached_sat_count = count;
    std::memcpy(s_cached_sats, sats, sizeof(SatInfo) * count);
    if (s_ui.root)
    {
        apply_cached_sats();
    }
}

void ui_gnss_skyplot_set_status(GnssStatus st)
{
    s_cached_status = st;
    s_cached_status_valid = true;
    if (s_ui.root)
    {
        apply_status(st);
    }
}

void ui_gnss_skyplot_enter(lv_obj_t* parent)
{
    if (s_ui.root)
    {
        ui_gnss_skyplot_exit(parent);
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    ui_gnss_skyplot_create(parent);

    refresh_gnss_data();
    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, 1000, nullptr);
    }

    extern lv_group_t* app_g;
    if (app_g && s_ui.top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_ui.top_bar.back_btn);
        if (s_ui.status_toggle_btn)
        {
            lv_group_add_obj(app_g, s_ui.status_toggle_btn);
        }
        lv_group_focus_obj(s_ui.top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }
}

void ui_gnss_skyplot_exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_refresh_timer)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
    }
    s_ui = {};
}
