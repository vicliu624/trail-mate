#include "platform/ui/reticulum_call_runtime.h"
#include "sys/clock.h"
#include <cassert>
#include <cstring>
namespace
{
unsigned wake = 0, disabled = 0;
}
namespace platform::ui::screen
{
void wake_for_modal() { ++wake; }
void disable_sleep() { ++disabled; }
void enable_sleep() { --disabled; }
void record_activity() {}
} // namespace platform::ui::screen
int main()
{
    namespace call = platform::ui::reticulum_call;
    sys::set_millis_provider([]
                             { return 0U; });
    call::Peer failed;
    failed.destination_hash[0] = 2;
    failed.display_name = "Failed peer";
    assert(call::report_dial_failure(failed));
    const auto first = call::snapshot();
    assert(first.failed_dial_revision && first.state == call::State::Failed && !call::modal_active());
    assert(call::report_dial_failure(failed) && call::snapshot().failed_dial_revision != first.failed_dial_revision);
    call::service_ui_runtime();
    assert(!wake && !disabled);
    call::MediaHooks media;
    media.is_supported = []
    { return true; };
    media.start = []
    { return true; };
    media.stop = [] {};
    call::set_media_hooks(media);
    call::Peer current;
    current.link_id[0] = 3;
    current.destination_hash[0] = 4;
    assert(call::begin_outgoing(current));
    call::mark_link_active(current.link_id);
    assert(call::prepare_media(current.link_id) && call::mark_call_active(current.link_id));
    const auto active = call::snapshot();
    assert(!call::report_dial_failure(failed));
    assert(call::state() == call::State::Active && !std::memcmp(call::snapshot().link_id, active.link_id, 16));
    call::notify_link_closed(current.link_id);
    call::service_ui_runtime();
    assert(!disabled);
    sys::set_millis_provider(nullptr);
}
