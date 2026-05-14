#include "ui_gps_runtime/gps_page_runtime_pump.h"

#include <cassert>

namespace
{

class FakeModel final : public ui::screens::gps::IGpsStatusRefreshModel
{
  public:
    void refresh() override
    {
        ++refresh_count;
    }

    int refresh_count = 0;
};

class FakeSink final : public ui::screens::gps::IGpsUiRefreshSink
{
  public:
    void onGpsRuntimeUpdated() override
    {
        ++notify_count;
    }

    int notify_count = 0;
};

} // namespace

int main()
{
    FakeModel model;
    FakeSink sink;
    ui::screens::gps::GpsPageRuntimePump pump(model, &sink, 500);

    pump.update(0);
    assert(model.refresh_count == 0);
    assert(sink.notify_count == 0);

    pump.setActive(true);
    assert(pump.active());
    pump.update(100);
    assert(model.refresh_count == 1);
    assert(sink.notify_count == 1);

    pump.update(400);
    assert(model.refresh_count == 1);
    assert(sink.notify_count == 1);

    pump.update(600);
    assert(model.refresh_count == 2);
    assert(sink.notify_count == 2);

    pump.setActive(false);
    assert(!pump.active());
    pump.update(2000);
    assert(model.refresh_count == 2);

    pump.setActive(true);
    pump.update(2100);
    assert(model.refresh_count == 3);
    assert(sink.notify_count == 3);

    FakeModel zero_model;
    ui::screens::gps::GpsPageRuntimePump zero_interval(zero_model, nullptr, 0);
    zero_interval.setActive(true);
    zero_interval.update(10);
    zero_interval.update(500);
    zero_interval.update(1010);
    assert(zero_model.refresh_count == 2);

    return 0;
}
