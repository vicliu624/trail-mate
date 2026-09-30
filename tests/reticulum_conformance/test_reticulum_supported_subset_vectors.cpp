#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/reticulum/lxst_call_state_machine.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "team/protocol/team_portnum.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace lxmf = chat::lxmf;
namespace lxst = chat::reticulum::lxst;
namespace lxst_call = chat::reticulum::lxst::call;
namespace reticulum = chat::reticulum;

constexpr const char* kFullHashTrailMate =
    "a313970d0f81a10c7d842c64938c43ac5a76372541b91104c8fd364d09c0af95";
constexpr const char* kTruncatedHashTrailMate =
    "a313970d0f81a10c7d842c64938c43ac";
constexpr const char* kNameHashLxmfDelivery =
    "6ec60bc318e2c0f0d908";
constexpr const char* kNameHashLxmfPropagation =
    "e03a09b77ac21b22258e";
constexpr const char* kIdentityHashIncrementalPublicKey =
    "20a7ec84684f7fe124cb3727d049734a";
constexpr const char* kDestinationHashDelivery =
    "c48232d0130fd6a336fb574f90f4bfcb";
constexpr const char* kDestinationHashPropagation =
    "025ce3c73c4780881210b492df17d70d";
constexpr const char* kPlainDestinationHashDelivery =
    "9497d16c52ac5faec04c36db5c301e8e";
constexpr const char* kPathRequestDestinationHash =
    "6b9f66014d9853faab220fba47d02761";
constexpr const char* kProtocolProbeAnnounce =
    "0100f083a7f4b00d799808c44a4634bba7d7006afd960bf3b01801a2e88b2ce1f7040817dc1b6bffa"
    "366b103468f3988e0db7f00dc1fdc15fa7fd31a34a02207cfb4d26e11e57504e43686ec7fad84774bec"
    "88fd68805f2ea383c8d6f6c39824652e00698fd5fd1088b38832f247a9daebf017d8bfe641882d9fe9b3"
    "7cf49a97402b7e3d8bec61b4950d39c0996588dd0288bf6a7a0a4390bb331bd82704b618f107cf8bf223"
    "0f";
constexpr const char* kOfferPathHash =
    "94fd9fd7b04a5caae5882616446bb9ef";
constexpr const char* kGetPathHash =
    "9dc1a72883468f57fed571e796e9ce98";
constexpr uint32_t kDeliveryNodeId = 2431958987UL;

constexpr const char* kHeader1DataPacket =
    "0002101112131415161718191a1b1c1d1e1f00a0a1a2";
constexpr const char* kHeader1DataPacketHash =
    "f75d624dffe88286698bc382578ea71ff622f07d03da3eaaa463dbe89c9a95a3";
constexpr const char* kHeader1DataPacketTruncatedHash =
    "f75d624dffe88286698bc382578ea71f";
constexpr const char* kHeader2ResourcePacket =
    "7005303132333435363738393a3b3c3d3e3f101112131415161718191a1b1c1d1e1f01a0a1a2";
constexpr const char* kHeader2ResourcePacketHash =
    "8e5eef8ca7293a02b93deb627ba24571cf207cd2509b5852e05cf7b40a4f3ba9";
constexpr const char* kProofPacket =
    "0300f75d624dffe88286698bc382578ea71f0055667788";

constexpr const char* kTextPayload =
    "94cb40934a0000000000c405547261696cc40f68656c6c6f207265746963756c756d80";
constexpr const char* kSidebandTelemetryTextPayload =
    "94cb0000000000000000c400c4008102c42a810297"
    "c40403956940"
    "c404017831f1"
    "c40400003039"
    "c40400000000"
    "c40400000000"
    "c40200fa"
    "ce65ec8780";
constexpr const char* kSidebandTelemetryRequestPayload =
    "94cb0000000000000000c400c400810991810192ce65ec8780c3";
constexpr const char* kLxstAvailableSignal = "81009103";
constexpr const char* kLxstPreferredLowSignal = "810091cd012f";
constexpr const char* kLxstCodec2Frames = "8101c4050206aabbcc";
constexpr const char* kPeerAnnounceAppData =
    "92c4087669636c69752d31c0";
constexpr const char* kAppDataPayload =
    "544d4150010100001234aabbccdd01020304deadbeef";
constexpr const char* kMessageHash =
    "9fcc23615dea51d98d2eb601661d050b665628e62720d604ca7abc345db68b1f";
constexpr const char* kSignedPart =
    "101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
    "94cb40934a0000000000c405547261696cc40f68656c6c6f207265746963756c756d80"
    "9fcc23615dea51d98d2eb601661d050b665628e62720d604ca7abc345db68b1f";
constexpr const char* kPackedMessage =
    "101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"
    "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
    "94cb40934a0000000000c405547261696cc40f68656c6c6f207265746963756c756d80";
constexpr const char* kOpportunisticPacketPayload =
    "202122232425262728292a2b2c2d2e2f"
    "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
    "94cb40934a0000000000c405547261696cc40f68656c6c6f207265746963756c756d80";

constexpr const char* kLinkRequestNil =
    "93cb4045200000000000c41094fd9fd7b04a5caae5882616446bb9efc0";
constexpr const char* kLinkRequestBool =
    "93cb4045c00000000000c4109dc1a72883468f57fed571e796e9ce98c3";
constexpr const char* kLinkResponseBool =
    "92c40472657131c3";
constexpr const char* kLinkResponseNil =
    "92c40472657132c0";

constexpr const char* kResourceAdvertisement =
    "8ba174cd0102a164ccf0a16e03a168c420404142434445464748494a4b4c4d4e4f"
    "505152535455565758595a5b5c5d5e5fa172c40401020304a16fc420606162636465"
    "666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7fa16901a16c02"
    "a171c4025251a16605a16dc402f00f";
// RNS 1.5.4 ResourceAdvertisement.pack(): an ordinary encrypted LXMF resource
// has no Reticulum request ID. A publication uses this same representation.
constexpr const char* kLxmfResourceAdvertisement =
    "8ba174cd0102a164ccf0a16e03a168c420404142434445464748494a4b4c4d4e4f"
    "505152535455565758595a5b5c5d5e5fa172c40401020304a16fc420606162636465"
    "666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7fa16901a16c01"
    "a171c0a16601a16dc40c000102030405060708090a0b";
constexpr const char* kResourceHashmapUpdate =
    "9202c403aabbcc";

constexpr const char* kPropagationOffer =
    "92c4014b92c420d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9ea"
    "ebecedeeefc420e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9"
    "fafbfcfdfeff";
constexpr const char* kPropagationGet =
    "9391c420d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaeb"
    "ecedeeefc040";
constexpr const char* kPropagationIdList =
    "91c420d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeef";
constexpr const char* kPropagationMessageList =
    "91c412101112131415161718191a1b1c1d1e1f0102";
constexpr const char* kPropagationBatch =
    "92cb409452000000000092c412101112131415161718191a1b1c1d1e1f0102"
    "c411202122232425262728292a2b2c2d2e2f03";
constexpr const char* kOfficialPropagationAnnounce =
    "97c2ce6553f100c340cc80930c02128101c408547261696c20504e";
constexpr const char* kOfficialPropagationGet =
    "9391c420000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    "91c420a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babb"
    "bcbdbebf40";
constexpr const char* kOfficialPropagationListRequest = "92c0c0";

uint8_t hexNibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<uint8_t>(10 + value - 'a');
    }
    if (value >= 'A' && value <= 'F')
    {
        return static_cast<uint8_t>(10 + value - 'A');
    }
    throw std::runtime_error("invalid hex digit");
}

std::vector<uint8_t> fromHex(std::string_view hex)
{
    if ((hex.size() % 2U) != 0U)
    {
        throw std::runtime_error("odd hex string");
    }

    std::vector<uint8_t> bytes(hex.size() / 2U, 0);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<uint8_t>((hexNibble(hex[index * 2U]) << 4U) |
                                            hexNibble(hex[(index * 2U) + 1U]));
    }
    return bytes;
}

std::string toHex(const uint8_t* bytes, std::size_t len)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2U);
    for (std::size_t index = 0; index < len; ++index)
    {
        out.push_back(kDigits[(bytes[index] >> 4U) & 0x0FU]);
        out.push_back(kDigits[bytes[index] & 0x0FU]);
    }
    return out;
}

template <std::size_t N>
std::array<uint8_t, N> filled(uint8_t start)
{
    std::array<uint8_t, N> value = {};
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        value[index] = static_cast<uint8_t>(start + index);
    }
    return value;
}

void expectBytes(const uint8_t* actual, std::size_t actual_len, std::string_view expected_hex)
{
    const std::vector<uint8_t> expected = fromHex(expected_hex);
    assert(actual != nullptr || expected.empty());
    assert(actual_len == expected.size());
    if (!expected.empty())
    {
        if (std::memcmp(actual, expected.data(), expected.size()) != 0)
        {
            throw std::runtime_error("byte vector mismatch: actual=" +
                                     toHex(actual, actual_len) + " expected=" +
                                     std::string(expected_hex));
        }
    }
}

template <std::size_t N>
void expectArray(const uint8_t (&actual)[N], std::string_view expected_hex)
{
    expectBytes(actual, N, expected_hex);
}

template <std::size_t N>
void expectStdArray(const std::array<uint8_t, N>& actual, std::string_view expected_hex)
{
    expectBytes(actual.data(), actual.size(), expected_hex);
}

std::vector<uint8_t> encodeBuffer(std::size_t size)
{
    return std::vector<uint8_t>(size, 0);
}

void expectReticulumHashAndDestinationVectors()
{
    uint8_t full_hash[reticulum::kFullHashSize] = {};
    reticulum::fullHash(reinterpret_cast<const uint8_t*>("trailmate"), 9, full_hash);
    expectArray(full_hash, kFullHashTrailMate);

    uint8_t truncated_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>("trailmate"),
                             9,
                             truncated_hash);
    expectArray(truncated_hash, kTruncatedHashTrailMate);

    uint8_t delivery_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", delivery_name_hash);
    expectArray(delivery_name_hash, kNameHashLxmfDelivery);

    uint8_t propagation_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "propagation", propagation_name_hash);
    expectArray(propagation_name_hash, kNameHashLxmfPropagation);

    const auto public_key = filled<reticulum::kCombinedPublicKeySize>(0x01);
    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeIdentityHash(public_key.data(), identity_hash);
    expectArray(identity_hash, kIdentityHashIncrementalPublicKey);

    uint8_t delivery_destination_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeDestinationHash(delivery_name_hash,
                                      identity_hash,
                                      delivery_destination_hash);
    expectArray(delivery_destination_hash, kDestinationHashDelivery);

    uint8_t propagation_destination_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeDestinationHash(propagation_name_hash,
                                      identity_hash,
                                      propagation_destination_hash);
    expectArray(propagation_destination_hash, kDestinationHashPropagation);

    uint8_t plain_destination_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computePlainDestinationHash(delivery_name_hash, plain_destination_hash);
    expectArray(plain_destination_hash, kPlainDestinationHashDelivery);

    uint8_t path_request_destination_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computePathRequestDestinationHash(path_request_destination_hash);
    expectArray(path_request_destination_hash, kPathRequestDestinationHash);

    uint8_t offer_path_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>("/offer"), 6, offer_path_hash);
    expectArray(offer_path_hash, kOfferPathHash);

    uint8_t get_path_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>("/get"), 4, get_path_hash);
    expectArray(get_path_hash, kGetPathHash);

    assert(reticulum::nodeIdFromDestinationHash(delivery_destination_hash) ==
           kDeliveryNodeId);
}

void expectReticulumPacketVectors()
{
    const auto destination_hash = filled<reticulum::kTruncatedHashSize>(0x10);
    const auto transport_id = filled<reticulum::kTruncatedHashSize>(0x30);
    const uint8_t payload[] = {0xA0, 0xA1, 0xA2};

    std::vector<uint8_t> packet = encodeBuffer(64);
    std::size_t packet_len = packet.size();
    assert(reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::None,
                                         false,
                                         destination_hash.data(),
                                         payload,
                                         sizeof(payload),
                                         packet.data(),
                                         &packet_len,
                                         2));
    packet.resize(packet_len);
    expectBytes(packet.data(), packet.size(), kHeader1DataPacket);

    reticulum::ParsedPacket parsed{};
    assert(reticulum::parsePacket(packet.data(), packet.size(), &parsed));
    assert(parsed.valid);
    assert(parsed.header_type == 0);
    assert(parsed.hops == 2);
    assert(parsed.packet_type == reticulum::PacketType::Data);
    assert(parsed.destination_type == reticulum::DestinationType::Single);
    assert(parsed.context == static_cast<uint8_t>(reticulum::PacketContext::None));
    assert(parsed.payload_len == sizeof(payload));

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(packet.data(), packet.size(), packet_hash);
    expectArray(packet_hash, kHeader1DataPacketHash);

    uint8_t packet_truncated_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeTruncatedPacketHash(packet.data(),
                                          packet.size(),
                                          packet_truncated_hash);
    expectArray(packet_truncated_hash, kHeader1DataPacketTruncatedHash);

    std::vector<uint8_t> header2_packet = encodeBuffer(64);
    std::size_t header2_len = header2_packet.size();
    assert(reticulum::buildHeader2Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::Resource,
                                         true,
                                         transport_id.data(),
                                         destination_hash.data(),
                                         payload,
                                         sizeof(payload),
                                         header2_packet.data(),
                                         &header2_len,
                                         5));
    header2_packet.resize(header2_len);
    expectBytes(header2_packet.data(), header2_packet.size(), kHeader2ResourcePacket);
    assert(reticulum::parsePacket(header2_packet.data(), header2_packet.size(), &parsed));
    assert(parsed.header_type == 1);
    assert(parsed.hops == 5);
    assert(parsed.transport_id != nullptr);
    assert(std::memcmp(parsed.transport_id,
                       transport_id.data(),
                       reticulum::kTruncatedHashSize) == 0);
    assert(parsed.context_flag == 1);
    assert(parsed.context == static_cast<uint8_t>(reticulum::PacketContext::Resource));

    reticulum::computePacketHash(header2_packet.data(), header2_packet.size(), packet_hash);
    expectArray(packet_hash, kHeader2ResourcePacketHash);

    const std::vector<uint8_t> proof_destination = fromHex(kHeader1DataPacketTruncatedHash);
    const uint8_t proof_payload[] = {0x55, 0x66, 0x77, 0x88};
    std::vector<uint8_t> proof_packet = encodeBuffer(64);
    std::size_t proof_len = proof_packet.size();
    assert(reticulum::buildHeader1Packet(reticulum::PacketType::Proof,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::None,
                                         false,
                                         proof_destination.data(),
                                         proof_payload,
                                         sizeof(proof_payload),
                                         proof_packet.data(),
                                         &proof_len));
    proof_packet.resize(proof_len);
    expectBytes(proof_packet.data(), proof_packet.size(), kProofPacket);

    const uint8_t short_packet[] = {0x00, 0x01};
    assert(!reticulum::parsePacket(short_packet, sizeof(short_packet), &parsed));
}

void expectLxmfEnvelopeVectors()
{
    const auto destination_hash = filled<reticulum::kTruncatedHashSize>(0x10);
    const auto source_hash = filled<reticulum::kTruncatedHashSize>(0x20);
    const auto signature = filled<reticulum::kSignatureSize>(0x80);

    std::vector<uint8_t> payload = encodeBuffer(128);
    std::size_t payload_len = payload.size();
    assert(lxmf::encodeTextPayload(1234.5,
                                   "Trail",
                                   "hello reticulum",
                                   payload.data(),
                                   &payload_len));
    payload.resize(payload_len);
    expectBytes(payload.data(), payload.size(), kTextPayload);

    lxmf::DecodedTextPayload decoded_text{};
    assert(lxmf::unpackTextPayload(payload.data(), payload.size(), &decoded_text));
    assert(decoded_text.timestamp == 1234.5);
    assert(decoded_text.title == "Trail");
    assert(decoded_text.content == "hello reticulum");
    assert(decoded_text.fields_empty);

    const std::vector<uint8_t> sideband_telemetry =
        fromHex(kSidebandTelemetryTextPayload);
    assert(lxmf::unpackTextPayload(sideband_telemetry.data(),
                                   sideband_telemetry.size(),
                                   &decoded_text));
    assert(decoded_text.content.empty());
    assert(!decoded_text.fields_empty);
    assert(decoded_text.fields.size() == 1);
    assert(lxmf::findField(decoded_text, lxmf::kFieldTelemetry) != nullptr);

    lxmf::SidebandTelemetryLocation location{};
    assert(lxmf::decodeSidebandTelemetryLocation(decoded_text, &location));
    assert(location.valid);
    assert(location.latitude_e6 == 60123456);
    assert(location.longitude_e6 == 24654321);
    assert(location.altitude_cm == 12345);
    assert(location.accuracy_cm == 250);
    assert(location.timestamp == 1710000000UL);

    std::vector<uint8_t> encoded_sideband_telemetry = encodeBuffer(96);
    std::size_t encoded_sideband_telemetry_len = encoded_sideband_telemetry.size();
    assert(lxmf::encodeSidebandTelemetryLocationPayload(
        0.0,
        location,
        encoded_sideband_telemetry.data(),
        &encoded_sideband_telemetry_len));
    encoded_sideband_telemetry.resize(encoded_sideband_telemetry_len);
    expectBytes(encoded_sideband_telemetry.data(),
                encoded_sideband_telemetry.size(),
                kSidebandTelemetryTextPayload);

    const std::vector<uint8_t> sideband_request =
        fromHex(kSidebandTelemetryRequestPayload);
    assert(lxmf::unpackTextPayload(sideband_request.data(),
                                   sideband_request.size(),
                                   &decoded_text));
    lxmf::SidebandTelemetryRequest telemetry_request{};
    assert(lxmf::decodeSidebandTelemetryRequest(decoded_text,
                                                &telemetry_request));
    assert(telemetry_request.valid);
    assert(telemetry_request.timebase == 1710000000UL);
    assert(telemetry_request.collector_request);

    std::vector<uint8_t> lxst_wire = encodeBuffer(32);
    std::size_t lxst_wire_len = lxst_wire.size();
    assert(lxst::encodeSignalling(lxst::kStatusAvailable,
                                  lxst_wire.data(),
                                  &lxst_wire_len));
    lxst_wire.resize(lxst_wire_len);
    expectBytes(lxst_wire.data(), lxst_wire.size(), kLxstAvailableSignal);

    lxst::DecodedPacket lxst_packet{};
    assert(lxst::decodePacket(lxst_wire.data(), lxst_wire.size(), &lxst_packet));
    assert(lxst_packet.signal_count == 1);
    assert(lxst_packet.signals[0] == lxst::kStatusAvailable);

    lxst_wire.assign(32, 0);
    lxst_wire_len = lxst_wire.size();
    assert(lxst::encodeSignalling(
        lxst::kPreferredProfile + lxst::kProfileBandwidthLow,
        lxst_wire.data(),
        &lxst_wire_len));
    lxst_wire.resize(lxst_wire_len);
    expectBytes(lxst_wire.data(), lxst_wire.size(), kLxstPreferredLowSignal);

    const uint16_t lxst_preferences[] = {
        static_cast<uint16_t>(lxst::kPreferredProfile +
                              lxst::kProfileBandwidthLow),
        static_cast<uint16_t>(lxst::kPreferredMode + lxst::kModeHalfDuplex),
    };
    lxst_wire.assign(32, 0);
    lxst_wire_len = lxst_wire.size();
    assert(lxst::encodeSignalling(lxst_preferences,
                                  2,
                                  lxst_wire.data(),
                                  &lxst_wire_len));
    lxst_wire.resize(lxst_wire_len);
    assert(lxst::decodePacket(lxst_wire.data(), lxst_wire.size(), &lxst_packet));
    assert(lxst_packet.signal_count == 2);
    assert(lxst_packet.signals[0] == lxst_preferences[0]);
    assert(lxst_packet.signals[1] == lxst_preferences[1]);

    const uint8_t codec2_frames[] = {0xAA, 0xBB, 0xCC};
    lxst_wire.assign(32, 0);
    lxst_wire_len = lxst_wire.size();
    assert(lxst::encodeCodec2Frames(
        chat::reticulum::audio_call::Codec2Mode::Mode3200,
        codec2_frames,
        sizeof(codec2_frames),
        lxst_wire.data(),
        &lxst_wire_len));
    lxst_wire.resize(lxst_wire_len);
    expectBytes(lxst_wire.data(), lxst_wire.size(), kLxstCodec2Frames);
    assert(lxst::decodePacket(lxst_wire.data(), lxst_wire.size(), &lxst_packet));
    assert(lxst_packet.frame_count == 1);
    assert(lxst_packet.frames[0].codec == lxst::kCodec2);
    assert(lxst_packet.frames[0].codec2_mode_valid);
    assert(lxst_packet.frames[0].codec2_mode ==
           chat::reticulum::audio_call::Codec2Mode::Mode3200);
    assert(lxst_packet.frames[0].encoded_len == sizeof(codec2_frames));
    assert(std::memcmp(lxst_packet.frames[0].encoded,
                       codec2_frames,
                       sizeof(codec2_frames)) == 0);

    std::vector<uint8_t> peer_announce = encodeBuffer(32);
    std::size_t peer_announce_len = peer_announce.size();
    assert(lxmf::packPeerAnnounceAppData("vicliu-1",
                                         false,
                                         0,
                                         peer_announce.data(),
                                         &peer_announce_len));
    peer_announce.resize(peer_announce_len);
    expectBytes(peer_announce.data(), peer_announce.size(), kPeerAnnounceAppData);

    char display_name[32] = {};
    bool has_stamp_cost = true;
    uint8_t stamp_cost = 0xFFU;
    assert(lxmf::unpackPeerAnnounceAppData(peer_announce.data(),
                                           peer_announce.size(),
                                           display_name,
                                           sizeof(display_name),
                                           &has_stamp_cost,
                                           &stamp_cost));
    assert(std::strcmp(display_name, "vicliu-1") == 0);
    assert(!has_stamp_cost);
    assert(stamp_cost == 0U);

    const std::vector<uint8_t> lxmf_current_announce =
        fromHex("93c4087669636c69752d31c09101");
    assert(lxmf::unpackPeerAnnounceAppData(lxmf_current_announce.data(),
                                           lxmf_current_announce.size(),
                                           display_name,
                                           sizeof(display_name),
                                           &has_stamp_cost,
                                           &stamp_cost));
    assert(std::strcmp(display_name, "vicliu-1") == 0);
    assert(!has_stamp_cost);
    assert(stamp_cost == 0U);

    const std::vector<uint8_t> raw_utf8_announce =
        fromHex("7669636c69752d31");
    assert(lxmf::unpackPeerAnnounceAppData(raw_utf8_announce.data(),
                                           raw_utf8_announce.size(),
                                           display_name,
                                           sizeof(display_name),
                                           &has_stamp_cost,
                                           &stamp_cost));
    assert(std::strcmp(display_name, "vicliu-1") == 0);
    assert(!has_stamp_cost);
    assert(stamp_cost == 0U);

    const std::vector<uint8_t> legacy_bin_announce =
        fromHex("92c4087669636c69752d31c0");
    assert(lxmf::unpackPeerAnnounceAppData(legacy_bin_announce.data(),
                                           legacy_bin_announce.size(),
                                           display_name,
                                           sizeof(display_name),
                                           &has_stamp_cost,
                                           &stamp_cost));
    assert(std::strcmp(display_name, "vicliu-1") == 0);

    const std::vector<uint8_t> str8_announce =
        fromHex("92d9087669636c69752d31c0");
    assert(lxmf::unpackPeerAnnounceAppData(str8_announce.data(),
                                           str8_announce.size(),
                                           display_name,
                                           sizeof(display_name),
                                           &has_stamp_cost,
                                           &stamp_cost));
    assert(std::strcmp(display_name, "vicliu-1") == 0);

    const std::vector<uint8_t> str16_announce =
        fromHex("92da00087669636c69752d31c0");
    assert(lxmf::unpackPeerAnnounceAppData(str16_announce.data(),
                                           str16_announce.size(),
                                           display_name,
                                           sizeof(display_name),
                                           &has_stamp_cost,
                                           &stamp_cost));
    assert(std::strcmp(display_name, "vicliu-1") == 0);

    const uint8_t app_payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::vector<uint8_t> app_data = encodeBuffer(64);
    std::size_t app_data_len = app_data.size();
    assert(lxmf::encodeAppDataPayload(0x1234,
                                      0xAABBCCDD,
                                      0x01020304,
                                      true,
                                      app_payload,
                                      sizeof(app_payload),
                                      app_data.data(),
                                      &app_data_len));
    app_data.resize(app_data_len);
    expectBytes(app_data.data(), app_data.size(), kAppDataPayload);

    lxmf::DecodedAppData decoded_app_data{};
    assert(lxmf::decodeAppDataPayload(app_data.data(),
                                      app_data.size(),
                                      &decoded_app_data));
    assert(decoded_app_data.version == 1);
    assert(decoded_app_data.want_response);
    assert(decoded_app_data.portnum == 0x1234);
    assert(decoded_app_data.packet_id == 0xAABBCCDD);
    assert(decoded_app_data.request_id == 0x01020304);
    assert(decoded_app_data.payload == std::vector<uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}));

    const uint8_t team_payload[] = {0x01, 0x02, 0x03, 0x04};
    const uint32_t team_location_ports[] = {
        team::proto::TEAM_POSITION_APP,
        team::proto::TEAM_TRACK_APP,
    };
    for (uint32_t portnum : team_location_ports)
    {
        std::vector<uint8_t> team_app_data = encodeBuffer(64);
        std::size_t team_app_data_len = team_app_data.size();
        assert(lxmf::encodeAppDataPayload(portnum,
                                          0x10203040,
                                          0,
                                          false,
                                          team_payload,
                                          sizeof(team_payload),
                                          team_app_data.data(),
                                          &team_app_data_len));
        team_app_data.resize(team_app_data_len);

        lxmf::DecodedAppData decoded_team_app_data{};
        assert(lxmf::decodeAppDataPayload(team_app_data.data(),
                                          team_app_data.size(),
                                          &decoded_team_app_data));
        assert(decoded_team_app_data.version == 1);
        assert(!decoded_team_app_data.want_response);
        assert(decoded_team_app_data.portnum == portnum);
        assert(decoded_team_app_data.packet_id == 0x10203040);
        assert(decoded_team_app_data.request_id == 0);
        assert(decoded_team_app_data.payload ==
               std::vector<uint8_t>({0x01, 0x02, 0x03, 0x04}));
    }

    uint8_t message_hash[reticulum::kFullHashSize] = {};
    lxmf::computeMessageHash(destination_hash.data(),
                             source_hash.data(),
                             payload.data(),
                             payload.size(),
                             message_hash);
    expectArray(message_hash, kMessageHash);

    std::vector<uint8_t> signed_part = encodeBuffer(256);
    std::size_t signed_part_len = signed_part.size();
    uint8_t signed_part_hash[reticulum::kFullHashSize] = {};
    assert(lxmf::buildSignedPart(destination_hash.data(),
                                 source_hash.data(),
                                 payload.data(),
                                 payload.size(),
                                 signed_part.data(),
                                 &signed_part_len,
                                 signed_part_hash));
    signed_part.resize(signed_part_len);
    expectBytes(signed_part.data(), signed_part.size(), kSignedPart);
    expectArray(signed_part_hash, kMessageHash);

    std::vector<uint8_t> packed_message = encodeBuffer(256);
    std::size_t packed_message_len = packed_message.size();
    assert(lxmf::packMessage(destination_hash.data(),
                             source_hash.data(),
                             signature.data(),
                             payload.data(),
                             payload.size(),
                             packed_message.data(),
                             &packed_message_len));
    packed_message.resize(packed_message_len);
    expectBytes(packed_message.data(), packed_message.size(), kPackedMessage);

    const std::vector<uint8_t> opportunistic_payload(
        packed_message.begin() + reticulum::kTruncatedHashSize,
        packed_message.end());
    expectBytes(opportunistic_payload.data(),
                opportunistic_payload.size(),
                kOpportunisticPacketPayload);

    lxmf::DecodedEnvelope envelope{};
    assert(lxmf::unpackMessageEnvelope(packed_message.data(),
                                       packed_message.size(),
                                       &envelope));
    expectArray(envelope.destination_hash, "101112131415161718191a1b1c1d1e1f");
    expectArray(envelope.source_hash, "202122232425262728292a2b2c2d2e2f");
    assert(envelope.packed_payload == payload);

    const std::vector<uint8_t> invalid_msgpack = fromHex("91c4");
    assert(!lxmf::unpackTextPayload(invalid_msgpack.data(),
                                    invalid_msgpack.size(),
                                    &decoded_text));
}

void expectLinkServiceVectors()
{
    const std::vector<uint8_t> offer_path_hash = fromHex(kOfferPathHash);
    const std::vector<uint8_t> get_path_hash = fromHex(kGetPathHash);

    std::vector<uint8_t> link_request = encodeBuffer(128);
    std::size_t link_request_len = link_request.size();
    assert(lxmf::encodeLinkRequestPayload(42.25,
                                          offer_path_hash.data(),
                                          nullptr,
                                          0,
                                          true,
                                          link_request.data(),
                                          &link_request_len));
    link_request.resize(link_request_len);
    expectBytes(link_request.data(), link_request.size(), kLinkRequestNil);

    lxmf::DecodedLinkRequest decoded_request{};
    assert(lxmf::decodeLinkRequestPayload(link_request.data(),
                                          link_request.size(),
                                          &decoded_request));
    assert(decoded_request.requested_at == 42.25);
    assert(decoded_request.data_is_nil);
    expectArray(decoded_request.path_hash, kOfferPathHash);

    const uint8_t packed_bool[] = {0xC3};
    link_request.assign(128, 0);
    link_request_len = link_request.size();
    assert(lxmf::encodeLinkRequestPayload(43.5,
                                          get_path_hash.data(),
                                          packed_bool,
                                          sizeof(packed_bool),
                                          false,
                                          link_request.data(),
                                          &link_request_len));
    link_request.resize(link_request_len);
    expectBytes(link_request.data(), link_request.size(), kLinkRequestBool);
    assert(lxmf::decodeLinkRequestPayload(link_request.data(),
                                          link_request.size(),
                                          &decoded_request));
    assert(!decoded_request.data_is_nil);
    assert(decoded_request.packed_data == std::vector<uint8_t>({0xC3}));
    expectArray(decoded_request.path_hash, kGetPathHash);

    const uint8_t request_id_1[] = {'r', 'e', 'q', '1'};
    std::vector<uint8_t> response = encodeBuffer(64);
    std::size_t response_len = response.size();
    assert(lxmf::encodeLinkResponsePayload(request_id_1,
                                           sizeof(request_id_1),
                                           packed_bool,
                                           sizeof(packed_bool),
                                           false,
                                           response.data(),
                                           &response_len));
    response.resize(response_len);
    expectBytes(response.data(), response.size(), kLinkResponseBool);

    lxmf::DecodedLinkResponse decoded_response{};
    assert(lxmf::decodeLinkResponsePayload(response.data(),
                                           response.size(),
                                           &decoded_response));
    assert(decoded_response.request_id == std::vector<uint8_t>({'r', 'e', 'q', '1'}));
    assert(!decoded_response.data_is_nil);
    assert(decoded_response.packed_data == std::vector<uint8_t>({0xC3}));

    const uint8_t request_id_2[] = {'r', 'e', 'q', '2'};
    response.assign(64, 0);
    response_len = response.size();
    assert(lxmf::encodeLinkResponsePayload(request_id_2,
                                           sizeof(request_id_2),
                                           nullptr,
                                           0,
                                           true,
                                           response.data(),
                                           &response_len));
    response.resize(response_len);
    expectBytes(response.data(), response.size(), kLinkResponseNil);
    assert(lxmf::decodeLinkResponsePayload(response.data(),
                                           response.size(),
                                           &decoded_response));
    assert(decoded_response.data_is_nil);
}

void expectResourceVectors()
{
    const auto resource_hash = filled<reticulum::kFullHashSize>(0x40);
    const auto original_hash = filled<reticulum::kFullHashSize>(0x60);
    const uint8_t random_hash[] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t request_id[] = {'R', 'Q'};
    const uint8_t hashmap[] = {0xF0, 0x0F};

    std::vector<uint8_t> advertisement = encodeBuffer(256);
    std::size_t advertisement_len = advertisement.size();
    assert(lxmf::encodeResourceAdvertisement(258,
                                             240,
                                             3,
                                             resource_hash.data(),
                                             random_hash,
                                             original_hash.data(),
                                             1,
                                             2,
                                             request_id,
                                             sizeof(request_id),
                                             0x05,
                                             hashmap,
                                             sizeof(hashmap),
                                             advertisement.data(),
                                             &advertisement_len));
    advertisement.resize(advertisement_len);
    expectBytes(advertisement.data(), advertisement.size(), kResourceAdvertisement);

    lxmf::DecodedResourceAdvertisement decoded_advertisement{};
    assert(lxmf::decodeResourceAdvertisement(advertisement.data(),
                                             advertisement.size(),
                                             &decoded_advertisement));
    assert(decoded_advertisement.transfer_size == 258);
    assert(decoded_advertisement.data_size == 240);
    assert(decoded_advertisement.part_count == 3);
    expectArray(decoded_advertisement.resource_hash,
                "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f");
    assert(decoded_advertisement.request_id == std::vector<uint8_t>({'R', 'Q'}));
    assert(decoded_advertisement.flags == 0x05);
    assert(decoded_advertisement.hashmap == std::vector<uint8_t>({0xF0, 0x0F}));

    const uint8_t lxmf_hashmap[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    advertisement_len = advertisement.capacity();
    advertisement.resize(advertisement_len);
    assert(lxmf::encodeResourceAdvertisement(258, 240, 3, resource_hash.data(), random_hash,
                                             original_hash.data(), 1, 1, nullptr, 0, 0x01,
                                             lxmf_hashmap, sizeof(lxmf_hashmap), advertisement.data(), &advertisement_len));
    advertisement.resize(advertisement_len);
    expectBytes(advertisement.data(), advertisement.size(), kLxmfResourceAdvertisement);
    assert(lxmf::decodeResourceAdvertisement(advertisement.data(), advertisement.size(), &decoded_advertisement));
    assert(decoded_advertisement.request_id.empty());
    assert(decoded_advertisement.flags == 0x01);
    assert(decoded_advertisement.total_segments == 1);
    assert(decoded_advertisement.hashmap == std::vector<uint8_t>(lxmf_hashmap, lxmf_hashmap + sizeof(lxmf_hashmap)));

    const uint8_t update_map[] = {0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> hashmap_update = encodeBuffer(64);
    std::size_t hashmap_update_len = hashmap_update.size();
    assert(lxmf::encodeResourceHashmapUpdate(2,
                                             update_map,
                                             sizeof(update_map),
                                             hashmap_update.data(),
                                             &hashmap_update_len));
    hashmap_update.resize(hashmap_update_len);
    expectBytes(hashmap_update.data(), hashmap_update.size(), kResourceHashmapUpdate);

    lxmf::DecodedResourceHashmapUpdate decoded_update{};
    assert(lxmf::decodeResourceHashmapUpdate(hashmap_update.data(),
                                             hashmap_update.size(),
                                             &decoded_update));
    assert(decoded_update.segment == 2);
    assert(decoded_update.hashmap == std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));
}

void expectPropagationVectors()
{
    const std::vector<uint8_t> offer = fromHex(kPropagationOffer);
    lxmf::DecodedPropagationOffer decoded_offer{};
    assert(lxmf::decodePropagationOfferPayload(offer.data(), offer.size(), &decoded_offer));
    assert(!decoded_offer.peering_key_is_nil);
    assert(decoded_offer.peering_key == std::vector<uint8_t>({'K'}));
    assert(decoded_offer.transient_ids.size() == 2);

    const std::vector<uint8_t> get = fromHex(kPropagationGet);
    lxmf::DecodedPropagationGetRequest decoded_get{};
    assert(lxmf::decodePropagationGetRequestPayload(get.data(),
                                                    get.size(),
                                                    &decoded_get));
    assert(!decoded_get.wants_is_nil);
    assert(decoded_get.wants.size() == 1);
    assert(decoded_get.haves_is_nil);
    assert(decoded_get.has_transfer_limit);
    assert(decoded_get.transfer_limit_kb == 64);

    std::vector<std::vector<uint8_t>> official_wants(1);
    std::vector<std::vector<uint8_t>> official_haves(1);
    for (uint8_t value = 0; value < reticulum::kFullHashSize; ++value)
    {
        official_wants.front().push_back(value);
        official_haves.front().push_back(static_cast<uint8_t>(0xA0U + value));
    }
    std::vector<uint8_t> official_get = encodeBuffer(128);
    std::size_t official_get_len = official_get.size();
    assert(lxmf::encodePropagationGetRequestPayload(&official_wants,
                                                    &official_haves,
                                                    true,
                                                    64,
                                                    official_get.data(),
                                                    &official_get_len));
    official_get.resize(official_get_len);
    expectBytes(official_get.data(),
                official_get.size(),
                kOfficialPropagationGet);

    std::vector<uint8_t> list_request = encodeBuffer(16);
    std::size_t list_request_len = list_request.size();
    assert(lxmf::encodePropagationGetRequestPayload(nullptr,
                                                    nullptr,
                                                    false,
                                                    0,
                                                    list_request.data(),
                                                    &list_request_len));
    list_request.resize(list_request_len);
    expectBytes(list_request.data(),
                list_request.size(),
                kOfficialPropagationListRequest);

    const std::vector<uint8_t> official_announce =
        fromHex(kOfficialPropagationAnnounce);
    lxmf::DecodedPropagationAnnounce decoded_announce{};
    assert(lxmf::decodePropagationAnnounceAppData(official_announce.data(),
                                                  official_announce.size(),
                                                  &decoded_announce));
    assert(decoded_announce.valid);
    assert(!decoded_announce.legacy_support);
    assert(decoded_announce.node_active);
    assert(decoded_announce.timebase_s == 1700000000U);
    assert(decoded_announce.transfer_limit_kb == 64U);
    assert(decoded_announce.sync_limit_kb == 128U);
    assert(decoded_announce.stamp_cost == 12U);
    assert(decoded_announce.stamp_cost_flexibility == 2U);
    assert(decoded_announce.peering_cost == 18U);
    assert(decoded_announce.display_name == "Trail PN");

    std::vector<uint8_t> id_list = encodeBuffer(64);
    std::size_t id_list_len = id_list.size();
    assert(lxmf::encodePropagationIdListPayload({decoded_get.wants.front()},
                                                id_list.data(),
                                                &id_list_len));
    id_list.resize(id_list_len);
    expectBytes(id_list.data(), id_list.size(), kPropagationIdList);

    const auto destination_hash = filled<reticulum::kTruncatedHashSize>(0x10);
    std::vector<uint8_t> propagated_message(destination_hash.begin(),
                                            destination_hash.end());
    propagated_message.push_back(0x01);
    propagated_message.push_back(0x02);
    std::vector<uint8_t> message_list = encodeBuffer(64);
    std::size_t message_list_len = message_list.size();
    assert(lxmf::encodePropagationMessageListPayload({propagated_message},
                                                     message_list.data(),
                                                     &message_list_len));
    message_list.resize(message_list_len);
    expectBytes(message_list.data(), message_list.size(), kPropagationMessageList);

    const auto source_hash = filled<reticulum::kTruncatedHashSize>(0x20);
    std::vector<uint8_t> second_message(source_hash.begin(), source_hash.end());
    second_message.push_back(0x03);
    std::vector<uint8_t> batch = encodeBuffer(128);
    std::size_t batch_len = batch.size();
    assert(lxmf::encodePropagationBatch(1300.5,
                                        {propagated_message, second_message},
                                        batch.data(),
                                        &batch_len));
    batch.resize(batch_len);
    expectBytes(batch.data(), batch.size(), kPropagationBatch);

    lxmf::DecodedPropagationBatch decoded_batch{};
    assert(lxmf::decodePropagationBatch(batch.data(), batch.size(), &decoded_batch));
    assert(decoded_batch.remote_timebase == 1300.5);
    assert(decoded_batch.messages.size() == 2);
    assert(decoded_batch.messages[0] == propagated_message);
    assert(decoded_batch.messages[1] == second_message);

    std::vector<uint8_t> trailing_batch = batch;
    trailing_batch.push_back(0x00);
    assert(!lxmf::decodePropagationBatch(trailing_batch.data(),
                                         trailing_batch.size(),
                                         &decoded_batch));
    const std::vector<uint8_t> oversized_list = fromHex("dc0041");
    assert(!lxmf::decodePropagationIdListPayload(oversized_list.data(),
                                                 oversized_list.size(),
                                                 &decoded_get.wants));

    const std::vector<uint8_t> invalid_msgpack = fromHex("91c4");
    assert(!lxmf::decodePropagationOfferPayload(invalid_msgpack.data(),
                                                invalid_msgpack.size(),
                                                &decoded_offer));
    assert(!lxmf::decodePropagationGetRequestPayload(invalid_msgpack.data(),
                                                     invalid_msgpack.size(),
                                                     &decoded_get));
    assert(!lxmf::decodePropagationBatch(invalid_msgpack.data(),
                                         invalid_msgpack.size(),
                                         &decoded_batch));
}

bool hasAction(const lxst_call::Transition& transition,
               lxst_call::Action expected)
{
    for (std::size_t index = 0; index < transition.action_count; ++index)
    {
        if (transition.actions[index] == expected)
        {
            return true;
        }
    }
    return false;
}

void expectLxstCallStateMachine()
{
    uint32_t now_ms = 100;
    auto caller = lxst_call::makeCaller(lxst::kProfileBandwidthLow, now_ms);
    assert(caller.phase == lxst_call::Phase::CallerAwaitingLink);

    auto transition = lxst_call::dispatch(
        &caller,
        {lxst_call::EventType::LinkActive},
        ++now_ms);
    assert(transition.accepted);
    assert(transition.action_count == 0);
    assert(caller.phase == lxst_call::Phase::CallerAwaitingAvailability);

    transition = lxst_call::dispatch(
        &caller,
        lxst_call::Event::remoteSignal(lxst::kStatusAvailable),
        ++now_ms);
    assert(transition.accepted);
    assert(hasAction(transition, lxst_call::Action::SendIdentify));
    assert(caller.phase == lxst_call::Phase::CallerAwaitingRinging);

    const auto duplicate_available = lxst_call::dispatch(
        &caller,
        lxst_call::Event::remoteSignal(lxst::kStatusAvailable),
        ++now_ms);
    assert(duplicate_available.accepted);
    assert(duplicate_available.action_count == 0);

    transition = lxst_call::dispatch(
        &caller,
        lxst_call::Event::remoteSignal(lxst::kStatusRinging),
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::SendPreferences));
    assert(caller.phase == lxst_call::Phase::CallerNegotiating);

    transition = lxst_call::dispatch(
        &caller,
        lxst_call::Event::remoteSignal(lxst::kStatusConnecting),
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::PrepareMedia));
    assert(caller.phase == lxst_call::Phase::CallerConnecting);

    transition = lxst_call::dispatch(
        &caller,
        {lxst_call::EventType::MediaReady},
        ++now_ms);
    assert(transition.accepted);
    assert(transition.action_count == 0);
    assert(caller.media_prepared);

    transition = lxst_call::dispatch(
        &caller,
        lxst_call::Event::remoteSignal(lxst::kStatusEstablished),
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::ActivateMedia));
    assert(caller.phase == lxst_call::Phase::Active);
    transition = lxst_call::dispatch(
        &caller,
        {lxst_call::EventType::LocalModeSwitch, lxst::kModeHalfDuplex},
        ++now_ms);
    assert(transition.accepted);
    assert(hasAction(transition, lxst_call::Action::SendPreferredMode));
    assert(caller.mode == lxst::kModeHalfDuplex);
    transition = lxst_call::dispatch(
        &caller,
        lxst_call::Event::remoteSignal(lxst::kPreferredMode +
                                       lxst::kModeFullDuplex),
        ++now_ms);
    assert(transition.accepted);
    assert(caller.mode == lxst::kModeFullDuplex);
    assert(!lxst_call::dispatch(
                &caller,
                lxst_call::Event::remoteSignal(lxst::kStatusRinging),
                ++now_ms)
                .accepted);

    auto callee = lxst_call::makeCallee(lxst::kProfileBandwidthLow, now_ms);
    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::LinkActive},
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::SendAvailable));
    assert(callee.phase == lxst_call::Phase::CalleeAwaitingIdentity);

    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::ControlRetry},
        now_ms + 1000);
    assert(!transition.accepted);

    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::ControlRetry},
        now_ms + 1200);
    assert(transition.accepted);
    assert(hasAction(transition, lxst_call::Action::SendAvailable));
    assert(callee.phase == lxst_call::Phase::CalleeAwaitingIdentity);
    now_ms += 1200;

    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::RemoteIdentified},
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::BeginRinging));
    assert(callee.phase == lxst_call::Phase::CalleeAwaitingAdmission);

    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::AdmissionGranted},
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::SendRinging));
    assert(callee.phase == lxst_call::Phase::CalleeRinging);

    assert(!lxst_call::dispatch(
                &callee,
                lxst_call::Event::remoteSignal(lxst::kStatusAvailable),
                ++now_ms)
                .accepted);
    transition = lxst_call::dispatch(
        &callee,
        lxst_call::Event::remoteSignal(
            lxst::kPreferredProfile + lxst::kProfileBandwidthVeryLow),
        ++now_ms);
    assert(transition.accepted);
    assert(hasAction(transition,
                     lxst_call::Action::SendPreferredProfile));

    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::LocalAccepted},
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::SendConnecting));
    assert(hasAction(transition, lxst_call::Action::PrepareMedia));
    assert(callee.phase == lxst_call::Phase::CalleeConnecting);

    transition = lxst_call::dispatch(
        &callee,
        {lxst_call::EventType::MediaReady},
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::SendEstablished));
    assert(hasAction(transition, lxst_call::Action::ActivateMedia));
    assert(callee.phase == lxst_call::Phase::Active);

    auto rejected = lxst_call::makeCallee(
        lxst::kProfileBandwidthLow,
        now_ms);
    (void)lxst_call::dispatch(
        &rejected,
        {lxst_call::EventType::LinkActive},
        ++now_ms);
    (void)lxst_call::dispatch(
        &rejected,
        {lxst_call::EventType::RemoteIdentified},
        ++now_ms);
    (void)lxst_call::dispatch(
        &rejected,
        {lxst_call::EventType::AdmissionGranted},
        ++now_ms);
    transition = lxst_call::dispatch(
        &rejected,
        {lxst_call::EventType::LocalHangup},
        ++now_ms);
    assert(hasAction(transition, lxst_call::Action::SendRejected));
    assert(hasAction(transition, lxst_call::Action::CloseLink));
    assert(rejected.phase == lxst_call::Phase::Closing);

    auto timeout = lxst_call::makeCaller(
        lxst::kProfileBandwidthLow,
        0);
    (void)lxst_call::dispatch(
        &timeout,
        {lxst_call::EventType::LinkActive},
        1);
    assert(!lxst_call::phaseTimedOut(timeout, 15000));
    assert(lxst_call::phaseTimedOut(timeout, 15001));
}

void expectProtocolProbeDiscoveryEvidence()
{
    const std::vector<uint8_t> announce_wire = fromHex(kProtocolProbeAnnounce);
    reticulum::ParsedPacket packet{};
    assert(reticulum::parsePacket(announce_wire.data(), announce_wire.size(), &packet));
    assert(reticulum::isPlausibleDiscoveryPacket(packet));

    std::vector<uint8_t> altered_announce = announce_wire;
    altered_announce[2] ^= 0x01U;
    assert(reticulum::parsePacket(altered_announce.data(), altered_announce.size(), &packet));
    assert(!reticulum::isPlausibleDiscoveryPacket(packet));

    std::vector<uint8_t> ifac_announce = announce_wire;
    ifac_announce[0] |= 0x80U;
    assert(reticulum::parsePacket(ifac_announce.data(), ifac_announce.size(), &packet));
    assert(!reticulum::isPlausibleDiscoveryPacket(packet));

    std::array<uint8_t, reticulum::kTruncatedHashSize> path_request_destination = {};
    std::array<uint8_t, reticulum::kTruncatedHashSize> path_request_payload = {};
    reticulum::computePathRequestDestinationHash(path_request_destination.data());
    path_request_payload[0] = 0x42;

    std::array<uint8_t, 64> wire = {};
    std::size_t wire_len = wire.size();
    assert(reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Plain,
                                         reticulum::PacketContext::None,
                                         false,
                                         path_request_destination.data(),
                                         path_request_payload.data(),
                                         path_request_payload.size(),
                                         wire.data(),
                                         &wire_len));
    assert(reticulum::parsePacket(wire.data(), wire_len, &packet));
    assert(reticulum::isPlausibleDiscoveryPacket(packet));

    const auto unrelated_destination = filled<reticulum::kTruncatedHashSize>(0x10);
    const auto unrelated_payload = filled<reticulum::kTruncatedHashSize>(0x20);
    wire_len = wire.size();
    assert(reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::None,
                                         false,
                                         unrelated_destination.data(),
                                         unrelated_payload.data(),
                                         unrelated_payload.size(),
                                         wire.data(),
                                         &wire_len));
    assert(reticulum::parsePacket(wire.data(), wire_len, &packet));
    assert(!reticulum::isPlausibleDiscoveryPacket(packet));
}

} // namespace

int main()
{
    expectReticulumHashAndDestinationVectors();
    expectReticulumPacketVectors();
    expectLxmfEnvelopeVectors();
    expectLinkServiceVectors();
    expectResourceVectors();
    expectPropagationVectors();
    expectLxstCallStateMachine();
    expectProtocolProbeDiscoveryEvidence();
    return 0;
}
