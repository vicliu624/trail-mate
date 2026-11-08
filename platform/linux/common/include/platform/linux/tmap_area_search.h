#pragma once
#include "platform/ui/tmap_creation_runtime.h"
#include <memory>
#include <string>

namespace platform::linux_runtime
{
// Explicit searches only. HTTP, parsing and bounded caching run off the UI thread.
class TmapAreaSearch
{
  public:
    explicit TmapAreaSearch(std::string endpoint = "https://photon.komoot.io/api/");
    ~TmapAreaSearch();
    bool search(const std::string& query);
    ::platform::ui::tmap_creation::AreaSearchSnapshot snapshot();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
