#pragma once
#include "geocaching/domain/record.h"

namespace geocaching
{
enum class InstallStep : std::uint8_t
{
    CheckIntent,
    ValidateStagedFile,
    PrepareJournal,
    ReplaceFile,
    ValidateInstalledFile,
    CommitJournal,
    Complete,
    Failed,
    Cancelled,
};

enum class InstallEffectResult : std::uint8_t
{
    Complete,
    Pending,
    Failed,
    StaleIntent,
};

struct InstallIdentity
{
    GeocacheId id;
    RevisionHash hash;
    std::uint64_t generation = 0;
};

class GpxInstallPort
{
  public:
    virtual ~GpxInstallPort() = default;
    // Called repeatedly with the same step while Pending. Implementations must
    // coalesce rather than start duplicate work. Check generation under the
    // store owner for every mutation, not just CheckIntent. Failed replacement
    // must retain the prepared journal/backup for recovery, never delete it.
    virtual InstallEffectResult execute(InstallStep step, const InstallIdentity& identity) = 0;
};

// One bounded maintenance step per invocation. Staged GPX generation is done
// before starting; the store verifies it against the expected authenticated hash.
class GpxInstall
{
  public:
    explicit GpxInstall(InstallIdentity identity) : identity_(identity) {}
    InstallStep step() const { return step_; }
    bool installed() const { return step_ == InstallStep::Complete; }
    void advance(GpxInstallPort& port)
    {
        if (step_ >= InstallStep::Complete) return;
        if (identity_.generation == 0)
        {
            step_ = InstallStep::Failed;
            return;
        }
        switch (port.execute(step_, identity_))
        {
        case InstallEffectResult::Pending:
            return;
        case InstallEffectResult::Failed:
            step_ = InstallStep::Failed;
            return;
        case InstallEffectResult::StaleIntent:
            step_ = InstallStep::Cancelled;
            return;
        case InstallEffectResult::Complete:
            step_ = static_cast<InstallStep>(static_cast<unsigned>(step_) + 1);
            return;
        }
    }

  private:
    InstallIdentity identity_;
    InstallStep step_ = InstallStep::CheckIntent;
};
} // namespace geocaching
