#include "platform/linux/http_download.h"
#include <algorithm>
#include <curl/curl.h>
#include <limits>
#include <memory>

namespace platform::linux_runtime
{
namespace
{
namespace http = platform::ui::http_client;
struct Transfer
{
    const http::Request& request;
    http::WriteCallback write;
    void* context;
    const std::atomic_bool* cancelled;
    uint32_t bytes = 0;
    uint32_t limit;
    const char* failure = nullptr;
};
size_t receive(char* data, size_t size, size_t count, void* context) noexcept
{
    auto& transfer = *static_cast<Transfer*>(context);
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const auto bytes = size * count;
    if (bytes > transfer.limit - transfer.bytes)
    {
        transfer.failure = "HTTP response exceeds size limit";
        return 0;
    }
    try
    {
        if ((transfer.cancelled && transfer.cancelled->load()) ||
            !transfer.write(reinterpret_cast<const uint8_t*>(data), bytes, transfer.context))
        {
            transfer.failure = "HTTP transfer cancelled or writer rejected data";
            return 0;
        }
    }
    catch (...)
    {
        transfer.failure = "HTTP writer failed";
        return 0;
    }
    transfer.bytes += static_cast<uint32_t>(bytes);
    return bytes;
}
int progress(void* context, curl_off_t total, curl_off_t, curl_off_t, curl_off_t) noexcept
{
    auto& transfer = *static_cast<Transfer*>(context);
    if (transfer.cancelled && transfer.cancelled->load()) return 1;
    if (total > transfer.limit)
    {
        transfer.failure = "HTTP response exceeds size limit";
        return 1;
    }
    try
    {
        if (transfer.request.progress)
            transfer.request.progress(transfer.bytes, total > 0 ? static_cast<uint32_t>(total) : 0, transfer.request.progress_context);
    }
    catch (...)
    {
        transfer.failure = "HTTP progress callback failed";
        return 1;
    }
    return 0;
}
} // namespace
bool http_download(const http::Request& request, http::WriteCallback write, void* context,
                   std::string& error, http::TransferStats* stats, const std::atomic_bool* cancelled)
{
    error.clear();
    if (stats) *stats = {};
    if (!request.url || !*request.url || !write || request.timeout_ms <= 0)
    {
        error = "Invalid HTTP request";
        return false;
    }
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (initialized != CURLE_OK || !curl)
    {
        error = "HTTP client unavailable";
        return false;
    }
    // Zero means no caller-specific limit; the host still bounds allocations.
    Transfer transfer{request, write, context, cancelled, 0, request.max_bytes ? request.max_bytes : 16U * 1024U * 1024U};
    curl_easy_setopt(curl.get(), CURLOPT_URL, request.url);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_ms));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min(request.timeout_ms, 10000)));
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    const auto result = curl_easy_perform(curl.get());
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (stats) *stats = {static_cast<int>(status), transfer.bytes};
    if (result != CURLE_OK || status < 200 || status >= 300)
    {
        error = transfer.failure ? transfer.failure : cancelled && cancelled->load() ? "HTTP transfer cancelled" :
                status >= 400 || result == CURLE_OK ? "HTTP " + std::to_string(status) : curl_easy_strerror(result);
        return false;
    }
    return true;
}
} // namespace platform::linux_runtime

namespace platform::ui::http_client
{
bool download(const Request& request, WriteCallback write, void* context, std::string& error, TransferStats* stats)
{
    return platform::linux_runtime::http_download(request, write, context, error, stats);
}
bool get_text(const Request& request, std::string& output, std::string& error, TransferStats* stats)
{
    output.clear();
    const bool ok = download(request, [](const uint8_t* data, size_t bytes, void* context)
                             { static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(data), bytes); return true; },
                             &output, error, stats);
    if (!ok) output.clear();
    return ok;
}
} // namespace platform::ui::http_client
