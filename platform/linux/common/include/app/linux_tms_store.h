#pragma once
#include "app/linux_tms_document.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/linux/settings_transaction.h"
#include <array>
#include <vector>

namespace trailmate::linux_app
{
struct TmsCommitOptions
{
    std::array<uint8_t, 32> expected_base_hash{};
    // Bounded durable transfer receipt, committed in the same transaction.
    std::string receipt;
};
inline constexpr const char* kTmsTransferNamespace = platform::linux_runtime::settings_storage::kConfigTransferNamespace;
inline constexpr const char* kTmsTransferKey = "upload";
class LinuxTmsImport
{
  public:
    // Does not change persistent or live state. Rejects the complete document
    // before constructing any transaction mutations.
    static std::unique_ptr<LinuxTmsImport> prepare(const char*, size_t, app::tms::DocumentKind);
    // The application owner must exclude new frontend work/calls and arrange
    // orderly service teardown and restart after success. The atomic store
    // fence rejects concurrent old-process writes; failure leaves admission open.
    bool commit();
    enum class CommitResult
    {
        Committed,
        Conflict,
        StorageError
    };
    CommitResult commit(const TmsCommitOptions&);

  private:
    std::array<std::vector<platform::linux_runtime::SettingsMutation>, 4> mutations_;
};

// All independent settings, AppConfig, Wi-Fi and network bytes are read through
// one SQLite view. No runtime getters or recursively locking store calls occur
// inside its callback. Failure leaves the caller's document unchanged.
bool export_linux_tms_snapshot(app::tms::DocumentKind, std::string& output);
bool export_linux_tms_snapshot(const platform::linux_runtime::SettingsReadView&, app::tms::DocumentKind, std::string& output);
} // namespace trailmate::linux_app
