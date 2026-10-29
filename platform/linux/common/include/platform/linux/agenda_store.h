#pragma once
#include "agenda/ports/agenda_store.h"
#include <filesystem>
#include <memory>
namespace platform::linux_runtime
{
// Single worker owner. Ok means the durable commit completed. No record table
// is hydrated in RAM, and the SQLite connection must never be used by LVGL.
class LinuxAgendaStore final : public ::agenda::IAgendaStore
{
  public:
    explicit LinuxAgendaStore(std::filesystem::path database);
    ~LinuxAgendaStore() override;
    ::agenda::StoreResult begin();
    ::agenda::StoreResult readSlot(uint16_t, ::agenda::EventRecord&) override;
    ::agenda::StoreResult writeSlot(uint16_t, const ::agenda::EventRecord&) override;
    ::agenda::StoreResult eraseSlot(uint16_t) override;
    uint16_t slotCount() const override { return ::agenda::kMaxActiveEvents; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
