#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/storage/install_record.h"
int main()
{
    using namespace geocaching;
    uint8_t key[16]{}, cache[32]{}, revision[32]{}, file_hash[32]{}, bytes[256]{};
    revision[0] = 1;
    file_hash[0] = 2;
    protocol::CmpWriter writer(bytes, sizeof(bytes));
    if (!writer.array(6) || !writer.binary({cache, 32}) || !writer.binary({revision, 32}) || !writer.binary({file_hash, 32}) ||
        !writer.nil() || !writer.unsignedInteger(1) || !writer.unsignedInteger(0)) return 1;
    storage::InstallRecordView record;
    if (!storage::decodeInstallRecord({key, 16}, {bytes, writer.size()}, record) || record.new_file_hash.data[0] != 2) return 2;
    storage::CacheHeadView head;
    head.install_generation = 1;
    using Action = storage::InstallRecoveryAction;
    if (storage::classifyInstallRecovery({cache, 32}, head, record) != Action::VerifyFiles) return 3;
    head.install_generation = 2;
    if (storage::classifyInstallRecovery({cache, 32}, head, record) != Action::Superseded) return 4;
    head.install_generation = 1;
    record.generation = 2;
    if (storage::classifyInstallRecovery({cache, 32}, head, record) != Action::Inconsistent) return 5;
    record.generation = 1;
    record.phase = storage::InstallPhase::ConflictExternal;
    if (storage::classifyInstallRecovery({cache, 32}, head, record) != Action::ExternalConflict) return 6;
    record.phase = storage::InstallPhase::Installed;
    if (storage::classifyInstallRecovery({cache, 32}, head, record) != Action::Inconsistent) return 7;
    head.current_hash = {revision, 32};
    if (storage::classifyInstallRecovery({cache, 32}, head, record) != Action::VerifyFiles) return 8;
    for (size_t n = 0; n < writer.size(); ++n)
        if (storage::decodeInstallRecord({key, 16}, {bytes, n}, record) || record.cache_id.data) return 9;
    return 0;
}
