#include "platform/linux/oneseg_arib_text.h"
#include <array>
#include <cstring>
#include <memory>
extern "C"
{
#include <aribb24/aribb24.h>
#include <aribb24/decoder.h>
}

namespace platform::linux_runtime
{
OneSegAribTextError validate_oneseg_arib_si(const uint8_t* data, size_t size)
{
    using Error = OneSegAribTextError;
    if (size > 255) return Error::TooLong;
    if (!data && size) return Error::Malformed;
    std::array<unsigned, 4> width{2, 1, 1, 1};
    unsigned gl = 0, gr = 2;
    int single = -1;
    for (size_t pos = 0; pos < size;)
    {
        const auto c = data[pos++];
        if (c >= 0x21 && c <= 0x7e)
        {
            const auto bytes = width[single < 0 ? gl : static_cast<unsigned>(single)];
            single = -1;
            if (bytes == 2)
            {
                if (pos == size) return Error::Incomplete;
                if (data[pos] < 0x21 || data[pos] > 0x7e) return Error::Malformed;
                ++pos;
            }
            continue;
        }
        if (single >= 0) return Error::Malformed;
        if (c >= 0xa1 && c <= 0xfe)
        {
            if (width[gr] == 2)
            {
                if (pos == size) return Error::Incomplete;
                if (data[pos] < 0xa1 || data[pos] > 0xfe) return Error::Malformed;
                ++pos;
            }
            continue;
        }
        switch (c)
        {
        case 0x00:
        case 0x07:
        case 0x0a:
        case 0x0d:
        case 0x20:
        case 0x7f:
        case 0x88:
        case 0x89:
        case 0x8a:
            break;
        case 0x0e:
            gl = 1;
            break;
        case 0x0f:
            gl = 0;
            break;
        case 0x19:
            single = 2;
            break;
        case 0x1d:
            single = 3;
            break;
        case 0x1b:
        {
            if (pos == size) return Error::Incomplete;
            auto next = data[pos++];
            if (next == 0x6e)
            {
                gl = 2;
                break;
            }
            if (next == 0x6f)
            {
                gl = 3;
                break;
            }
            if (next == 0x7e)
            {
                gr = 1;
                break;
            }
            if (next == 0x7d)
            {
                gr = 2;
                break;
            }
            if (next == 0x7c)
            {
                gr = 3;
                break;
            }
            const bool two = next == 0x24;
            unsigned target = 0;
            if (two)
            {
                if (pos == size) return Error::Incomplete;
                next = data[pos++];
            }
            if (next >= 0x28 && next <= 0x2b)
            {
                target = next - 0x28;
                if (pos == size) return Error::Incomplete;
                next = data[pos++];
            }
            else if (!two) return Error::Malformed;
            if (next == 0x20)
                return pos == size ? Error::Incomplete : Error::Unsupported; // DRCS designation.
            if (two)
            {
                if (next != 0x42 && next != 0x39 && next != 0x3b) return Error::Unsupported;
            }
            else if (next != 0x4a && next != 0x30 && next != 0x31 && next != 0x36 && next != 0x37 && next != 0x38)
                return Error::Unsupported;
            width[target] = two ? 2 : 1;
            break;
        }
        default:
            return c == 0xff || c == 0xa0 ? Error::Malformed : Error::Unsupported;
        }
    }
    return single < 0 ? Error::None : Error::Incomplete;
}

OneSegAribTextResult decode_oneseg_arib_si(const uint8_t* data, size_t size)
{
    OneSegAribTextResult result;
    result.error = validate_oneseg_arib_si(data, size);
    if (!result || !size) return result;
    bool failed = false;
    std::unique_ptr<arib_instance_t, decltype(&arib_instance_destroy)> instance(arib_instance_new(&failed), arib_instance_destroy);
    if (!instance)
    {
        result.error = OneSegAribTextError::BackendUnavailable;
        return result;
    }
    instance->b_generate_drcs = false;
    instance->b_use_private_conv = false;
    arib_register_messages_callback(instance.get(), [](void* context, const char* message)
                                    { if (std::strstr(message,"could not decode ARIB string")) *static_cast<bool*>(context) = true; });
    auto* decoder = arib_get_decoder(instance.get());
    if (!decoder)
    {
        result.error = OneSegAribTextError::BackendUnavailable;
        return result;
    }
    arib_initialize_decoder(decoder); // SI initial sets; never caption A/C profiles.
    std::array<char, 4097> output{};
    const auto count = arib_decode_buffer(decoder, data, size, output.data(), output.size() - 1);
    if (failed || count >= output.size() - 1 || std::memchr(output.data(), 0, count))
    {
        result.error = OneSegAribTextError::Decode;
        return result;
    }
    result.text.assign(output.data(), count);
    return result;
}
} // namespace platform::linux_runtime
