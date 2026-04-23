#pragma once

#include "ui/app_screen.h"
#include "ui/localization.h"
#include "ui/theme/theme_registry.h"

namespace ui
{

class CallbackAppScreen : public AppScreen
{
  public:
    using SimpleCallback = void (*)(lv_obj_t* parent);
    using Callback = void (*)(void* user_data, lv_obj_t* parent);

    CallbackAppScreen(const char* stable_id,
                      const char* name,
                      const lv_image_dsc_t* icon,
                      SimpleCallback enter,
                      SimpleCallback exit)
        : stable_id_(stable_id), name_(name), icon_(icon), simple_enter_(enter), simple_exit_(exit)
    {
    }

    CallbackAppScreen(const char* stable_id,
                      const char* name,
                      ::ui::theme::AssetSlot icon_slot,
                      SimpleCallback enter,
                      SimpleCallback exit)
        : stable_id_(stable_id),
          name_(name),
          icon_slot_(icon_slot),
          icon_slot_bound_(true),
          simple_enter_(enter),
          simple_exit_(exit)
    {
    }

    CallbackAppScreen(const char* stable_id,
                      const char* name,
                      const lv_image_dsc_t* icon,
                      Callback enter,
                      Callback exit,
                      void* user_data = nullptr)
        : stable_id_(stable_id),
          name_(name),
          icon_(icon),
          callback_enter_(enter),
          callback_exit_(exit),
          user_data_(user_data)
    {
    }

    CallbackAppScreen(const char* stable_id,
                      const char* name,
                      ::ui::theme::AssetSlot icon_slot,
                      Callback enter,
                      Callback exit,
                      void* user_data = nullptr)
        : stable_id_(stable_id),
          name_(name),
          icon_slot_(icon_slot),
          icon_slot_bound_(true),
          callback_enter_(enter),
          callback_exit_(exit),
          user_data_(user_data)
    {
    }

    const char* stable_id() const override { return stable_id_; }
    const char* name() const override { return ::ui::i18n::tr(name_); }
    const lv_image_dsc_t* icon() const override
    {
        if (icon_slot_bound_)
        {
            return ::ui::theme::builtin_asset(icon_slot_);
        }
        return icon_;
    }

    void enter(lv_obj_t* parent) override
    {
        if (callback_enter_)
        {
            callback_enter_(user_data_, parent);
            return;
        }
        if (simple_enter_)
        {
            simple_enter_(parent);
        }
    }

    void exit(lv_obj_t* parent) override
    {
        if (callback_exit_)
        {
            callback_exit_(user_data_, parent);
            return;
        }
        if (simple_exit_)
        {
            simple_exit_(parent);
        }
    }

  private:
    const char* stable_id_ = nullptr;
    const char* name_ = nullptr;
    const lv_image_dsc_t* icon_ = nullptr;
    ::ui::theme::AssetSlot icon_slot_ = ::ui::theme::AssetSlot::BootLogo;
    bool icon_slot_bound_ = false;
    SimpleCallback simple_enter_ = nullptr;
    SimpleCallback simple_exit_ = nullptr;
    Callback callback_enter_ = nullptr;
    Callback callback_exit_ = nullptr;
    void* user_data_ = nullptr;
};

} // namespace ui
