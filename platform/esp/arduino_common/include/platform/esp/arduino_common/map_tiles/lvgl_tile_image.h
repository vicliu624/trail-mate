#pragma once

#include "lvgl.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <cstring>
#include <new>

namespace platform::esp::map_tiles
{

// A cache image owns either a completed decoder session or copied pixels.
// Keep ownership out of the cache policy and out of device/storage selection.
class LvglTileImage final : public lv_image_dsc_t
{
  public:
    // allocate_owner must return storage compatible with lv_free.
    static lv_image_dsc_t* capture(lv_image_decoder_dsc_t& decoder,
                                   void* (*allocate_owner)(size_t) = lv_malloc)
    {
        const lv_draw_buf_t* pixels = decoder.decoded;
        if (!pixels || !pixels->data || !pixels->data_size || !pixels->header.w || !pixels->header.h)
        {
            return nullptr;
        }
        void* storage = allocate_owner(sizeof(LvglTileImage));
        if (!storage) return nullptr;
        auto* image = new (storage) LvglTileImage();
        image->header = pixels->header;
        // Storage ownership belongs to this object, not to consumers of its view.
        image->header.flags &= ~LV_IMAGE_FLAGS_ALLOCATED;
        image->data_size = pixels->data_size;

        if (canRetainSession(decoder))
        {
            image->data = pixels->data;
            image->session_ = decoder;
            // LODEPNG has consumed all input during open; close only destroys
            // decoded. Keep a stable source descriptor, but not compressed data.
            image->source_ = *static_cast<const lv_image_dsc_t*>(decoder.src);
            image->source_.data = nullptr;
            image->source_.data_size = 0;
            image->session_.src = &image->source_;
            decoder = {}; // Move the session; the caller's close is now a no-op.
        }
        else
        {
            auto* copy = static_cast<uint8_t*>(lv_malloc(pixels->data_size));
            if (!copy)
            {
                image->~LvglTileImage();
                lv_free(image);
                return nullptr;
            }
            std::memcpy(copy, pixels->data, pixels->data_size);
            image->data = copy;
        }
        return image;
    }

    // Only descriptors returned by capture may be passed here.
    static void destroy(lv_image_dsc_t* descriptor)
    {
        if (!descriptor) return;
        lv_image_cache_drop(descriptor);
        auto* image = static_cast<LvglTileImage*>(descriptor);
        image->~LvglTileImage();
        lv_free(image);
    }

  private:
    LvglTileImage() : lv_image_dsc_t{} {}
    ~LvglTileImage()
    {
        if (session_.decoder)
            lv_image_decoder_close(&session_);
        else
            lv_free(const_cast<uint8_t*>(data));
    }
    LvglTileImage(const LvglTileImage&) = delete;
    LvglTileImage& operator=(const LvglTileImage&) = delete;

    static bool canRetainSession(const lv_image_decoder_dsc_t& decoder)
    {
        // Audited against the pinned LVGL release. Unknown decoders/versions
        // retain the copy path: they may borrow input or own additional state.
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 4 && LVGL_VERSION_PATCH == 0
        return decoder.decoder && decoder.decoder->name &&
               std::strcmp(decoder.decoder->name, "LODEPNG") == 0 &&
               decoder.src_type == LV_IMAGE_SRC_VARIABLE && decoder.src &&
               decoder.args.no_cache && !decoder.cache_entry && !decoder.user_data;
#else
        return false;
#endif
    }

    lv_image_decoder_dsc_t session_{};
    lv_image_dsc_t source_{};
};

} // namespace platform::esp::map_tiles
