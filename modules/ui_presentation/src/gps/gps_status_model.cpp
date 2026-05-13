#include "ui_presentation/gps/gps_status_model.h"

namespace ui::gps
{

GpsStatusModel::GpsStatusModel(IGpsStatusSource& source)
    : source_(source)
{
}

GpsStatusSnapshot GpsStatusModel::snapshot() const
{
    GpsStatusSnapshot out{};
    if (!source_.buildGpsStatusSnapshot(out))
    {
        out.header.valid = false;
    }
    return out;
}

} // namespace ui::gps
