/**
 * @file lxmf_memory.h
 * @brief Memory ownership helpers for embedded LXMF runtime buffers.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "platform/memory/psram_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace chat::lxmf::runtime
{

// LXMF keeps its public buffer aliases while sharing the platform allocator.
template <typename T>
using PsramAllocator =
    ::platform::memory::PsramAllocator<T>;

using RuntimeByteBuffer = std::vector<uint8_t, PsramAllocator<uint8_t>>;
using RuntimeByteBufferList =
    std::vector<RuntimeByteBuffer, PsramAllocator<RuntimeByteBuffer>>;
using RuntimeByteSpanList =
    std::vector<::chat::lxmf::ByteSpan, PsramAllocator<::chat::lxmf::ByteSpan>>;
using RuntimeMapHash = std::array<uint8_t, 4>;
using RuntimeMapHashList =
    std::vector<RuntimeMapHash, PsramAllocator<RuntimeMapHash>>;
using ResourcePayloadBuffer = RuntimeByteBuffer;
using ResourcePayloadList = RuntimeByteBufferList;
using ResourceMetadataBuffer = RuntimeByteBuffer;
using ResourceBitmapBuffer = RuntimeByteBuffer;
using ResourceMapHashList = RuntimeMapHashList;
using PropagationIdList = RuntimeByteBufferList;
using PropagationMessageList = RuntimeByteBufferList;

inline bool appendRuntimeByteBufferCallback(const uint8_t* data,
                                            std::size_t len,
                                            void* context)
{
    auto* items = static_cast<RuntimeByteBufferList*>(context);
    if (!items || (!data && len != 0U))
    {
        return false;
    }

    RuntimeByteBuffer item;
    if (len != 0U)
    {
        item.assign(data, data + len);
    }
    items->push_back(std::move(item));
    return true;
}

inline RuntimeByteSpanList makeRuntimeByteSpans(
    const RuntimeByteBufferList& items)
{
    RuntimeByteSpanList spans;
    spans.reserve(items.size());
    for (const auto& item : items)
    {
        spans.push_back(::chat::lxmf::ByteSpan{item.data(), item.size()});
    }
    return spans;
}

inline ::chat::lxmf::ByteSpanList viewRuntimeByteSpans(
    const RuntimeByteSpanList& spans)
{
    return ::chat::lxmf::ByteSpanList{spans.data(), spans.size()};
}

} // namespace chat::lxmf::runtime
