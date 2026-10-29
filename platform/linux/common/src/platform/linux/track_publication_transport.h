#pragma once
#include "chat/ports/i_geocaching_transport.h"
#include <functional>
namespace platform::linux_runtime
{
using TrackPublicationTransport = std::function<bool(const uint8_t*, const uint8_t*, chat::lxmf::ByteSpan, chat::MessageId)>;
using TrackPublicationMessageSource = std::function<chat::MessageId()>;
} // namespace platform::linux_runtime
