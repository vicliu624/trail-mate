#include "app/linux_tms_document.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using Document = trailmate::linux_app::LinuxTmsDocument;
namespace tms = app::tms;

static std::string without_record(std::string text, const char* key)
{
    const auto start = text.find(std::string("\n") + key + "=");
    assert(start != std::string::npos);
    const auto end = text.find('\n', start + 1);
    assert(end != std::string::npos);
    text.erase(start + 1, end - start);
    return text;
}

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--roundtrip"))
    {
        std::string input;
        char byte = 0;
        while (std::cin.get(byte))
        {
            if (input.size() == tms::kMaxDocumentBytes) return 2;
            input.push_back(byte);
        }
        auto document = Document::parse(input.data(), input.size(), tms::DocumentKind::Working);
        std::string output;
        if (!document || !document->write(tms::DocumentKind::Working, output)) return 3;
        std::cout << output;
        return std::cout.good() ? 0 : 4;
    }
    auto source = std::make_unique<Document>();
    std::strcpy(source->settings.locale, "en");
    source->settings.wifi_profile_count = 2;
    std::strcpy(source->settings.wifi_profiles[0].ssid, "open network");
    std::strcpy(source->settings.wifi_profiles[1].ssid, "private network");
    std::strcpy(source->settings.wifi_profiles[1].password, "secret %:=\n");
    source->compatibility.mt_ble.enabled = false;
    source->compatibility.mt_ble.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    phone::meshtastic::config_bridge::initDefaultModuleConfig(&source->compatibility.module, 0x123456);
    source->compatibility.module.mqtt.enabled = true;
    std::strcpy(source->compatibility.module.mqtt.address, "retained.example.test");
    std::string text;
    assert(source->write(tms::DocumentKind::Working, text));
    auto decoded = Document::parse(text.data(), text.size(), tms::DocumentKind::Working);
    assert(decoded && decoded->settings.wifi_profile_count == 2);
    assert(!std::strcmp(decoded->settings.wifi_profiles[1].password, "secret %:=\n"));
    assert(!decoded->compatibility.mt_ble.enabled);
    assert(decoded->compatibility.module.mqtt.enabled);
    assert(!std::strcmp(decoded->compatibility.module.mqtt.address, "retained.example.test"));
    std::string roundtrip;
    assert(decoded->write(tms::DocumentKind::Working, roundtrip) && roundtrip == text);
    assert(!Document::parse(text.data(), text.size(), tms::DocumentKind::Backup));
    assert(!Document::parse(nullptr, text.size(), tms::DocumentKind::Working));
    assert(!Document::parse(text.data(), 0, tms::DocumentKind::Working));
    assert(!Document::parse(text.data(), tms::kMaxDocumentBytes + 1, tms::DocumentKind::Working));

    for (const auto* key : {"ui.locale", "wifi.profile.0.password", "mt_ble.enabled",
                            "mt_module.chunk.0", "mt_module.chunk.1", "mt_module.chunk.2"})
    {
        const auto missing = without_record(text, key);
        assert(!Document::parse(missing.data(), missing.size(), tms::DocumentKind::Working));
    }
    auto damaged = text;
    damaged[damaged.size() / 2] = 0;
    assert(!Document::parse(damaged.data(), damaged.size(), tms::DocumentKind::Working));

    // A last chunk is bounded by the remaining protobuf buffer, even when a
    // hostile record would fit the nominal 240-byte chunk size.
    if (sizeof(source->compatibility.module_bytes) < 3 * tms::CompatibilityRecords::kModuleChunkBytes)
    {
        std::string chunk;
        tms::LineScratch scratch{};
        tms::RecordWriter writer({&chunk, [](void* context, const char* bytes, size_t length)
                                  {
                                      static_cast<std::string*>(context)->append(bytes, length);
                                      return true;
                                  }},
                                 scratch, nullptr);
        uint8_t oversized[240]{};
        assert(writer.blob("mt_module.chunk.2", oversized, sizeof(oversized)));
        damaged = without_record(text, "mt_module.chunk.2");
        damaged.insert(damaged.find("END\n"), chunk);
        assert(!Document::parse(damaged.data(), damaged.size(), tms::DocumentKind::Working));
    }
    damaged = text;
    damaged.insert(damaged.find("END\n"), "cellular.enabled=bool:1\n");
    assert(!Document::parse(damaged.data(), damaged.size(), tms::DocumentKind::Working));
    damaged = text;
    damaged.insert(damaged.find("END\n"), "mt_ble.enabled=bool:0\n");
    assert(!Document::parse(damaged.data(), damaged.size(), tms::DocumentKind::Working));

    // C6 is retained as data without introducing a Linux companion service.
    source->companion.seen = tms::c6::kComplete;
    assert(source->write(tms::DocumentKind::Backup, text));
    decoded = Document::parse(text.data(), text.size(), tms::DocumentKind::Backup);
    assert(decoded && decoded->companion.seen == tms::c6::kComplete);
    assert(decoded->write(tms::DocumentKind::Backup, roundtrip) && roundtrip == text);
    const auto partial_c6 = without_record(text, "c6_companion.wifi_sta_ssid");
    assert(!Document::parse(partial_c6.data(), partial_c6.size(), tms::DocumentKind::Backup));
    // Failed emission does not replace the last valid caller document.
    source->settings.screen_timeout_ms = 123;
    roundtrip = "preserved";
    assert(!source->write(tms::DocumentKind::Working, roundtrip) && roundtrip == "preserved");
    std::puts("Native TMS: full settings/network/compatibility documents, optional C6, strict rejection and roundtrip passed");
}
