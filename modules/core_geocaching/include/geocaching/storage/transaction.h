#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"

namespace geocaching::storage
{
struct MutationView
{
    uint8_t table = 0;
    ByteView key;
    ByteView value;
    bool erase = false;
};

struct TransactionView
{
    uint64_t previous_sequence = 0;
    const MutationView* mutations = nullptr;
    size_t count = 0;
};

inline bool readTransactionHeader(protocol::CmpReader& reader, uint64_t expected_previous, size_t& count)
{
    size_t fields = 0;
    uint64_t schema = 0, previous = 0;
    count = 0;
    return reader.array(fields, 3) && fields == 3 && reader.unsignedInteger(schema) && schema == 1 &&
           reader.unsignedInteger(previous) && previous == expected_previous && reader.array(count, 64) && count != 0;
}

inline bool readTransactionMutation(protocol::CmpReader& reader, MutationView& out)
{
    out = {};
    MutationView mutation;
    size_t fields = 0;
    uint64_t table = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(table) ||
        table < 1 || table > 13 || !reader.binary(mutation.key, 96) || mutation.key.size == 0) return false;
    mutation.table = static_cast<uint8_t>(table);
    auto nullable = reader;
    if (nullable.nil())
    {
        reader = nullable;
        mutation.erase = true;
    }
    else if (!reader.binary(mutation.value, 32768)) return false;
    out = mutation;
    return true;
}

// Constant auxiliary storage. Revisit only in-memory views for duplicate keys;
// binary values are skipped without copying and no filesystem reads occur.
inline bool validateTransaction(ByteView payload, uint64_t expected_previous)
{
    if (!payload.data || payload.size > 65536) return false;
    protocol::CmpReader reader(payload);
    size_t count = 0;
    if (!readTransactionHeader(reader, expected_previous, count)) return false;
    const auto first = reader;
    for (size_t i = 0; i < count; ++i)
    {
        MutationView current;
        if (!readTransactionMutation(reader, current)) return false;
        auto prefix = first;
        for (size_t j = 0; j < i; ++j)
        {
            MutationView prior;
            if (!readTransactionMutation(prefix, prior)) return false;
            if (prior.table == current.table && prior.key.size == current.key.size &&
                !std::memcmp(prior.key.data, current.key.data, current.key.size)) return false;
        }
    }
    return reader.finished();
}

// The caller validates GCR1 framing/CRC before decoding and validates table
// values/references before applying any mutation. Output stays empty on failure;
// scratch is unspecified and must never be applied independently of success.
inline bool decodeTransaction(ByteView payload, uint64_t expected_previous,
                              MutationView* scratch, size_t capacity, TransactionView& out)
{
    out = {};
    if (!payload.data || payload.size > 65536 || !scratch) return false;
    protocol::CmpReader reader(payload);
    size_t count = 0;
    if (!readTransactionHeader(reader, expected_previous, count) || count > capacity) return false;
    for (size_t i = 0; i < count; ++i)
    {
        MutationView mutation;
        if (!readTransactionMutation(reader, mutation)) return false;
        for (size_t j = 0; j < i; ++j)
        {
            const auto& prior = scratch[j];
            if (prior.table == mutation.table && prior.key.size == mutation.key.size &&
                std::memcmp(prior.key.data, mutation.key.data, mutation.key.size) == 0) return false;
        }
        scratch[i] = mutation;
    }
    if (!reader.finished()) return false;
    out = {expected_previous, scratch, count};
    return true;
}

// Borrows descriptors and their bytes. They must remain immutable and alive
// until the caller finishes consuming the encoding, including across I/O steps.
// No file operations or full-payload scratch storage are owned here.
class TransactionEncoding
{
  public:
    bool open(uint64_t previous_sequence, const MutationView* mutations, size_t count)
    {
        *this = {};
        if (!mutations || count == 0 || count > 64) return false;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& m = mutations[i];
            if (m.table < 1 || m.table > 13 || !m.key.data || !m.key.size || m.key.size > 96 ||
                m.value.size > 32768 || (m.value.size && !m.value.data) || (m.erase && m.value.size)) return false;
            for (size_t j = 0; j < i; ++j)
            {
                const auto& prior = mutations[j];
                if (prior.table == m.table && prior.key.size == m.key.size &&
                    !std::memcmp(prior.key.data, m.key.data, m.key.size)) return false;
            }
        }
        previous_ = previous_sequence;
        mutations_ = mutations;
        count_ = count;
        size_t total = 0;
        if (!visit([&](ByteView part)
                   {
                if (part.size > 65536 - total) return false;
                total += part.size;
                return true; }))
        {
            *this = {};
            return false;
        }
        size_ = total;
        return true;
    }

    size_t size() const { return size_; }
    bool inputOverlaps(ByteView storage) const
    {
        const auto overlaps = [&](ByteView input)
        {
            if (!input.size || !storage.size) return false;
            const auto a = reinterpret_cast<uintptr_t>(input.data), b = reinterpret_cast<uintptr_t>(storage.data);
            return a <= b ? b - a < input.size : a - b < storage.size;
        };
        if (overlaps({reinterpret_cast<const uint8_t*>(mutations_), count_ * sizeof(MutationView)})) return true;
        for (size_t i = 0; i < count_; ++i)
            if (overlaps(mutations_[i].key) || overlaps(mutations_[i].value)) return true;
        return false;
    }

    // The sink consumes each span synchronously; header spans are temporary.
    // It must not retain span pointers. Returning false stops immediately.
    template <typename Sink>
    bool visit(Sink&& sink) const
    {
        if (!mutations_) return false;
        uint8_t token[16];
        protocol::CmpWriter prefix(token, sizeof(token));
        if (!prefix.array(3) || !prefix.unsignedInteger(1) ||
            !prefix.unsignedInteger(previous_) || !prefix.array(static_cast<uint32_t>(count_)) ||
            !sink(ByteView{token, prefix.size()})) return false;
        for (size_t i = 0; i < count_; ++i)
        {
            const auto& m = mutations_[i];
            protocol::CmpWriter key(token, sizeof(token));
            if (!key.array(3) || !key.unsignedInteger(m.table) || !key.binaryHeader(m.key.size) ||
                !sink(ByteView{token, key.size()}) || !sink(m.key)) return false;
            protocol::CmpWriter value(token, sizeof(token));
            if (!(m.erase ? value.nil() : value.binaryHeader(m.value.size)) ||
                !sink(ByteView{token, value.size()})) return false;
            if (!m.erase && m.value.size && !sink(m.value)) return false;
        }
        return true;
    }

    // A bounded copy for the storage owner's next slice. Traversal only skips
    // borrowed spans in memory; it does not read their contents or perform I/O.
    bool readSlice(size_t offset, uint8_t* output, size_t capacity, size_t& written) const
    {
        written = 0;
        if (!mutations_ || offset > size_ || (!output && capacity)) return false;
        if (!capacity || offset == size_) return true;
        size_t skip = offset;
        visit([&](ByteView part)
              {
            if (skip >= part.size) { skip -= part.size; return true; }
            const size_t remaining = part.size - skip;
            const size_t take = remaining < capacity - written ? remaining : capacity - written;
            std::memcpy(output + written, part.data + skip, take);
            written += take;
            skip = 0;
            return written < capacity; });
        const size_t available = size_ - offset;
        return written == (available < capacity ? available : capacity);
    }

    // Verify a readback slice without allocating a second I/O buffer.
    bool matchesSlice(size_t offset, ByteView bytes) const
    {
        if (!mutations_ || offset > size_ || bytes.size > size_ - offset || (!bytes.data && bytes.size)) return false;
        if (!bytes.size) return true;
        size_t skip = offset, compared = 0;
        bool equal = true;
        visit([&](ByteView part)
              {
            if (skip >= part.size) { skip -= part.size; return true; }
            const size_t available = part.size - skip;
            const size_t take = available < bytes.size - compared ? available : bytes.size - compared;
            equal = !std::memcmp(part.data + skip, bytes.data + compared, take);
            compared += take;
            skip = 0;
            return equal && compared < bytes.size; });
        return equal && compared == bytes.size;
    }

  private:
    const MutationView* mutations_ = nullptr;
    uint64_t previous_ = 0;
    size_t count_ = 0;
    size_t size_ = 0;
};
static_assert(sizeof(TransactionEncoding) <= 32, "Transaction encoding must only retain borrowed metadata");

// Compatibility entry point: the same encoder supplies contiguous callers.
// Table-specific validation and durable commit remain the store owner's job.
inline bool encodeTransaction(uint64_t previous_sequence, const MutationView* mutations,
                              size_t count, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    TransactionEncoding encoding;
    if (!output || !encoding.open(previous_sequence, mutations, count) || encoding.size() > capacity) return false;
    return encoding.readSlice(0, output, capacity, written);
}
} // namespace geocaching::storage
