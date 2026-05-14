#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace ui
{

class TeamPositionPickerRenderer
{
  public:
    struct Callbacks
    {
        void (*on_icon_selected)(void* user_data, uint8_t icon_id) = nullptr;
        void (*on_cancel)(void* user_data) = nullptr;
        void* user_data = nullptr;
    };

    TeamPositionPickerRenderer(lv_obj_t* parent, const Callbacks& callbacks);
    ~TeamPositionPickerRenderer();

    bool open();
    void close(bool restore_group);
    bool isOpen() const;
    void updateHint(uint8_t icon_id);

  private:
    static constexpr std::size_t kMaxIcons = 8;

    struct IconEventCtx
    {
        TeamPositionPickerRenderer* renderer = nullptr;
        uint8_t icon_id = 0;
    };

    void handleIconFocused(uint8_t icon_id, lv_obj_t* target);
    void handleIconSelected(uint8_t icon_id);
    void handleCancelFocused();
    void handleCancel();
    void clearIconContexts();

    static void iconEventCb(lv_event_t* e);
    static void cancelEventCb(lv_event_t* e);

    lv_obj_t* parent_ = nullptr;
    Callbacks callbacks_{};

    IconEventCtx icon_ctxs_[kMaxIcons]{};
    std::size_t icon_ctx_count_ = 0;

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* desc_ = nullptr;
    lv_group_t* group_ = nullptr;
    lv_group_t* prev_group_ = nullptr;
};

} // namespace ui
} // namespace chat
