#pragma once
#include "geocaching/storage/checkpoint.h"

namespace geocaching::storage
{
// Descriptors and their bytes remain immutable until the owner has written and
// verified the page. The owner supplies globally ordered live rows, not the
// bucket order of an index scan. No full-page buffer is retained here.
class CheckpointPageEncoding
{
  public:
    bool open(uint64_t index, const MutationView* entries, size_t count,
              const MutationView* previous = nullptr)
    {
        *this = {};
        if ((!entries && count) || count > 65536 ||
            (previous && (previous->table < 1 || previous->table > 13 ||
                          !previous->key.data || !previous->key.size || previous->key.size > 96))) return false;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& row = entries[i];
            if (row.table < 1 || row.table > 13 || row.erase || !row.key.data ||
                !row.key.size || row.key.size > 96 || row.value.size > 32768 ||
                (row.value.size && !row.value.data) ||
                (i && !checkpointKeyBefore(entries[i - 1], row)) ||
                (!i && previous && !checkpointKeyBefore(*previous, row))) return false;
        }
        entries_ = entries;
        count_ = count;
        index_ = index;
        valid_ = true;
        size_t total = 0;
        if (!visit([&](ByteView span)
                   {
                       if (span.size > 65536 - total) return false;
                       total += span.size;
                       return true; }))
        {
            *this = {};
            return false;
        }
        size_ = total;
        return true;
    }

    size_t size() const { return size_; }

    // Sink consumes each span synchronously. Prefix spans are temporary.
    template <class Sink>
    bool visit(Sink&& sink) const
    {
        if (!valid_) return false;
        uint8_t token[16];
        protocol::CmpWriter prefix(token, sizeof(token));
        if (!prefix.array(2) || !prefix.unsignedInteger(index_) ||
            !prefix.array(static_cast<uint32_t>(count_)) || !sink(ByteView{token, prefix.size()})) return false;
        for (size_t i = 0; i < count_; ++i)
        {
            const auto& row = entries_[i];
            protocol::CmpWriter key(token, sizeof(token));
            if (!key.array(3) || !key.unsignedInteger(row.table) || !key.binaryHeader(row.key.size) ||
                !sink(ByteView{token, key.size()}) || !sink(row.key)) return false;
            protocol::CmpWriter value(token, sizeof(token));
            if (!value.binaryHeader(row.value.size) || !sink(ByteView{token, value.size()}) ||
                (row.value.size && !sink(row.value))) return false;
        }
        return true;
    }

    bool readSlice(size_t offset, uint8_t* output, size_t capacity, size_t& written) const
    {
        written = 0;
        if (!valid_ || offset > size_ || (!output && capacity)) return false;
        if (!capacity || offset == size_) return true;
        const size_t available = size_ - offset;
        const size_t wanted = available < capacity ? available : capacity;
        const auto overlaps = [&](const void* data, size_t length)
        {
            if (!length) return false;
            const auto a = reinterpret_cast<uintptr_t>(data), b = reinterpret_cast<uintptr_t>(output);
            return a <= b ? b - a < length : a - b < wanted;
        };
        if (overlaps(this, sizeof(*this)) || overlaps(entries_, count_ * sizeof(MutationView))) return false;
        for (size_t i = 0; i < count_; ++i)
            if (overlaps(entries_[i].key.data, entries_[i].key.size) ||
                overlaps(entries_[i].value.data, entries_[i].value.size)) return false;
        size_t skip = offset;
        visit([&](ByteView span)
              {
                  if (skip >= span.size)
                  {
                      skip -= span.size;
                      return true;
                  }
                  const size_t remaining = span.size - skip;
                  const size_t take = remaining < wanted - written ? remaining : wanted - written;
                  std::memcpy(output + written, span.data + skip, take);
                  written += take;
                  skip = 0;
                  return written < wanted; });
        return written == wanted;
    }

  private:
    const MutationView* entries_ = nullptr;
    uint64_t index_ = 0;
    size_t count_ = 0, size_ = 0;
    bool valid_ = false;
};
static_assert(sizeof(CheckpointPageEncoding) <= 40, "Checkpoint encoder retains borrowed metadata only");

// Tail payload only; the storage owner adds the existing GCR1 wrapper. The
// digest is over complete page frames and must come from the actual writer.
inline bool encodeCheckpointTail(uint64_t pages, uint64_t entries, ByteView digest,
                                 uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || !digest.data || digest.size != 32) return false;
    // Preserve the small digest before writing so an overlapping input is safe.
    uint8_t copy[32];
    std::memcpy(copy, digest.data, sizeof(copy));
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(3) || !writer.unsignedInteger(pages) || !writer.unsignedInteger(entries) ||
        !writer.binary({copy, sizeof(copy)})) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
