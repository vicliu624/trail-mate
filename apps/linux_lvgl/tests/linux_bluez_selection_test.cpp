#include "platform/linux/bluez_adapters.h"
#include <cassert>
#include <cstdio>

int main(int argc, char** argv)
{
    using namespace platform::linux_runtime;
    using Status = BluezSelectionStatus;
    if (argc > 1)
    {
        if (argc != 2 || std::string(argv[1]) != "--system") return 2;
        const auto system = queryBluezAdapters();
        const auto selected = selectBluezAdapter(system);
        std::printf("queried=%d adapters=%zu selection=%u path=%s error=%s\n", system.queried,
                    system.adapters.size(), static_cast<unsigned>(selected.status), selected.adapter.path.c_str(), system.error.c_str());
        return selected.status == Status::Ready ? 0 : 1;
    }
    BluezAdapters listing;
    assert(selectBluezAdapter(listing).status == Status::Unavailable);
    listing.queried = true;
    assert(selectBluezAdapter(listing).status == Status::Missing);
    listing.adapters.push_back({"/org/bluez/hci9", "AB:CD:EF:01:02:03", "First controller", true, true, true});
    auto selected = selectBluezAdapter(listing);
    assert(selected.status == Status::Ready && selected.adapter.path == "/org/bluez/hci9");
    assert(selectBluezAdapter(listing, "ab:cd:ef:01:02:03").status == Status::Ready);
    listing.adapters.push_back({"/org/bluez/hci2", "AB:CD:EF:01:02:04", "Second controller", false, true, true});
    selected = selectBluezAdapter(listing);
    assert(selected.status == Status::Ambiguous && selected.adapter.path.empty());
    assert(selectBluezAdapter(listing, "AB:CD:EF:01:02:04").status == Status::PoweredOff);
    assert(selectBluezAdapter(listing, "AB:CD:EF:01:02:05").status == Status::Missing);
    assert(selectBluezAdapter(listing, "not-an-address").status == Status::InvalidAddress);
    listing.adapters[1].gatt = false;
    assert(selectBluezAdapter(listing).status == Status::Ready);
    assert(selectBluezAdapter(listing, "AB:CD:EF:01:02:04").status == Status::Unsupported);
    listing.adapters.erase(listing.adapters.begin());
    assert(selectBluezAdapter(listing).status == Status::Unsupported);
    listing.queried = false;
    assert(selectBluezAdapter(listing).status == Status::Unavailable);
}
