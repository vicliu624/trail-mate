#include "geocaching/usecase/gpx_install.h"
#include <cassert>
#include <vector>

struct Port : geocaching::GpxInstallPort
{
    std::vector<geocaching::InstallStep> calls;
    geocaching::InstallStep stop_at = geocaching::InstallStep::Complete;
    geocaching::InstallEffectResult stop_result = geocaching::InstallEffectResult::Failed;
    geocaching::InstallEffectResult execute(geocaching::InstallStep step, const geocaching::InstallIdentity&) override
    {
        calls.push_back(step);
        return step == stop_at ? stop_result : geocaching::InstallEffectResult::Complete;
    }
};
int main()
{
    using namespace geocaching;
    InstallIdentity identity;
    identity.generation = 1;
    GpxInstall install(identity);
    Port port;
    for (unsigned i = 0; i < 6; ++i)
    {
        assert(!install.installed());
        install.advance(port);
    }
    assert(install.installed());
    install.advance(port);
    assert(port.calls.size() == 6);
    for (unsigned i = 0; i < 6; ++i)
    {
        Port failure;
        failure.stop_at = static_cast<InstallStep>(i);
        GpxInstall rejected(identity);
        for (unsigned j = 0; j < 8; ++j) rejected.advance(failure);
        assert(!rejected.installed() && rejected.step() == InstallStep::Failed);
        assert(failure.calls.size() == i + 1);
    }
    Port cancelled;
    cancelled.stop_at = InstallStep::ReplaceFile;
    cancelled.stop_result = InstallEffectResult::StaleIntent;
    GpxInstall stale(identity);
    for (unsigned i = 0; i < 8; ++i) stale.advance(cancelled);
    assert(stale.step() == InstallStep::Cancelled && !stale.installed());
    Port pending;
    pending.stop_at = InstallStep::CheckIntent;
    pending.stop_result = InstallEffectResult::Pending;
    GpxInstall waiting(identity);
    waiting.advance(pending);
    assert(waiting.step() == InstallStep::CheckIntent);
}
