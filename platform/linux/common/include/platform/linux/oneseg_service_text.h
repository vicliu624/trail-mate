#pragma once
#include "platform/linux/oneseg_arib_text.h"
#include "platform/linux/oneseg_service_information.h"

namespace platform::linux_runtime
{
struct OneSegServiceText
{
    uint16_t service = 0;
    bool service_valid = false, present_valid = false, following_valid = false;
    OneSegAribTextResult name, provider, present_title, present_description, following_title, following_description;
};

// Owner-thread cache. Never call native character conversion under the UI lock.
class OneSegServiceTextCache
{
  public:
    void reset()
    {
        slots_ = {};
        identity_ = {};
        conversions_ = 0;
    }
    uint64_t conversionCount() const { return conversions_; }
    OneSegServiceText update(const OneSegServiceInformation::Snapshot& input)
    {
        const std::array<uint16_t, 3> identity{input.service, input.transport, input.network};
        if (identity != identity_)
        {
            slots_ = {};
            identity_ = identity;
        }
        OneSegServiceText result;
        result.service = input.service;
        result.service_valid = input.service_valid;
        result.present_valid = input.present.valid;
        result.following_valid = input.following.valid;
        result.name = get(0, input.name, input.service_valid);
        result.provider = get(1, input.provider, input.service_valid);
        result.present_title = get(2, input.present.title, input.present.valid);
        result.present_description = get(3, input.present.description, input.present.valid);
        result.following_title = get(4, input.following.title, input.following.valid);
        result.following_description = get(5, input.following.description, input.following.valid);
        return result;
    }

  private:
    struct Slot
    {
        OneSegServiceInformation::Text raw;
        OneSegAribTextResult decoded;
        bool initialized = false;
    };
    std::array<Slot, 6> slots_{};
    std::array<uint16_t, 3> identity_{};
    uint64_t conversions_ = 0;
    OneSegAribTextResult get(size_t index, const OneSegServiceInformation::Text& text, bool valid)
    {
        auto& slot = slots_[index];
        if (!valid)
        {
            slot = {};
            return {};
        }
        const bool same = slot.initialized && slot.raw.size == text.size &&
                          (text.size > text.bytes.size() || !std::memcmp(slot.raw.bytes.data(), text.bytes.data(), text.size));
        if (!same)
        {
            slot.raw = text;
            slot.initialized = true;
            if (text.size > text.bytes.size()) slot.decoded = {OneSegAribTextError::Malformed, {}};
            else
            {
                if (text.size) ++conversions_;
                slot.decoded = decode_oneseg_arib_si(text.bytes.data(), text.size);
            }
        }
        return slot.decoded;
    }
};
} // namespace platform::linux_runtime
