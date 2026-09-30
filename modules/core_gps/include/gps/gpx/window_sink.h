#pragma once
#include "gps/gpx/text_writer.h"
#include <cstring>

namespace gps::gpx
{
// A bounded window over the existing GPX serializer, with no file operations.
// A zero-capacity window counts output. The serializer remains the sole source
// of XML formatting/escaping; callers retain only one transfer-sized slice.
class WindowSink final : public OutputSink
{
  public:
    WindowSink(size_t offset, uint8_t* output, size_t capacity)
        : offset_(offset), capacity_(capacity), output_(output) {}
    WindowSink(size_t offset, std::string_view expected)
        : offset_(offset), capacity_(expected.size()), output_(nullptr), expected_(expected.data()) {}
    bool write(std::string_view bytes) override
    {
        if ((!bytes.data() && bytes.size()) || bytes.size() > SIZE_MAX - total_ || (!output_ && !expected_ && capacity_)) return false;
        const size_t start = total_;
        total_ += bytes.size();
        if (total_ <= offset_ || written_ == capacity_) return true;
        const size_t skip = offset_ > start ? offset_ - start : 0;
        const size_t available = bytes.size() - skip;
        const size_t count = available < capacity_ - written_ ? available : capacity_ - written_;
        if (count)
        {
            if (expected_)
            {
                if (std::memcmp(expected_ + written_, bytes.data() + skip, count)) return false;
            }
            else std::memcpy(output_ + written_, bytes.data() + skip, count);
        }
        written_ += count;
        return true;
    }
    size_t written() const { return written_; }
    size_t total() const { return total_; }

  private:
    size_t offset_, capacity_, written_ = 0, total_ = 0;
    uint8_t* output_;
    const char* expected_ = nullptr;
};
} // namespace gps::gpx
