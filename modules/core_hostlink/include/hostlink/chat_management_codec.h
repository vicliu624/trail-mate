#pragma once

#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/chat_submission_owner.h"
#include <string_view>

namespace hostlink::chat_management
{
using geocaching::ByteView;
using geocaching::protocol::CmpReader;
using geocaching::protocol::CmpWriter;
struct TargetView
{
    uint8_t protocol = 0;
    uint8_t channel = 0;
    uint8_t kind = 0; // 0 channel, 1 direct peer, 2 Team. Full identities only.
    ByteView identity{};
};
inline bool readTarget(CmpReader& r, TargetView& target)
{
    size_t count;
    uint64_t protocol, channel, kind;
    if (!r.array(count, 4) || count != 4 || !r.unsignedInteger(protocol) || protocol > 4 || protocol == 0 ||
        !r.unsignedInteger(channel) || channel > 7 || !r.unsignedInteger(kind) || kind > 2 ||
        !r.binary(target.identity, 32)) return false;
    target.protocol = static_cast<uint8_t>(protocol);
    target.channel = static_cast<uint8_t>(channel);
    target.kind = static_cast<uint8_t>(kind);
    if (kind == 0) return target.identity.size == 0;
    bool nonzero = false;
    for (size_t i = 0; i < target.identity.size; ++i) nonzero = nonzero || target.identity.data[i] != 0;
    if (kind == 2) return nonzero && target.identity.size == 8 && channel == 2;
    return nonzero && ((protocol == 1 && target.identity.size == 4) ||
                       (protocol == 4 && target.identity.size == 16));
}
inline bool decodeTarget(const SubmissionRecord& record, TargetView& target)
{
    CmpReader r({record.conversation, record.conversation_size});
    return readTarget(r, target) && r.finished();
}
struct SendRequestView
{
    ByteView store{}, context{}, operation{}, conversation{};
    TargetView target{};
    std::string_view text{};
};
inline bool id(CmpReader& r, ByteView& value) { return r.binary(value, kIdSize) && value.size == kIdSize; }
inline bool decodeSend(ByteView body, SendRequestView& out)
{
    CmpReader r(body);
    size_t count;
    if (!r.array(count, 4) || count != 4 || !r.array(count, 2) || count != 2 ||
        !id(r, out.store) || !id(r, out.context) || !id(r, out.operation)) return false;
    const size_t start = r.position();
    if (!readTarget(r, out.target)) return false;
    out.conversation = {body.data + start, r.position() - start};
    return out.conversation.size <= kConversationBytes && r.text(out.text, kTextBytes) && !out.text.empty() &&
           out.text.find('\0') == std::string_view::npos && r.finished();
}
// Versioned metadata, not a second message-body store. NVS supplies atomic
// blob publication and corruption detection; the codec still validates all
// lengths/enums before the record can authorise a replay or replacement.
inline bool encodeRecord(const SubmissionRecord& rec, uint8_t* out, size_t capacity, size_t& size)
{
    CmpWriter w(out, capacity);
    const bool ok = w.array(12) && w.unsignedInteger(1) && w.binary({rec.store, kIdSize}) &&
                    w.binary({rec.owner, kIdSize}) && w.binary({rec.operation, kIdSize}) &&
                    w.binary({rec.content_hash, kDigestSize}) && w.binary({rec.conversation, rec.conversation_size}) &&
                    w.unsignedInteger(rec.text_size) && w.unsignedInteger(static_cast<uint8_t>(rec.state)) &&
                    w.binary({rec.message, kIdSize}) && w.unsignedInteger(rec.delivery_state) &&
                    w.unsignedInteger(rec.revision) && w.unsignedInteger(rec.failure);
    size = w.size();
    return ok;
}
inline bool decodeRecord(ByteView bytes, SubmissionRecord& rec)
{
    CmpReader r(bytes);
    size_t count;
    uint64_t version, text, state, delivery, revision, failure;
    ByteView store, owner, operation, hash, conversation, message;
    if (!r.array(count, 12) || count != 12 || !r.unsignedInteger(version) || version != 1 ||
        !id(r, store) || !id(r, owner) || !id(r, operation) || !r.binary(hash, kDigestSize) || hash.size != kDigestSize ||
        !r.binary(conversation, kConversationBytes) || conversation.size == 0 ||
        !r.unsignedInteger(text) || text == 0 || text > kTextBytes ||
        !r.unsignedInteger(state) || state > uint8_t(SubmissionState::Uncertain) || !id(r, message) ||
        !r.unsignedInteger(delivery) || delivery > UINT8_MAX || !r.unsignedInteger(revision) || revision > UINT32_MAX ||
        !r.unsignedInteger(failure) || failure > UINT16_MAX || !r.finished()) return false;
    std::memcpy(rec.store, store.data, kIdSize);
    std::memcpy(rec.owner, owner.data, kIdSize);
    std::memcpy(rec.operation, operation.data, kIdSize);
    std::memcpy(rec.content_hash, hash.data, kDigestSize);
    std::memcpy(rec.conversation, conversation.data, conversation.size);
    rec.conversation_size = static_cast<uint8_t>(conversation.size);
    rec.text_size = static_cast<uint16_t>(text);
    rec.state = static_cast<SubmissionState>(state);
    std::memcpy(rec.message, message.data, kIdSize);
    rec.delivery_state = static_cast<uint8_t>(delivery);
    rec.revision = static_cast<uint32_t>(revision);
    rec.failure = static_cast<uint16_t>(failure);
    TargetView target;
    return decodeTarget(rec, target);
}
inline bool writeReceipt(CmpWriter& w, const SubmissionRecord& rec)
{
    bool has_message = false;
    for (const auto byte : rec.message) has_message = has_message || byte != 0;
    return w.array(8) && w.binary({rec.operation, kIdSize}) && w.binary({rec.owner, kIdSize}) &&
           w.binary({rec.content_hash, kDigestSize}) && w.unsignedInteger(static_cast<uint8_t>(rec.state)) &&
           (has_message ? w.binary({rec.message, kIdSize}) : w.nil()) &&
           w.unsignedInteger(rec.delivery_state) && w.unsignedInteger(rec.revision) && w.unsignedInteger(rec.failure);
}
} // namespace hostlink::chat_management
