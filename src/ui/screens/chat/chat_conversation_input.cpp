#include <Arduino.h>
#include "chat_conversation_input.h"
#include "chat_conversation_components.h"

#define CHAT_CONV_INPUT_DEBUG 1
#if CHAT_CONV_INPUT_DEBUG
#define CHAT_CONV_INPUT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_CONV_INPUT_LOG(...)
#endif

namespace chat::ui::conversation::input {

static ChatConversationScreen* s_screen = nullptr;

namespace {
constexpr int kEncoderKeyRotateUp = 19;
constexpr int kEncoderKeyRotateDown = 20;
constexpr lv_coord_t kScrollStep = 24;
}

static void on_msg_list_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen) return;

    uint32_t key = lv_event_get_key(e);

    if (lv_group_t* g = lv_group_get_default()) {
        if (key == LV_KEY_ENTER) {
            lv_group_set_editing(g, !lv_group_get_editing(g));
            lv_event_stop_processing(e);
            return;
        }

        if (!lv_group_get_editing(g)) {
            return;
        }

        lv_coord_t delta = 0;
        if (key == LV_KEY_UP || key == kEncoderKeyRotateUp) {
            delta = -kScrollStep;
        } else if (key == LV_KEY_DOWN || key == kEncoderKeyRotateDown) {
            delta = kScrollStep;
        }
        if (delta != 0) {
            if (lv_obj_t* msg_list = screen->getMsgList()) {
                lv_obj_scroll_by(msg_list, 0, delta, LV_ANIM_OFF);
                lv_event_stop_processing(e);
            }
        }
    }
}

void init(ChatConversationScreen* screen)
{
    s_screen = screen;
    if (!s_screen) {
        CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (no screen)\n");
        return;
    }
    if (lv_group_t* g = lv_group_get_default()) {
        if (lv_obj_t* msg_list = s_screen->getMsgList()) {
            lv_group_add_obj(g, msg_list);
            lv_group_focus_obj(msg_list);
            lv_group_set_editing(g, true);
            lv_obj_add_event_cb(msg_list, on_msg_list_key, LV_EVENT_KEY, s_screen);
        }
        if (lv_obj_t* reply_btn = s_screen->getReplyBtn()) {
            lv_group_add_obj(g, reply_btn);
        }
        if (lv_obj_t* back_btn = s_screen->getBackBtn()) {
            lv_group_add_obj(g, back_btn);
        }
    }
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (group focus msg list)\n");
}

void cleanup()
{
    s_screen = nullptr;
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] cleanup\n");
}

} // namespace chat::ui::conversation::input
