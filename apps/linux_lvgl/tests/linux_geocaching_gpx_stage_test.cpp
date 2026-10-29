#include "geocaching/gpx/write_record.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/geocaching_gpx_stage.h"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using Stage = platform::linux_runtime::LinuxGeocachingGpxStage;
using Result = Stage::Result;
namespace gc = geocaching;
struct StringSink final : gc::gpx::OutputSink
{
    std::string text;
    bool write(std::string_view bytes) override
    {
        text.append(bytes);
        return true;
    }
};
std::string contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void overwrite(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), bytes.size());
    assert(file.good());
}
int main(int argc, char** argv)
{
#if !defined(__linux__) || !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)argc;
    (void)argv;
    return 77;
#else
    assert(argc == 2);
    const auto binary = contents(argv[1]);
    gc::ByteView signed_record{reinterpret_cast<const uint8_t*>(binary.data()), binary.size()};
    platform::linux_runtime::LinuxGeocachingCrypto crypto;
    std::array<uint8_t, 4200> scratch{};
    gc::protocol::VerifiedRecordView verified;
    assert(gc::protocol::verifyGeocache(signed_record, crypto, scratch.data(), scratch.size(), verified) == gc::protocol::VerificationResult::Valid);
    StringSink canonical;
    assert(gc::gpx::writeGeocacheGpx(verified, crypto, canonical));
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-gpx-stage-" + std::to_string(getpid()));
    const auto cache = root / "cache";
    const auto outside = root / "outside";
    std::filesystem::create_directories(cache);
    std::filesystem::create_directories(outside);
    Stage stage(cache, crypto);
    Stage::TransactionId transaction{};
    transaction[0] = 1;
    Stage::Proof proof;
    assert(stage.stage({}, verified.id, verified.hash, signed_record, proof) == Result::Invalid);
    auto wrong_hash = verified.hash;
    wrong_hash.bytes[0] ^= 1;
    assert(stage.stage(transaction, verified.id, wrong_hash, signed_record, proof) == Result::Invalid);
    assert(!std::filesystem::exists(cache / ".state"));
    assert(stage.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Ready);
    const auto path = cache / proof.relative_path;
    const auto original_proof = proof;
    assert(proof.bytes == canonical.text.size() && contents(path) == canonical.text);
    std::array<uint8_t, 32> digest{};
    assert(crypto.sha256({reinterpret_cast<const uint8_t*>(canonical.text.data()), canonical.text.size()}, digest.data()) && digest == proof.file_hash);
    assert(stage.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Exists && !proof.bytes && proof.relative_path.empty());
    assert(contents(path) == canonical.text);
    // Exercise a real short write/EFBIG without a mock filesystem. Limits and
    // signal disposition belong to this isolated test process and are restored.
    Stage::TransactionId interrupted{};
    interrupted[0] = 2;
    struct rlimit previous_limit
    {
    };
    assert(::getrlimit(RLIMIT_FSIZE, &previous_limit) == 0);
    auto restricted = previous_limit;
    restricted.rlim_cur = 512;
    const auto previous_signal = std::signal(SIGXFSZ, SIG_IGN);
    assert(previous_signal != SIG_ERR && ::setrlimit(RLIMIT_FSIZE, &restricted) == 0);
    const auto interrupted_result = stage.stage(interrupted, verified.id, verified.hash, signed_record, proof);
    assert(::setrlimit(RLIMIT_FSIZE, &previous_limit) == 0);
    assert(std::signal(SIGXFSZ, previous_signal) != SIG_ERR);
    assert(interrupted_result == Result::IoError && !proof.bytes && proof.relative_path.empty());
    const auto interrupted_path = path.parent_path() / ("02" + std::string(30, '0') + ".gpx");
    const auto interrupted_bytes = contents(interrupted_path);
    assert(!interrupted_bytes.empty() && interrupted_bytes.size() <= 512);
    assert(stage.verify(interrupted, verified.id, verified.hash, signed_record, proof) == Result::IoError);
    assert(stage.stage(interrupted, verified.id, verified.hash, signed_record, proof) == Result::Exists);
    assert(contents(interrupted_path) == interrupted_bytes);
    Stage reopened(cache, crypto);
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Ready);
    assert(proof.file_hash == original_proof.file_hash && proof.bytes == original_proof.bytes);
    // Damaged/partial staging files are retained, never silently overwritten.
    overwrite(path, canonical.text.substr(0, canonical.text.size() / 2));
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::IoError && !proof.bytes);
    const auto partial = contents(path);
    assert(stage.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Exists && contents(path) == partial);
    auto changed = canonical.text;
    changed[0] ^= 1;
    overwrite(path, changed);
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::IoError);
    overwrite(path, canonical.text + "x");
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::IoError);
    overwrite(path, canonical.text);
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Ready);
    std::filesystem::create_hard_link(path, outside / "linked.gpx");
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Unsafe);
    std::filesystem::remove(outside / "linked.gpx");
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Ready);
    std::filesystem::remove(path);
    overwrite(outside / "protected.gpx", "must remain unchanged");
    std::filesystem::create_symlink(outside / "protected.gpx", path);
    assert(stage.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Exists);
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Unsafe);
    assert(contents(outside / "protected.gpx") == "must remain unchanged");
    std::filesystem::remove(path);
    assert(::mkfifo(path.c_str(), 0600) == 0);
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Unsafe); // Must not block on FIFO input.
    std::filesystem::remove(path);
    assert(reopened.verify(transaction, verified.id, verified.hash, signed_record, proof) == Result::Missing);
    const auto alias_root = root / "aliased-cache";
    std::filesystem::create_directories(alias_root);
    std::filesystem::create_directory_symlink(outside, alias_root / ".state");
    Stage alias(alias_root, crypto);
    assert(alias.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Unsafe);
    assert(!std::filesystem::exists(outside / "staging"));
    Stage absent(root / "absent-volume", crypto);
    assert(absent.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Unavailable);
    assert(!std::filesystem::exists(root / "absent-volume"));
    const auto second_cache = root / "second-cache";
    std::filesystem::create_directories(second_cache);
    Stage second(second_cache, crypto);
    assert(second.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::Ready);
    assert(proof.file_hash == original_proof.file_hash && contents(second_cache / proof.relative_path) == canonical.text);
    struct Unavailable final : gc::protocol::RecordCrypto
    {
        bool sha256(gc::ByteView, uint8_t[32]) override { return false; }
        gc::protocol::VerificationResult verifyEd25519(gc::ByteView, gc::ByteView, gc::ByteView) override { return gc::protocol::VerificationResult::CryptoUnavailable; }
    } unavailable;
    const auto disabled_cache = root / "disabled-cache";
    std::filesystem::create_directories(disabled_cache);
    Stage disabled(disabled_cache, unavailable);
    assert(disabled.stage(transaction, verified.id, verified.hash, signed_record, proof) == Result::CryptoUnavailable);
    assert(std::filesystem::is_empty(disabled_cache));
    std::filesystem::remove_all(root);
#endif
}
