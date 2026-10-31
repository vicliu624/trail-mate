#pragma once
#include "platform/linux/fm_session.h"
#include "ui/app_screen.h"
#include "ui/assets/radio_icons.h"

namespace platform::linux_runtime
{
class FmRadioApp final : public AppScreen
{
  public:
    explicit FmRadioApp(std::unique_ptr<FmSession>);
    ~FmRadioApp() override;
    const char* stable_id() const override { return "fm_radio"; }
    const char* name() const override { return "FM Radio"; }
    const lv_image_dsc_t* icon() const override { return &fm_radio_icon; }
    void enter(lv_obj_t*) override;
    void exit(lv_obj_t*) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::unique_ptr<FmSession> make_fm_session();
} // namespace platform::linux_runtime
