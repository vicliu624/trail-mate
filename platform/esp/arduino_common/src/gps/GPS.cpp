/**
 * @file      GPS.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-07
 *
 */
#include "platform/esp/arduino_common/gps/GPS.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef GPS_NMEA_SAMPLE_LOG_ENABLE
#define GPS_NMEA_SAMPLE_LOG_ENABLE 0
#endif

struct uBloxGnssModelInfo
{ // Structure to hold the module info (uses 341 bytes of RAM)
    char softVersion[30];
    char hardwareVersion[10];
    uint8_t extensionNo = 0;
    char extension[10][30];
};

GPS::GPS() : model("Unkown")
{
}

GPS::~GPS()
{
}

void GPS::attach(Stream* stream, ConfigProtocol config_protocol)
{
    _stream = stream;
    config_protocol_ = config_protocol;
    assert(_stream);
    debug_nmea_sentence_count_ = 0;
    debug_raw_sample_len_ = 0;
    debug_raw_first_ms_ = 0;
    debug_raw_sample_logged_ = false;
    debug_raw_saw_nmea_ = false;
    debug_raw_burst_sample_len_ = 0;
    debug_raw_burst_count_ = 0;
    debug_raw_burst_first_ms_ = 0;
    debug_raw_burst_last_ms_ = 0;
    debug_raw_burst_idle_gap_ms_ = 0;
    debug_raw_burst_bytes_ = 0;
    debug_raw_burst_printable_ = 0;
    debug_raw_burst_comma_ = 0;
    debug_raw_burst_high_ = 0;
    debug_raw_burst_zero_ = 0;
    debug_raw_burst_ubx_sync_ = 0;
    debug_raw_burst_logged_ = false;
    last_loop_read_bytes_ = 0;
}

bool GPS::init(Stream* stream)
{
    struct uBloxGnssModelInfo info
    {
    };

    attach(stream);

    GPS_LOG("[GPS::init] Starting GPS initialization, stream=%p\n", stream);

    uint8_t buffer[256];

    int retry = 3;

    while (retry--)
    {
        GPS_LOG("[GPS::init] Attempt %d/3: Getting GPS module version...\n", 4 - retry);
        // bool legacy_ubx_message = true;
        //  Get UBlox GPS module version
        uint8_t cfg_get_hw[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
        _stream->write(cfg_get_hw, sizeof(cfg_get_hw));
        GPS_LOG("[GPS::init] Sent UBX command, waiting for ACK...\n");

        uint16_t len = getAck(buffer, 256, 0x0A, 0x04);
        GPS_LOG("[GPS::init] getAck returned len=%d\n", len);
        if (len)
        {
            memset((void*)&info, 0, sizeof(info));
            uint16_t position = 0;
            for (int i = 0; i < 30; i++)
            {
                info.softVersion[i] = buffer[position];
                position++;
            }
            for (int i = 0; i < 10; i++)
            {
                info.hardwareVersion[i] = buffer[position];
                position++;
            }
            while (len >= position + 30)
            {
                for (int i = 0; i < 30; i++)
                {
                    info.extension[info.extensionNo][i] = buffer[position];
                    position++;
                }
                info.extensionNo++;
                if (info.extensionNo > 9)
                    break;
            }

            GPS_LOG("[GPS::init] Module Info:\n");
            GPS_LOG("[GPS::init]   Soft version: %s\n", info.softVersion);
            GPS_LOG("[GPS::init]   Hard version: %s\n", info.hardwareVersion);
            GPS_LOG("[GPS::init]   Extensions: %d\n", info.extensionNo);
            for (int i = 0; i < info.extensionNo; i++)
            {
                GPS_LOG("[GPS::init]   Extension[%d]: %s\n", i, info.extension[i]);
            }
            GPS_LOG("[GPS::init]   Model: %s\n", info.extension[2]);

            log_i("Module Info : ");
            log_i("Soft version: %s", info.softVersion);
            log_i("Hard version: %s", info.hardwareVersion);
            log_i("Extensions: %d", info.extensionNo);
            for (int i = 0; i < info.extensionNo; i++)
            {
                log_i("%s", info.extension[i]);
            }
            log_i("Model:%s", info.extension[2]);

            for (int i = 0; i < info.extensionNo; ++i)
            {
                if (!strncmp(info.extension[i], "MOD=", 4))
                {
                    memcpy(buffer, &info.extension[i][4], 26);
                    buffer[26] = '\0';
                    GPS_LOG("[GPS::init] GPS Model: %s\n", (char*)buffer);
                    log_i("GPS Model: %s", (char*)buffer);
                    model = (char*)buffer;
                }
            }
            GPS_LOG("[GPS::init] GPS initialization SUCCESS\n");
            return true;
        }
        GPS_LOG("[GPS::init] Attempt failed, retrying in 200ms...\n");
        delay(200);
    }

    GPS_LOG("[GPS::init] ERROR: Failed to find GPS after 3 attempts\n");
    log_e("Warning: Failed to find GPS.\n");
    return false;
}

int GPS::getAck(uint8_t* buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID)
{
    uint8_t phase = 0;
    const uint32_t startTime = millis();
    uint16_t needRead = 0;
    uint16_t payloadRead = 0;
    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    bool matches = false;
    bool checksum_a_ok = false;
    assert(_stream);

    while (millis() - startTime < 800)
    {
        while (_stream->available())
        {
            const int value = _stream->read();
            if (value < 0) continue;
            const uint8_t c = static_cast<uint8_t>(value);
            switch (phase)
            {
            case 0:
                if (c == 0xB5)
                {
                    phase = 1;
                }
                else
                {
                    // Configuration replies share UART with live navigation data.
                    // Keep parsing NMEA while waiting for a complete UBX response.
                    if (c == '$' || nmea_collecting_) encode(static_cast<char>(c));
                    handleNmeaChar(static_cast<char>(c));
                }
                break;
            case 1:
                phase = c == 0x62 ? 2 : (c == 0xB5 ? 1 : 0);
                break;
            case 2:
                matches = c == requestedClass;
                ck_a = ck_b = 0;
                calcUbxChecksum(&c, 1, ck_a, ck_b);
                phase = 3;
                break;
            case 3:
                matches = matches && c == requestedID;
                calcUbxChecksum(&c, 1, ck_a, ck_b);
                phase = 4;
                break;
            case 4:
                needRead = c;
                calcUbxChecksum(&c, 1, ck_a, ck_b);
                phase = 5;
                break;
            case 5:
                needRead |= static_cast<uint16_t>(c) << 8;
                payloadRead = 0;
                calcUbxChecksum(&c, 1, ck_a, ck_b);
                phase = needRead == 0 ? 7 : 6;
                break;
            case 6:
                if (matches && buffer != nullptr && needRead <= size)
                {
                    buffer[payloadRead] = c;
                }
                calcUbxChecksum(&c, 1, ck_a, ck_b);
                if (++payloadRead == needRead) phase = 7;
                break;
            case 7:
                checksum_a_ok = c == ck_a;
                phase = 8;
                break;
            case 8:
                if (checksum_a_ok && c == ck_b && matches && buffer != nullptr && needRead <= size)
                {
                    return needRead;
                }
                phase = 0;
                break;
            default:
                phase = 0;
                break;
            }
        }
        delay(1);
    }
    return 0;
}

bool GPS::factory()
{
    assert(_stream);

    uint8_t buffer[256];
    // Revert module Clear, save and load configurations
    // B5 62 06 09 0D 00 FF FB 00 00 00 00 00 00  FF FF 00 00 17 2B 7E
    uint8_t _legacy_message_reset[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x17, 0x2B, 0x7E};
    _stream->write(_legacy_message_reset, sizeof(_legacy_message_reset));
    if (!getAck(buffer, 256, 0x05, 0x01))
    {
        return false;
    }
    delay(50);

    // UBX-CFG-RATE, Size 8, 'Navigation/measurement rate settings'
    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
    _stream->write(cfg_rate, sizeof(cfg_rate));
    if (!getAck(buffer, 256, 0x06, 0x08))
    {
        return false;
    }
    log_d("GPS reset successes!");
    return true;
}

void GPS::calcUbxChecksum(const uint8_t* data, uint16_t len, uint8_t& ck_a, uint8_t& ck_b)
{
    for (uint16_t i = 0; i < len; ++i)
    {
        ck_a = static_cast<uint8_t>(ck_a + data[i]);
        ck_b = static_cast<uint8_t>(ck_b + ck_a);
    }
}

bool GPS::sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, bool wait_ack)
{
    if (_stream == nullptr)
    {
        return false;
    }

    uint8_t header[6] = {0xB5, 0x62, cls, id,
                         static_cast<uint8_t>(len & 0xFF),
                         static_cast<uint8_t>((len >> 8) & 0xFF)};

    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    calcUbxChecksum(&header[2], 4, ck_a, ck_b);
    if (payload && len > 0)
    {
        calcUbxChecksum(payload, len, ck_a, ck_b);
    }

    _stream->write(header, sizeof(header));
    if (payload && len > 0)
    {
        _stream->write(payload, len);
    }
    uint8_t checksum[2] = {ck_a, ck_b};
    _stream->write(checksum, sizeof(checksum));

    if (!wait_ack)
    {
        return true;
    }

    uint8_t ack_buf[4] = {};
    uint16_t ack_len = getAck(ack_buf, sizeof(ack_buf), 0x05, 0x01);
    if (ack_len < 2)
    {
        return false;
    }
    return ack_buf[0] == cls && ack_buf[1] == id;
}

bool GPS::setReceiverMode(uint8_t mode, uint8_t sat_mask)
{
    if (_stream == nullptr)
    {
        return false;
    }

    bool want_power_save = (mode != 0);
    if (want_power_save && (sat_mask & 0x2))
    {
        // Power Save is not supported when GLONASS is enabled.
        want_power_save = false;
    }

    if (config_protocol_ == ConfigProtocol::UbxValset)
    {
        static constexpr uint32_t key = 0x20D00001; // CFG-PM-OPERATEMODE
        const uint8_t value = want_power_save ? 1 : 0;
        return sendValsetU1(&key, &value, 1);
    }

    uint8_t payload[2] = {0x00, static_cast<uint8_t>(want_power_save ? 0x01 : 0x00)};
    bool ok = sendUbx(0x06, 0x11, payload, sizeof(payload), true);
    GPS_LOG("[GPS] CFG-RXM lpMode=%u ok=%d\n", payload[1], ok ? 1 : 0);
    return ok;
}

bool GPS::configureGnss(uint8_t sat_mask)
{
    if (_stream == nullptr)
    {
        return false;
    }

    if (config_protocol_ == ConfigProtocol::UbxValset)
    {
        // Keep each receiver's supported signal selection; only select systems.
        static constexpr uint32_t keys[] = {
            0x1031001F, // CFG-SIGNAL-GPS_ENA
            0x10310025, // CFG-SIGNAL-GLO_ENA
            0x10310021, // CFG-SIGNAL-GAL_ENA
            0x10310022, // CFG-SIGNAL-BDS_ENA
        };
        const uint8_t values[] = {static_cast<uint8_t>((sat_mask & 1) != 0),
                                  static_cast<uint8_t>((sat_mask & 2) != 0),
                                  static_cast<uint8_t>((sat_mask & 4) != 0),
                                  static_cast<uint8_t>((sat_mask & 8) != 0)};
        const bool ok = sendValsetU1(keys, values, 4);
        if (ok) delay(600); // CFG-SIGNAL restarts GNSS; M10 requires >= 500 ms.
        return ok;
    }

    struct GnssBlock
    {
        uint8_t gnss_id;
        uint8_t res_trk_ch;
        uint8_t max_trk_ch;
        uint8_t reserved1;
        uint32_t flags;
    };

    auto make_flags = [](bool enable, uint8_t sig_cfg_mask) -> uint32_t
    {
        uint32_t flags = 0;
        if (enable)
        {
            flags |= 0x01u;
        }
        flags |= (static_cast<uint32_t>(sig_cfg_mask) << 16);
        return flags;
    };

    const bool enable_gps = (sat_mask & 0x1) != 0;
    const bool enable_glo = (sat_mask & 0x2) != 0;
    const bool enable_gal = (sat_mask & 0x4) != 0;
    const bool enable_bds = (sat_mask & 0x8) != 0;

    // Defaults for SPG 3.0x (numConfigBlocks = 7).
    GnssBlock blocks[] = {
        {0, 8, 16, 0, make_flags(enable_gps, 0x01)}, // GPS
        {1, 1, 3, 0, make_flags(false, 0x00)},       // SBAS
        {2, 4, 8, 0, make_flags(enable_gal, 0x01)},  // Galileo
        {3, 8, 16, 0, make_flags(enable_bds, 0x01)}, // BeiDou
        {4, 0, 8, 0, make_flags(false, 0x00)},       // IMES
        {5, 0, 3, 0, make_flags(false, 0x00)},       // QZSS
        {6, 8, 14, 0, make_flags(enable_glo, 0x01)}  // GLONASS
    };

    uint8_t payload[4 + sizeof(blocks)] = {};
    payload[0] = 0x00; // msgVer
    payload[1] = 32;   // numTrkChHw
    payload[2] = 32;   // numTrkChUse
    payload[3] = static_cast<uint8_t>(sizeof(blocks) / sizeof(blocks[0]));
    memcpy(&payload[4], blocks, sizeof(blocks));

    bool ok = sendUbx(0x06, 0x3E, payload, sizeof(payload), true);
    GPS_LOG("[GPS] CFG-GNSS mask=0x%02X ok=%d\n", sat_mask, ok ? 1 : 0);

    if (ok)
    {
        delay(600); // allow receiver to reinitialize GNSS config
    }
    return ok;
}

bool GPS::configureNmeaOutput(uint8_t output_hz, uint8_t sentence_mask)
{
    if (_stream == nullptr)
    {
        return false;
    }

    bool enable_gga = true;
    bool enable_rmc = true;
    bool enable_gsa = true;
    bool enable_gsv = true;
    switch (sentence_mask)
    {
    case 0: // GGA + RMC + GSA + GSV
        enable_gga = true;
        enable_rmc = true;
        enable_gsa = true;
        enable_gsv = true;
        break;
    case 1: // RMC + GSA + GSV
        enable_gga = false;
        enable_rmc = true;
        enable_gsa = true;
        enable_gsv = true;
        break;
    case 2: // GGA + RMC
        enable_gga = true;
        enable_rmc = true;
        enable_gsa = false;
        enable_gsv = false;
        break;
    default:
        enable_gga = true;
        enable_rmc = true;
        enable_gsa = true;
        enable_gsv = true;
        break;
    }

    if (output_hz == 0)
    {
        enable_gga = false;
        enable_rmc = false;
        enable_gsa = false;
        enable_gsv = false;
    }

    if (config_protocol_ == ConfigProtocol::UbxValset)
    {
        static constexpr uint32_t keys[] = {
            0x209100BB, // CFG-MSGOUT-NMEA_ID_GGA_UART1
            0x209100C0, // CFG-MSGOUT-NMEA_ID_GSA_UART1
            0x209100AC, // CFG-MSGOUT-NMEA_ID_RMC_UART1
            0x209100C5, // CFG-MSGOUT-NMEA_ID_GSV_UART1
        };
        const uint8_t values[] = {static_cast<uint8_t>(enable_gga ? output_hz : 0),
                                  static_cast<uint8_t>(enable_gsa ? output_hz : 0),
                                  static_cast<uint8_t>(enable_rmc ? output_hz : 0),
                                  static_cast<uint8_t>(enable_gsv ? output_hz : 0)};
        return sendValsetU1(keys, values, 4);
    }

    auto set_msg_rate = [this](uint8_t msg_id, uint8_t rate) -> bool
    {
        uint8_t payload[8] = {
            0xF0, // NMEA standard message class
            msg_id,
            0x00, // I2C
            rate, // UART1
            0x00, // UART2
            0x00, // USB
            0x00, // SPI
            0x00  // reserved
        };
        return sendUbx(0x06, 0x01, payload, sizeof(payload), true);
    };

    bool ok = true;
    ok = set_msg_rate(0x00, enable_gga ? output_hz : 0) && ok; // GGA
    ok = set_msg_rate(0x02, enable_gsa ? output_hz : 0) && ok; // GSA
    ok = set_msg_rate(0x04, enable_rmc ? output_hz : 0) && ok; // RMC
    ok = set_msg_rate(0x03, enable_gsv ? output_hz : 0) && ok; // GSV
    Serial.printf("[GPS] nmea config rate=%u mask=%u gga=%u gsa=%u rmc=%u gsv=%u ok=%d\n",
                  static_cast<unsigned>(output_hz),
                  static_cast<unsigned>(sentence_mask),
                  enable_gga ? 1U : 0U,
                  enable_gsa ? 1U : 0U,
                  enable_rmc ? 1U : 0U,
                  enable_gsv ? 1U : 0U,
                  ok ? 1 : 0);
    return ok;
}

bool GPS::sendValsetU1(const uint32_t* keys, const uint8_t* values, size_t count)
{
    if (keys == nullptr || values == nullptr || count == 0 || count > 4) return false;
    // RAM layer only: never rewrite receiver BBR/flash on application startup.
    uint8_t payload[4 + 4 * 5] = {0, 1, 0, 0};
    size_t offset = 4;
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t key = keys[i];
        payload[offset++] = static_cast<uint8_t>(key);
        payload[offset++] = static_cast<uint8_t>(key >> 8);
        payload[offset++] = static_cast<uint8_t>(key >> 16);
        payload[offset++] = static_cast<uint8_t>(key >> 24);
        payload[offset++] = values[i];
    }
    return sendUbx(0x06, 0x8A, payload, static_cast<uint16_t>(offset), true);
}

size_t GPS::getSatellites(gps::GnssSatInfo* out, size_t max) const
{
    if (!out || max == 0)
    {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < gps::kMaxGnssSats && count < max; ++i)
    {
        const SatEntry& entry = sats_[i];
        if (!entry.valid)
        {
            continue;
        }
        gps::GnssSatInfo sat{};
        sat.id = entry.id;
        sat.sys = entry.sys;
        sat.azimuth = entry.azimuth;
        sat.elevation = entry.elevation;
        sat.snr = entry.snr;
        sat.used = entry.used;
        out[count++] = sat;
    }
    return count;
}

gps::GnssStatus GPS::getGnssStatus() const
{
    gps::GnssStatus st{};
    st.sats_in_use = sats_in_use_;
    st.sats_in_view = sats_in_view_;
    st.hdop = hdop_;
    st.fix = fix_type_;
    return st;
}

void GPS::observeDebugByte(uint8_t b)
{
    if (b == '$')
    {
        debug_raw_saw_nmea_ = true;
        debug_raw_sample_len_ = 0;
        if (debug_raw_burst_sample_len_ > 0 && !debug_raw_burst_logged_)
        {
            logDebugRawBurst("interrupted_by_nmea", millis());
        }
        return;
    }
    if (debug_raw_sample_logged_)
    {
        observeDebugRawBurstByte(b, millis());
    }
    if (debug_raw_saw_nmea_ || debug_raw_sample_logged_)
    {
        return;
    }
    if (debug_raw_sample_len_ < sizeof(debug_raw_sample_))
    {
        if (debug_raw_sample_len_ == 0)
        {
            debug_raw_first_ms_ = millis();
        }
        debug_raw_sample_[debug_raw_sample_len_++] = b;
    }
    if (debug_raw_sample_len_ == sizeof(debug_raw_sample_))
    {
        logDebugRawSample("first64_no_nmea");
    }
}

void GPS::flushDebugRawSampleIfStale(uint32_t now_ms)
{
    if (debug_raw_saw_nmea_ || debug_raw_sample_logged_ || debug_raw_sample_len_ == 0 || debug_raw_first_ms_ == 0)
    {
        if (debug_raw_burst_sample_len_ > 0 && !debug_raw_burst_logged_ &&
            debug_raw_burst_last_ms_ != 0 && (now_ms - debug_raw_burst_last_ms_) >= 1000UL)
        {
            logDebugRawBurst("partial_idle_1s", now_ms);
        }
        return;
    }
    if ((now_ms - debug_raw_first_ms_) >= 2000UL)
    {
        logDebugRawSample("partial_no_nmea_2s");
    }
}

void GPS::logDebugRawSample(const char* reason)
{
    if (debug_raw_sample_logged_ || debug_raw_sample_len_ == 0)
    {
        return;
    }
    char hex[sizeof(debug_raw_sample_) * 3 + 1]{};
    size_t pos = 0;
    for (size_t i = 0; i < debug_raw_sample_len_ && pos + 3 < sizeof(hex); ++i)
    {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X%s",
                        static_cast<unsigned>(debug_raw_sample_[i]),
                        (i + 1 == debug_raw_sample_len_) ? "" : " ");
    }
    Serial.printf("[GPS][RAW] reason=%s len=%u hex=%s\n",
                  reason ? reason : "no_nmea",
                  static_cast<unsigned>(debug_raw_sample_len_),
                  hex);
    debug_raw_sample_logged_ = true;
}

void GPS::observeDebugRawBurstByte(uint8_t b, uint32_t now_ms)
{
    constexpr uint32_t kBurstIdleGapMs = 2000UL;
    constexpr uint16_t kMaxLoggedBursts = 8;

    if (debug_raw_saw_nmea_ || debug_raw_burst_count_ >= kMaxLoggedBursts)
    {
        return;
    }

    uint32_t idle_gap_ms = 0;
    if (debug_raw_burst_last_ms_ != 0 && now_ms >= debug_raw_burst_last_ms_)
    {
        idle_gap_ms = now_ms - debug_raw_burst_last_ms_;
    }

    if (debug_raw_burst_last_ms_ != 0 && idle_gap_ms >= kBurstIdleGapMs)
    {
        if (debug_raw_burst_sample_len_ > 0 && !debug_raw_burst_logged_)
        {
            logDebugRawBurst("partial_before_gap", now_ms);
        }
        debug_raw_burst_sample_len_ = 0;
        debug_raw_burst_first_ms_ = 0;
        debug_raw_burst_idle_gap_ms_ = idle_gap_ms;
        debug_raw_burst_bytes_ = 0;
        debug_raw_burst_printable_ = 0;
        debug_raw_burst_comma_ = 0;
        debug_raw_burst_high_ = 0;
        debug_raw_burst_zero_ = 0;
        debug_raw_burst_ubx_sync_ = 0;
        debug_raw_burst_logged_ = false;
    }

    if (debug_raw_burst_sample_len_ == 0)
    {
        debug_raw_burst_first_ms_ = now_ms;
        debug_raw_burst_idle_gap_ms_ = idle_gap_ms;
    }

    if (debug_raw_burst_sample_len_ < sizeof(debug_raw_burst_sample_))
    {
        debug_raw_burst_sample_[debug_raw_burst_sample_len_++] = b;
    }
    debug_raw_burst_bytes_++;
    if (b >= 32 && b <= 126)
    {
        debug_raw_burst_printable_++;
    }
    if (b == ',')
    {
        debug_raw_burst_comma_++;
    }
    if (b >= 128)
    {
        debug_raw_burst_high_++;
    }
    if (b == 0)
    {
        debug_raw_burst_zero_++;
    }
    if (b == 0xB5 || b == 0x62)
    {
        debug_raw_burst_ubx_sync_++;
    }
    debug_raw_burst_last_ms_ = now_ms;

    if (!debug_raw_burst_logged_ && debug_raw_burst_sample_len_ == sizeof(debug_raw_burst_sample_))
    {
        logDebugRawBurst("non_nmea_burst64", now_ms);
    }
}

void GPS::logDebugRawBurst(const char* reason, uint32_t now_ms)
{
    if (debug_raw_burst_logged_ || debug_raw_burst_sample_len_ == 0)
    {
        return;
    }
    char hex[sizeof(debug_raw_burst_sample_) * 3 + 1]{};
    size_t pos = 0;
    for (size_t i = 0; i < debug_raw_burst_sample_len_ && pos + 3 < sizeof(hex); ++i)
    {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X%s",
                        static_cast<unsigned>(debug_raw_burst_sample_[i]),
                        (i + 1 == debug_raw_burst_sample_len_) ? "" : " ");
    }
    const uint32_t age_ms = (debug_raw_burst_first_ms_ > 0 && now_ms >= debug_raw_burst_first_ms_)
                                ? (now_ms - debug_raw_burst_first_ms_)
                                : 0;
    Serial.printf("[GPS][RAW_BURST] n=%u reason=%s len=%u bytes=%lu gap_ms=%lu age_ms=%lu printable=%u comma=%u high=%u zero=%u ubx_sync=%u hex=%s\n",
                  static_cast<unsigned>(debug_raw_burst_count_ + 1),
                  reason ? reason : "non_nmea",
                  static_cast<unsigned>(debug_raw_burst_sample_len_),
                  static_cast<unsigned long>(debug_raw_burst_bytes_),
                  static_cast<unsigned long>(debug_raw_burst_idle_gap_ms_),
                  static_cast<unsigned long>(age_ms),
                  static_cast<unsigned>(debug_raw_burst_printable_),
                  static_cast<unsigned>(debug_raw_burst_comma_),
                  static_cast<unsigned>(debug_raw_burst_high_),
                  static_cast<unsigned>(debug_raw_burst_zero_),
                  static_cast<unsigned>(debug_raw_burst_ubx_sync_),
                  hex);
    debug_raw_burst_count_++;
    debug_raw_burst_logged_ = true;
}

void GPS::logDebugNmeaSentence(const char* sentence)
{
#if GPS_NMEA_SAMPLE_LOG_ENABLE
    if (!sentence || debug_nmea_sentence_count_ >= 8)
    {
        return;
    }

    char preview[128]{};
    size_t out = 0;
    for (size_t i = 0; sentence[i] != '\0' && out < sizeof(preview) - 1; ++i)
    {
        char c = sentence[i];
        if (c == '\r' || c == '\n')
        {
            break;
        }
        preview[out++] = (c >= 32 && c <= 126) ? c : '.';
    }
    preview[out] = '\0';
    Serial.printf("[GPS][NMEA] sample%u=%s\n",
                  static_cast<unsigned>(debug_nmea_sentence_count_ + 1),
                  preview);
    debug_nmea_sentence_count_++;
#else
    (void)sentence;
#endif
}

void GPS::handleNmeaChar(char c)
{
    if (c == '$')
    {
        nmea_collecting_ = true;
        nmea_len_ = 0;
        nmea_buf_[nmea_len_++] = c;
        return;
    }

    if (!nmea_collecting_)
    {
        return;
    }

    if (nmea_len_ < (sizeof(nmea_buf_) - 1))
    {
        nmea_buf_[nmea_len_++] = c;
    }

    if (c == '\n')
    {
        nmea_buf_[nmea_len_] = '\0';
        nmea_collecting_ = false;
        parseNmeaSentence(nmea_buf_);
    }
}

void GPS::parseNmeaSentence(char* sentence)
{
    if (!sentence || sentence[0] != '$')
    {
        return;
    }
    logDebugNmeaSentence(sentence);

    char* start = sentence + 1;
    char* checksum = strchr(start, '*');
    if (checksum)
    {
        *checksum = '\0';
    }

    for (char* p = start; *p; ++p)
    {
        if (*p == '\r' || *p == '\n')
        {
            *p = '\0';
            break;
        }
    }

    char* fields[32]{};
    int field_count = 0;
    fields[field_count++] = start;
    for (char* p = start; *p && field_count < 32; ++p)
    {
        if (*p == ',')
        {
            *p = '\0';
            fields[field_count++] = p + 1;
        }
    }

    if (field_count <= 0 || strlen(fields[0]) < 5)
    {
        return;
    }

    const char* talker = fields[0];
    const char* type = fields[0] + 2;
    if (strncmp(type, "GSV", 3) == 0)
    {
        parseGsv(talker, fields, field_count);
    }
    else if (strncmp(type, "GSA", 3) == 0)
    {
        parseGsa(talker, fields, field_count);
    }
}

void GPS::parseGsv(const char* talker, char** fields, int field_count)
{
    if (!talker || field_count < 4)
    {
        return;
    }

    const int msg_num = atoi(fields[2]);
    if (msg_num == 1)
    {
        if (strncmp(talker, "GN", 2) == 0)
        {
            for (size_t i = 0; i < gps::kMaxGnssSats; ++i)
            {
                sats_[i] = SatEntry{};
            }
        }
        else
        {
            gps::GnssSystem sys_hint = sysFromTalker(talker, 0);
            if (sys_hint != gps::GnssSystem::UNKNOWN)
            {
                clearSatellitesForSystem(sys_hint);
            }
        }
    }

    for (int i = 4; i + 3 < field_count; i += 4)
    {
        int id = atoi(fields[i]);
        if (id <= 0)
        {
            continue;
        }
        int elev = atoi(fields[i + 1]);
        int azim = atoi(fields[i + 2]);
        int snr = (fields[i + 3] && fields[i + 3][0] != '\0') ? atoi(fields[i + 3]) : -1;

        gps::GnssSystem sys = sysFromTalker(talker, id);
        int idx = findSatellite(sys, id);
        if (idx < 0)
        {
            idx = allocSatellite();
        }
        if (idx < 0)
        {
            continue;
        }

        SatEntry& entry = sats_[idx];
        entry.valid = true;
        entry.sys = sys;
        entry.id = static_cast<uint16_t>(id);
        entry.elevation = static_cast<uint8_t>(std::max(0, std::min(90, elev)));
        if (azim < 0) azim = 0;
        if (azim > 359) azim = 359;
        entry.azimuth = static_cast<uint16_t>(azim);
        entry.snr = static_cast<int8_t>(snr);
    }

    recalcCounts();
}

void GPS::parseGsa(const char* talker, char** fields, int field_count)
{
    if (!talker || field_count < 3)
    {
        return;
    }

    int fix = atoi(fields[2]);
    if (fix <= 1)
    {
        fix_type_ = gps::GnssFix::NOFIX;
    }
    else if (fix == 2)
    {
        fix_type_ = gps::GnssFix::FIX2D;
    }
    else
    {
        fix_type_ = gps::GnssFix::FIX3D;
    }

    gps::GnssSystem sys_hint = sysFromTalker(talker, 0);
    clearUsedForSystem(sys_hint);

    int start = 3;
    int end = std::min(field_count, 15);
    for (int i = start; i < end; ++i)
    {
        int id = atoi(fields[i]);
        if (id <= 0)
        {
            continue;
        }
        gps::GnssSystem sys = sysFromTalker(talker, id);
        int idx = findSatellite(sys, id);
        if (idx < 0)
        {
            idx = allocSatellite();
        }
        if (idx < 0)
        {
            continue;
        }
        SatEntry& entry = sats_[idx];
        entry.valid = true;
        entry.sys = sys;
        entry.id = static_cast<uint16_t>(id);
        entry.used = true;
    }

    if (field_count >= 17 && fields[16] && fields[16][0] != '\0')
    {
        hdop_ = static_cast<float>(atof(fields[16]));
    }
    else
    {
        hdop_ = 0.0f;
    }

    recalcCounts();
}

gps::GnssSystem GPS::sysFromTalker(const char* talker, int sat_id) const
{
    if (!talker || strlen(talker) < 2)
    {
        return gps::GnssSystem::UNKNOWN;
    }

    if (strncmp(talker, "GP", 2) == 0)
    {
        return gps::GnssSystem::GPS;
    }
    if (strncmp(talker, "GL", 2) == 0)
    {
        return gps::GnssSystem::GLN;
    }
    if (strncmp(talker, "GA", 2) == 0)
    {
        return gps::GnssSystem::GAL;
    }
    if (strncmp(talker, "BD", 2) == 0 || strncmp(talker, "GB", 2) == 0)
    {
        return gps::GnssSystem::BD;
    }
    if (strncmp(talker, "GN", 2) == 0)
    {
        if (sat_id <= 0)
        {
            return gps::GnssSystem::UNKNOWN;
        }
        if (sat_id >= 201 && sat_id <= 237)
        {
            return gps::GnssSystem::BD;
        }
        if (sat_id >= 301 && sat_id <= 336)
        {
            return gps::GnssSystem::GAL;
        }
        if (sat_id >= 65 && sat_id <= 96)
        {
            return gps::GnssSystem::GLN;
        }
        return gps::GnssSystem::GPS;
    }

    return gps::GnssSystem::UNKNOWN;
}

void GPS::clearSatellitesForSystem(gps::GnssSystem sys)
{
    if (sys == gps::GnssSystem::UNKNOWN)
    {
        return;
    }
    for (size_t i = 0; i < gps::kMaxGnssSats; ++i)
    {
        if (sats_[i].valid && sats_[i].sys == sys)
        {
            sats_[i] = SatEntry{};
        }
    }
}

void GPS::clearUsedForSystem(gps::GnssSystem sys)
{
    for (size_t i = 0; i < gps::kMaxGnssSats; ++i)
    {
        if (!sats_[i].valid)
        {
            continue;
        }
        if (sys == gps::GnssSystem::UNKNOWN || sats_[i].sys == sys)
        {
            sats_[i].used = false;
        }
    }
}

int GPS::findSatellite(gps::GnssSystem sys, int id) const
{
    for (size_t i = 0; i < gps::kMaxGnssSats; ++i)
    {
        const SatEntry& entry = sats_[i];
        if (!entry.valid)
        {
            continue;
        }
        if (entry.id == id &&
            (sys == gps::GnssSystem::UNKNOWN || entry.sys == sys || entry.sys == gps::GnssSystem::UNKNOWN))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int GPS::allocSatellite()
{
    for (size_t i = 0; i < gps::kMaxGnssSats; ++i)
    {
        if (!sats_[i].valid)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void GPS::recalcCounts()
{
    uint8_t in_view = 0;
    uint8_t in_use = 0;
    for (size_t i = 0; i < gps::kMaxGnssSats; ++i)
    {
        if (!sats_[i].valid)
        {
            continue;
        }
        in_view++;
        if (sats_[i].used)
        {
            in_use++;
        }
    }
    sats_in_view_ = in_view;
    sats_in_use_ = in_use;
}
