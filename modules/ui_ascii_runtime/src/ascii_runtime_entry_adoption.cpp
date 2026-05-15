#include "ui_ascii_runtime/ascii_runtime_entry_adoption.h"

namespace trailmate::linux_sim
{

bool AsciiRuntimeEntryAdoption::load(
    const product_composition::PresentationBundle& presentation)
{
    ready_ = presenter_.load(presentation);
    return ready_;
}

bool AsciiRuntimeEntryAdoption::ready() const
{
    return ready_;
}

std::size_t AsciiRuntimeEntryAdoption::menuCount() const
{
    return ready_ ? presenter_.menuCount() : 0;
}

std::size_t AsciiRuntimeEntryAdoption::screenCount() const
{
    return ready_ ? presenter_.screenCount() : 0;
}

const AsciiRuntimeScreenGraphPresenter&
AsciiRuntimeEntryAdoption::presenter() const
{
    return presenter_;
}

} // namespace trailmate::linux_sim
