#pragma once
#include "platform/ui/http_client_runtime.h"
#include <atomic>

namespace platform::linux_runtime
{
bool http_download(const platform::ui::http_client::Request& request,
                   platform::ui::http_client::WriteCallback write, void* context,
                   std::string& error, platform::ui::http_client::TransferStats* stats,
                   const std::atomic_bool* cancelled = nullptr);
} // namespace platform::linux_runtime
