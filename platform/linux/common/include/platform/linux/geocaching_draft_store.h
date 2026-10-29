#pragma once
#include "geocaching/storage/draft_record.h"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace platform::linux_runtime
{
// Blocking storage port. Call only from the geocaching storage owner, never UI
// callbacks. Rows retain the core draft encoding; Linux owns transactions only.
class LinuxGeocachingDraftStore
{
  public:
    using Id = std::array<uint8_t, 16>;
    enum class Result
    {
        Ready,
        Missing,
        Conflict,
        Invalid,
        Corrupt,
        PublicationRequired,
        Forbidden,
        Error
    };
    struct Row
    {
        Id id{};
        std::vector<uint8_t> bytes;
        bool view(::geocaching::storage::DraftView& out) const;
    };
    struct Summary
    {
        Id id{};
        uint64_t generation = 0;
        uint8_t state = 0;
        std::array<char, 97> name{};
        int32_t latitude_e7 = 0, longitude_e7 = 0;
        bool has_coordinates = false;
    };
    struct Page
    {
        std::array<Summary, 4> rows{};
        size_t count = 0, total = 0;
    };
    struct Catalog
    {
        uint64_t generation = 0;
        size_t total = 0;
        Row row;
        bool present = false;
    };
    explicit LinuxGeocachingDraftStore(std::filesystem::path database);
    ~LinuxGeocachingDraftStore();
    LinuxGeocachingDraftStore(const LinuxGeocachingDraftStore&) = delete;
    LinuxGeocachingDraftStore& operator=(const LinuxGeocachingDraftStore&) = delete;
    Result open();
    Result read(const Id& id, Row& row);
    // Editor bytes must have no supplied identity metadata. Retain committed
    // author/base fields; a published draft needs publication retention support.
    Result save(const Id& id, uint64_t expected, ::geocaching::ByteView editor);
    Result erase(const Id& id, uint64_t expected);
    Result page(size_t offset, Page& out);
    Result catalog(uint32_t offset, bool bound, uint64_t expected, Catalog& out);
    Result catalogGeneration(uint64_t& out);
    // The row and exact retry receipt commit together. Retain at most one
    // receipt per draft; a later edit must never replay an older success.
    Result saveWeb(const Id&, uint64_t expected, ::geocaching::ByteView editor,
                   const uint8_t author[64], uint32_t generation, const Id& transfer, const uint8_t hash[32]);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
