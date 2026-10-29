#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_uploads.h"
#include "track_sharing/upload_protocol.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sqlite3.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
namespace ts = track_sharing;
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
using Reader = geocaching::protocol::CmpReader;
using Op = mgmt::Operation;
using Status = mgmt::Status;
template <class Encode>
static Bytes encode(Encode encode)
{
    Bytes bytes(471);
    Writer writer(bytes.data(), bytes.size());
    encode(writer);
    assert(writer.good());
    bytes.resize(writer.size());
    return bytes;
}
struct Page
{
    uint64_t media = 0;
    std::optional<uint64_t> next;
    bool row = false;
    std::array<uint8_t, 16> id{};
    std::array<uint8_t, 32> hash{};
    uint64_t kind = 0, bytes = 0, generation = 0, phase = 0, offset = 0;
};
static Page page(const Bytes& bytes)
{
    Page result;
    Reader reader({bytes.data(), bytes.size()});
    size_t fields = 0;
    assert(reader.array(fields, 3) && fields == 3 && reader.unsignedInteger(result.media) && result.media && result.media <= UINT32_MAX);
    if (bytes[reader.position()] == 0xc0) assert(reader.nil());
    else
    {
        uint64_t next = 0;
        assert(reader.unsignedInteger(next) && next % 32 == 0);
        result.next = next;
    }
    if (bytes[reader.position()] == 0xc0) assert(reader.nil());
    else
    {
        geocaching::ByteView id, hash;
        assert(reader.array(fields, 7) && fields == 7 && reader.binary(id, 16) && id.size == 16 &&
               reader.unsignedInteger(result.kind) && reader.unsignedInteger(result.bytes) && reader.binary(hash, 32) && hash.size == 32 &&
               reader.unsignedInteger(result.generation) && reader.unsignedInteger(result.phase) && reader.unsignedInteger(result.offset));
        result.row = true;
        std::copy_n(id.data, 16, result.id.begin());
        std::copy_n(hash.data, 32, result.hash.begin());
    }
    assert(reader.finished());
    return result;
}
static void write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
int main()
{
    const auto root = fs::temp_directory_path() / ("trailmate-upload-maintenance-" + std::to_string(getpid()));
    const auto sd = root / "sd", database = root / "identity.sqlite3";
    fs::create_directories(sd);
    native::HostlinkUploads uploads(sd, database);
    uint64_t now = 1;
    uint8_t session[16]{1}, other[16]{2}, id[16]{3};
    unsigned kind = 3;
    Bytes content(500, 0x71);
    std::array<uint8_t, 32> digest{};
    native::LinuxGeocachingCrypto crypto;
    assert(crypto.sha256({content.data(), content.size()}, digest.data()));
    const auto descriptor = [&]
    { return encode([&](Writer& w)
                    { w.array(4); w.binary({id, 16}); w.unsignedInteger(kind); w.unsignedInteger(content.size()); w.binary({digest.data(), 32}); }); };
    const auto list = [&](std::optional<std::pair<uint64_t, uint64_t>> cursor = {})
    { return encode([&](Writer& w)
                    { w.array(2); w.unsignedInteger(kind); if (cursor) { w.array(2); w.unsignedInteger(cursor->first); w.unsignedInteger(cursor->second); } else w.nil(); }); };
    const auto call_on = [&](native::HostlinkUploads& target, Op operation, const Bytes& body, Status expected, const uint8_t* owner)
    {
        mgmt::EnvelopeView request{owner, nullptr, uint16_t(operation), body.data(), body.size()};
        std::array<uint8_t, 448> output{};
        size_t size = 0;
        assert(target.process(request, now++, output.data(), output.size(), size) == expected);
        return Bytes(output.begin(), output.begin() + size);
    };
    const auto call = [&](Op op, const Bytes& body, Status expected = Status::Ok)
    { return call_on(uploads, op, body, expected, session); };
    auto initial = page(call(Op::TransferListOwned, list()));
    assert(!initial.next && !initial.row);
    call(Op::TransferBegin, descriptor());
    call(Op::TransferChunk, encode([&](Writer& w)
                                   { w.array(3); w.binary({id, 16}); w.unsignedInteger(0); w.binary({content.data(), 320}); }));
    call(Op::TransferListOwned, list(), Status::Busy);
    {
        native::HostlinkUploads competing(sd, database);
        call_on(competing, Op::TransferCleanup, descriptor(), Status::Busy, other);
    }
    digest[0] ^= 1;
    call(Op::TransferCleanup, descriptor(), Status::Conflict);
    digest[0] ^= 1;
    call_on(uploads, Op::TransferCleanup, descriptor(), Status::Unauthorized, other);
    id[0] ^= 1;
    call(Op::TransferCleanup, descriptor(), Status::Busy);
    id[0] ^= 1;
    uploads.close(session);
    auto current = page(call(Op::TransferListOwned, list()));
    const auto media = current.media;
    unsigned rows = 0;
    for (unsigned iteration = 0;; ++iteration)
    {
        assert(iteration < 16);
        if (current.row)
        {
            ++rows;
            assert(!std::memcmp(current.id.data(), id, 16) && current.hash == digest && current.kind == kind && current.bytes == content.size() &&
                   current.generation == 1 && current.phase == unsigned(ts::UploadPhase::Receiving) && current.offset == 320);
        }
        if (!current.next) break;
        current = page(call(Op::TransferListOwned, list(std::make_pair(current.media, *current.next))));
    }
    assert(rows >= 1);
    call(Op::TransferListOwned, list(std::make_pair(media ^ 1, 0)), Status::Conflict);
    call(Op::TransferListOwned, list(std::make_pair(media, 1)), Status::InvalidField);
    // A reused pathname cannot make an old cursor target another root.
    fs::rename(sd, root / "retired");
    fs::create_directory(sd);
    call(Op::TransferListOwned, list(std::make_pair(media, 0)), Status::Conflict);
    fs::remove(sd);
    fs::rename(root / "retired", sd);
    const auto before_mutation = page(call(Op::TransferListOwned, list()));
    const auto unknown = sd / "trailmate/web-upload/foreign";
    write(unknown, Bytes{1});
    call(Op::TransferListOwned, list(std::make_pair(before_mutation.media, before_mutation.next.value_or(0))), Status::Conflict);
    fs::remove(unknown);
    char path[64]{};
    const auto file = [&](ts::UploadFile which)
    { assert(ts::resolveUploadPath(id, 16, which, path, sizeof(path), static_cast<ts::UploadKind>(kind))); return sd / (path + 1); };
    fs::remove(file(ts::UploadFile::Candidate)); // Journals alone prove exact cleanup ownership.
    content.push_back(0);                        // Wrong length must not erase either journal.
    call(Op::TransferCleanup, descriptor(), Status::Conflict);
    content.pop_back();
    assert(fs::exists(file(ts::UploadFile::JournalA)));
    assert(call(Op::TransferCleanup, descriptor()) == Bytes{0x90});
    assert(call(Op::TransferCleanup, descriptor()) == Bytes{0x90});
    assert(!fs::exists(file(ts::UploadFile::JournalA)) && !fs::exists(file(ts::UploadFile::JournalB)));
    id[0] = 4;
    write(file(ts::UploadFile::Candidate), content);
    call(Op::TransferCleanup, descriptor(), Status::Conflict); // No journals: no deletion authority.
    assert(fs::file_size(file(ts::UploadFile::Candidate)) == content.size());
    assert(!page(call(Op::TransferListOwned, list())).row);
    // Both journal slots corrupt: fail closed, even with a known candidate.
    id[0] = 5;
    call(Op::TransferBegin, descriptor());
    call(Op::TransferChunk, encode([&](Writer& w)
                                   { w.array(3); w.binary({id, 16}); w.unsignedInteger(0); w.binary({content.data(), 100}); }));
    uploads.close(session);
    write(file(ts::UploadFile::JournalA), Bytes(108, 0x37));
    write(file(ts::UploadFile::JournalB), Bytes(108, 0x38));
    call(Op::TransferCleanup, descriptor(), Status::Conflict);
    call(Op::TransferListOwned, list(), Status::Conflict);
    assert(fs::exists(file(ts::UploadFile::Candidate)));
    // Another generation's metadata is neither returned nor cleaned.
    id[0] = 6;
    kind = 4;
    call(Op::TransferBegin, descriptor());
    uploads.close(session);
    sqlite3* db = nullptr;
    assert(sqlite3_open(database.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "UPDATE linux_reticulum_identity SET generation=generation+1", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    current = page(call(Op::TransferListOwned, list()));
    for (unsigned iteration = 0; current.next; ++iteration)
    {
        assert(iteration < 16 && !current.row);
        current = page(call(Op::TransferListOwned, list(std::make_pair(current.media, *current.next))));
    }
    assert(!current.row);
    call(Op::TransferCleanup, descriptor(), Status::Conflict);
    assert(fs::exists(file(ts::UploadFile::Candidate)));
    // One torn slot is recoverable when the other journal still proves binding.
    id[0] = 7;
    kind = 11;
    call(Op::TransferBegin, descriptor());
    call(Op::TransferChunk, encode([&](Writer& w)
                                   { w.array(3); w.binary({id, 16}); w.unsignedInteger(0); w.binary({content.data(), 100}); }));
    uploads.close(session);
    write(file(ts::UploadFile::JournalB), Bytes(108, 0x46));
    current = page(call(Op::TransferListOwned, list()));
    assert(!current.row || current.offset == 0);
    assert(call(Op::TransferCleanup, descriptor()) == Bytes{0x90});
    assert(!fs::exists(file(ts::UploadFile::Candidate)) && !fs::exists(file(ts::UploadFile::JournalA)) && !fs::exists(file(ts::UploadFile::JournalB)));
    fs::remove_all(root);
}
