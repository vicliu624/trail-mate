#include "board/TLoRaPagerBoard.h"
#include "ui/LV_Helper.h"
#include <cmath>

#include "display/DisplayConfig.h"

namespace {
constexpr int kDesignWidth = 218;
constexpr int kDesignHeight = 107;
constexpr int kTopPanelX = 6;
constexpr int kTopPanelY = 8;
constexpr int kTopPanelW = 206;
constexpr int kTopPanelH = 60;
constexpr int kBtnY = 5;
constexpr int kBtnW = 64;
constexpr int kBtnH = 52;
constexpr int kBtn1X = 1;
constexpr int kBtn2X = 71;
constexpr int kBtn3X = 141;
constexpr int kBtn3W = 65;
constexpr int kTitleY = 76;

lv_obj_t *g_title_label = nullptr;
lv_obj_t *g_buttons[3] = {nullptr, nullptr, nullptr};
lv_style_t g_style_card;
lv_style_t g_style_panel;
lv_style_t g_style_button;
lv_style_t g_style_button_selected;
lv_style_t g_style_icon;
lv_style_t g_style_title;

const char *kTitleText[3] = {"GPS", "LoRa Chat", "Setting"};
const char *kIconText[3] = {LV_SYMBOL_GPS, LV_SYMBOL_FILE, LV_SYMBOL_SETTINGS};

struct LayoutMetrics {
    int card_x = 0;
    int card_y = 0;
    int card_w = kDesignWidth;
    int card_h = kDesignHeight;
    int panel_x = kTopPanelX;
    int panel_y = kTopPanelY;
    int panel_w = kTopPanelW;
    int panel_h = kTopPanelH;
    int btn_y = kBtnY;
    int btn_w = kBtnW;
    int btn_h = kBtnH;
    int btn_x[3] = {kBtn1X, kBtn2X, kBtn3X};
    int btn_w_list[3] = {kBtnW, kBtnW, kBtn3W};
    int title_y = kTitleY;
};

display::ScreenSize get_screen_size()
{
    display::Config config = display::get_config();
    if (config.screen.width > 0 && config.screen.height > 0) {
        return config.screen;
    }
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) {
        return {kDesignWidth, kDesignHeight};
    }
    return {static_cast<int>(lv_disp_get_hor_res(disp)), static_cast<int>(lv_disp_get_ver_res(disp))};
}

LayoutMetrics compute_layout(const display::ScreenSize &screen)
{
    LayoutMetrics metrics;
    float scale = 1.0f;
    if (screen.width < kDesignWidth || screen.height < kDesignHeight) {
        scale = std::min(static_cast<float>(screen.width) / kDesignWidth,
                         static_cast<float>(screen.height) / kDesignHeight);
    }

    auto scale_val = [scale](int value) { return static_cast<int>(std::lround(value * scale)); };

    metrics.card_w = scale_val(kDesignWidth);
    metrics.card_h = scale_val(kDesignHeight);
    metrics.card_x = (screen.width - metrics.card_w) / 2;
    metrics.card_y = (screen.height - metrics.card_h) / 2;
    metrics.panel_x = scale_val(kTopPanelX);
    metrics.panel_y = scale_val(kTopPanelY);
    metrics.panel_w = scale_val(kTopPanelW);
    metrics.panel_h = scale_val(kTopPanelH);
    metrics.btn_y = scale_val(kBtnY);
    metrics.btn_w = scale_val(kBtnW);
    metrics.btn_h = scale_val(kBtnH);
    metrics.btn_x[0] = scale_val(kBtn1X);
    metrics.btn_x[1] = scale_val(kBtn2X);
    metrics.btn_x[2] = scale_val(kBtn3X);
    metrics.btn_w_list[0] = scale_val(kBtnW);
    metrics.btn_w_list[1] = scale_val(kBtnW);
    metrics.btn_w_list[2] = scale_val(kBtn3W);
    metrics.title_y = scale_val(kTitleY);
    return metrics;
}

void set_selected_index(int index)
{
    for (int i = 0; i < 3; ++i) {
        if (!g_buttons[i]) {
            continue;
        }
        lv_obj_remove_style(g_buttons[i], &g_style_button_selected, LV_PART_MAIN);
        if (i == index) {
            lv_obj_add_style(g_buttons[i], &g_style_button_selected, LV_PART_MAIN);
        }
    }
    if (g_title_label) {
        lv_label_set_text(g_title_label, kTitleText[index]);
    }
}

void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *btn = lv_event_get_target_obj(e);
    for (int i = 0; i < 3; ++i) {
        if (btn == g_buttons[i]) {
            set_selected_index(i);
            return;
        }
    }
}
} // namespace

void setup()
{
    Serial.begin(115200);

    instance.begin();
    beginLvglHelper(instance);

    lv_style_init(&g_style_card);
    lv_style_set_radius(&g_style_card, 6);
    lv_style_set_bg_color(&g_style_card, lv_color_hex(0xF0F0F0));
    lv_style_set_border_color(&g_style_card, lv_color_hex(0x747474));
    lv_style_set_border_width(&g_style_card, 1);
    lv_style_set_shadow_width(&g_style_card, 6);
    lv_style_set_shadow_color(&g_style_card, lv_color_hex(0x9A9A9A));
    lv_style_set_shadow_ofs_y(&g_style_card, 2);

    lv_style_init(&g_style_panel);
    lv_style_set_radius(&g_style_panel, 5);
    lv_style_set_bg_color(&g_style_panel, lv_color_hex(0xE8E8E8));
    lv_style_set_border_color(&g_style_panel, lv_color_hex(0xC0C0C0));
    lv_style_set_border_width(&g_style_panel, 1);

    lv_style_init(&g_style_button);
    lv_style_set_radius(&g_style_button, 6);
    lv_style_set_bg_color(&g_style_button, lv_color_hex(0xF0F0F0));
    lv_style_set_border_color(&g_style_button, lv_color_hex(0xD0D0D0));
    lv_style_set_border_width(&g_style_button, 1);
    lv_style_set_shadow_width(&g_style_button, 6);
    lv_style_set_shadow_color(&g_style_button, lv_color_hex(0xB0B0B0));
    lv_style_set_shadow_ofs_y(&g_style_button, 2);

    lv_style_init(&g_style_button_selected);
    lv_style_set_border_color(&g_style_button_selected, lv_color_hex(0x8C8C8C));
    lv_style_set_border_width(&g_style_button_selected, 2);

    lv_style_init(&g_style_icon);
    lv_style_set_text_color(&g_style_icon, lv_color_hex(0x1F1F1F));
    lv_style_set_text_font(&g_style_icon, &lv_font_montserrat_24);

    lv_style_init(&g_style_title);
    lv_style_set_text_color(&g_style_title, lv_color_hex(0x1F1F1F));
    lv_style_set_text_font(&g_style_title, &lv_font_montserrat_16);
    lv_style_set_text_letter_space(&g_style_title, 1);

    display::ScreenSize screen = get_screen_size();
    LayoutMetrics layout = compute_layout(screen);

    lv_obj_t *card = lv_obj_create(lv_screen_active());
    lv_obj_set_size(card, layout.card_w, layout.card_h);
    lv_obj_set_pos(card, layout.card_x, layout.card_y);
    lv_obj_add_style(card, &g_style_card, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(card);
    lv_obj_set_size(panel, layout.panel_w, layout.panel_h);
    lv_obj_set_pos(panel, layout.panel_x, layout.panel_y);
    lv_obj_add_style(panel, &g_style_panel, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t *btn = lv_button_create(card);
        g_buttons[i] = btn;
        lv_obj_set_size(btn, layout.btn_w_list[i], layout.btn_h);
        lv_obj_set_pos(btn, layout.panel_x + layout.btn_x[i], layout.panel_y + layout.btn_y);
        lv_obj_add_style(btn, &g_style_button, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, kIconText[i]);
        lv_obj_add_style(icon, &g_style_icon, LV_PART_MAIN);
        lv_obj_center(icon);
    }

    g_title_label = lv_label_create(card);
    lv_label_set_text(g_title_label, kTitleText[1]);
    lv_obj_add_style(g_title_label, &g_style_title, LV_PART_MAIN);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, layout.title_y);

    set_selected_index(1);
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

void loop()
{
    lv_timer_handler();
    delay(2);
}
