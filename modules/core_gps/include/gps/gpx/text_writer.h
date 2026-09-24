#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <cstdio>

namespace gps::gpx
{
struct ByteView { const std::uint8_t* data = nullptr; std::size_t size = 0; };
class OutputSink
{
  public:
    virtual ~OutputSink() = default;
    // Must consume all bytes before returning. Does not imply durable commit.
    virtual bool write(std::string_view bytes) = 0;
};

class GpxTextWriter
{
  public:
    explicit GpxTextWriter(OutputSink& sink, std::size_t limit = 65536)
        : sink_(sink), limit_(limit) {}
    bool good() const { return good_; }
    std::size_t size() const { return size_; }
    bool raw(std::string_view text)
    {
        if (!good_ || text.size() > limit_ - size_) return fail();
        if (text.empty()) return true;
        if (!sink_.write(text)) return fail();
        size_ += text.size();
        return true;
    }
    bool escaped(std::string_view text)
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char* replacement = nullptr;
            switch (text[i])
            {
            case '&':
                replacement = "&amp;";
                break;
            case '<':
                replacement = "&lt;";
                break;
            case '>':
                replacement = "&gt;";
                break;
            case '"':
                replacement = "&quot;";
                break;
            case '\'':
                replacement = "&apos;";
                break;
            default:
                break;
            }
            if (!replacement) continue;
            if (!raw(text.substr(start, i - start)) || !raw(replacement)) return false;
            start = i + 1;
        }
        return raw(text.substr(start));
    }
    bool coordinate(std::int32_t e7)
    {
        const std::int64_t signed_value = e7;
        const auto magnitude = static_cast<std::uint64_t>(signed_value < 0 ? -signed_value : signed_value);
        char text[32];
        const int n = std::snprintf(text, sizeof(text), "%s%llu.%07llu", e7 < 0 ? "-" : "",
                                    static_cast<unsigned long long>(magnitude / 10000000),
                                    static_cast<unsigned long long>(magnitude % 10000000));
        return n > 0 && static_cast<std::size_t>(n) < sizeof(text)
                   ? raw({text, static_cast<std::size_t>(n)})
                   : fail();
    }
    bool base64(ByteView bytes)
    {
        if (!bytes.data && bytes.size) return fail();
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t i = 0; i < bytes.size;)
        {
            const std::size_t remaining = bytes.size - i;
            const std::uint32_t a = bytes.data[i++];
            const std::uint32_t b = remaining > 1 ? bytes.data[i++] : 0;
            const std::uint32_t c = remaining > 2 ? bytes.data[i++] : 0;
            const char encoded[4] = {alphabet[a >> 2], alphabet[((a & 3) << 4) | (b >> 4)],
                                     remaining > 1 ? alphabet[((b & 15) << 2) | (c >> 6)] : '=',
                                     remaining > 2 ? alphabet[c & 63] : '='};
            if (!raw({encoded, sizeof(encoded)})) return false;
        }
        return good_;
    }

  private:
    bool fail()
    {
        good_ = false;
        return false;
    }
    OutputSink& sink_;
    std::size_t limit_;
    std::size_t size_ = 0;
    bool good_ = true;
};
} // namespace gps::gpx
