#pragma once

#include "lvgl.h"
#include "ui_presentation/key_verification/key_verification_snapshot.h"

#include <memory>

namespace ui::widgets
{
class ImeWidget;
}

namespace chat::ui
{

struct KeyVerificationModalRefs
{
    lv_obj_t* overlay = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* desc = nullptr;
    lv_obj_t* textarea = nullptr;
    lv_obj_t* error_label = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* prev_group = nullptr;
};

struct KeyVerificationModalCallbacks
{
    lv_event_cb_t submit = nullptr;
    lv_event_cb_t close = nullptr;
    lv_event_cb_t trust = nullptr;
    void* user_data = nullptr;
};

void destroyKeyVerificationModal(
    KeyVerificationModalRefs& refs,
    std::unique_ptr<::ui::widgets::ImeWidget>& ime,
    bool restore_group);

void renderKeyVerificationModal(
    lv_obj_t* parent,
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot,
    const KeyVerificationModalCallbacks& callbacks,
    KeyVerificationModalRefs& refs,
    std::unique_ptr<::ui::widgets::ImeWidget>& ime);

void clearKeyVerificationError(const KeyVerificationModalRefs& refs);

} // namespace chat::ui
