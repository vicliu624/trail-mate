#pragma once

#include "ui_gtk_runtime/gtk_runtime_entry_adoption.h"
#include "ui_presentation/menu/menu_model.h"

#include <cstddef>

namespace trailmate::uconsole
{

struct GtkDescriptorPage
{
    const char* binding_id = nullptr;
    const char* label = nullptr;
    bool enabled = false;
};

class GtkDescriptorPageRegistry
{
  public:
    bool load(const gtk::GtkRuntimeEntryAdoption& adoption);

    std::size_t pageCount() const noexcept;
    const GtkDescriptorPage* pages() const noexcept;

  private:
    GtkDescriptorPage pages_[ui::menu::MenuModel::kMaxItems]{};
    std::size_t page_count_ = 0;
};

} // namespace trailmate::uconsole
