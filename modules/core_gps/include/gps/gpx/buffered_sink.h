#pragma once
#include "gps/gpx/text_writer.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace gps::gpx
{
// Explicit finish is required. Destructor never hides an I/O failure by flushing.
class BufferedOutputSink final : public OutputSink
{
  public:
    explicit BufferedOutputSink(OutputSink& downstream) : downstream_(downstream) {}
    bool write(std::string_view bytes) override
    {
        if (!good_ || finished_) return false;
        while (!bytes.empty())
        {
            const auto n = std::min(bytes.size(), buffer_.size() - used_);
            std::memcpy(buffer_.data() + used_, bytes.data(), n);
            used_ += n;
            bytes.remove_prefix(n);
            if (used_ == buffer_.size() && !drain()) return false;
        }
        return true;
    }
    bool finish()
    {
        if (finished_) return good_;
        if (!drain()) return false;
        finished_ = true;
        return true;
    }

  private:
    bool drain()
    {
        if (!good_) return false;
        if (used_ && !downstream_.write({buffer_.data(), used_}))
        {
            good_ = false;
            return false;
        }
        used_ = 0;
        return true;
    }
    OutputSink& downstream_;
    std::array<char, 1024> buffer_{};
    std::size_t used_ = 0;
    bool good_ = true;
    bool finished_ = false;
};
} // namespace gps::gpx
