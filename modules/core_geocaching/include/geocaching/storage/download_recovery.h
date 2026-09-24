#pragma once
#include "geocaching/protocol/query_response.h"
#include "geocaching/storage/install_record.h"
#include "geocaching/storage/task_record.h"
#include "geocaching/storage/transaction.h"
#include "geocaching/usecase/gpx_install.h"

namespace geocaching::storage
{
// A boot cursor is a stable request key, never an offset into mutable ledger
// bytes. Keep it until all candidates have been examined, including Installed
// tasks whose GPX history rename may not have finished before power loss.
struct DownloadRecoveryRequest
{
    std::array<uint8_t, 48> key{};
    std::array<uint8_t, 16> task{};
    InstallIdentity identity;
    StoredTime created;
    bool installed = false;
};
enum class DownloadRecoverySelection : uint8_t
{
    End,
    Found,
    Invalid
};

inline bool waitingDownloadEligible(ByteView key, const OutgoingView& outgoing, const TaskView& task, const CacheHeadView& head)
{
    return task.kind == 2 && task.state < 3 && task.continue_intent && outgoing.state < 4 && outgoing.continue_intent &&
           outgoing.install_generation && head.install_generation == outgoing.install_generation &&
           task.cache_id.size == 32 && task.revision_hash.size == 32 && requestBelongsToTask(outgoing.task_id, task, key, outgoing);
}

// The preview must come from the same local/directory pair as the durable Get.
// Its name borrows the query response until the caller releases the read lease.
inline bool findWaitingDownloadPreview(ByteView key, const OutgoingView& query, const DownloadRecoveryRequest& request,
                                       protocol::SummaryView& out)
{
    out = {};
    if (!key.data || key.size != request.key.size() || std::memcmp(key.data, request.key.data(), 32) || query.state != 4) return false;
    RequestId query_id;
    std::memcpy(query_id.bytes.data(), key.data + 32, 16);
    protocol::QueryPageView page;
    if (!protocol::decodeQueryPage(query.terminal_data, query_id, 8192, 64, page)) return false;
    protocol::CmpReader items(page.encoded_items);
    protocol::SummaryView summary;
    for (size_t i = 0; i < page.count; ++i)
    {
        if (!protocol::decodeSummary(items, summary)) return false;
        if (summary.id.bytes == request.identity.id.bytes && summary.hash.bytes == request.identity.hash.bytes)
        {
            out = summary;
            return true;
        }
    }
    return false;
}

template <class View>
DownloadRecoverySelection selectWaitingDownload(const View& view, const Destination& local, DownloadRecoveryRequest& out,
                                                protocol::SummaryView& preview)
{
    out = {};
    preview = {};
    bool found = false;
    size_t cursor = 0;
    MutationView row;
    while (view.next(cursor, row))
    {
        if (row.table != 5) continue;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        ByteView value;
        if (!decodeOutgoing(row.key, row.value, outgoing)) return DownloadRecoverySelection::Invalid;
        if (std::memcmp(row.key.data, local.bytes.data(), 16) || outgoing.state >= 4 || !outgoing.continue_intent || !outgoing.install_generation ||
            (found && std::memcmp(row.key.data, out.key.data(), out.key.size()) >= 0)) continue;
        if (!view.find(10, outgoing.task_id, value) || !decodeTask(outgoing.task_id, value, task)) return DownloadRecoverySelection::Invalid;
        if (task.kind != 2 || task.state >= 3 || !task.continue_intent) continue;
        if (!view.find(2, task.cache_id, value) || !decodeCacheHead(task.cache_id, value, head)) return DownloadRecoverySelection::Invalid;
        if (!waitingDownloadEligible(row.key, outgoing, task, head)) continue;
        std::memcpy(out.key.data(), row.key.data, out.key.size());
        std::memcpy(out.task.data(), outgoing.task_id.data, out.task.size());
        std::memcpy(out.identity.id.bytes.data(), task.cache_id.data, 32);
        std::memcpy(out.identity.hash.bytes.data(), task.revision_hash.data, 32);
        out.identity.generation = outgoing.install_generation;
        out.created = outgoing.created;
        found = true;
    }
    if (!found) return DownloadRecoverySelection::End;
    cursor = 0;
    while (view.next(cursor, row))
    {
        if (row.table != 5) continue;
        OutgoingView query;
        if (!decodeOutgoing(row.key, row.value, query)) return DownloadRecoverySelection::Invalid;
        if (findWaitingDownloadPreview(row.key, query, out, preview)) return DownloadRecoverySelection::Found;
    }
    return DownloadRecoverySelection::Invalid;
}

// Select the next request from an unordered view. Only the returned metadata is
// retained; no list or borrowed payload survives a state commit. Indexed owners
// can use the same eligibility rule while reading rows asynchronously.
inline bool downloadRecoveryEligible(ByteView key, const OutgoingView& outgoing,
                                     const TaskView& task, const CacheHeadView& head,
                                     const InstallRecordView* install)
{
    if (task.kind != 2 || outgoing.state != 4 || !outgoing.install_generation ||
        task.cache_id.size != 32 || task.revision_hash.size != 32 ||
        !requestBelongsToTask(outgoing.task_id, task, key, outgoing) ||
        head.install_generation != outgoing.install_generation) return false;
    if (task.state == 3)
        return install && install->phase == InstallPhase::Installed &&
               install->generation == outgoing.install_generation &&
               !std::memcmp(install->revision_hash.data, task.revision_hash.data, 32) &&
               classifyInstallRecovery(task.cache_id, head, *install) == InstallRecoveryAction::VerifyFiles;
    return task.state < 3 && task.continue_intent && outgoing.continue_intent &&
           (!install || (install->phase == InstallPhase::Prepared &&
                         install->generation == outgoing.install_generation &&
                         !std::memcmp(install->revision_hash.data, task.revision_hash.data, 32) &&
                         classifyInstallRecovery(task.cache_id, head, *install) == InstallRecoveryAction::VerifyFiles));
}

template <class View>
DownloadRecoverySelection nextDownloadRecovery(const View& view, ByteView after, DownloadRecoveryRequest& out)
{
    out = {};
    if (after.size && (!after.data || after.size != out.key.size())) return DownloadRecoverySelection::Invalid;
    bool found = false;
    size_t cursor = 0;
    MutationView row;
    while (view.next(cursor, row))
    {
        if (row.table != 5) continue;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        InstallRecordView install;
        ByteView value;
        if (!decodeOutgoing(row.key, row.value, outgoing)) return DownloadRecoverySelection::Invalid;
        if (outgoing.state != 4 || (after.size && std::memcmp(row.key.data, after.data, after.size) <= 0) ||
            (found && std::memcmp(row.key.data, out.key.data(), out.key.size()) >= 0)) continue;
        if (!view.find(10, outgoing.task_id, value) || !decodeTask(outgoing.task_id, value, task)) return DownloadRecoverySelection::Invalid;
        if (task.kind != 2 || task.state == 5) continue;
        if (!view.find(2, task.cache_id, value) || !decodeCacheHead(task.cache_id, value, head)) return DownloadRecoverySelection::Invalid;
        const bool has_install = view.find(12, outgoing.task_id, value);
        if (has_install && !decodeInstallRecord(outgoing.task_id, value, install)) return DownloadRecoverySelection::Invalid;
        if (!downloadRecoveryEligible(row.key, outgoing, task, head, has_install ? &install : nullptr)) continue;
        std::memcpy(out.key.data(), row.key.data, out.key.size());
        std::memcpy(out.task.data(), outgoing.task_id.data, out.task.size());
        std::memcpy(out.identity.id.bytes.data(), task.cache_id.data, 32);
        std::memcpy(out.identity.hash.bytes.data(), task.revision_hash.data, 32);
        out.identity.generation = outgoing.install_generation;
        out.created = outgoing.created;
        out.installed = task.state == 3;
        found = true;
    }
    return found ? DownloadRecoverySelection::Found : DownloadRecoverySelection::End;
}
} // namespace geocaching::storage
