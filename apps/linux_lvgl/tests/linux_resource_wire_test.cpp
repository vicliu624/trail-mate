#include "chat/infra/lxmf/lxmf_resource_wire.h"
#include <algorithm>
#include <bzlib.h>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>

namespace rt = chat::lxmf::runtime;
namespace r = chat::reticulum;
struct Random
{
    uint8_t next = 0;
    bool fail = false;
    static bool fill(void* pointer, uint8_t* bytes, size_t size)
    {
        auto& random = *static_cast<Random*>(pointer);
        if (random.fail) return false;
        while (size--) *bytes++ = random.next++;
        return true;
    }
};
int main()
{
    auto outgoing = std::make_unique<rt::LinkResourceTransfer>();
    auto incoming = std::make_unique<rt::LinkResourceTransfer>();
    std::vector<uint8_t> data(40000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>((i * 17) % 251);
    uint8_t key[64];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(i + 1);
    Random random;
    assert(rt::prepareOutgoingResource(data.data(), data.size(), key, 431, 0, nullptr, 0, 100, 4,
                                       Random::fill, &random, *outgoing));
    assert(outgoing->data_size == data.size() && outgoing->part_count > rt::resourceHashmapCapacity(431));
    assert(outgoing->hashmap.size() == outgoing->part_count * 4 && outgoing->encrypted);
    const auto hex = [](const uint8_t* bytes)
    {
        const char* digits = "0123456789abcdef";
        std::string result;
        for (size_t i = 0; i < 32; ++i)
        {
            result += digits[bytes[i] >> 4];
            result += digits[bytes[i] & 15];
        }
        return result;
    };
    assert(hex(outgoing->resource_hash) == "a7e5782d5d59eae8bce75cc397b78445ae6fbc885e25f8a71c52e87903038995");
    assert(hex(outgoing->expected_proof) == "2517b6f0e76b78af24d563cc96817fa9be0c803d7f9cc974f282a57682acc549");
    const size_t initial_hashes = rt::resourceHashmapCapacity(431);
    assert(rt::initialiseIncomingResourceTransfer(*incoming, outgoing->resource_hash, outgoing->random_hash,
                                                  outgoing->original_hash, nullptr, 0, outgoing->hashmap.data(), initial_hashes * 4, outgoing->data_size,
                                                  outgoing->transfer_size, outgoing->part_count, 1, 1, outgoing->flags, true, false, false, false, 200, 4));
    assert(rt::applyResourceHashmapUpdate(*incoming, 1, outgoing->hashmap.data() + initial_hashes * 4,
                                          outgoing->hashmap.size() - initial_hashes * 4, initial_hashes, 201));
    bool complete = false;
    for (size_t index = outgoing->parts.size(); index-- > 0;)
    {
        uint8_t hash[32];
        assert(rt::resourceJoinedHash(outgoing->parts[index].data(), outgoing->parts[index].size(), outgoing->random_hash, 4, hash));
        assert(rt::recordResourcePart(*incoming, outgoing->parts[index].data(), outgoing->parts[index].size(), hash, 202,
                                      nullptr, &complete));
    }
    assert(complete);
    rt::ResourcePayloadBuffer decoded;
    uint8_t proof[32];
    assert(rt::decodeCompletedResource(*incoming, key, decoded, proof));
    assert(decoded.size() == data.size() && !std::memcmp(decoded.data(), data.data(), data.size()));
    assert(!std::memcmp(proof, outgoing->expected_proof, 32));
    incoming->parts[0][0] ^= 1;
    assert(!rt::decodeCompletedResource(*incoming, key, decoded, proof) && decoded.empty());
    assert(std::all_of(std::begin(proof), std::end(proof), [](uint8_t byte)
                       { return byte == 0; }));
    incoming->parts[0][0] ^= 1;
    key[0] ^= 1;
    assert(!rt::decodeCompletedResource(*incoming, key, decoded, proof));
    key[0] ^= 1;
    incoming->resource_hash[0] ^= 1;
    assert(!rt::decodeCompletedResource(*incoming, key, decoded, proof));
    incoming->resource_hash[0] ^= 1;
    const auto compress = +[](const uint8_t* bytes, size_t count, rt::ResourcePayloadBuffer& result, size_t limit)
    {
        result.resize(limit);
        unsigned int capacity = static_cast<unsigned int>(limit);
        if (BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(result.data()), &capacity,
                                     const_cast<char*>(reinterpret_cast<const char*>(bytes)), count, 9, 0, 30) != BZ_OK) return false;
        result.resize(capacity);
        return true;
    };
    const auto expand = +[](const uint8_t* bytes, size_t count, rt::ResourcePayloadBuffer& result, size_t limit)
    {
        result.resize(limit);
        unsigned int capacity = static_cast<unsigned int>(limit);
        if (BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(result.data()), &capacity,
                                       const_cast<char*>(reinterpret_cast<const char*>(bytes)), count, 0, 0) != BZ_OK) return false;
        result.resize(capacity);
        return true;
    };
    std::vector<uint8_t> repeated(40000, 'r');
    assert(rt::prepareOutgoingResource(repeated.data(), repeated.size(), key, 431, 0, nullptr, 0, 100, 4,
                                       Random::fill, &random, *outgoing, compress));
    assert(outgoing->compressed && outgoing->flags == 3 && outgoing->transfer_size < 431);
    // Python bz2.compress(b'r' * 40000), independent of the shared resource encoder.
    const uint8_t reference[] = {0x42, 0x5a, 0x68, 0x39, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0x43, 0x08,
                                 0xd0, 0x13, 0x00, 0x00, 0x4e, 0x00, 0x82, 0x90, 0x00, 0x00, 0x40, 0x00,
                                 0x08, 0x20, 0x00, 0x30, 0xcc, 0x05, 0x29, 0xa6, 0x2a, 0x36, 0x25, 0x47,
                                 0x8b, 0xb9, 0x22, 0x9c, 0x28, 0x48, 0x21, 0x84, 0x68, 0x09, 0x80};
    rt::ResourcePayloadBuffer token(outgoing->transfer_size);
    size_t token_size = token.size();
    assert(r::tokenDecrypt(key, outgoing->parts[0].data(), outgoing->parts[0].size(), token.data(), &token_size));
    assert(token_size == sizeof(reference) + rt::kResourcePrefixSize);
    assert(!std::memcmp(token.data() + rt::kResourcePrefixSize, reference, sizeof(reference)));
    assert(!rt::decodeCompletedResource(*outgoing, key, decoded, proof));
    assert(rt::decodeCompletedResource(*outgoing, key, decoded, proof, expand));
    assert(decoded.size() == repeated.size() && std::equal(decoded.begin(), decoded.end(), repeated.begin()) && !std::memcmp(proof, outgoing->expected_proof, 32));
    --outgoing->data_size;
    assert(!rt::decodeCompletedResource(*outgoing, key, decoded, proof, expand) && decoded.empty());
    outgoing->data_size += 2;
    assert(!rt::decodeCompletedResource(*outgoing, key, decoded, proof, expand) && decoded.empty());
    random.fail = true;
    assert(!rt::prepareOutgoingResource(data.data(), data.size(), key, 431, 0, nullptr, 0, 100, 4,
                                        Random::fill, &random, *outgoing));
    assert(outgoing->parts.empty() && outgoing->data_size == 0);
    assert(!rt::prepareOutgoingResource(data.data(), data.size(), key, 0, 0, nullptr, 0, 100, 4,
                                        Random::fill, &random, *outgoing));
}
