#pragma once
#include "platform/linux/oneseg_tv_session.h"
#include "ui/app_screen.h"
#include "ui/assets/radio_icons.h"

namespace platform::linux_runtime
{
// Target-owned independent TV application; all LVGL access stays on the UI thread.
class OneSegTvApp final : public AppScreen
{
  public:
    explicit OneSegTvApp(std::unique_ptr<OneSegTvSession>);
    ~OneSegTvApp() override;
    const char* stable_id() const override { return "oneseg_tv"; }
    const char* name() const override { return "1seg TV"; }
    const lv_image_dsc_t* icon() const override { return &oneseg_tv_icon; }
    void enter(lv_obj_t*) override;
    void exit(lv_obj_t*) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
