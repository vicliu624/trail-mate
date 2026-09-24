#pragma once
#include "gps/gpx/text_writer.h"
#include <ostream>

namespace gps::gpx
{
class OstreamSink final : public OutputSink
{
  public:
    explicit OstreamSink(std::ostream& stream) : stream_(stream) {}
    bool write(std::string_view bytes) override
    {
        stream_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return stream_.good();
    }
  private:
    std::ostream& stream_;
};
} // namespace gps::gpx
