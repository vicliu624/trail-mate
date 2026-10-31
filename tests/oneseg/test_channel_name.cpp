#include "platform/linux/oneseg_channel_name.h"
#include "platform/linux/oneseg_channel_store.h"
#include <cassert>
#include <iostream>

using namespace platform::linux_runtime;
int main()
{
    OneSegServiceText text;
    text.service = 24;
    text.service_valid = true;
    text.name.text = "Received station";
    assert(oneseg_received_channel_name(text, 24) == "Received station");
    assert(oneseg_received_channel_name(text, 25).empty());
    text.service_valid = false;
    assert(oneseg_received_channel_name(text, 24).empty());
    text.service_valid = true;
    text.name.error = OneSegAribTextError::Unsupported;
    assert(oneseg_received_channel_name(text, 24).empty());
    text.name.error = OneSegAribTextError::None;
    text.name.text = std::string(192, 'A');
    assert(oneseg_received_channel_name(text, 24) == text.name.text);
    text.name.text = std::string(188, 'A') + "\xe6\x97\xa5\xe6\x9c\xac";
    const auto bounded = oneseg_received_channel_name(text, 24);
    assert(bounded == std::string(188, 'A') + "\xe2\x80\xa6");
    assert(bounded.size() <= 192);
    const auto folder = std::filesystem::temp_directory_path() / ("trailmate-tv-name-" + std::to_string(getpid()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "channels.txt";
    std::string error;
    OneSegChannelStore store(path);
    assert(store.rememberReceived({13, 24, {}}, error));
    assert(store.rememberReceived({13, 24, bounded}, error));
    OneSegChannelStore restart(path);
    assert(restart.load(error) && restart.lastChannel()->name == bounded);
    assert(restart.rememberReceived({13, 24, {}}, error));
    assert(restart.lastChannel()->name == bounded);
    std::filesystem::remove(path);
    std::filesystem::remove(folder);
    std::cout << "Received names: identity, conversion errors, UTF-8 boundary and persistence passed\n";
}
