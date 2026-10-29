#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_uploads.h"
#include "track_sharing/upload_protocol.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
namespace ts = track_sharing;
using Status = mgmt::Status;
using Op = mgmt::Operation;
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
template <class Encode>
static Bytes encode(Encode encode)
{
    Bytes result(471);
    Writer writer(result.data(), result.size());
    encode(writer);
    assert(writer.good());
    result.resize(writer.size());
    return result;
}
static void write(const fs::path& path, const Bytes& bytes, bool append = false)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    assert(stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
int main()
{
    const auto root = fs::temp_directory_path() / ("trailmate-upload-" + std::to_string(getpid()));
    const auto sd = root / "sd", database = root / "identity.sqlite3";
    fs::create_directories(root);
    native::HostlinkUploads uploads(sd, database);
    uint64_t now = 1;
    uint8_t session[16]{1}, other[16]{2}, id[16]{3};
    Bytes bytes(500);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i);
    std::array<uint8_t, 32> digest{};
    native::LinuxGeocachingCrypto crypto;
    assert(crypto.sha256({bytes.data(), bytes.size()}, digest.data()));
    auto begin = [&]
    { return encode([&](Writer& w)
                    { w.array(4); w.binary({id, 16}); w.unsignedInteger(3); w.unsignedInteger(bytes.size()); w.binary({digest.data(), 32}); }); };
    auto simple = [&]
    { return encode([&](Writer& w)
                    { w.array(1); w.binary({id, 16}); }); };
    auto chunk = [&](uint32_t offset, const Bytes& content)
    { return encode([&](Writer& w)
                    { w.array(3); w.binary({id, 16}); w.unsignedInteger(offset); w.binary({content.data(), content.size()}); }); };
    const auto call_on = [&](native::HostlinkUploads& target, Op operation, const Bytes& body, Status expected, const uint8_t* owner)
    {
        mgmt::EnvelopeView request{owner, nullptr, uint16_t(operation), body.data(), body.size()};
        std::array<uint8_t, 448> output{};
        size_t written = 0;
        assert(target.process(request, now++, output.data(), output.size(), written) == expected);
        return Bytes(output.begin(), output.begin() + written);
    };
    const auto call = [&](Op op, const Bytes& body, Status expected = Status::Ok)
    { return call_on(uploads, op, body, expected, session); };
    const auto progress = [&](const Bytes& body, uint32_t offset, bool sealed)
    {
        geocaching::protocol::CmpReader reader({body.data(), body.size()});
        geocaching::ByteView transfer;
        size_t count = 0;
        uint64_t value = 0;
        assert(reader.array(count, 4) && count == 4 && reader.binary(transfer, 16) && transfer.size == 16 && !std::memcmp(transfer.data, id, 16));
        assert(reader.unsignedInteger(value) && value == 320 && reader.unsignedInteger(value) && value == offset);
        assert(reader.unsignedInteger(value) && value == unsigned(sealed) && reader.finished());
    };
    progress(call(Op::TransferBegin, begin()), 0, false);
    const Bytes first(bytes.begin(), bytes.begin() + 320), last(bytes.begin() + 320, bytes.end());
    progress(call(Op::TransferChunk, chunk(0, first)), 320, false);
    progress(call(Op::TransferChunk, chunk(0, first)), 320, false);
    auto changed = first;
    changed[0] ^= 1;
    call(Op::TransferChunk, chunk(0, changed), Status::Conflict);
    call(Op::TransferChunk, chunk(400, Bytes{1}), Status::Conflict);
    call_on(uploads, Op::TransferChunk, chunk(320, last), Status::Unauthorized, other);
    {
        native::HostlinkUploads competing(sd, database);
        call_on(competing, Op::TransferBegin, begin(), Status::Busy, other);
    }
    uploads.close(other);
    progress(call(Op::TransferStatus, simple()), 320, false);
    uploads.close(session);
    call(Op::TransferStatus, simple(), Status::Expired);
    char candidate[64]{};
    assert(ts::resolveUploadPath(id, 16, ts::UploadFile::Candidate, candidate, sizeof(candidate)));
    const auto file = sd / (candidate + 1);
    write(file, Bytes(20, 0xab), true); // Crash tail beyond the acknowledged offset.
    progress(call(Op::TransferBegin, begin()), 320, false);
    progress(call(Op::TransferChunk, chunk(320, last)), 500, false);
    progress(call(Op::TransferFinish, simple()), 500, false);
    uploads.advance(now++, true);
    progress(call(Op::TransferStatus, simple()), 500, true);
    uploads.close(session);
    progress(call(Op::TransferBegin, begin()), 500, false); // Sealed CRC requires fresh hash.
    uploads.advance(now++, true);
    progress(call(Op::TransferStatus, simple()), 500, true);
    uploads.advance(now++, false);
    auto damaged = bytes;
    damaged.back() ^= 1;
    write(file, damaged);
    progress(call(Op::TransferBegin, begin()), 500, false);
    uploads.advance(now++, true);
    call(Op::TransferStatus, simple(), Status::HashMismatch);
    assert(call(Op::TransferAbort, simple()) == Bytes{0x90});
    assert(!fs::exists(file));
    // Wrong expected digest persists Failed, never Sealed; abort is still owned.
    digest[0] ^= 1;
    call(Op::TransferBegin, begin());
    call(Op::TransferChunk, chunk(0, first));
    call(Op::TransferChunk, chunk(320, last));
    call(Op::TransferFinish, simple());
    uploads.advance(now++, true);
    call(Op::TransferStatus, simple(), Status::HashMismatch);
    uploads.close(session);
    call(Op::TransferBegin, begin(), Status::HashMismatch);
    call(Op::TransferAbort, simple());
    digest[0] ^= 1;
    call(Op::TransferBegin, begin());
    now += 30000;
    uploads.advance(now++, true);
    call(Op::TransferStatus, simple(), Status::Expired);
    progress(call_on(uploads, Op::TransferBegin, begin(), Status::Ok, other), 0, false);
    uploads.close(other);
    call(Op::TransferBegin, begin());
    sqlite3* db = nullptr;
    assert(sqlite3_open(database.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "UPDATE linux_reticulum_identity SET generation=generation+1", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    call(Op::TransferChunk, chunk(0, first), Status::StaleIdentity);
    now += 30000;
    uploads.advance(now++, true);
    call(Op::TransferBegin, begin(), Status::Conflict); // Old generation cannot be rebound.
    // Reject a symlink directory without touching its target.
    const auto outside = root / "outside", linked = root / "linked";
    fs::create_directories(outside);
    fs::create_directories(linked / "trailmate");
    fs::create_directory_symlink(outside, linked / "trailmate/web-upload");
    {
        native::HostlinkUploads unsafe(linked, database);
        call_on(unsafe, Op::TransferBegin, begin(), Status::StorageUnavailable, session);
    }
    assert(fs::is_empty(outside));
    // Unknown files count against the shared directory quota.
    const auto quota = root / "quota";
    for (unsigned i = 0; i < 4; ++i) write(quota / "trailmate/web-upload" / std::to_string(i), Bytes{1});
    {
        native::HostlinkUploads limited(quota, database);
        call_on(limited, Op::TransferBegin, begin(), Status::LimitExceeded, session);
    }
    // Every shared upload kind retains its own path/limit, using the same writer.
    {
        native::HostlinkUploads kinds(root / "kinds", database);
        for (unsigned kind : {3, 4, 7, 8, 9, 10, 11})
        {
            const auto descriptor = encode([&](Writer& w)
                                           { w.array(4); w.binary({id, 16}); w.unsignedInteger(kind); w.unsignedInteger(bytes.size()); w.binary({digest.data(), 32}); });
            call_on(kinds, Op::TransferBegin, descriptor, Status::Ok, session);
            call_on(kinds, Op::TransferChunk, chunk(0, first), Status::Ok, session);
            call_on(kinds, Op::TransferChunk, chunk(320, last), Status::Ok, session);
            call_on(kinds, Op::TransferFinish, simple(), Status::Ok, session);
            kinds.advance(now++, true);
            progress(call_on(kinds, Op::TransferStatus, simple(), Status::Ok, session), 500, true);
            call_on(kinds, Op::TransferAbort, simple(), Status::Ok, session);
        }
    }
    call(Op::TransferChunk, chunk(0, Bytes(321, 1)), Status::InvalidField);
    // A changed root inode is a media change, even when the pathname is reused.
    {
        const auto mounted = root / "mounted", retired = root / "retired";
        native::HostlinkUploads media(mounted, database);
        call_on(media, Op::TransferBegin, begin(), Status::Ok, session);
        fs::rename(mounted, retired);
        fs::create_directory(mounted);
        media.advance(now++, true);
        call_on(media, Op::TransferStatus, simple(), Status::Expired, session);
        assert(fs::is_empty(mounted) && fs::exists(retired / (candidate + 1)));
    }
    // Reject hard-linked journal leaves before any rewrite/truncation.
    {
        const auto hard = root / "hard";
        const auto sentinel = outside / "sentinel";
        write(sentinel, Bytes(108, 0x71));
        char journal[64]{};
        assert(ts::resolveUploadPath(id, 16, ts::UploadFile::JournalA, journal, sizeof(journal)));
        fs::create_directories((hard / (journal + 1)).parent_path());
        fs::create_hard_link(sentinel, hard / (journal + 1));
        native::HostlinkUploads unsafe(hard, database);
        call_on(unsafe, Op::TransferBegin, begin(), Status::StorageUnavailable, session);
        assert(fs::file_size(sentinel) == 108);
    }
    fs::remove_all(root);
}
