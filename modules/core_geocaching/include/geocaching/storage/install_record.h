#pragma once
#include "geocaching/storage/cache_head.h"
#include <cstring>

namespace geocaching::storage
{
enum class InstallPhase : uint8_t
{
    Prepared,
    Installed,
    ConflictExternal,
    RolledBack
};
struct InstallRecordView
{
    ByteView cache_id;
    ByteView revision_hash;
    ByteView new_file_hash;
    ByteView old_file_hash;
    uint64_t generation = 0;
    InstallPhase phase = InstallPhase::Prepared;
};
inline bool decodeInstallRecord(ByteView key, ByteView value, InstallRecordView& out)
{
    out = {};
    if (!key.data || key.size != 16 || !value.data || value.size > 32768) return false;
    protocol::CmpReader reader(value);
    InstallRecordView candidate;
    size_t fields = 0;
    uint64_t phase = 0;
    if (!reader.array(fields, 6) || fields != 6 || !reader.binary(candidate.cache_id, 32) || candidate.cache_id.size != 32 ||
        !reader.binary(candidate.revision_hash, 32) || candidate.revision_hash.size != 32 ||
        !reader.binary(candidate.new_file_hash, 32) || candidate.new_file_hash.size != 32) return false;
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(candidate.old_file_hash, 32) || candidate.old_file_hash.size != 32) return false;
    if (!reader.unsignedInteger(candidate.generation) || candidate.generation == 0 ||
        !reader.unsignedInteger(phase) || phase > 3 || !reader.finished()) return false;
    candidate.phase = static_cast<InstallPhase>(phase);
    out = candidate;
    return true;
}

inline bool encodeInstallRecord(ByteView key, const InstallRecordView& install, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(6) || !writer.binary(install.cache_id) || !writer.binary(install.revision_hash) ||
        !writer.binary(install.new_file_hash) || !(install.old_file_hash.size ? writer.binary(install.old_file_hash) : writer.nil()) ||
        !writer.unsignedInteger(install.generation) || !writer.unsignedInteger(static_cast<uint8_t>(install.phase))) return false;
    InstallRecordView checked;
    if (!decodeInstallRecord(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}

enum class InstallRecoveryAction : uint8_t
{
    VerifyFiles,
    Superseded,
    ExternalConflict,
    FinishedRollback,
    Inconsistent
};

// Classifies metadata only. VerifyFiles is not permission to rename: file hashes,
// signature mappings, and generation must still be checked at each mutation.
inline InstallRecoveryAction classifyInstallRecovery(ByteView head_key, const CacheHeadView& head,
                                                     const InstallRecordView& install)
{
    if (!head_key.data || head_key.size != 32 || !install.cache_id.data || install.cache_id.size != 32 ||
        std::memcmp(head_key.data, install.cache_id.data, 32) || !head.install_generation || !install.generation ||
        install.generation > head.install_generation) return InstallRecoveryAction::Inconsistent;
    if (install.generation < head.install_generation) return InstallRecoveryAction::Superseded;
    if (install.phase == InstallPhase::ConflictExternal) return InstallRecoveryAction::ExternalConflict;
    if (install.phase == InstallPhase::RolledBack) return InstallRecoveryAction::FinishedRollback;
    if (install.phase == InstallPhase::Installed &&
        (!head.current_hash.data || head.current_hash.size != 32 || !install.revision_hash.data || install.revision_hash.size != 32 ||
         std::memcmp(head.current_hash.data, install.revision_hash.data, 32))) return InstallRecoveryAction::Inconsistent;
    if (install.phase != InstallPhase::Prepared && install.phase != InstallPhase::Installed) return InstallRecoveryAction::Inconsistent;
    return InstallRecoveryAction::VerifyFiles;
}
} // namespace geocaching::storage
