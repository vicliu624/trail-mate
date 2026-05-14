#pragma once

namespace ui
{
namespace screens
{
namespace gps
{

class IGpsUiRefreshSink
{
  public:
    virtual ~IGpsUiRefreshSink() = default;

    virtual void onGpsRuntimeUpdated() = 0;
};

} // namespace gps
} // namespace screens
} // namespace ui
