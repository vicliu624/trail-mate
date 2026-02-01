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

namespace {
constexpr int kEncoderKeyRotateUp = 19;
constexpr int kEncoderKeyRotateDown = 20;
constexpr lv_coord_t kScrollStep = 24;
}

static void on_msg_list_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->isAlive()) return;

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

void init(ChatConversationScreen* screen, Binding* binding)
{
    if (!binding) {
        return;
    }
    binding->bound = false;
    binding->msg_list = screen ? screen->getMsgList() : nullptr;
    binding->reply_btn = screen ? screen->getReplyBtn() : nullptr;
    binding->back_btn = screen ? screen->getBackBtn() : nullptr;
    binding->group = lv_group_get_default();

    if (!screen) {
        CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (no screen)\n");
        return;
    }
    if (!binding->group) {
        CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (no group)\n");
        return;
    }

    if (binding->msg_list) {
        lv_group_add_obj(binding->group, binding->msg_list);
        lv_group_focus_obj(binding->msg_list);
        lv_group_set_editing(binding->group, true);
        lv_obj_add_event_cb(binding->msg_list, on_msg_list_key, LV_EVENT_KEY, screen);
    }
    if (binding->reply_btn) {
        lv_group_add_obj(binding->group, binding->reply_btn);
    }
    if (binding->back_btn) {
        lv_group_add_obj(binding->group, binding->back_btn);
    }
    binding->bound = true;
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (group focus msg list)\n");
}

void cleanup(Binding* binding)
{
    if (!binding || !binding->bound) {
        return;
    }
    if (binding->group) {
        if (binding->msg_list) {
            lv_group_remove_obj(binding->msg_list);
        }
        if (binding->reply_btn) {
            lv_group_remove_obj(binding->reply_btn);
        }
        if (binding->back_btn) {
            lv_group_remove_obj(binding->back_btn);
        }
    }
    binding->msg_list = nullptr;
    binding->reply_btn = nullptr;
    binding->back_btn = nullptr;
    binding->group = nullptr;
    binding->bound = false;
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] cleanup\n");
}

} // namespace chat::ui::conversation::input
