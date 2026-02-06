/**
 * @file      GPS.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-07
 *
 */
#include "GPS.h"

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

bool GPS::init(Stream* stream)
{
    struct uBloxGnssModelInfo info;

    _stream = stream;
    assert(_stream);

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
                if (!strncmp(info.extension[i], "OD=", 3))
                {
                    strcpy((char*)buffer, &(info.extension[i][3]));
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
    uint16_t ubxFrameCounter = 0;
    uint32_t startTime = millis();
    uint16_t needRead = 0;
    uint32_t bytesRead = 0;
    assert(_stream);

    GPS_LOG("[GPS::getAck] Waiting for ACK (class=0x%02X, id=0x%02X), timeout=800ms\n", requestedClass, requestedID);

    while (millis() - startTime < 800)
    {
        while (_stream->available())
        {
            int c = _stream->read();
            bytesRead++;
            switch (ubxFrameCounter)
            {
            case 0:
                if (c == 0xB5)
                {
                    ubxFrameCounter++;
                    GPS_LOG("[GPS::getAck] Found sync byte 1 (0xB5)\n");
                }
                break;
            case 1:
                if (c == 0x62)
                {
                    ubxFrameCounter++;
                    GPS_LOG("[GPS::getAck] Found sync byte 2 (0x62)\n");
                }
                else
                {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                if (c == requestedClass)
                {
                    ubxFrameCounter++;
                    GPS_LOG("[GPS::getAck] Found class 0x%02X\n", c);
                }
                else
                {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                if (c == requestedID)
                {
                    ubxFrameCounter++;
                    GPS_LOG("[GPS::getAck] Found ID 0x%02X\n", c);
                }
                else
                {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                needRead = c;
                ubxFrameCounter++;
                GPS_LOG("[GPS::getAck] Length low byte: %d\n", c);
                break;
            case 5:
                needRead |= (c << 8);
                ubxFrameCounter++;
                GPS_LOG("[GPS::getAck] Length high byte: %d, total length: %d\n", c, needRead);
                break;
            case 6:
                if (needRead >= size)
                {
                    GPS_LOG("[GPS::getAck] ERROR: needRead (%d) >= size (%d)\n", needRead, size);
                    ubxFrameCounter = 0;
                    break;
                }
                if (_stream->readBytes(buffer, needRead) != needRead)
                {
                    GPS_LOG("[GPS::getAck] ERROR: Failed to read %d bytes\n", needRead);
                    ubxFrameCounter = 0;
                }
                else
                {
                    GPS_LOG("[GPS::getAck] SUCCESS: Read %d bytes, total bytes read: %lu\n", needRead, bytesRead);
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
    }
    GPS_LOG("[GPS::getAck] TIMEOUT: No ACK received after 800ms, bytes read: %lu, frame counter: %d\n", bytesRead, ubxFrameCounter);
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
    bool enable_gsv = true;
    switch (sentence_mask)
    {
    case 0: // GGA + RMC + GSV
        enable_gga = true;
        enable_rmc = true;
        enable_gsv = true;
        break;
    case 1: // RMC + GSV
        enable_gga = false;
        enable_rmc = true;
        enable_gsv = true;
        break;
    case 2: // GGA + RMC
        enable_gga = true;
        enable_rmc = true;
        enable_gsv = false;
        break;
    default:
        enable_gga = true;
        enable_rmc = true;
        enable_gsv = true;
        break;
    }

    if (output_hz == 0)
    {
        enable_gga = false;
        enable_rmc = false;
        enable_gsv = false;
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
    ok = ok && set_msg_rate(0x00, enable_gga ? output_hz : 0); // GGA
    ok = ok && set_msg_rate(0x04, enable_rmc ? output_hz : 0); // RMC
    ok = ok && set_msg_rate(0x03, enable_gsv ? output_hz : 0); // GSV
    GPS_LOG("[GPS] CFG-MSG nmea_rate=%u mask=%u ok=%d\n", output_hz, sentence_mask, ok ? 1 : 0);
    return ok;
}
