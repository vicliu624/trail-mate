#pragma once
#include "gps/gpx/text_writer.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

namespace platform::esp::arduino_common::gps
{
// Caller owns the opened staging file and the storage-maintenance lifecycle.
// Wrap with BufferedOutputSink, finish it, then flushFile before validation.
class SdGpxOutput final : public ::gps::gpx::OutputSink
{
  public:
    explicit SdGpxOutput(storage::SdRuntimeFile& file) : file_(file) {}
    bool good() const { return good_; }
    bool write(std::string_view bytes) override
    {
        if (!good_ || !file_.is_open())
        {
            good_ = false;
            return false;
        }
        if (file_.write(bytes.data(), bytes.size()) != bytes.size())
        {
            good_ = false;
            return false;
        }
        return true;
    }
    bool flushFile()
    {
        if (!good_ || !file_.is_open() || !file_.flush())
        {
            good_ = false;
            return false;
        }
        return true;
    }

  private:
    storage::SdRuntimeFile& file_;
    bool good_ = true;
};
} // namespace platform::esp::arduino_common::gps
