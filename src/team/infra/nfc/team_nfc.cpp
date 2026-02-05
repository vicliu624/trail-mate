/**
 * @file team_nfc.cpp
 * @brief NFC payload + key exchange helpers (Invite Code protected)
 */

#include "team_nfc.h"

#include <AES.h>
#include <Arduino.h>
#include <GCM.h>
#include <SHA256.h>
#include <cstring>

#ifdef USING_ST25R3916
#include "board/TLoRaPagerBoard.h"
#include "board/nfc_include.h"
#endif

namespace team::nfc
{
namespace
{
constexpr uint8_t kMagic[4] = {'T', 'N', 'F', '1'};
constexpr size_t kDerivedKeyLen = 16;
constexpr uint32_t kPbkdf2Iterations = 10000;
constexpr size_t kHeaderSize = 4 + 1 + team::proto::kTeamIdSize + 4 + 4 + kNfcSaltSize + kNfcNonceSize;
constexpr char kMimeType[] = "application/vnd.trailmate.teamkey";
constexpr uint8_t kT4tCcFileId[2] = {0xE1, 0x03};
constexpr uint8_t kT4tNdefFileId[2] = {0xE1, 0x04};
constexpr size_t kT4tCcFileLen = 15;

#ifndef TEAM_NFC_LOG_ENABLE
#define TEAM_NFC_LOG_ENABLE 1
#endif

#ifndef TEAM_NFC_LOG_SENSITIVE
#define TEAM_NFC_LOG_SENSITIVE 1
#endif

#if TEAM_NFC_LOG_ENABLE
#define TEAM_NFC_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define TEAM_NFC_LOG(...)
#endif

void log_hex(const char* label, const uint8_t* data, size_t len)
{
#if TEAM_NFC_LOG_ENABLE
    if (!label)
    {
        label = "";
    }
    TEAM_NFC_LOG("[NFC] %s (%u): ", label, static_cast<unsigned>(len));
    for (size_t i = 0; i < len; ++i)
    {
        TEAM_NFC_LOG("%02X", data ? data[i] : 0);
    }
    TEAM_NFC_LOG("\n");
#else
    (void)label;
    (void)data;
    (void)len;
#endif
}

void write_u32_le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

bool read_u32_le(const uint8_t* data, size_t len, size_t& offset, uint32_t& out)
{
    if (!data || offset + 4 > len)
    {
        return false;
    }
    out = static_cast<uint32_t>(data[offset]) |
          (static_cast<uint32_t>(data[offset + 1]) << 8) |
          (static_cast<uint32_t>(data[offset + 2]) << 16) |
          (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

void fill_random(uint8_t* out, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        out[i] = static_cast<uint8_t>(random(0, 256));
    }
}

void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t out[32])
{
    uint8_t key_block[64];
    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block))
    {
        SHA256 hash;
        hash.reset();
        hash.update(key, key_len);
        hash.finalize(key_block, sizeof(key_block));
    }
    else
    {
        memcpy(key_block, key, key_len);
    }

    uint8_t o_key_pad[64];
    uint8_t i_key_pad[64];
    for (size_t i = 0; i < sizeof(key_block); ++i)
    {
        o_key_pad[i] = static_cast<uint8_t>(key_block[i] ^ 0x5c);
        i_key_pad[i] = static_cast<uint8_t>(key_block[i] ^ 0x36);
    }

    uint8_t inner[32];
    SHA256 hash;
    hash.reset();
    hash.update(i_key_pad, sizeof(i_key_pad));
    hash.update(data, data_len);
    hash.finalize(inner, sizeof(inner));

    hash.reset();
    hash.update(o_key_pad, sizeof(o_key_pad));
    hash.update(inner, sizeof(inner));
    hash.finalize(out, 32);
}

bool pbkdf2_hmac_sha256(const uint8_t* password, size_t password_len,
                        const uint8_t* salt, size_t salt_len,
                        uint32_t iterations,
                        uint8_t* out, size_t out_len)
{
    if (!password || !salt || !out || out_len == 0 || out_len > 32 || iterations == 0)
    {
        return false;
    }

    uint8_t block[32];
    uint8_t u[32];
    uint8_t salt_block[64];
    if (salt_len + 4 > sizeof(salt_block))
    {
        return false;
    }

    memcpy(salt_block, salt, salt_len);
    salt_block[salt_len + 0] = 0;
    salt_block[salt_len + 1] = 0;
    salt_block[salt_len + 2] = 0;
    salt_block[salt_len + 3] = 1;

    hmac_sha256(password, password_len, salt_block, salt_len + 4, u);
    memcpy(block, u, sizeof(block));

    for (uint32_t i = 1; i < iterations; ++i)
    {
        hmac_sha256(password, password_len, u, sizeof(u), u);
        for (size_t j = 0; j < sizeof(block); ++j)
        {
            block[j] ^= u[j];
        }
    }

    memcpy(out, block, out_len);
    return true;
}

bool aes_gcm_encrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* plain, size_t plain_len,
                     uint8_t* out_cipher,
                     uint8_t* out_tag, size_t tag_len)
{
    if (!key || !nonce || !plain || !out_cipher || !out_tag)
    {
        return false;
    }

    GCM<AES128> gcm;
    if (!gcm.setKey(key, key_len))
    {
        return false;
    }
    if (!gcm.setIV(nonce, nonce_len))
    {
        return false;
    }
    if (aad && aad_len > 0)
    {
        gcm.addAuthData(aad, aad_len);
    }
    if (plain_len > 0)
    {
        gcm.encrypt(out_cipher, plain, plain_len);
    }
    gcm.computeTag(out_tag, tag_len);
    return true;
}

bool aes_gcm_decrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* cipher, size_t cipher_len,
                     const uint8_t* tag, size_t tag_len,
                     uint8_t* out_plain)
{
    if (!key || !nonce || !cipher || !tag || !out_plain)
    {
        return false;
    }

    GCM<AES128> gcm;
    if (!gcm.setKey(key, key_len))
    {
        return false;
    }
    if (!gcm.setIV(nonce, nonce_len))
    {
        return false;
    }
    if (aad && aad_len > 0)
    {
        gcm.addAuthData(aad, aad_len);
    }
    if (cipher_len > 0)
    {
        gcm.decrypt(out_plain, cipher, cipher_len);
    }
    return gcm.checkTag(tag, tag_len);
}

void build_aad(const Payload& payload, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(kHeaderSize);
    out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
    out.push_back(kNfcPayloadVersion);
    out.insert(out.end(), payload.team_id.begin(), payload.team_id.end());
    write_u32_le(out, payload.key_id);
    write_u32_le(out, payload.expires_at);
    out.insert(out.end(), payload.salt.begin(), payload.salt.end());
    out.insert(out.end(), payload.nonce.begin(), payload.nonce.end());
}

bool nfc_available()
{
#ifdef USING_ST25R3916
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    return board && board->isNFCReady() && board->nfc;
#else
    return false;
#endif
}

bool write_ndef_message(const std::vector<uint8_t>& payload)
{
#ifdef USING_ST25R3916
    TEAM_NFC_LOG("[NFC] write_ndef_message payload_len=%u\n", static_cast<unsigned>(payload.size()));
    if (!nfc_available())
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message nfc_not_available\n");
        return false;
    }

    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    rfalNfcDevice* dev = nullptr;
    if (board->nfc->rfalNfcGetActiveDevice(&dev) != ERR_NONE || !dev)
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message no_active_device\n");
        return false;
    }

    ndefConstBuffer8 type_buf{reinterpret_cast<const uint8_t*>(kMimeType),
                              static_cast<uint8_t>(sizeof(kMimeType) - 1)};
    ndefConstBuffer payload_buf{payload.data(), static_cast<uint32_t>(payload.size())};

    NdefClass ndef(board->nfc);
    if (ndef.ndefPollerContextInitializationWrapper(dev) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message ctx_init_failed\n");
        return false;
    }
    if (ndef.ndefPollerNdefDetectWrapper(nullptr) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message ndef_detect_failed\n");
        return false;
    }
    ndefMessage message{};
    ndefMessageInit(&message);

    ndefRecord record{};
    if (ndefRecordInit(&record, NDEF_TNF_MEDIA_TYPE, &type_buf, nullptr, &payload_buf) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message record_init_failed\n");
        return false;
    }
    if (ndefMessageAppend(&message, &record) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message message_append_failed\n");
        return false;
    }

    uint8_t raw_buf[256];
    ndefBuffer raw{raw_buf, sizeof(raw_buf)};
    if (ndefMessageEncode(&message, &raw) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] write_ndef_message encode_failed\n");
        return false;
    }

    // NOTE: This writes to a physical tag (poller mode). Card emulation still needs
    // a listen-mode responder; see TODO in start_share().
    bool ok = (ndef.ndefPollerWriteRawMessageWrapper(raw.buffer, raw.length) == ERR_NONE);
    TEAM_NFC_LOG("[NFC] write_ndef_message write_raw %s\n", ok ? "ok" : "fail");
    return ok;
#else
    (void)payload;
    return false;
#endif
}

bool read_ndef_message(std::vector<uint8_t>& out_payload)
{
#ifdef USING_ST25R3916
    TEAM_NFC_LOG("[NFC] read_ndef_message start\n");
    if (!nfc_available())
    {
        TEAM_NFC_LOG("[NFC] read_ndef_message nfc_not_available\n");
        return false;
    }

    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    rfalNfcDevice* dev = nullptr;
    if (board->nfc->rfalNfcGetActiveDevice(&dev) != ERR_NONE || !dev)
    {
        TEAM_NFC_LOG("[NFC] read_ndef_message no_active_device\n");
        return false;
    }

    NdefClass ndef(board->nfc);
    if (ndef.ndefPollerContextInitializationWrapper(dev) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] read_ndef_message ctx_init_failed\n");
        return false;
    }
    ndefInfo info{};
    if (ndef.ndefPollerNdefDetectWrapper(&info) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] read_ndef_message ndef_detect_failed\n");
        return false;
    }

    uint8_t raw_buf[256];
    uint32_t rcvd_len = 0;
    if (ndef.ndefPollerReadRawMessageWrapper(raw_buf, sizeof(raw_buf), &rcvd_len, false) != ERR_NONE || rcvd_len == 0)
    {
        TEAM_NFC_LOG("[NFC] read_ndef_message read_raw_failed\n");
        return false;
    }
    TEAM_NFC_LOG("[NFC] read_ndef_message raw_len=%u\n", static_cast<unsigned>(rcvd_len));

    ndefMessage message{};
    ndefMessageInit(&message);
    ndefConstBuffer msg_buf{raw_buf, rcvd_len};
    if (ndefMessageDecode(&msg_buf, &message) != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] read_ndef_message decode_failed\n");
        return false;
    }

    static const uint8_t kMimeType[] = "application/vnd.trailmate.teamkey";
    ndefConstBuffer8 type_buf{kMimeType, static_cast<uint8_t>(sizeof(kMimeType) - 1)};

    for (ndefRecord* rec = ndefMessageGetFirstRecord(&message); rec; rec = ndefMessageGetNextRecord(rec))
    {
        if (!ndefRecordTypeMatch(rec, NDEF_TNF_MEDIA_TYPE, &type_buf))
        {
            continue;
        }
        ndefConstBuffer payload_buf{};
        if (ndefRecordGetPayload(rec, &payload_buf) != ERR_NONE)
        {
            TEAM_NFC_LOG("[NFC] read_ndef_message payload_parse_failed\n");
            continue;
        }
        if (payload_buf.buffer && payload_buf.length > 0)
        {
            out_payload.assign(payload_buf.buffer, payload_buf.buffer + payload_buf.length);
            TEAM_NFC_LOG("[NFC] read_ndef_message payload_len=%u\n", static_cast<unsigned>(payload_buf.length));
            return true;
        }
    }
    TEAM_NFC_LOG("[NFC] read_ndef_message no_payload\n");
    return false;
#else
    (void)out_payload;
    return false;
#endif
}

bool s_scan_active = false;
bool s_share_active = false;
std::vector<uint8_t> s_share_payload;
uint32_t s_scan_deadline_ms = 0;

enum class T4tFile : uint8_t
{
    None,
    Cc,
    Ndef
};

enum class ShareState : uint8_t
{
    Idle,
    WaitingForCmd,
    SendingResp
};

ShareState s_share_state = ShareState::Idle;
T4tFile s_selected_file = T4tFile::None;
std::vector<uint8_t> s_ndef_file;
std::array<uint8_t, kT4tCcFileLen> s_cc_file{};
std::vector<uint8_t> s_share_response;
uint8_t* s_share_rx = nullptr;
uint16_t* s_share_rx_len = nullptr;
#ifdef USING_ST25R3916
rfalNfcState s_last_nfc_state = RFAL_NFC_STATE_NOTINIT;
#endif

void reset_share_exchange()
{
    s_share_state = ShareState::Idle;
    s_selected_file = T4tFile::None;
    s_share_response.clear();
    s_share_rx = nullptr;
    s_share_rx_len = nullptr;
#ifdef USING_ST25R3916
    s_last_nfc_state = RFAL_NFC_STATE_NOTINIT;
#endif
}

bool build_t4t_files(const std::vector<uint8_t>& payload)
{
    if (payload.empty())
    {
        TEAM_NFC_LOG("[NFC] build_t4t_files empty_payload\n");
        return false;
    }
    const size_t type_len = strlen(kMimeType);
    if (type_len > 255 || payload.size() > 255)
    {
        TEAM_NFC_LOG("[NFC] build_t4t_files oversized type_len=%u payload_len=%u\n",
                     static_cast<unsigned>(type_len), static_cast<unsigned>(payload.size()));
        return false;
    }

    const size_t msg_len = 1 + 1 + 1 + type_len + payload.size();
    if (msg_len > 0xFFFF)
    {
        TEAM_NFC_LOG("[NFC] build_t4t_files msg_len_overflow=%u\n", static_cast<unsigned>(msg_len));
        return false;
    }

    s_ndef_file.clear();
    s_ndef_file.reserve(2 + msg_len);
    s_ndef_file.push_back(static_cast<uint8_t>((msg_len >> 8) & 0xFF));
    s_ndef_file.push_back(static_cast<uint8_t>(msg_len & 0xFF));
    s_ndef_file.push_back(0xD2); // MB=1, ME=1, SR=1, TNF=0x02 (MIME)
    s_ndef_file.push_back(static_cast<uint8_t>(type_len));
    s_ndef_file.push_back(static_cast<uint8_t>(payload.size()));
    s_ndef_file.insert(s_ndef_file.end(), kMimeType, kMimeType + type_len);
    s_ndef_file.insert(s_ndef_file.end(), payload.begin(), payload.end());

    const uint16_t ndef_file_size = static_cast<uint16_t>(s_ndef_file.size());
    s_cc_file = {0x00, 0x0F, 0x20, 0x00, 0xFF, 0x00, 0xFF,
                 0x04, 0x06, kT4tNdefFileId[0], kT4tNdefFileId[1],
                 static_cast<uint8_t>((ndef_file_size >> 8) & 0xFF),
                 static_cast<uint8_t>(ndef_file_size & 0xFF),
                 0x00, 0xFF};
    TEAM_NFC_LOG("[NFC] build_t4t_files ok ndef_file_size=%u\n",
                 static_cast<unsigned>(ndef_file_size));
    log_hex("cc_file", s_cc_file.data(), s_cc_file.size());
    return true;
}

void append_status(std::vector<uint8_t>& out, uint16_t status)
{
    out.push_back(static_cast<uint8_t>((status >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(status & 0xFF));
}

void set_status(std::vector<uint8_t>& out, uint16_t status)
{
    out.clear();
    append_status(out, status);
}

bool select_file_by_id(const uint8_t* data, size_t len)
{
    if (!data || len != 2)
    {
        return false;
    }
    if (data[0] == kT4tCcFileId[0] && data[1] == kT4tCcFileId[1])
    {
        s_selected_file = T4tFile::Cc;
        return true;
    }
    if (data[0] == kT4tNdefFileId[0] && data[1] == kT4tNdefFileId[1])
    {
        s_selected_file = T4tFile::Ndef;
        return true;
    }
    return false;
}

void handle_apdu(const uint8_t* apdu, size_t len, std::vector<uint8_t>& response)
{
    response.clear();
    if (!apdu || len < 4)
    {
        TEAM_NFC_LOG("[NFC] apdu invalid len=%u\n", static_cast<unsigned>(len));
        set_status(response, 0x6700);
        return;
    }

    const uint8_t ins = apdu[1];
    const uint8_t p1 = apdu[2];
    const uint8_t p2 = apdu[3];
    TEAM_NFC_LOG("[NFC] apdu ins=0x%02X p1=0x%02X p2=0x%02X len=%u\n",
                 ins, p1, p2, static_cast<unsigned>(len));

    if (ins == 0xA4)
    {
        if (len < 5)
        {
            set_status(response, 0x6700);
            return;
        }
        const uint8_t lc = apdu[4];
        if (len < static_cast<size_t>(5 + lc))
        {
            set_status(response, 0x6700);
            return;
        }
        const uint8_t* data = apdu + 5;
        if (p1 == 0x04)
        {
            static const uint8_t kAidV2[] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};
            static const uint8_t kAidV1[] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x00};
            bool match_v2 = (lc == sizeof(kAidV2)) && (memcmp(data, kAidV2, sizeof(kAidV2)) == 0);
            bool match_v1 = (lc == sizeof(kAidV1)) && (memcmp(data, kAidV1, sizeof(kAidV1)) == 0);
            if (!match_v2 && !match_v1)
            {
                TEAM_NFC_LOG("[NFC] apdu select AID not_found lc=%u\n", static_cast<unsigned>(lc));
                set_status(response, 0x6A82);
                return;
            }
            s_selected_file = T4tFile::None;
            if (p2 == 0x00)
            {
                const uint8_t* aid = match_v2 ? kAidV2 : kAidV1;
                response.push_back(0x6F);
                response.push_back(static_cast<uint8_t>(2 + sizeof(kAidV2)));
                response.push_back(0x84);
                response.push_back(static_cast<uint8_t>(sizeof(kAidV2)));
                response.insert(response.end(), aid, aid + sizeof(kAidV2));
            }
            append_status(response, 0x9000);
            TEAM_NFC_LOG("[NFC] apdu select AID ok\n");
            return;
        }
        if (p1 == 0x00)
        {
            if (lc != 2)
            {
                TEAM_NFC_LOG("[NFC] apdu select file bad_lc=%u\n", static_cast<unsigned>(lc));
                set_status(response, 0x6700);
                return;
            }
            if (!select_file_by_id(data, lc))
            {
                TEAM_NFC_LOG("[NFC] apdu select file not_found\n");
                set_status(response, 0x6A82);
                return;
            }
            append_status(response, 0x9000);
            TEAM_NFC_LOG("[NFC] apdu select file ok\n");
            return;
        }

        set_status(response, 0x6A86);
        return;
    }

    if (ins == 0xB0)
    {
        if (len < 5)
        {
            set_status(response, 0x6700);
            return;
        }
        const uint16_t offset = (static_cast<uint16_t>(p1) << 8) | p2;
        uint16_t le = apdu[4];
        if (le == 0)
        {
            le = 256;
        }

        const uint8_t* file_data = nullptr;
        size_t file_len = 0;
        if (s_selected_file == T4tFile::Cc)
        {
            file_data = s_cc_file.data();
            file_len = s_cc_file.size();
        }
        else if (s_selected_file == T4tFile::Ndef)
        {
            file_data = s_ndef_file.data();
            file_len = s_ndef_file.size();
        }
        else
        {
            TEAM_NFC_LOG("[NFC] apdu read no_file_selected\n");
            set_status(response, 0x6985);
            return;
        }

        if (offset >= file_len)
        {
            TEAM_NFC_LOG("[NFC] apdu read offset_oob offset=%u file_len=%u\n",
                         static_cast<unsigned>(offset), static_cast<unsigned>(file_len));
            set_status(response, 0x6B00);
            return;
        }
        const size_t remaining = file_len - offset;
        const size_t to_copy = (static_cast<size_t>(le) < remaining) ? static_cast<size_t>(le) : remaining;
        response.insert(response.end(), file_data + offset, file_data + offset + to_copy);
        append_status(response, 0x9000);
        TEAM_NFC_LOG("[NFC] apdu read offset=%u le=%u copied=%u\n",
                     static_cast<unsigned>(offset), static_cast<unsigned>(le), static_cast<unsigned>(to_copy));
        return;
    }

    if (ins == 0xD6)
    {
        TEAM_NFC_LOG("[NFC] apdu update rejected (read-only)\n");
        set_status(response, 0x6982);
        return;
    }

    TEAM_NFC_LOG("[NFC] apdu unsupported ins=0x%02X\n", ins);
    set_status(response, 0x6D00);
}
} // namespace

bool encode_payload(const Payload& payload, std::vector<uint8_t>& out)
{
    TEAM_NFC_LOG("[NFC] encode_payload team_id_len=%u key_id=%u expires_at=%u\n",
                 static_cast<unsigned>(payload.team_id.size()),
                 payload.key_id,
                 payload.expires_at);
    log_hex("team_id", payload.team_id.data(), payload.team_id.size());
    log_hex("salt", payload.salt.data(), payload.salt.size());
    log_hex("nonce", payload.nonce.data(), payload.nonce.size());
    log_hex("cipher", payload.cipher.data(), payload.cipher.size());
    log_hex("tag", payload.tag.data(), payload.tag.size());

    out.clear();
    out.reserve(kHeaderSize + team::proto::kTeamChannelPskSize + kNfcTagSize);
    out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
    out.push_back(kNfcPayloadVersion);
    out.insert(out.end(), payload.team_id.begin(), payload.team_id.end());
    write_u32_le(out, payload.key_id);
    write_u32_le(out, payload.expires_at);
    out.insert(out.end(), payload.salt.begin(), payload.salt.end());
    out.insert(out.end(), payload.nonce.begin(), payload.nonce.end());
    out.insert(out.end(), payload.cipher.begin(), payload.cipher.end());
    out.insert(out.end(), payload.tag.begin(), payload.tag.end());
    return true;
}

bool decode_payload(const uint8_t* data, size_t len, Payload* out)
{
    TEAM_NFC_LOG("[NFC] decode_payload len=%u\n", static_cast<unsigned>(len));
    if (!data || !out)
    {
        TEAM_NFC_LOG("[NFC] decode_payload invalid_args\n");
        return false;
    }
    const size_t expected = kHeaderSize + team::proto::kTeamChannelPskSize + kNfcTagSize;
    if (len < expected)
    {
        TEAM_NFC_LOG("[NFC] decode_payload too_short expected=%u\n", static_cast<unsigned>(expected));
        return false;
    }
    if (memcmp(data, kMagic, sizeof(kMagic)) != 0)
    {
        TEAM_NFC_LOG("[NFC] decode_payload bad_magic\n");
        return false;
    }
    size_t offset = sizeof(kMagic);
    uint8_t version = data[offset++];
    if (version != kNfcPayloadVersion)
    {
        TEAM_NFC_LOG("[NFC] decode_payload bad_version=%u\n", static_cast<unsigned>(version));
        return false;
    }
    if (offset + team::proto::kTeamIdSize > len)
    {
        return false;
    }
    memcpy(out->team_id.data(), data + offset, team::proto::kTeamIdSize);
    offset += team::proto::kTeamIdSize;
    if (!read_u32_le(data, len, offset, out->key_id))
    {
        TEAM_NFC_LOG("[NFC] decode_payload key_id_failed\n");
        return false;
    }
    if (!read_u32_le(data, len, offset, out->expires_at))
    {
        TEAM_NFC_LOG("[NFC] decode_payload expires_failed\n");
        return false;
    }
    if (offset + kNfcSaltSize + kNfcNonceSize + team::proto::kTeamChannelPskSize + kNfcTagSize > len)
    {
        return false;
    }
    memcpy(out->salt.data(), data + offset, kNfcSaltSize);
    offset += kNfcSaltSize;
    memcpy(out->nonce.data(), data + offset, kNfcNonceSize);
    offset += kNfcNonceSize;
    memcpy(out->cipher.data(), data + offset, team::proto::kTeamChannelPskSize);
    offset += team::proto::kTeamChannelPskSize;
    memcpy(out->tag.data(), data + offset, kNfcTagSize);
    TEAM_NFC_LOG("[NFC] decode_payload ok key_id=%u expires_at=%u\n", out->key_id, out->expires_at);
    log_hex("team_id", out->team_id.data(), out->team_id.size());
    log_hex("salt", out->salt.data(), out->salt.size());
    log_hex("nonce", out->nonce.data(), out->nonce.size());
    log_hex("cipher", out->cipher.data(), out->cipher.size());
    log_hex("tag", out->tag.data(), out->tag.size());
    return true;
}

bool build_payload(const TeamId& team_id,
                   uint32_t key_id,
                   uint32_t expires_at,
                   const uint8_t* psk,
                   size_t psk_len,
                   const std::string& invite_code,
                   std::vector<uint8_t>& out)
{
    TEAM_NFC_LOG("[NFC] build_payload key_id=%u expires_at=%u psk_len=%u\n",
                 key_id, expires_at, static_cast<unsigned>(psk_len));
#if TEAM_NFC_LOG_SENSITIVE
    TEAM_NFC_LOG("[NFC] build_payload invite_code=%s\n", invite_code.c_str());
    log_hex("psk", psk, psk_len);
#endif
    if (!psk || psk_len != team::proto::kTeamChannelPskSize || invite_code.empty())
    {
        TEAM_NFC_LOG("[NFC] build_payload invalid_args\n");
        return false;
    }

    Payload payload;
    payload.team_id = team_id;
    payload.key_id = key_id;
    payload.expires_at = expires_at;
    fill_random(payload.salt.data(), payload.salt.size());
    fill_random(payload.nonce.data(), payload.nonce.size());

    uint8_t key[kDerivedKeyLen];
    if (!pbkdf2_hmac_sha256(reinterpret_cast<const uint8_t*>(invite_code.data()),
                            invite_code.size(),
                            payload.salt.data(),
                            payload.salt.size(),
                            kPbkdf2Iterations,
                            key, sizeof(key)))
    {
        TEAM_NFC_LOG("[NFC] build_payload kdf_failed\n");
        return false;
    }
#if TEAM_NFC_LOG_SENSITIVE
    log_hex("kdf_key", key, sizeof(key));
#endif

    std::vector<uint8_t> aad;
    build_aad(payload, aad);
    if (!aes_gcm_encrypt(key, sizeof(key),
                         payload.nonce.data(), payload.nonce.size(),
                         aad.data(), aad.size(),
                         psk, psk_len,
                         payload.cipher.data(),
                         payload.tag.data(), payload.tag.size()))
    {
        TEAM_NFC_LOG("[NFC] build_payload gcm_encrypt_failed\n");
        return false;
    }

    return encode_payload(payload, out);
}

bool decrypt_payload(const Payload& payload,
                     const std::string& invite_code,
                     std::array<uint8_t, team::proto::kTeamChannelPskSize>& out_psk)
{
    TEAM_NFC_LOG("[NFC] decrypt_payload key_id=%u expires_at=%u\n", payload.key_id, payload.expires_at);
#if TEAM_NFC_LOG_SENSITIVE
    TEAM_NFC_LOG("[NFC] decrypt_payload invite_code=%s\n", invite_code.c_str());
#endif
    if (invite_code.empty())
    {
        TEAM_NFC_LOG("[NFC] decrypt_payload empty_invite_code\n");
        return false;
    }
    uint8_t key[kDerivedKeyLen];
    if (!pbkdf2_hmac_sha256(reinterpret_cast<const uint8_t*>(invite_code.data()),
                            invite_code.size(),
                            payload.salt.data(),
                            payload.salt.size(),
                            kPbkdf2Iterations,
                            key, sizeof(key)))
    {
        TEAM_NFC_LOG("[NFC] decrypt_payload kdf_failed\n");
        return false;
    }
#if TEAM_NFC_LOG_SENSITIVE
    log_hex("kdf_key", key, sizeof(key));
#endif
    std::vector<uint8_t> aad;
    build_aad(payload, aad);
    bool ok = aes_gcm_decrypt(key, sizeof(key),
                              payload.nonce.data(), payload.nonce.size(),
                              aad.data(), aad.size(),
                              payload.cipher.data(), payload.cipher.size(),
                              payload.tag.data(), payload.tag.size(),
                              out_psk.data());
#if TEAM_NFC_LOG_SENSITIVE
    if (ok)
    {
        log_hex("psk", out_psk.data(), out_psk.size());
    }
#endif
    TEAM_NFC_LOG("[NFC] decrypt_payload %s\n", ok ? "ok" : "fail");
    return ok;
}

bool start_share(const std::vector<uint8_t>& payload)
{
#ifdef USING_ST25R3916
    TEAM_NFC_LOG("[NFC] start_share payload_len=%u\n", static_cast<unsigned>(payload.size()));
    s_share_payload = payload;
    s_share_active = false;
    reset_share_exchange();
    if (!build_t4t_files(payload))
    {
        TEAM_NFC_LOG("[NFC] start_share build_t4t_files_failed\n");
        return false;
    }

    if (!nfc_available())
    {
        TEAM_NFC_LOG("[NFC] start_share nfc_not_available\n");
        return false;
    }

    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    // Start listen mode for NFC-A (card emulation). APDU responses are
    // handled in poll_share().
    if (!board->startNFCDiscovery(RFAL_NFC_LISTEN_TECH_A, 60000))
    {
        TEAM_NFC_LOG("[NFC] start_share listen_start_failed\n");
        return false;
    }
    s_share_active = true;
    TEAM_NFC_LOG("[NFC] start_share ok\n");
    return true;
#else
    (void)payload;
    return false;
#endif
}

void stop_share()
{
#ifndef USING_ST25R3916
    return;
#endif
    TEAM_NFC_LOG("[NFC] stop_share\n");
    s_share_active = false;
    s_share_payload.clear();
    reset_share_exchange();
    s_ndef_file.clear();
#ifdef USING_ST25R3916
    if (nfc_available())
    {
        TLoRaPagerBoard::getInstance()->stopNFCDiscovery();
    }
#endif
}

void poll_share()
{
#ifndef USING_ST25R3916
    return;
#endif
#ifdef USING_ST25R3916
    if (!s_share_active || !nfc_available())
    {
        return;
    }

    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board || !board->nfc)
    {
        return;
    }

    if (board->LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(2)))
    {
        board->pollNfcIrq();
        board->nfc->rfalNfcWorker();
        board->LilyGoDispArduinoSPI::unlock();
    }

    rfalNfcState state = board->nfc->rfalNfcGetState();
    if (state != s_last_nfc_state)
    {
        TEAM_NFC_LOG("[NFC] poll_share state=%d\n", static_cast<int>(state));
        s_last_nfc_state = state;
    }
    if (state < RFAL_NFC_STATE_ACTIVATED)
    {
        return;
    }

    if (s_share_state == ShareState::Idle)
    {
        ReturnCode err = board->nfc->rfalNfcDataExchangeStart(nullptr, 0, &s_share_rx, &s_share_rx_len, RFAL_FWT_NONE);
        if (err == ERR_NONE)
        {
            s_share_state = ShareState::WaitingForCmd;
            TEAM_NFC_LOG("[NFC] poll_share wait_for_cmd\n");
        }
        else
        {
            TEAM_NFC_LOG("[NFC] poll_share start_wait_failed err=%d\n", err);
        }
        return;
    }

    ReturnCode err = board->nfc->rfalNfcDataExchangeGetStatus();
    if (err == ERR_BUSY)
    {
        return;
    }
    if (err == ERR_SLEEP_REQ || err == ERR_LINK_LOSS)
    {
        TEAM_NFC_LOG("[NFC] poll_share link_sleep err=%d\n", err);
        reset_share_exchange();
        return;
    }
    if (err != ERR_NONE)
    {
        TEAM_NFC_LOG("[NFC] poll_share exchange_err=%d\n", err);
        reset_share_exchange();
        return;
    }

    if (s_share_state == ShareState::WaitingForCmd)
    {
        size_t cmd_len = s_share_rx_len ? static_cast<size_t>(*s_share_rx_len) : 0;
        TEAM_NFC_LOG("[NFC] poll_share cmd_len=%u\n", static_cast<unsigned>(cmd_len));
        if (cmd_len > 0)
        {
            log_hex("apdu", s_share_rx, cmd_len);
        }
        handle_apdu(s_share_rx, cmd_len, s_share_response);
        err = board->nfc->rfalNfcDataExchangeStart(s_share_response.data(),
                                                   static_cast<uint16_t>(s_share_response.size()),
                                                   &s_share_rx, &s_share_rx_len,
                                                   RFAL_FWT_NONE);
        if (err == ERR_NONE)
        {
            s_share_state = ShareState::SendingResp;
            TEAM_NFC_LOG("[NFC] poll_share resp_len=%u\n", static_cast<unsigned>(s_share_response.size()));
            log_hex("rapdu", s_share_response.data(), s_share_response.size());
        }
        else
        {
            TEAM_NFC_LOG("[NFC] poll_share send_resp_failed err=%d\n", err);
            reset_share_exchange();
        }
        return;
    }

    if (s_share_state == ShareState::SendingResp)
    {
        TEAM_NFC_LOG("[NFC] poll_share resp_sent\n");
        s_share_state = ShareState::Idle;
    }
#else
    (void)0;
#endif
}

bool start_scan(uint16_t duration_ms)
{
#ifdef USING_ST25R3916
    s_scan_active = false;
    s_scan_deadline_ms = 0;

    TEAM_NFC_LOG("[NFC] start_scan duration_ms=%u\n", static_cast<unsigned>(duration_ms));
    if (!nfc_available())
    {
        TEAM_NFC_LOG("[NFC] start_scan nfc_not_available\n");
        return false;
    }
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board->startNFCDiscovery(RFAL_NFC_POLL_TECH_A, duration_ms))
    {
        TEAM_NFC_LOG("[NFC] start_scan discovery_failed\n");
        return false;
    }
    s_scan_active = true;
    s_scan_deadline_ms = millis() + duration_ms;
    TEAM_NFC_LOG("[NFC] start_scan ok deadline_ms=%u\n", static_cast<unsigned>(s_scan_deadline_ms));
    return true;
#else
    (void)duration_ms;
    return false;
#endif
}

void stop_scan()
{
#ifndef USING_ST25R3916
    return;
#endif
    TEAM_NFC_LOG("[NFC] stop_scan\n");
    s_scan_active = false;
    s_scan_deadline_ms = 0;
#ifdef USING_ST25R3916
    if (nfc_available())
    {
        TLoRaPagerBoard::getInstance()->stopNFCDiscovery();
    }
#endif
}

bool poll_scan(std::vector<uint8_t>& out_payload)
{
#ifndef USING_ST25R3916
    (void)out_payload;
    return false;
#endif
    if (!s_scan_active)
    {
        return false;
    }
    if (s_scan_deadline_ms != 0 && millis() > s_scan_deadline_ms)
    {
        TEAM_NFC_LOG("[NFC] poll_scan deadline_reached\n");
        stop_scan();
        return false;
    }
    if (read_ndef_message(out_payload))
    {
        TEAM_NFC_LOG("[NFC] poll_scan payload_len=%u\n", static_cast<unsigned>(out_payload.size()));
        stop_scan();
        return true;
    }
    return false;
}

bool is_scan_active()
{
    return s_scan_active;
}

bool is_share_active()
{
    return s_share_active;
}

} // namespace team::nfc
