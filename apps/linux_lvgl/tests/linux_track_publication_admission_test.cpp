#include "native_track_fixture.h"
#include "platform/linux/hostlink_track_publication_admission.h"
#include "platform/linux/hostlink_track_signer.h"
#include "track_sharing/confirm_publication.h"
#include <fstream>

namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string scenario(argv[2]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-publication-admission-" + std::to_string(getpid()));
    {
        auto fixture = std::make_unique<native_track_fixture::Fixture>(root, argv[1], scenario == "v2");
        native::LinuxReticulumIdentity server(root / "directory.sqlite");
        assert(server.begin());
        ts::PublicationDirectory directory;
        std::copy_n(server.publicKey().data(), 64, directory.public_key);
        std::copy_n(server.destinationHash().data(), 16, directory.delivery);
        directory.epoch[0] = 7;
        directory.file_limit = 8388608;
        directory.chunk_limit = 1024;
        directory.query_limit = 32;
        ts::CommitRequest request{{fixture->operation.data(), 16}, {fixture->object.data(), 32}, {fixture->revision.data(), 32}, {directory.delivery, 16}, {directory.epoch, 16}, fixture->author.generation(), 1};
        native::UploadIo io(fixture->sd);
        assert(io.lock());
        std::array<uint8_t, 448> scratch{};
        const auto admit = [&](const ts::CommitRequest& selected, const ts::PublicationDirectory& pinned, bool historical, mgmt::Status expected)
        {
            auto input = std::make_unique<native::HostlinkTrackPublicationAdmission>(selected, pinned, io, fixture->author.publicKey().data(), fixture->actor.data(), historical);
            native::HostlinkTrackSigner signer(fixture->author);
            mgmt::Status status;
            unsigned steps = 0;
            do
            {
                status = input->step(signer, scratch.data(), scratch.size());
                assert(++steps < 10000);
            } while (status == mgmt::Status::Busy);
            assert(status == expected && input->ready() == (expected == mgmt::Status::Ok));
            if (expected != mgmt::Status::Ok) assert(!input->result() && !input->manifest());
            return input;
        };
        if (scenario == "directory")
        {
            auto wrong = directory;
            wrong.public_key[0] ^= 1;
            admit(request, wrong, false, mgmt::Status::Conflict);
        }
        else if (scenario == "stale")
        {
            auto stale = request;
            stale.generation++;
            admit(stale, directory, false, mgmt::Status::StaleIdentity);
        }
        else if (scenario == "blob")
        {
            std::array<char, 160> path{};
            assert(ts::resolveObjectPath(request.object, ts::ObjectFile::KmlFile, {fixture->package.kml_hash.data(), 32}, path.data(), path.size()));
            std::ofstream(fixture->sd / (path.data() + 1)) << std::string(fixture->package.kml.size(), 'x');
            admit(request, directory, false, mgmt::Status::HashMismatch);
        }
        else
        {
            assert(scenario == "v1" || scenario == "v2" || scenario == "receipt" || scenario == "forged");
            auto input = admit(request, directory, false, mgmt::Status::Ok);
            assert(input->result()->phase == ts::PublicationPhase::Accepted); // Local admission is not publication.
            const auto original = *input->result();
            input.reset();
            auto recovered = admit(request, directory, true, mgmt::Status::Ok);
            assert(recovered->result()->sequence == original.sequence && recovered->result()->phase == ts::PublicationPhase::Accepted);
            recovered.reset();
            std::array<char, 160> a{}, b{}, temporary{}, receipt{};
            assert(ts::resolveObjectPath(request.object, ts::ObjectFile::PublicationA, request.operation, a.data(), a.size()) &&
                   ts::resolveObjectPath(request.object, ts::ObjectFile::PublicationB, request.operation, b.data(), b.size()) &&
                   ts::resolveObjectPath(request.object, ts::ObjectFile::ReceiptTemporary, request.operation, temporary.data(), temporary.size()) &&
                   ts::resolveObjectPath(request.object, ts::ObjectFile::Receipt, request.operation, receipt.data(), receipt.size()));
            ts::PublicationJournalStore<native::UploadIo> jobs(io, a.data(), b.data());
            ts::PublicationJournal current, next, verify;
            assert(jobs.load(current, verify, scratch.data(), scratch.size()) == ts::PublicationStoreResult::Ready);
            next = current;
            next.sequence++;
            next.offset = next.file_bytes;
            next.phase = ts::PublicationPhase::FileStored;
            assert(jobs.advance(current, next, verify, scratch.data(), scratch.size()) == ts::PublicationStoreResult::Ready);
            current = next;
            next = current;
            next.sequence++;
            next.phase = ts::PublicationPhase::Publishing;
            assert(jobs.advance(current, next, verify, scratch.data(), scratch.size()) == ts::PublicationStoreResult::Ready);
            current = next;
            const auto raw = encode([&](Writer& w)
                                    {w.array(13);w.unsignedInteger(1);w.binary(request.operation);w.binary(request.object);w.binary(request.revision_hash);
                w.unsignedInteger(1);w.unsignedInteger(0);w.binary({current.file_hash,32});w.unsignedInteger(current.file_bytes);w.binary({directory.public_key,64});
                w.binary({directory.delivery,16});w.binary({directory.epoch,16});w.unsignedInteger(1);w.unsignedInteger(100); });
            constexpr char domain[] = "TrailMate.Track.Receipt.v1";
            Bytes message(reinterpret_cast<const uint8_t*>(domain), reinterpret_cast<const uint8_t*>(domain) + sizeof(domain));
            message.insert(message.end(), raw.begin(), raw.end());
            std::array<uint8_t, 64> signature{};
            assert(server.sign(message.data(), message.size(), signature.data()));
            const auto signed_receipt = encode([&](Writer& w)
                                               {w.array(2);w.binary({raw.data(),raw.size()});w.binary({signature.data(),signature.size()}); });
            native::LinuxTrackReceiptCrypto crypto;
            ts::VerifiedReceiptView proof;
            std::array<uint8_t, 1400> workspace{};
            assert(ts::verifyReceipt({signed_receipt.data(), signed_receipt.size()}, current, request.operation, {directory.public_key, 64}, 0, crypto, workspace.data(), workspace.size(), proof) == ts::ReceiptVerification::Valid);
            ts::SignedManifestStore<native::UploadIo> receipts(io, temporary.data(), receipt.data());
            assert(ts::confirmPublication(jobs, receipts, proof, current, next, verify, request.operation, {directory.public_key, 64}, 0, workspace.data(), workspace.size()) == ts::PublicationStoreResult::Ready);
            auto confirmed = admit(request, directory, true, mgmt::Status::Ok);
            assert(confirmed->result()->phase == ts::PublicationPhase::Confirmed);
            confirmed.reset();
            if (scenario == "receipt" || scenario == "forged")
            {
                if (scenario == "receipt") std::ofstream(fixture->sd / (receipt.data() + 1)) << "corrupt receipt";
                else
                {
                    auto damaged = signed_receipt;
                    damaged.back() ^= 1;
                    std::ofstream output(fixture->sd / (receipt.data() + 1), std::ios::binary | std::ios::trunc);
                    output.write(reinterpret_cast<const char*>(damaged.data()), damaged.size());
                    output.close();
                }
                admit(request, directory, true, mgmt::Status::Conflict);
            }
        }
    }
    std::filesystem::remove_all(root);
}
