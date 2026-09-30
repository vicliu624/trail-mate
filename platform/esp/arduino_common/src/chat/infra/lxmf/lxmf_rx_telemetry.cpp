/**
 * @file lxmf_rx_telemetry.cpp
 * @brief RX budget and summary telemetry for the embedded LXMF runtime.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_rx_telemetry.h"

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <cstdio>
#endif

#if defined(ARDUINO)
#define LXMF_RAW_RX_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LXMF_RAW_RX_LOG(...) std::printf(__VA_ARGS__)
#endif

namespace chat::lxmf::runtime
{

bool RawRxTelemetry::consumeDiscoveryBudget(
    reticulum::interfaces::InterfaceKind ingress_interface,
    uint32_t now_ms,
    uint32_t sample_interval_ms)
{
    uint32_t& last_sample_ms =
        ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway
            ? last_wifi_discovery_sample_ms_
            : last_lora_discovery_sample_ms_;
    if (last_sample_ms != 0 &&
        (now_ms - last_sample_ms) < sample_interval_ms)
    {
        return false;
    }
    last_sample_ms = now_ms;
    return true;
}

bool RawRxTelemetry::shouldLogLoraDiscoveryDetail(uint32_t now_ms,
                                                  uint32_t interval_ms,
                                                  const char* phase)
{
    if (last_lora_discovery_detail_log_ms_ != 0 &&
        now_ms - last_lora_discovery_detail_log_ms_ < interval_ms)
    {
        ++suppressed_lora_discovery_detail_logs_;
        return false;
    }
    if (suppressed_lora_discovery_detail_logs_ != 0)
    {
        LXMF_RAW_RX_LOG("[LXMF][RawRX] detail_suppressed iface=lora public_discovery=1 phase=%s suppressed=%u\n",
                        phase ? phase : "-",
                        static_cast<unsigned>(suppressed_lora_discovery_detail_logs_));
        suppressed_lora_discovery_detail_logs_ = 0;
    }
    last_lora_discovery_detail_log_ms_ = now_ms;
    return true;
}

bool RawRxTelemetry::shouldLogLoraAnnounceIgnore(uint32_t now_ms,
                                                 uint32_t interval_ms)
{
    if (last_lora_announce_ignore_log_ms_ != 0 &&
        now_ms - last_lora_announce_ignore_log_ms_ < interval_ms)
    {
        ++suppressed_lora_announce_ignore_logs_;
        return false;
    }
    if (suppressed_lora_announce_ignore_logs_ != 0)
    {
        LXMF_RAW_RX_LOG("[LXMF][AnnounceRX] ignored_suppressed iface=lora suppressed=%u\n",
                        static_cast<unsigned>(suppressed_lora_announce_ignore_logs_));
        suppressed_lora_announce_ignore_logs_ = 0;
    }
    last_lora_announce_ignore_log_ms_ = now_ms;
    return true;
}

void RawRxTelemetry::noteSummary(bool wifi_skipped,
                                 bool duplicate,
                                 bool parse_failed_event,
                                 bool deferred_event,
                                 bool deferred_dropped_event,
                                 bool throttled_discovery_event,
                                 uint32_t now_ms,
                                 uint32_t summary_interval_ms)
{
    if (!wifi_skipped && !duplicate && !parse_failed_event &&
        !deferred_event && !deferred_dropped_event &&
        !throttled_discovery_event)
    {
        ++packets_;
    }
    if (wifi_skipped)
    {
        ++wifi_skipped_;
    }
    if (duplicate)
    {
        ++duplicates_;
    }
    if (parse_failed_event)
    {
        ++parse_failed_;
    }
    if (deferred_event)
    {
        ++deferred_;
    }
    if (deferred_dropped_event)
    {
        ++deferred_dropped_;
    }
    if (throttled_discovery_event)
    {
        ++throttled_discovery_;
    }

    if (last_summary_ms_ == 0)
    {
        last_summary_ms_ = now_ms;
        return;
    }
    if ((now_ms - last_summary_ms_) < summary_interval_ms)
    {
        return;
    }
    if (packets_ == 0 && wifi_skipped_ == 0 && duplicates_ == 0 &&
        parse_failed_ == 0 && deferred_ == 0 && deferred_dropped_ == 0 &&
        throttled_discovery_ == 0)
    {
        last_summary_ms_ = now_ms;
        return;
    }

#if !defined(TRAIL_MATE_VERBOSE_RUNTIME_LOGS) || !TRAIL_MATE_VERBOSE_RUNTIME_LOGS
    if (parse_failed_ != 0)
#endif
        LXMF_RAW_RX_LOG("[LXMF][RawRX] stats packets=%u wifi_skipped=%u duplicate=%u parse_failed=%u deferred=%u deferred_drop=%u throttled_discovery=%u\n",
                        static_cast<unsigned>(packets_),
                        static_cast<unsigned>(wifi_skipped_),
                        static_cast<unsigned>(duplicates_),
                        static_cast<unsigned>(parse_failed_),
                        static_cast<unsigned>(deferred_),
                        static_cast<unsigned>(deferred_dropped_),
                        static_cast<unsigned>(throttled_discovery_));
    packets_ = 0;
    wifi_skipped_ = 0;
    duplicates_ = 0;
    parse_failed_ = 0;
    deferred_ = 0;
    deferred_dropped_ = 0;
    throttled_discovery_ = 0;
    last_summary_ms_ = now_ms;
}

} // namespace chat::lxmf::runtime
