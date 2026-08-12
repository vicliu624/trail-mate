#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/infra/rnode/rnode_packet_wire.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_14
#endif

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

using Host = energy_sweep::ui::shell::Host;

namespace
{

constexpr lv_coord_t kPagerWidth = 480;
constexpr lv_coord_t kPagerHeight = 222;
constexpr lv_coord_t kPagerTopBarHeight = 30;
constexpr lv_coord_t kPagerBottomBarHeight = 24;
constexpr lv_coord_t kPagerOuterMargin = 10;
constexpr int kMaxCandidates = 24;
constexpr int kMaxObservations = 8;
constexpr std::size_t kPacketScratchSize = 255;
constexpr uint32_t kRefreshIntervalMs = 35;
constexpr uint32_t kActiveProfileDwellMs = 10000;
constexpr uint32_t kKnownProfileDwellMs = 2500;
constexpr uint32_t kObservedProfileDwellMs = 4200;
constexpr uint32_t kFullFrameGuardMs = 1200;
constexpr uint32_t kMeshCoreMinimumResponseWindowMs = 5000;
constexpr uint32_t kMeshtasticAckWindowMs = 12000;
constexpr uint32_t kMeshCorePassiveListenMs = 1100;
constexpr uint8_t kMeshCoreDirectRoute = 0x02;
constexpr uint8_t kMeshtasticDefaultPskIndex = 1;
constexpr uint32_t kMeshCoreDiscoverResponseDelaySlots = 20;
constexpr uint32_t kMeshCoreDiscoverResponseGuardMs = 2500;
constexpr std::size_t kMeshCoreDiscoverResponseFrameBytes =
    2 + chat::meshcore::kMeshCoreDiscoverResponseBasePayloadSize +
    chat::meshcore::kMeshCorePubKeySize;

constexpr uint32_t kColorAmber = 0xEBA341;
constexpr uint32_t kColorAmberDark = 0xC98118;
constexpr uint32_t kColorWarmBg = 0xFFF3DF;
constexpr uint32_t kColorPanelBg = 0xF8E6C3;
constexpr uint32_t kColorLine = 0xB3915D;
constexpr uint32_t kColorText = 0x593D1C;
constexpr uint32_t kColorTextDim = 0x846A42;
constexpr uint32_t kColorOk = 0x397046;
constexpr uint32_t kColorWarning = 0xA6422A;
constexpr uint32_t kColorInfo = 0x315F91;
constexpr uint32_t kColorFocus = 0xD59A36;

const Host* s_host = nullptr;

struct AirProfile
{
    float frequency_mhz = 0.0f;
    float bw_khz = 125.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    int8_t tx_power = 14;
    uint16_t preamble_len = 16;
    uint8_t sync_word = 0x12;
    uint8_t crc_len = 2;
};

enum class CandidateOrigin : uint8_t
{
    Active,
    Standard,
    Regional,
};

enum class EvidenceLevel : uint8_t
{
    None,
    Observed,
    Confirmed,
};

enum class VerificationKind : uint8_t
{
    None,
    MeshCoreDiscover,
    MeshtasticAck,
};

struct PacketEvidence
{
    EvidenceLevel level = EvidenceLevel::None;
    chat::NodeId source_node = 0;
    uint8_t channel_hash = 0;
};

struct ProbeVerification
{
    VerificationKind kind = VerificationKind::None;
    int candidate_index = -1;
    uint32_t ticket = 0;
    chat::NodeId target_node = 0;
    uint8_t channel_hash = 0;
    uint32_t started_ms = 0;
    uint32_t deadline_ms = 0;
};

struct ProbeCandidate
{
    AirProfile profile{};
    CandidateOrigin origin = CandidateOrigin::Standard;
};

struct RadioContext
{
    bool supported_protocol = false;
    bool acquired = false;
    chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
    AirProfile base_profile{};
    int candidate_count = 0;
    std::array<ProbeCandidate, kMaxCandidates> candidates{};
};

struct ProbeObservation
{
    AirProfile profile{};
    EvidenceLevel level = EvidenceLevel::None;
    chat::NodeId last_mt_source = 0;
    uint8_t last_mt_channel_hash = 0;
    uint32_t evidence_count = 0;
    uint32_t first_seen_ms = 0;
    uint32_t last_seen_ms = 0;
    float last_rssi_dbm = 0.0f;
    float last_snr_db = 0.0f;
};

struct ProbeState
{
    bool scanning = false;
    bool radio_error = false;
    bool applied = false;
    bool confirmation_open = false;
    bool confirm_apply_selected = false;
    int candidate_index = 0;
    int checked_in_pass = 0;
    uint32_t completed_passes = 0;
    uint32_t candidate_started_ms = 0;
    int selected_observation = 0;
    int visible_page_start = 0;
    int observation_count = 0;
    uint32_t crc_frame_count = 0;
    std::array<bool, kMaxCandidates> verification_attempted{};
    ProbeVerification verification{};
    std::array<ProbeObservation, kMaxObservations> observations{};
    std::array<uint8_t, kPacketScratchSize> receive_scratch{};
    std::array<uint8_t, kPacketScratchSize> protocol_scratch{};
    std::array<uint8_t, kPacketScratchSize> plaintext_scratch{};
    std::array<uint8_t, kPacketScratchSize> transmit_scratch{};
    std::array<uint8_t, chat::kMeshtasticChannelKeyMaxLen> mt_key_scratch{};
    meshtastic_Data mt_data_scratch = meshtastic_Data_init_zero;
    meshtastic_Routing mt_routing_scratch = meshtastic_Routing_init_zero;
};

struct PacketProbeLayout
{
    lv_coord_t screen_w = kPagerWidth;
    lv_coord_t screen_h = kPagerHeight;
    lv_coord_t topbar_h = kPagerTopBarHeight;
    lv_coord_t bottom_bar_h = kPagerBottomBarHeight;
    lv_coord_t work_top = kPagerTopBarHeight;
    lv_coord_t work_bottom = kPagerHeight - kPagerBottomBarHeight;
    lv_coord_t content_x = kPagerOuterMargin;
    lv_coord_t content_w = kPagerWidth - (kPagerOuterMargin * 2);
    bool pager = true;
    bool compact = true;
};

struct PacketProbeUi
{
    lv_obj_t* root = nullptr;
    ::ui::widgets::TopBar top_bar = {};
    lv_obj_t* content_area = nullptr;
    lv_obj_t* state_label = nullptr;
    lv_obj_t* empty_label = nullptr;
    std::array<lv_obj_t*, kMaxObservations> result_rows{};
    std::array<lv_obj_t*, kMaxObservations> result_primary{};
    std::array<lv_obj_t*, kMaxObservations> result_secondary{};
    std::array<lv_obj_t*, kMaxObservations> result_state{};
    std::array<lv_obj_t*, kMaxObservations> result_count{};
    lv_obj_t* progress_label = nullptr;
    lv_obj_t* bottom_bar = nullptr;
    lv_obj_t* stop_button = nullptr;
    lv_obj_t* stop_label = nullptr;
    lv_obj_t* set_button = nullptr;
    lv_obj_t* set_label = nullptr;
    lv_obj_t* confirmation = nullptr;
    lv_obj_t* confirm_cancel = nullptr;
    lv_obj_t* confirm_apply = nullptr;
};

struct PacketProbePageState
{
    PacketProbeUi ui{};
    ProbeState state{};
    RadioContext radio{};
    PacketProbeLayout layout{};
};

PacketProbePageState* s_page_state = nullptr;

#define s_ui (s_page_state->ui)
#define s_state (s_page_state->state)
#define s_radio (s_page_state->radio)
#define s_layout (s_page_state->layout)

lv_timer_t* s_refresh_timer = nullptr;
lv_timer_t* s_text_scan_timer = nullptr;
bool s_text_runtime_active = false;

PacketProbePageState* allocate_page_state()
{
#if defined(ESP_PLATFORM)
    void* storage = heap_caps_malloc(sizeof(PacketProbePageState),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* storage = std::malloc(sizeof(PacketProbePageState));
#endif
    if (!storage)
    {
        return nullptr;
    }
    return new (storage) PacketProbePageState{};
}

bool ensure_page_state()
{
    if (s_page_state)
    {
        return true;
    }

    s_page_state = allocate_page_state();
    if (!s_page_state)
    {
        std::printf("[UI][EnergySweep] enter denied reason=psram_state_alloc bytes=%u\n",
                    static_cast<unsigned>(sizeof(PacketProbePageState)));
        return false;
    }
    return true;
}

void release_page_state()
{
    if (!s_page_state)
    {
        return;
    }

    s_page_state->~PacketProbePageState();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_page_state);
#else
    std::free(s_page_state);
#endif
    s_page_state = nullptr;
}

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

int active_candidate_count()
{
    return std::clamp(s_radio.candidate_count, 0, kMaxCandidates);
}

int clamp_observation_index(int index)
{
    if (s_state.observation_count <= 0)
    {
        return 0;
    }
    return std::clamp(index, 0, s_state.observation_count - 1);
}

int visible_result_capacity()
{
    return 4;
}

chat::MeshProtocol normalize_probe_protocol(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::RNode ? chat::MeshProtocol::Reticulum
                                                 : protocol;
}

const char* protocol_tag(chat::MeshProtocol protocol)
{
    switch (normalize_probe_protocol(protocol))
    {
    case chat::MeshProtocol::MeshCore:
        return "MC";
    case chat::MeshProtocol::Reticulum:
        return "RT";
    case chat::MeshProtocol::Meshtastic:
    default:
        return "MT";
    }
}

const ProbeCandidate& current_candidate()
{
    const int count = active_candidate_count();
    const int index = count > 0 ? std::clamp(s_state.candidate_index, 0, count - 1) : 0;
    return s_radio.candidates[index];
}

bool same_profile(const AirProfile& lhs, const AirProfile& rhs);

bool profile_is_observed(const AirProfile& profile)
{
    for (int index = 0; index < s_state.observation_count; ++index)
    {
        const AirProfile& seen = s_state.observations[index].profile;
        if (std::fabs(seen.frequency_mhz - profile.frequency_mhz) < 0.0005f &&
            std::fabs(seen.bw_khz - profile.bw_khz) < 0.01f && seen.sf == profile.sf &&
            seen.cr == profile.cr && seen.preamble_len == profile.preamble_len &&
            seen.sync_word == profile.sync_word && seen.crc_len == profile.crc_len)
        {
            return true;
        }
    }
    return false;
}

bool profile_is_confirmed(const AirProfile& profile)
{
    for (int index = 0; index < s_state.observation_count; ++index)
    {
        const ProbeObservation& observation = s_state.observations[index];
        if (observation.level == EvidenceLevel::Confirmed &&
            same_profile(observation.profile, profile))
        {
            return true;
        }
    }
    return false;
}

uint32_t maximum_frame_dwell_ms(const AirProfile& profile)
{
    const int sf = std::clamp<int>(profile.sf, 5, 12);
    const int coding_rate_denom = std::clamp<int>(profile.cr, 5, 8);
    const double bandwidth_hz = std::max(1.0, static_cast<double>(profile.bw_khz) * 1000.0);
    const bool low_data_rate_optimize = sf >= 11 && profile.bw_khz <= 125.0f;
    const int denominator = 4 * (sf - (low_data_rate_optimize ? 2 : 0));
    const int crc_bits = profile.crc_len > 0 ? 16 : 0;
    const double payload_term =
        (8.0 * static_cast<double>(kPacketScratchSize) - (4.0 * static_cast<double>(sf)) +
         28.0 + static_cast<double>(crc_bits)) /
        static_cast<double>(denominator);
    const double payload_symbols =
        8.0 + (std::max(0.0, std::ceil(payload_term)) * static_cast<double>(coding_rate_denom));
    const double symbol_ms = std::ldexp(1.0, sf) * 1000.0 / bandwidth_hz;
    const double frame_ms = (static_cast<double>(profile.preamble_len) + 4.25 + payload_symbols) *
                            symbol_ms;
    return static_cast<uint32_t>(std::ceil(frame_ms)) + kFullFrameGuardMs;
}

uint32_t meshcore_discover_response_window_ms(const AirProfile& profile)
{
    // MeshCore peers schedule a Discover response in one of twenty airtime
    // slots (random 1..5, multiplied by four). The current adapter estimates
    // that slot with an eight-symbol preamble, while this probe receives using
    // the candidate's actual preamble length.
    const float peer_slot_airtime_ms = chat::meshcore::estimateLoRaAirtimeMs(
        kMeshCoreDiscoverResponseFrameBytes, profile.bw_khz, profile.sf, profile.cr);
    if (!std::isfinite(peer_slot_airtime_ms) || peer_slot_airtime_ms <= 0.0f)
    {
        return kMeshCoreMinimumResponseWindowMs;
    }

    const double symbol_ms =
        std::ldexp(1.0, std::clamp<int>(profile.sf, 5, 12)) * 1000.0 /
        std::max(1.0, static_cast<double>(profile.bw_khz) * 1000.0);
    const double preamble_adjustment_ms =
        std::max(0, static_cast<int>(profile.preamble_len) - 8) * symbol_ms;
    const uint64_t response_slot_ms = std::max<uint64_t>(
        1, static_cast<uint64_t>(std::llround((peer_slot_airtime_ms * 52.0f / 50.0f) / 2.0f)));
    const uint64_t response_window_ms =
        (response_slot_ms * kMeshCoreDiscoverResponseDelaySlots) +
        static_cast<uint64_t>(std::ceil(peer_slot_airtime_ms + preamble_adjustment_ms)) +
        kMeshCoreDiscoverResponseGuardMs;
    return static_cast<uint32_t>(std::min<uint64_t>(
        std::numeric_limits<uint32_t>::max(),
        std::max<uint64_t>(kMeshCoreMinimumResponseWindowMs, response_window_ms)));
}

uint32_t candidate_dwell_ms(const ProbeCandidate& candidate)
{
    const uint32_t policy_dwell =
        profile_is_observed(candidate.profile)
            ? kObservedProfileDwellMs
            : (candidate.origin == CandidateOrigin::Active ? kActiveProfileDwellMs
                                                           : kKnownProfileDwellMs);
    // A short fixed visit cannot receive a long low-rate frame that starts
    // shortly after retuning. Keep RX through one maximum protocol-sized frame.
    return std::max(policy_dwell, maximum_frame_dwell_ms(candidate.profile));
}

PacketProbeLayout resolve_layout(lv_obj_t* parent)
{
    if (parent)
    {
        lv_obj_update_layout(parent);
    }

    PacketProbeLayout layout{};
    const lv_coord_t parent_w = parent ? lv_obj_get_width(parent) : 0;
    const lv_coord_t parent_h = parent ? lv_obj_get_height(parent) : 0;
    const bool pager = parent_w <= 0 || parent_h <= 0 ||
                       (parent_w == kPagerWidth && parent_h == kPagerHeight);
    const bool compact = pager || (parent_w <= 360 && parent_h <= 280);
    layout.pager = pager;
    layout.compact = compact;
    layout.screen_w = pager ? kPagerWidth : parent_w;
    layout.screen_h = pager ? kPagerHeight : parent_h;
    layout.topbar_h = compact ? kPagerTopBarHeight : ::ui::page_profile::current().top_bar_height;
    layout.bottom_bar_h = compact ? kPagerBottomBarHeight : 34;
    layout.work_top = layout.topbar_h;
    layout.work_bottom = layout.screen_h - layout.bottom_bar_h;

    const lv_coord_t margin = compact ? kPagerOuterMargin : 18;
    layout.content_x = margin;
    layout.content_w = layout.screen_w - (margin * 2);
    return layout;
}

platform::ui::lora::ReceiveConfig receive_config_for(const AirProfile& profile)
{
    platform::ui::lora::ReceiveConfig config{};
    config.bw_khz = profile.bw_khz;
    config.sf = profile.sf;
    config.cr = profile.cr;
    config.tx_power = profile.tx_power;
    config.preamble_len = profile.preamble_len;
    config.sync_word = profile.sync_word;
    config.crc_len = profile.crc_len;
    return config;
}

bool same_profile(const AirProfile& lhs, const AirProfile& rhs)
{
    return std::fabs(lhs.frequency_mhz - rhs.frequency_mhz) < 0.0005f &&
           std::fabs(lhs.bw_khz - rhs.bw_khz) < 0.01f && lhs.sf == rhs.sf &&
           lhs.cr == rhs.cr && lhs.preamble_len == rhs.preamble_len &&
           lhs.sync_word == rhs.sync_word && lhs.crc_len == rhs.crc_len;
}

bool add_candidate(const AirProfile& profile, CandidateOrigin origin)
{
    if (!std::isfinite(profile.frequency_mhz) || profile.frequency_mhz <= 0.0f ||
        !std::isfinite(profile.bw_khz) || profile.bw_khz <= 0.0f ||
        s_radio.candidate_count >= kMaxCandidates)
    {
        return false;
    }

    for (int index = 0; index < s_radio.candidate_count; ++index)
    {
        if (same_profile(s_radio.candidates[index].profile, profile))
        {
            if (origin == CandidateOrigin::Active)
            {
                s_radio.candidates[index].origin = CandidateOrigin::Active;
            }
            return false;
        }
    }

    ProbeCandidate& candidate = s_radio.candidates[s_radio.candidate_count++];
    candidate.profile = profile;
    candidate.origin = origin;
    return true;
}

AirProfile profile_from_meshtastic(const chat::meshtastic::RadioConfig& radio)
{
    AirProfile profile{};
    profile.frequency_mhz = radio.freq_mhz;
    profile.bw_khz = radio.bw_khz;
    profile.sf = radio.sf;
    profile.cr = radio.cr_denom;
    profile.tx_power = radio.tx_power_dbm;
    profile.preamble_len = radio.preamble_len;
    profile.sync_word = radio.sync_word;
    profile.crc_len = radio.crc_len;
    return profile;
}

AirProfile profile_from_meshcore(const chat::MeshConfig& mesh)
{
    AirProfile profile{};
    profile.frequency_mhz = mesh.meshcore_freq_mhz;
    profile.bw_khz = mesh.meshcore_bw_khz;
    profile.sf = std::clamp<uint8_t>(mesh.meshcore_sf, 5, 12);
    profile.cr = std::clamp<uint8_t>(mesh.meshcore_cr, 5, 8);
    profile.tx_power = mesh.tx_power;
    profile.preamble_len = 16;
    profile.sync_word = 0x12;
    profile.crc_len = 2;

    if (mesh.meshcore_region_preset > 0)
    {
        if (const auto* preset = chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset))
        {
            profile.frequency_mhz = preset->freq_mhz;
            profile.bw_khz = preset->bw_khz;
            profile.sf = preset->sf;
            profile.cr = preset->cr;
            profile.tx_power = preset->tx_power_dbm;
        }
    }
    return profile;
}

AirProfile profile_from_reticulum(const chat::MeshConfig& mesh)
{
    AirProfile profile{};
    profile.frequency_mhz = mesh.override_frequency_mhz > 0.0f
                                ? mesh.override_frequency_mhz
                                : app::AppConfig::kRNodeDefaultFreqMHz;
    profile.bw_khz = std::isfinite(mesh.bandwidth_khz) && mesh.bandwidth_khz > 0.0f
                         ? mesh.bandwidth_khz
                         : app::AppConfig::kRNodeDefaultBwKHz;
    profile.sf = std::clamp<uint8_t>(mesh.spread_factor, 5, 12);
    profile.cr = std::clamp<uint8_t>(mesh.coding_rate, 5, 8);
    profile.tx_power = mesh.tx_power;
    profile.sync_word = 0x12;
    profile.crc_len = 2;
    const uint32_t bandwidth_hz = static_cast<uint32_t>(std::round(profile.bw_khz * 1000.0f));
    profile.preamble_len = chat::rnode::recommendPreambleSymbols(bandwidth_hz,
                                                                 profile.sf,
                                                                 profile.cr);
    return profile;
}

void add_meshtastic_standard_candidates(const app::AppConfig& config)
{
    constexpr std::array<meshtastic_Config_LoRaConfig_ModemPreset, 9> kPresets = {
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
    };

    for (const auto preset : kPresets)
    {
        const chat::meshtastic::RadioConfig radio =
            chat::meshtastic::deriveRadioConfigForModemPreset(config.meshtastic_config,
                                                              preset);
        (void)add_candidate(profile_from_meshtastic(radio), CandidateOrigin::Standard);
    }
}

bool same_frequency_family(float lhs_mhz, float rhs_mhz)
{
    return std::fabs(lhs_mhz - rhs_mhz) <= 20.0f;
}

void add_meshcore_regional_candidates(const AirProfile& active_profile)
{
    size_t preset_count = 0;
    const chat::meshcore::RegionPreset* presets =
        chat::meshcore::getRegionPresetTable(&preset_count);
    for (size_t index = 0; presets && index < preset_count; ++index)
    {
        const auto& preset = presets[index];
        if (!same_frequency_family(active_profile.frequency_mhz, preset.freq_mhz))
        {
            continue;
        }

        AirProfile profile = active_profile;
        profile.frequency_mhz = preset.freq_mhz;
        profile.bw_khz = preset.bw_khz;
        profile.sf = preset.sf;
        profile.cr = preset.cr;
        profile.tx_power = preset.tx_power_dbm;
        (void)add_candidate(profile, CandidateOrigin::Regional);
    }
}

void setup_radio_context()
{
    s_radio = {};

    const app::AppConfig& config = app::configFacade().readConfig();
    s_radio.protocol = normalize_probe_protocol(config.mesh_protocol);
    if (s_radio.protocol == chat::MeshProtocol::Meshtastic)
    {
        const chat::meshtastic::RadioConfig radio =
            chat::meshtastic::deriveRadioConfig(config.meshtastic_config);
        s_radio.supported_protocol = true;
        s_radio.base_profile = profile_from_meshtastic(radio);
        (void)add_candidate(s_radio.base_profile, CandidateOrigin::Active);
        add_meshtastic_standard_candidates(config);
        return;
    }

    if (s_radio.protocol == chat::MeshProtocol::MeshCore)
    {
        s_radio.supported_protocol = true;
        s_radio.base_profile = profile_from_meshcore(config.meshcore_config);
        (void)add_candidate(s_radio.base_profile, CandidateOrigin::Active);
        add_meshcore_regional_candidates(s_radio.base_profile);
        return;
    }

    if (s_radio.protocol == chat::MeshProtocol::Reticulum)
    {
        s_radio.supported_protocol = true;
        s_radio.base_profile = profile_from_reticulum(config.reticulumConfig());
        (void)add_candidate(s_radio.base_profile, CandidateOrigin::Active);
    }
}

bool acquire_radio_runtime()
{
    if (!platform::ui::lora::acquire() || !platform::ui::lora::is_online())
    {
        return false;
    }
    s_radio.acquired = true;
    return true;
}

void release_radio_runtime()
{
    if (!s_radio.acquired)
    {
        return;
    }
    platform::ui::lora::release();
    s_radio.acquired = false;
}

bool configure_current_candidate()
{
    const AirProfile& profile = current_candidate().profile;
    if (!platform::ui::lora::configure_receive(profile.frequency_mhz,
                                               receive_config_for(profile)))
    {
        return false;
    }
    s_state.candidate_started_ms = sys::millis_now();
    return true;
}

bool resolve_meshtastic_channel(uint8_t channel_hash,
                                uint8_t* out_key,
                                std::size_t* out_key_len)
{
    if (!out_key || !out_key_len)
    {
        return false;
    }

    const chat::MeshConfig& mesh = app::configFacade().readConfig().meshtastic_config;
    const auto prepare_primary = [&mesh, out_key, out_key_len]() -> uint8_t
    {
        if (chat::meshtastic::isZeroKey(mesh.primary_key, sizeof(mesh.primary_key)))
        {
            chat::meshtastic::expandShortPsk(kMeshtasticDefaultPskIndex, out_key, out_key_len);
        }
        else
        {
            *out_key_len = chat::normalizeMeshtasticChannelKeyLen(
                mesh.primary_key, sizeof(mesh.primary_key), mesh.primary_key_len);
            std::memcpy(out_key, mesh.primary_key, *out_key_len);
        }
        return chat::meshtastic::computeChannelHash(
            chat::meshtastic::primaryChannelName(mesh), out_key, *out_key_len);
    };

    const auto prepare_secondary = [&mesh, out_key, out_key_len]() -> uint8_t
    {
        if (chat::meshtastic::isZeroKey(mesh.secondary_key, sizeof(mesh.secondary_key)))
        {
            *out_key_len = 0;
            return 0;
        }
        *out_key_len = chat::normalizeMeshtasticChannelKeyLen(
            mesh.secondary_key, sizeof(mesh.secondary_key), mesh.secondary_key_len);
        std::memcpy(out_key, mesh.secondary_key, *out_key_len);
        return chat::meshtastic::computeChannelHash(
            chat::meshtastic::secondaryChannelName(mesh), out_key, *out_key_len);
    };

    const uint8_t primary_hash = prepare_primary();
    if (channel_hash == primary_hash)
    {
        return *out_key_len > 0;
    }

    const uint8_t secondary_hash = prepare_secondary();
    return channel_hash == secondary_hash && *out_key_len > 0;
}

bool is_matching_meshtastic_ack(const chat::meshtastic::PacketHeaderWire& header,
                                const uint8_t* cipher,
                                std::size_t cipher_size)
{
    const ProbeVerification& verification = s_state.verification;
    if (verification.kind != VerificationKind::MeshtasticAck ||
        header.from != verification.target_node ||
        header.to != app::appFacade().getSelfNodeId() ||
        header.channel != verification.channel_hash ||
        !cipher || cipher_size == 0)
    {
        return false;
    }

    std::size_t key_len = 0;
    if (!resolve_meshtastic_channel(header.channel,
                                    s_state.mt_key_scratch.data(),
                                    &key_len))
    {
        return false;
    }

    std::size_t plaintext_size = s_state.plaintext_scratch.size();
    if (!chat::meshtastic::decryptPayload(header,
                                          cipher,
                                          cipher_size,
                                          s_state.mt_key_scratch.data(),
                                          key_len,
                                          s_state.plaintext_scratch.data(),
                                          &plaintext_size))
    {
        return false;
    }

    s_state.mt_data_scratch = meshtastic_Data_init_zero;
    pb_istream_t stream =
        pb_istream_from_buffer(s_state.plaintext_scratch.data(), plaintext_size);
    if (!pb_decode(&stream, meshtastic_Data_fields, &s_state.mt_data_scratch) ||
        s_state.mt_data_scratch.portnum != meshtastic_PortNum_ROUTING_APP ||
        s_state.mt_data_scratch.request_id != verification.ticket ||
        s_state.mt_data_scratch.payload.size == 0)
    {
        return false;
    }

    s_state.mt_routing_scratch = meshtastic_Routing_init_zero;
    pb_istream_t routing_stream = pb_istream_from_buffer(
        s_state.mt_data_scratch.payload.bytes, s_state.mt_data_scratch.payload.size);
    if (!pb_decode(&routing_stream, meshtastic_Routing_fields, &s_state.mt_routing_scratch))
    {
        return false;
    }

    return s_state.mt_routing_scratch.which_variant == meshtastic_Routing_error_reason_tag &&
           s_state.mt_routing_scratch.error_reason == meshtastic_Routing_Error_NONE;
}

bool has_valid_meshtastic_data(const chat::meshtastic::PacketHeaderWire& header,
                               const uint8_t* cipher,
                               std::size_t cipher_size)
{
    if (!cipher || cipher_size == 0)
    {
        return false;
    }

    std::size_t key_len = 0;
    if (!resolve_meshtastic_channel(header.channel,
                                    s_state.mt_key_scratch.data(),
                                    &key_len))
    {
        // Meshtastic's unauthenticated outer header is not protocol proof.
        // Without a locally configured channel key the frame remains E1
        // diagnostics only, never a selectable Protocol Probe result.
        return false;
    }

    std::size_t plaintext_size = s_state.plaintext_scratch.size();
    return chat::meshtastic::decryptAndValidateDataPayload(
        header,
        cipher,
        cipher_size,
        s_state.mt_key_scratch.data(),
        key_len,
        s_state.plaintext_scratch.data(),
        &plaintext_size,
        &s_state.mt_data_scratch);
}

PacketEvidence classify_protocol_packet(const uint8_t* data, std::size_t size)
{
    PacketEvidence evidence{};
    if (!data || size == 0)
    {
        return evidence;
    }

    if (s_radio.protocol == chat::MeshProtocol::Meshtastic)
    {
        chat::meshtastic::PacketHeaderWire header{};
        std::size_t payload_size = s_state.protocol_scratch.size();
        if (chat::meshtastic::parseWirePacket(data,
                                              size,
                                              &header,
                                              s_state.protocol_scratch.data(),
                                              &payload_size) &&
            header.id != 0 && header.from != 0 && payload_size > 0 &&
            has_valid_meshtastic_data(header,
                                      s_state.protocol_scratch.data(),
                                      payload_size))
        {
            evidence.level = is_matching_meshtastic_ack(
                                 header, s_state.protocol_scratch.data(), payload_size)
                                 ? EvidenceLevel::Confirmed
                                 : EvidenceLevel::Observed;
            evidence.source_node = header.from;
            evidence.channel_hash = header.channel;
        }
        return evidence;
    }

    if (s_radio.protocol == chat::MeshProtocol::MeshCore)
    {
        chat::meshcore::ParsedPacket packet{};
        if (chat::meshcore::parsePacket(data, size, &packet) &&
            chat::meshcore::isPlausibleProtocolPacket(packet))
        {
            evidence.level = EvidenceLevel::Observed;
            chat::meshcore::DecodedDiscoverResponse response{};
            if (s_state.verification.kind == VerificationKind::MeshCoreDiscover &&
                chat::meshcore::decodeDiscoverResponse(
                    packet.payload, packet.payload_len, &response) &&
                response.valid && response.tag == s_state.verification.ticket)
            {
                evidence.level = EvidenceLevel::Confirmed;
            }
        }
        return evidence;
    }

    if (s_radio.protocol == chat::MeshProtocol::Reticulum)
    {
        chat::reticulum::ParsedPacket packet{};
        if (chat::reticulum::parsePacket(data, size, &packet) && packet.valid &&
            chat::reticulum::isPlausibleDiscoveryPacket(packet))
        {
            evidence.level = EvidenceLevel::Observed;
        }
        return evidence;
    }

    return evidence;
}

void restore_page_focus();

void record_observation(const AirProfile& profile,
                        const platform::ui::lora::ReceivedPacket& packet,
                        const PacketEvidence& evidence)
{
    int observation_index = -1;
    bool is_new_observation = false;
    for (int index = 0; index < s_state.observation_count; ++index)
    {
        if (same_profile(s_state.observations[index].profile, profile))
        {
            observation_index = index;
            break;
        }
    }
    if (observation_index < 0)
    {
        if (s_state.observation_count >= kMaxObservations)
        {
            return;
        }
        observation_index = s_state.observation_count++;
        is_new_observation = true;
        s_state.observations[observation_index] = {};
        s_state.observations[observation_index].profile = profile;
        s_state.observations[observation_index].first_seen_ms = sys::millis_now();
    }

    ProbeObservation& observation = s_state.observations[observation_index];
    observation.level = std::max(observation.level, evidence.level);
    if (evidence.source_node != 0)
    {
        observation.last_mt_source = evidence.source_node;
        observation.last_mt_channel_hash = evidence.channel_hash;
    }
    observation.evidence_count++;
    observation.last_seen_ms = sys::millis_now();
    observation.last_rssi_dbm = packet.rssi_dbm;
    observation.last_snr_db = packet.snr_db;
    if (s_state.observation_count == 1)
    {
        s_state.selected_observation = 0;
    }
    if (is_new_observation && s_ui.root)
    {
        restore_page_focus();
    }
}

void stop_probe()
{
    s_state.scanning = false;
    s_state.verification = {};
    release_radio_runtime();
}

uint32_t make_verification_ticket()
{
    const uint32_t node_id = app::appFacade().getSelfNodeId();
    uint32_t ticket = sys::millis_now() ^ node_id ^
                      (static_cast<uint32_t>(s_state.candidate_index + 1) * 0x45D9F3BU);
    return ticket == 0 ? 1 : ticket;
}

bool begin_meshcore_discover_verification()
{
    const chat::MeshConfig& mesh = app::configFacade().readConfig().meshcore_config;
    if (!mesh.tx_enabled)
    {
        return false;
    }

    chat::meshcore::MeshCoreDiscoverRequestBuildInfo request{};
    request.tag = make_verification_ticket();
    request.type_filter = chat::meshcore::kMeshCoreDiscoverTypeFilterAll;

    std::size_t payload_size = s_state.protocol_scratch.size();
    if (!chat::meshcore::buildDiscoverRequestControlPayload(request,
                                                            s_state.protocol_scratch.data(),
                                                            s_state.protocol_scratch.size(),
                                                            &payload_size))
    {
        return false;
    }

    const chat::meshcore::PayloadProfile profile =
        mesh.meshcore_send_profile == chat::MeshCorePayloadSendProfile::V1Only
            ? chat::meshcore::PayloadProfile::V1
            : chat::meshcore::PayloadProfile::V2;
    std::size_t frame_size = s_state.transmit_scratch.size();
    if (!chat::meshcore::buildFrameNoTransport(profile,
                                               kMeshCoreDirectRoute,
                                               chat::meshcore::kMeshCorePayloadTypeControl,
                                               nullptr,
                                               0,
                                               s_state.protocol_scratch.data(),
                                               payload_size,
                                               s_state.transmit_scratch.data(),
                                               s_state.transmit_scratch.size(),
                                               &frame_size) ||
        !platform::ui::lora::transmit_packet(s_state.transmit_scratch.data(), frame_size))
    {
        return false;
    }

    s_state.verification.kind = VerificationKind::MeshCoreDiscover;
    s_state.verification.candidate_index = s_state.candidate_index;
    s_state.verification.ticket = request.tag;
    s_state.verification.started_ms = sys::millis_now();
    s_state.verification.deadline_ms =
        s_state.verification.started_ms + meshcore_discover_response_window_ms(current_candidate().profile);
    return true;
}

const ProbeObservation* find_meshtastic_observation(const AirProfile& profile)
{
    for (int index = 0; index < s_state.observation_count; ++index)
    {
        const ProbeObservation& observation = s_state.observations[index];
        if (same_profile(observation.profile, profile) && observation.last_mt_source != 0)
        {
            return &observation;
        }
    }
    return nullptr;
}

bool begin_meshtastic_ack_verification(const ProbeObservation& observation)
{
    const app::AppConfig& config = app::configFacade().readConfig();
    const uint32_t self_node = app::appFacade().getSelfNodeId();
    if (self_node == 0 || !config.meshtastic_config.tx_enabled)
    {
        return false;
    }

    std::size_t key_len = 0;
    if (!resolve_meshtastic_channel(observation.last_mt_channel_hash,
                                    s_state.mt_key_scratch.data(),
                                    &key_len))
    {
        return false;
    }

    // Private-app payload keeps the packet outside the user-visible text and
    // NodeInfo paths while still requesting the normal routing acknowledgement.
    constexpr std::array<uint8_t, 6> kProbeAppData = {0x08, 0x80, 0x02, 0x12, 0x01, 0x00};
    const uint32_t ticket = make_verification_ticket();
    std::size_t wire_size = s_state.transmit_scratch.size();
    const uint8_t hop_limit = config.meshtastic_config.hop_limit > 0
                                  ? config.meshtastic_config.hop_limit
                                  : 1;
    if (!chat::meshtastic::buildWirePacket(kProbeAppData.data(),
                                           kProbeAppData.size(),
                                           self_node,
                                           ticket,
                                           observation.last_mt_source,
                                           observation.last_mt_channel_hash,
                                           hop_limit,
                                           true,
                                           s_state.mt_key_scratch.data(),
                                           key_len,
                                           s_state.transmit_scratch.data(),
                                           &wire_size) ||
        !platform::ui::lora::transmit_packet(s_state.transmit_scratch.data(), wire_size))
    {
        return false;
    }

    s_state.verification.kind = VerificationKind::MeshtasticAck;
    s_state.verification.candidate_index = s_state.candidate_index;
    s_state.verification.ticket = ticket;
    s_state.verification.target_node = observation.last_mt_source;
    s_state.verification.channel_hash = observation.last_mt_channel_hash;
    s_state.verification.started_ms = sys::millis_now();
    s_state.verification.deadline_ms =
        s_state.verification.started_ms + kMeshtasticAckWindowMs;
    return true;
}

bool maybe_start_verification(uint32_t now_ms)
{
    if (s_state.verification.kind != VerificationKind::None ||
        s_state.candidate_index < 0 || s_state.candidate_index >= active_candidate_count() ||
        s_state.verification_attempted[s_state.candidate_index] ||
        profile_is_confirmed(current_candidate().profile))
    {
        return false;
    }

    if (s_radio.protocol == chat::MeshProtocol::MeshCore)
    {
        if ((now_ms - s_state.candidate_started_ms) < kMeshCorePassiveListenMs)
        {
            return false;
        }
        s_state.verification_attempted[s_state.candidate_index] = true;
        return begin_meshcore_discover_verification();
    }

    if (s_radio.protocol == chat::MeshProtocol::Meshtastic)
    {
        const ProbeObservation* observation = find_meshtastic_observation(current_candidate().profile);
        if (!observation)
        {
            return false;
        }
        s_state.verification_attempted[s_state.candidate_index] = true;
        return begin_meshtastic_ack_verification(*observation);
    }

    return false;
}

void start_probe()
{
    s_state.applied = false;
    s_state.radio_error = false;
    if (!s_radio.supported_protocol || active_candidate_count() == 0 ||
        !platform::ui::lora::is_supported() ||
        !acquire_radio_runtime())
    {
        s_state.radio_error = true;
        return;
    }

    s_state.candidate_index = 0;
    s_state.checked_in_pass = 0;
    s_state.candidate_started_ms = 0;
    s_state.verification = {};
    s_state.verification_attempted.fill(false);
    if (!configure_current_candidate())
    {
        s_state.radio_error = true;
        release_radio_runtime();
        return;
    }
    s_state.scanning = true;
}

void process_scan_step()
{
    if (!s_state.scanning)
    {
        return;
    }

    for (int packet_index = 0; packet_index < 4; ++packet_index)
    {
        platform::ui::lora::ReceivedPacket packet{};
        if (!platform::ui::lora::poll_received_packet(s_state.receive_scratch.data(),
                                                      s_state.receive_scratch.size(),
                                                      &packet))
        {
            break;
        }
        // RadioLib only yields CRC-passing frames here. Keep that diagnostic
        // separate from protocol evidence, which needs a protocol parser.
        s_state.crc_frame_count++;
        const PacketEvidence evidence =
            classify_protocol_packet(s_state.receive_scratch.data(), packet.size);
        if (evidence.level != EvidenceLevel::None)
        {
            record_observation(current_candidate().profile, packet, evidence);
            if (evidence.level == EvidenceLevel::Confirmed)
            {
                s_state.verification = {};
            }
        }
    }

    const uint32_t now = sys::millis_now();
    if (s_state.verification.kind != VerificationKind::None)
    {
        if (static_cast<int32_t>(now - s_state.verification.deadline_ms) < 0)
        {
            return;
        }
        s_state.verification = {};
    }

    if (maybe_start_verification(now))
    {
        return;
    }
    if ((now - s_state.candidate_started_ms) < candidate_dwell_ms(current_candidate()))
    {
        return;
    }

    s_state.checked_in_pass++;
    s_state.candidate_index++;
    if (s_state.candidate_index >= active_candidate_count())
    {
        s_state.candidate_index = 0;
        s_state.checked_in_pass = 0;
        s_state.completed_passes++;
    }
    // An unconfirmed profile gets one low-rate active attempt per full pass.
    // Confirmed profiles are excluded by maybe_start_verification().
    s_state.verification_attempted[s_state.candidate_index] = false;
    if (!configure_current_candidate())
    {
        s_state.radio_error = true;
        stop_probe();
    }
}

void format_profile_params(const AirProfile& profile, char* buffer, std::size_t buffer_size)
{
    const float rounded_bandwidth = std::round(profile.bw_khz);
    if (std::fabs(profile.bw_khz - rounded_bandwidth) < 0.01f)
    {
        snprintf(buffer,
                 buffer_size,
                 "%.0fK SF%02u C4/%u",
                 static_cast<double>(rounded_bandwidth),
                 static_cast<unsigned>(profile.sf),
                 static_cast<unsigned>(profile.cr));
        return;
    }
    snprintf(buffer,
             buffer_size,
             "%.1fK SF%02u C4/%u",
             static_cast<double>(profile.bw_khz),
             static_cast<unsigned>(profile.sf),
             static_cast<unsigned>(profile.cr));
}

const char* evidence_state(EvidenceLevel level)
{
    return level == EvidenceLevel::Confirmed ? "CONFIRMED" : "OBSERVED";
}

uint32_t total_protocol_evidence()
{
    uint32_t total = 0;
    for (int index = 0; index < s_state.observation_count; ++index)
    {
        total += s_state.observations[index].evidence_count;
    }
    return total;
}

void refresh_rows()
{
    const int visible_rows = visible_result_capacity();
    for (int visual_index = 0; visual_index < kMaxObservations; ++visual_index)
    {
        lv_obj_t* row = s_ui.result_rows[visual_index];
        if (!row)
        {
            continue;
        }
        const int observation_index = s_state.visible_page_start + visual_index;
        if (visual_index >= visible_rows || observation_index >= s_state.observation_count)
        {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        const ProbeObservation& observation = s_state.observations[observation_index];
        char primary[32];
        char secondary[40];
        char state[16];
        char count[16];
        snprintf(primary,
                 sizeof(primary),
                 "%s %.3f",
                 protocol_tag(s_radio.protocol),
                 observation.profile.frequency_mhz);
        format_profile_params(observation.profile, secondary, sizeof(secondary));
        snprintf(state, sizeof(state), "%s", evidence_state(observation.level));
        snprintf(count, sizeof(count), "x%lu", static_cast<unsigned long>(observation.evidence_count));
        lv_label_set_text(s_ui.result_primary[visual_index], primary);
        lv_label_set_text(s_ui.result_secondary[visual_index], secondary);
        lv_label_set_text(s_ui.result_state[visual_index], state);
        lv_label_set_text(s_ui.result_count[visual_index], count);

        const bool selected = observation_index == clamp_observation_index(s_state.selected_observation);
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(selected ? kColorAmber : kColorPanelBg),
                                  0);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(selected ? kColorAmberDark : kColorLine),
                                      0);
        lv_obj_set_style_text_color(s_ui.result_primary[visual_index],
                                    lv_color_hex(selected ? kColorText : kColorText),
                                    0);
        lv_obj_set_style_text_color(s_ui.result_secondary[visual_index],
                                    lv_color_hex(selected ? kColorText : kColorTextDim),
                                    0);
        lv_obj_set_style_text_color(
            s_ui.result_state[visual_index],
            lv_color_hex(selected ? kColorText
                                  : (observation.level == EvidenceLevel::Confirmed ? kColorOk
                                                                                   : kColorInfo)),
            0);
        lv_obj_set_style_text_color(s_ui.result_count[visual_index],
                                    lv_color_hex(selected ? kColorText : kColorOk),
                                    0);
    }

    if (s_ui.empty_label)
    {
        if (s_state.observation_count == 0)
        {
            lv_obj_clear_flag(s_ui.empty_label, LV_OBJ_FLAG_HIDDEN);
            ::ui::i18n::set_label_text(s_ui.empty_label,
                                       s_state.scanning
                                           ? "NO PROTOCOL EVIDENCE YET"
                                           : (s_state.completed_passes > 0
                                                  ? "NO EVIDENCE IN THIS PASS"
                                                  : "READY TO PROBE KNOWN PROFILES"));
        }
        else
        {
            lv_obj_add_flag(s_ui.empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void refresh_status()
{
    if (!s_ui.state_label)
    {
        return;
    }

    char text[48];
    uint32_t color = kColorTextDim;
    if (!s_radio.supported_protocol)
    {
        snprintf(text, sizeof(text), "UNSUPPORTED PROTOCOL");
        color = kColorWarning;
    }
    else if (s_state.radio_error)
    {
        snprintf(text, sizeof(text), "RADIO UNAVAILABLE");
        color = kColorWarning;
    }
    else if (s_state.applied)
    {
        snprintf(text, sizeof(text), "APPLIED PROTOCOL PROFILE");
        color = kColorOk;
    }
    else if (s_state.verification.kind == VerificationKind::MeshCoreDiscover)
    {
        snprintf(text,
                 sizeof(text),
                 "MC TX %.3f",
                 current_candidate().profile.frequency_mhz);
        color = kColorInfo;
    }
    else if (s_state.verification.kind == VerificationKind::MeshtasticAck)
    {
        snprintf(text,
                 sizeof(text),
                 "MT ACK %.3f",
                 current_candidate().profile.frequency_mhz);
        color = kColorInfo;
    }
    else if (s_state.scanning)
    {
        if (s_radio.protocol == chat::MeshProtocol::Reticulum &&
            total_protocol_evidence() > 0)
        {
            snprintf(text,
                     sizeof(text),
                     "RT TRAFFIC %lu",
                     static_cast<unsigned long>(total_protocol_evidence()));
        }
        else
        {
            const AirProfile& profile = current_candidate().profile;
            snprintf(text,
                     sizeof(text),
                     "%s %.3f B%.0f S%u %d/%d",
                     protocol_tag(s_radio.protocol),
                     profile.frequency_mhz,
                     profile.bw_khz,
                     static_cast<unsigned>(profile.sf),
                     s_state.candidate_index + 1,
                     active_candidate_count());
        }
        color = kColorInfo;
    }
    else
    {
        snprintf(text, sizeof(text), "READY TO PROBE KNOWN PROFILES");
    }
    lv_label_set_text(s_ui.state_label, text);
    lv_obj_set_style_text_color(s_ui.state_label, lv_color_hex(color), 0);

    if (s_ui.progress_label)
    {
        snprintf(text,
                 sizeof(text),
                 "%d FOUND  %lu PROTOCOL FRAMES",
                 s_state.observation_count,
                 static_cast<unsigned long>(total_protocol_evidence()));
        lv_label_set_text(s_ui.progress_label, text);
    }

    if (s_ui.stop_label)
    {
        ::ui::i18n::set_label_text(s_ui.stop_label, s_state.scanning ? "S  STOP" : "S  START");
    }
}

void refresh_selected_profile()
{
    const bool has_selection = s_state.observation_count > 0;
    if (s_ui.set_button && s_ui.set_label)
    {
        const uint32_t color = has_selection ? kColorAmber : kColorLine;
        lv_obj_set_style_bg_color(s_ui.set_button, lv_color_hex(has_selection ? kColorPanelBg : kColorWarmBg), 0);
        lv_obj_set_style_border_color(s_ui.set_button, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(s_ui.set_label, lv_color_hex(has_selection ? kColorText : kColorTextDim), 0);
    }
}

void refresh_all_ui()
{
    ui_update_top_bar_battery(s_ui.top_bar);
    refresh_status();
    refresh_rows();
    refresh_selected_profile();
}

void refresh_timer_cb(lv_timer_t*)
{
    if (!s_ui.root)
    {
        return;
    }
    process_scan_step();
    refresh_all_ui();
}

void text_scan_timer_cb(lv_timer_t*)
{
    // Radio cadence is intentionally independent from the display cadence.
    // This timer advances the scan only; the text adapter renders a snapshot
    // after an explicit user action and never receives timer-driven LVGL work.
    if (!s_text_runtime_active || !s_page_state)
    {
        return;
    }
    process_scan_step();
}

void stop_text_runtime()
{
    if (s_text_scan_timer != nullptr)
    {
        lv_timer_del(s_text_scan_timer);
        s_text_scan_timer = nullptr;
    }
    if (s_page_state != nullptr)
    {
        stop_probe();
        s_state = {};
        s_radio = {};
        release_page_state();
    }
    s_text_runtime_active = false;
}

void restore_page_focus()
{
    if (!::app_g)
    {
        return;
    }

    lv_group_remove_all_objs(::app_g);
    if (s_ui.top_bar.back_btn)
    {
        lv_group_add_obj(::app_g, s_ui.top_bar.back_btn);
    }
    const int visible_count = std::min(
        visible_result_capacity(),
        std::max(0, s_state.observation_count - s_state.visible_page_start));
    for (int index = 0; index < visible_count; ++index)
    {
        if (s_ui.result_rows[index])
        {
            lv_group_add_obj(::app_g, s_ui.result_rows[index]);
        }
    }
    if (s_ui.stop_button)
    {
        lv_group_add_obj(::app_g, s_ui.stop_button);
    }
    if (s_ui.set_button)
    {
        lv_group_add_obj(::app_g, s_ui.set_button);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_group_focus_obj(s_ui.top_bar.back_btn);
    }
    set_default_group(::app_g);
    lv_group_set_editing(::app_g, false);
}

void close_confirmation();
void control_key_event_cb(lv_event_t* event);

void on_confirm_cancel_clicked(lv_event_t*)
{
    close_confirmation();
}

bool apply_selected_profile()
{
    if (s_state.observation_count <= 0)
    {
        return false;
    }

    const AirProfile profile =
        s_state.observations[clamp_observation_index(s_state.selected_observation)].profile;
    stop_probe();

    app::IAppFacade& app_ctx = app::appFacade();
    auto edit = app_ctx.beginConfigEdit();
    if (!edit || normalize_probe_protocol(edit.config().mesh_protocol) != s_radio.protocol)
    {
        s_state.radio_error = true;
        return false;
    }

    if (s_radio.protocol == chat::MeshProtocol::Meshtastic)
    {
        chat::MeshConfig& mesh = edit.config().meshtastic_config;
        mesh.use_preset = false;
        mesh.bandwidth_khz = profile.bw_khz;
        mesh.spread_factor = profile.sf;
        mesh.coding_rate = profile.cr;
        mesh.override_frequency_mhz = profile.frequency_mhz;
        mesh.frequency_offset_mhz = 0.0f;
    }
    else if (s_radio.protocol == chat::MeshProtocol::MeshCore)
    {
        chat::MeshConfig& mesh = edit.config().meshcore_config;
        mesh.meshcore_region_preset = 0;
        mesh.meshcore_freq_mhz = profile.frequency_mhz;
        mesh.meshcore_bw_khz = profile.bw_khz;
        mesh.meshcore_sf = profile.sf;
        mesh.meshcore_cr = profile.cr;
    }
    else if (s_radio.protocol == chat::MeshProtocol::Reticulum)
    {
        chat::MeshConfig& mesh = edit.config().reticulumConfig();
        mesh.use_preset = false;
        mesh.bandwidth_khz = profile.bw_khz;
        mesh.spread_factor = profile.sf;
        mesh.coding_rate = profile.cr;
        mesh.override_frequency_mhz = profile.frequency_mhz;
        mesh.frequency_offset_mhz = 0.0f;
    }
    else
    {
        return false;
    }

    edit.commit(app::AppConfigChangeSet::mesh());
    app_ctx.applyMeshConfig();
    // The next manual probe must start from the profile just committed, not
    // from the candidate queue captured when this page was entered.
    setup_radio_context();
    s_state.applied = true;
    s_state.radio_error = false;
    return true;
}

void on_confirm_apply_clicked(lv_event_t*)
{
    close_confirmation();
    (void)apply_selected_profile();
    refresh_all_ui();
}

void open_confirmation()
{
    if (s_state.confirmation_open || s_state.observation_count <= 0 || !s_ui.root)
    {
        return;
    }

    s_state.confirmation_open = true;
    s_state.confirm_apply_selected = false;
    s_ui.confirmation = lv_obj_create(s_ui.root);
    lv_obj_set_size(s_ui.confirmation, s_layout.screen_w, s_layout.screen_h);
    lv_obj_set_pos(s_ui.confirmation, 0, 0);
    lv_obj_set_style_bg_color(s_ui.confirmation, lv_color_hex(0x3A2A1A), 0);
    lv_obj_set_style_bg_opa(s_ui.confirmation, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_ui.confirmation, 0, 0);
    lv_obj_set_style_radius(s_ui.confirmation, 0, 0);
    lv_obj_set_style_pad_all(s_ui.confirmation, 0, 0);
    lv_obj_clear_flag(s_ui.confirmation, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t requested_panel_w = s_layout.pager ? 236 : 340;
    const lv_coord_t panel_w =
        std::min<lv_coord_t>(requested_panel_w, s_layout.screen_w - (s_layout.compact ? 20 : 32));
    const lv_coord_t panel_h = s_layout.pager ? 126 : (s_layout.compact ? 148 : 168);
    lv_obj_t* panel = lv_obj_create(s_ui.confirmation);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, s_layout.compact ? 8 : 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel);
    ::ui::i18n::set_label_text(title, "APPLY PROTOCOL PROFILE?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(title, 8, 8);

    const ProbeObservation& observation =
        s_state.observations[clamp_observation_index(s_state.selected_observation)];
    char frequency[32];
    char params[40];
    snprintf(frequency,
             sizeof(frequency),
             "%s %.3f MHz",
             protocol_tag(s_radio.protocol),
             observation.profile.frequency_mhz);
    format_profile_params(observation.profile, params, sizeof(params));

    lv_obj_t* detail = lv_label_create(panel);
    lv_label_set_text(detail, frequency);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(kColorInfo), 0);
    lv_obj_set_pos(detail, 8, 30);

    lv_obj_t* detail_params = lv_label_create(panel);
    lv_label_set_text(detail_params, params);
    lv_obj_set_style_text_font(detail_params, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detail_params, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(detail_params, 8, 50);

    lv_obj_t* evidence = lv_label_create(panel);
    ::ui::i18n::set_label_text(evidence,
                               observation.level == EvidenceLevel::Confirmed
                                   ? "CONFIRMED BY PROTOCOL RESPONSE"
                                   : "OBSERVED VIA PROTOCOL FRAME");
    lv_obj_set_style_text_font(evidence, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(evidence,
                                lv_color_hex(observation.level == EvidenceLevel::Confirmed
                                                 ? kColorOk
                                                 : kColorInfo),
                                0);
    lv_obj_set_pos(evidence, 8, 69);

    const lv_coord_t button_y = panel_h - (s_layout.compact ? 32 : 46);
    const lv_coord_t button_h = s_layout.compact ? 24 : 34;
    const lv_coord_t button_w = (panel_w - 24) / 2;
    s_ui.confirm_cancel = lv_btn_create(panel);
    lv_obj_set_pos(s_ui.confirm_cancel, 8, button_y);
    lv_obj_set_size(s_ui.confirm_cancel, button_w, button_h);
    lv_obj_set_style_bg_color(s_ui.confirm_cancel, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.confirm_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.confirm_cancel, 1, 0);
    lv_obj_set_style_border_color(s_ui.confirm_cancel, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.confirm_cancel, 4, 0);

    lv_obj_t* cancel_label = lv_label_create(s_ui.confirm_cancel);
    ::ui::i18n::set_label_text(cancel_label, "ESC  CANCEL");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(kColorText), 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(s_ui.confirm_cancel,
                        on_confirm_cancel_clicked,
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(s_ui.confirm_cancel, control_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.confirm_apply = lv_btn_create(panel);
    lv_obj_set_pos(s_ui.confirm_apply, panel_w - button_w - 8, button_y);
    lv_obj_set_size(s_ui.confirm_apply, button_w, button_h);
    lv_obj_set_style_bg_color(s_ui.confirm_apply, lv_color_hex(kColorAmber), 0);
    lv_obj_set_style_bg_opa(s_ui.confirm_apply, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.confirm_apply, 1, 0);
    lv_obj_set_style_border_color(s_ui.confirm_apply, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_radius(s_ui.confirm_apply, 4, 0);

    lv_obj_t* apply_label = lv_label_create(s_ui.confirm_apply);
    ::ui::i18n::set_label_text(apply_label, "ENTER  APPLY");
    lv_obj_set_style_text_font(apply_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(apply_label, lv_color_hex(kColorText), 0);
    lv_obj_center(apply_label);
    lv_obj_add_event_cb(s_ui.confirm_apply,
                        on_confirm_apply_clicked,
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(s_ui.confirm_apply, control_key_event_cb, LV_EVENT_KEY, nullptr);

    if (::app_g)
    {
        lv_group_remove_all_objs(::app_g);
        lv_group_add_obj(::app_g, s_ui.confirm_cancel);
        lv_group_add_obj(::app_g, s_ui.confirm_apply);
        lv_group_focus_obj(s_ui.confirm_cancel);
        set_default_group(::app_g);
        lv_group_set_editing(::app_g, false);
    }
}

void close_confirmation()
{
    if (s_ui.confirmation)
    {
        lv_obj_del(s_ui.confirmation);
    }
    s_ui.confirmation = nullptr;
    s_ui.confirm_cancel = nullptr;
    s_ui.confirm_apply = nullptr;
    s_state.confirmation_open = false;
    s_state.confirm_apply_selected = false;
    restore_page_focus();
}

void handle_confirmation_key(uint32_t key)
{
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        close_confirmation();
        return;
    }
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT)
    {
        s_state.confirm_apply_selected = !s_state.confirm_apply_selected;
        if (::app_g)
        {
            lv_group_focus_obj(s_state.confirm_apply_selected ? s_ui.confirm_apply
                                                              : s_ui.confirm_cancel);
        }
        return;
    }
    if (key == LV_KEY_ENTER)
    {
        const bool should_apply = s_state.confirm_apply_selected;
        close_confirmation();
        if (should_apply)
        {
            (void)apply_selected_profile();
            refresh_all_ui();
        }
    }
}

void select_observation(int index)
{
    if (s_state.observation_count <= 0)
    {
        return;
    }
    s_state.selected_observation = clamp_observation_index(index);
    const int visible_rows = visible_result_capacity();
    s_state.visible_page_start =
        (s_state.selected_observation / visible_rows) * visible_rows;
    refresh_all_ui();
}

void on_result_row_clicked(lv_event_t* event)
{
    const auto visual_index =
        static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    select_observation(s_state.visible_page_start + visual_index);
}

void on_stop_clicked(lv_event_t*)
{
    if (s_state.scanning)
    {
        stop_probe();
    }
    else
    {
        start_probe();
    }
    refresh_all_ui();
}

void on_set_clicked(lv_event_t*)
{
    open_confirmation();
}

void on_back_requested(lv_event_t*)
{
    if (s_state.confirmation_open)
    {
        close_confirmation();
        return;
    }
    request_exit();
}

void top_bar_back_requested(void*)
{
    on_back_requested(nullptr);
}

void handle_key_common(uint32_t key)
{
    if (s_state.confirmation_open)
    {
        handle_confirmation_key(key);
        return;
    }

    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        on_back_requested(nullptr);
        return;
    }
    if (key == LV_KEY_UP)
    {
        select_observation(s_state.selected_observation - 1);
        return;
    }
    if (key == LV_KEY_DOWN)
    {
        select_observation(s_state.selected_observation + 1);
        return;
    }
    if (key == LV_KEY_ENTER)
    {
        open_confirmation();
        return;
    }
    if (key == 's' || key == 'S')
    {
        on_stop_clicked(nullptr);
    }
}

void root_key_event_cb(lv_event_t* event)
{
    handle_key_common(lv_event_get_key(event));
}

void control_key_event_cb(lv_event_t* event)
{
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER)
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (target == s_ui.top_bar.back_btn)
        {
            on_back_requested(nullptr);
        }
        else if (target == s_ui.stop_button)
        {
            on_stop_clicked(nullptr);
        }
        else if (target == s_ui.set_button)
        {
            on_set_clicked(nullptr);
        }
        else if (target == s_ui.confirm_cancel)
        {
            on_confirm_cancel_clicked(nullptr);
        }
        else if (target == s_ui.confirm_apply)
        {
            on_confirm_apply_clicked(nullptr);
        }
        else
        {
            for (int index = 0; index < s_state.observation_count; ++index)
            {
                if (target == s_ui.result_rows[index])
                {
                    select_observation(s_state.visible_page_start + index);
                    open_confirmation();
                    return;
                }
            }
        }
        return;
    }
    handle_key_common(key);
}

void build_topbar(lv_obj_t* root)
{
    ::ui::widgets::TopBarConfig config{};
    config.height = s_layout.topbar_h;
    ::ui::widgets::top_bar_init(s_ui.top_bar, root, config);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, ::ui::i18n::tr("PROTOCOL PROBE"));
    ::ui::widgets::top_bar_set_back_callback(s_ui.top_bar, top_bar_back_requested, nullptr);
    if (s_ui.top_bar.container)
    {
        lv_obj_set_pos(s_ui.top_bar.container, 0, 0);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, control_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_ui.top_bar);
}

lv_obj_t* create_text(lv_obj_t* parent,
                      const char* text,
                      const lv_font_t* font,
                      uint32_t color,
                      lv_coord_t x,
                      lv_coord_t y)
{
    lv_obj_t* label = lv_label_create(parent);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

void build_work_area(lv_obj_t* root)
{
    const lv_coord_t work_height = s_layout.work_bottom - s_layout.work_top;
    s_ui.content_area = lv_obj_create(root);
    lv_obj_set_pos(s_ui.content_area, s_layout.content_x, s_layout.work_top);
    lv_obj_set_size(s_ui.content_area, s_layout.content_w, work_height);
    lv_obj_set_style_bg_opa(s_ui.content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.content_area, 0, 0);
    lv_obj_set_style_radius(s_ui.content_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.content_area, 0, 0);
    lv_obj_clear_flag(s_ui.content_area, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.state_label = create_text(s_ui.content_area,
                                   "READY TO PROBE KNOWN PROFILES",
                                   &lv_font_montserrat_12,
                                   kColorTextDim,
                                   0,
                                   s_layout.compact ? 7 : 10);

    const lv_coord_t row_y = s_layout.compact ? 28 : 34;
    const lv_coord_t row_h = s_layout.pager ? 24 : (s_layout.compact ? 30 : 44);
    const lv_coord_t row_gap = s_layout.pager ? 3 : (s_layout.compact ? 4 : 6);
    for (int index = 0; index < kMaxObservations; ++index)
    {
        lv_obj_t* row = lv_btn_create(s_ui.content_area);
        lv_obj_set_pos(row, 0, row_y + static_cast<lv_coord_t>(index) * (row_h + row_gap));
        lv_obj_set_size(row, s_layout.content_w, row_h);
        lv_obj_set_style_bg_color(row, lv_color_hex(kColorPanelBg), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(row,
                            on_result_row_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(row, control_key_event_cb, LV_EVENT_KEY, nullptr);
        s_ui.result_rows[index] = row;

        s_ui.result_primary[index] = create_text(row,
                                                 "MT ---.---",
                                                 &lv_font_montserrat_12,
                                                 kColorText,
                                                 6,
                                                 s_layout.pager ? 4 : (s_layout.compact ? 2 : 5));
        s_ui.result_secondary[index] = create_text(row,
                                                   "---K SF-- C4/-",
                                                   &lv_font_montserrat_12,
                                                   kColorTextDim,
                                                   s_layout.pager ? 90 : 6,
                                                   s_layout.pager ? 4 : (s_layout.compact ? 15 : 22));
        s_ui.result_state[index] = create_text(row,
                                               "OBSERVED",
                                               &lv_font_montserrat_12,
                                               kColorInfo,
                                               s_layout.pager ? s_layout.content_w - 164
                                                              : s_layout.content_w -
                                                                    (s_layout.compact ? 96 : 92),
                                               s_layout.pager ? 4 : 5);
        s_ui.result_count[index] = create_text(row,
                                               "x0",
                                               &lv_font_montserrat_12,
                                               kColorOk,
                                               s_layout.content_w -
                                                   (s_layout.pager ? 31 : (s_layout.compact ? 28 : 45)),
                                               s_layout.pager ? 4 : (s_layout.compact ? 5 : 22));
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    s_ui.empty_label = create_text(s_ui.content_area,
                                   "READY TO PROBE KNOWN PROFILES",
                                   &lv_font_montserrat_12,
                                   kColorTextDim,
                                   0,
                                   row_y + 7);
    s_ui.progress_label = create_text(s_ui.content_area,
                                      "0 FOUND  0 PROTOCOL FRAMES",
                                      &lv_font_montserrat_12,
                                      kColorTextDim,
                                      0,
                                      work_height - (s_layout.compact ? 18 : 26));
}

lv_obj_t* create_bottom_control(lv_obj_t* parent,
                                lv_coord_t x,
                                lv_coord_t width,
                                const char* text,
                                lv_event_cb_t callback,
                                lv_obj_t** out_label)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, 2);
    lv_obj_set_size(button, width, s_layout.bottom_bar_h - 4);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
    if (callback)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_add_event_cb(button, control_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kColorText), 0);
    lv_obj_center(label);
    if (out_label)
    {
        *out_label = label;
    }
    return button;
}

void build_bottom_bar(lv_obj_t* root)
{
    s_ui.bottom_bar = lv_obj_create(root);
    lv_obj_set_pos(s_ui.bottom_bar, 0, s_layout.work_bottom);
    lv_obj_set_size(s_ui.bottom_bar, s_layout.screen_w, s_layout.bottom_bar_h);
    lv_obj_set_style_bg_color(s_ui.bottom_bar, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.bottom_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_ui.bottom_bar, 1, 0);
    lv_obj_set_style_border_color(s_ui.bottom_bar, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(s_ui.bottom_bar, 0, 0);
    lv_obj_clear_flag(s_ui.bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t gap = s_layout.compact ? 4 : 8;
    const lv_coord_t available = s_layout.screen_w - (gap * 5);
    const lv_coord_t first_width = (available * 30) / 100;
    const lv_coord_t action_width = (available - first_width) / 3;
    lv_coord_t x = gap;
    (void)create_bottom_control(s_ui.bottom_bar, x, first_width, "UP/DN  SELECT", nullptr, nullptr);
    x += first_width + gap;
    s_ui.set_button = create_bottom_control(s_ui.bottom_bar,
                                            x,
                                            action_width,
                                            "ENTER  SET",
                                            on_set_clicked,
                                            &s_ui.set_label);
    x += action_width + gap;
    s_ui.stop_button = create_bottom_control(s_ui.bottom_bar,
                                             x,
                                             action_width,
                                             "S  START",
                                             on_stop_clicked,
                                             &s_ui.stop_label);
    x += action_width + gap;
    (void)create_bottom_control(s_ui.bottom_bar,
                                x,
                                action_width,
                                "ESC  BACK",
                                on_back_requested,
                                nullptr);
}

void reset_ui_state()
{
    s_ui = {};
    s_layout = {};
}

} // namespace

lv_obj_t* ui_energy_sweep_create(lv_obj_t* parent)
{
    if (!parent || !ensure_page_state())
    {
        return nullptr;
    }
    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }

    s_layout = resolve_layout(parent);
    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, s_layout.screen_w, s_layout.screen_h);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    build_topbar(s_ui.root);
    build_work_area(s_ui.root);
    build_bottom_bar(s_ui.root);
    refresh_all_ui();
    return s_ui.root;
}

void ui_energy_sweep_enter(lv_obj_t* parent)
{
    if (!ensure_page_state())
    {
        return;
    }

    lv_group_t* previous_group = lv_group_get_default();
    set_default_group(nullptr);

    s_state = {};
    setup_radio_context();
    ui_energy_sweep_create(parent);
    restore_page_focus();
    if (!::app_g)
    {
        set_default_group(previous_group);
    }

    platform::ui::screen::disable_sleep();
    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, kRefreshIntervalMs, nullptr);
    }
    refresh_all_ui();
}

void ui_energy_sweep_exit(lv_obj_t* parent)
{
    (void)parent;
    if (!s_page_state)
    {
        s_host = nullptr;
        return;
    }

    if (s_refresh_timer)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    close_confirmation();
    stop_probe();
    platform::ui::screen::enable_sleep();

    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }
    s_state = {};
    s_radio = {};
    s_host = nullptr;
    release_page_state();
}

namespace energy_sweep::ui::runtime
{

bool is_available()
{
    return platform::ui::lora::is_supported();
}

bool text_start()
{
    if (s_text_runtime_active)
    {
        if (!s_state.scanning)
        {
            s_state.applied = false;
            start_probe();
        }
        return s_state.scanning;
    }
    if (!ensure_page_state())
    {
        return false;
    }

    s_state = {};
    setup_radio_context();
    s_text_runtime_active = true;
    platform::ui::screen::disable_sleep();
    start_probe();
    if (!s_state.scanning)
    {
        stop_text_runtime();
        platform::ui::screen::enable_sleep();
        return false;
    }

    s_text_scan_timer = lv_timer_create(text_scan_timer_cb, kRefreshIntervalMs, nullptr);
    if (s_text_scan_timer == nullptr)
    {
        stop_text_runtime();
        platform::ui::screen::enable_sleep();
        return false;
    }
    return true;
}

void text_stop()
{
    stop_text_runtime();
    platform::ui::screen::enable_sleep();
}

bool text_snapshot(TextSnapshot& out)
{
    out = {};
    out.available = is_available();
    if (!s_text_runtime_active || !s_page_state)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", out.available ? "READY" : "RADIO UNAVAILABLE");
        return out.available;
    }

    out.scanning = s_state.scanning;
    out.radio_error = s_state.radio_error;
    out.applied = s_state.applied;
    out.has_selection = s_state.observation_count > 0;
    out.candidate_index = static_cast<std::uint32_t>(std::max(0, s_state.candidate_index));
    out.candidate_count = static_cast<std::uint32_t>(active_candidate_count());
    out.completed_passes = s_state.completed_passes;
    out.observation_count = static_cast<std::uint32_t>(std::max(0, s_state.observation_count));
    out.evidence_count = total_protocol_evidence();
    out.crc_frame_count = s_state.crc_frame_count;

    if (!s_radio.supported_protocol)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "UNSUPPORTED PROTOCOL");
        return true;
    }
    if (s_state.radio_error)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "RADIO UNAVAILABLE");
        return true;
    }
    if (s_state.applied)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "PROFILE APPLIED");
        return true;
    }
    if (s_state.verification.kind == VerificationKind::MeshCoreDiscover)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "MESHCORE VERIFYING");
    }
    else if (s_state.verification.kind == VerificationKind::MeshtasticAck)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "MESHTASTIC VERIFYING");
    }
    else if (s_state.scanning)
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "SCANNING");
    }
    else
    {
        std::snprintf(out.status, sizeof(out.status), "%s", "READY");
    }

    if (active_candidate_count() > 0)
    {
        format_profile_params(current_candidate().profile,
                              out.current_profile,
                              sizeof(out.current_profile));
    }
    if (out.has_selection)
    {
        format_profile_params(s_state.observations[clamp_observation_index(s_state.selected_observation)].profile,
                              out.selected_profile,
                              sizeof(out.selected_profile));
    }
    return true;
}

bool text_select_observation_delta(int delta)
{
    if (!s_text_runtime_active || !s_page_state || s_state.observation_count <= 0 || delta == 0)
    {
        return false;
    }
    const int count = s_state.observation_count;
    int selected = s_state.selected_observation + delta;
    selected %= count;
    if (selected < 0)
    {
        selected += count;
    }
    s_state.selected_observation = selected;
    return true;
}

bool text_apply_selected()
{
    return s_text_runtime_active && s_page_state != nullptr && apply_selected_profile();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_host = host;
    ui_energy_sweep_enter(parent);
}

void exit(lv_obj_t* parent)
{
    ui_energy_sweep_exit(parent);
}

} // namespace energy_sweep::ui::runtime

#else

namespace energy_sweep::ui::runtime
{

bool is_available()
{
    return false;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    (void)host;
    (void)parent;
}

void exit(lv_obj_t* parent)
{
    (void)parent;
}

} // namespace energy_sweep::ui::runtime

#endif
