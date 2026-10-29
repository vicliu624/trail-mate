#pragma once
#include "ui_presentation/geocaching/geocaching_source.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// Owns the native draft storage thread. UI methods only copy bounded state or
// submit work; read callbacks are synchronous and never retained by the worker.
class LinuxGeocachingSource final : public ::ui::geocaching::Source
{
  public:
    explicit LinuxGeocachingSource(std::filesystem::path database, std::filesystem::path storage_root = {});
    ~LinuxGeocachingSource() override;
    void snapshot(::ui::geocaching::Section, ::ui::geocaching::Snapshot&) override;
    void mapSnapshot(::ui::geocaching::Section, ::ui::geocaching::Snapshot&) override;
    bool localMapRevision(uint64_t&) override;
    void requestWindow(::ui::geocaching::Section, size_t offset, size_t count) override;
    bool item(::ui::geocaching::Section, size_t index, uint64_t generation, ::ui::geocaching::Item&) override;
    void refresh(::ui::geocaching::Section) override;
    void open(const ::ui::geocaching::Item&, uint64_t) override;
    bool readDetail(const std::array<uint8_t, 32>&, const std::array<uint8_t, 32>&,
                    void (*)(const ::ui::geocaching::DetailView&, void*), void*) override;
    void closeDetail() override;
    ::ui::geocaching::DraftReadStatus readDraft(const std::array<uint8_t, 16>&,
                                                void (*)(const ::ui::geocaching::DraftInput&, void*), void*) override;
    void cancelDraftRead(const std::array<uint8_t, 16>&) override;
    bool saveDraft(::ui::geocaching::DraftInput&) override;
    bool deleteDraft(const std::array<uint8_t, 16>&, uint64_t) override;
    ::ui::geocaching::DraftSaveStatus draftSaveStatus(const std::array<uint8_t, 16>&, uint64_t) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
