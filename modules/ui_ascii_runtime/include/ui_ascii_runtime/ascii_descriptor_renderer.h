#pragma once

#include "ui_ascii_runtime/ascii_runtime_entry_adoption.h"
#include "ui_presentation/menu/menu_model.h"

#include <cstddef>

namespace trailmate::linux_sim
{

class AsciiDescriptorRenderer
{
  public:
    bool render(const AsciiRuntimeEntryAdoption& adoption);

    std::size_t lineCount() const noexcept;
    const char* line(std::size_t index) const noexcept;

  private:
    static constexpr std::size_t kMaxLineLength = 96;

    char lines_[ui::menu::MenuModel::kMaxItems][kMaxLineLength]{};
    std::size_t line_count_ = 0;
};

} // namespace trailmate::linux_sim
