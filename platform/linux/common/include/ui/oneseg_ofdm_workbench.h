#pragma once
#include "platform/linux/oneseg_ofdm_controls.h"
#include "platform/linux/oneseg_reception_settings.h"
#include "platform/linux/oneseg_tv_session.h"
#include <functional>
#include <lvgl.h>
#include <memory>

namespace platform::linux_runtime
{
class OneSegOfdmWorkbench
{
  public:
    struct Callbacks
    {
        std::function<void(const OneSegReceptionSettings&, const OneSegOfdmExperiment&)> apply;
        std::function<void(const OneSegReceptionSettings&, const OneSegOfdmExperiment&)> save;
        std::function<void()> load;
        std::function<void()> factory_reset;
    };
    explicit OneSegOfdmWorkbench(lv_obj_t* parent, lv_group_t* group, Callbacks);
    ~OneSegOfdmWorkbench();
    lv_obj_t* videoHost() const;
    lv_obj_t* object() const;
    void update(const OneSegTvSession::Snapshot&);
    void setWorking(const OneSegReceptionSettings&, const OneSegOfdmExperiment&);
    void fullscreen(bool);
    void notice(const char*);
    void selectStage(OneSegOfdmStage);
    void setRecordedSource(bool);
    void setSyntheticSource(bool);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
