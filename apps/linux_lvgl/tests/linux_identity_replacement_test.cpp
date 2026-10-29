#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Result = chat::DeviceIdentityRestoreResult;
void sql(const std::filesystem::path& path, const char* text)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
int main()
{
    char directory[] = "/tmp/trailmate-identity-replacement-XXXXXX";
    assert(mkdtemp(directory));
    const std::filesystem::path root(directory), path = root / "target.db";
    {
        Identity source(root / "source.db");
        assert(source.begin());
        const auto candidate_public = source.publicKey();
        std::array<uint8_t, 96> seed{}, expanded{}, copied{};
        size_t size = 0;
        assert(source.privateFormat() == 1 && source.copyPrivateKey(1, seed.data(), seed.size(), size) && size == 64);
        assert(Identity::validateKeyPair(1, seed.data(), size, candidate_public.data()));
        std::copy_n(seed.data(), 32, expanded.data());
        expanded[0] &= 0xf8;
        expanded[31] = (expanded[31] & 0x7f) | 0x40;
        std::array<uint8_t, 32> derived{};
        ed25519_create_keypair(derived.data(), expanded.data() + 32, seed.data() + 32);
        assert(std::equal(derived.begin(), derived.end(), candidate_public.begin() + 32));
        assert(Identity::validateKeyPair(0, expanded.data(), 96, candidate_public.data()));
        assert(!Identity::validateKeyPair(1, expanded.data(), 96, candidate_public.data()));
        assert(!Identity::validateKeyPair(0, seed.data(), 64, candidate_public.data()));
        auto bad = expanded;
        bad[0] |= 1;
        assert(!Identity::validateKeyPair(0, bad.data(), 96, candidate_public.data()));
        auto target = std::make_unique<Identity>(path);
        assert(target->begin());
        const auto old_public = target->publicKey();
        copied.fill(0xaa);
        assert(!target->copyPrivateKey(2, copied.data(), copied.size(), size) && size == 0);
        assert(std::all_of(copied.begin(), copied.begin() + 64, [](uint8_t v)
                           { return !v; }));
        assert(!target->copyPrivateKey(1, const_cast<uint8_t*>(target->publicKey().data()), 64, size) && target->publicKey() == old_public);
        assert(target->replaceKeyPair(2, old_public.data(), 0, expanded.data(), 96, candidate_public.data()) == Result::StaleIdentity);
        auto wrong = old_public;
        wrong[0] ^= 1;
        assert(target->replaceKeyPair(1, wrong.data(), 0, expanded.data(), 96, candidate_public.data()) == Result::StaleIdentity);
        assert(target->replaceKeyPair(1, old_public.data(), 0, bad.data(), 96, candidate_public.data()) == Result::InvalidInput);
        {
            Identity other_owner(path);
            assert(other_owner.begin());
            assert(target->replaceKeyPair(1, old_public.data(), 0, expanded.data(), 96, candidate_public.data()) == Result::Unavailable);
            assert(target->ready()); // Failed atomic lease upgrade retained read ownership.
        }
        int ready[2], release[2];
        assert(pipe(ready) == 0 && pipe(release) == 0);
        const auto child = fork();
        assert(child >= 0);
        if (!child)
        {
            close(ready[0]);
            close(release[1]);
            target.reset(); // Close only the inherited duplicate of the parent's lease.
            {
                Identity reader(path);
                if (!reader.begin() || write(ready[1], "1", 1) != 1) _exit(2);
                char byte;
                if (read(release[0], &byte, 1) != 1) _exit(3);
            }
            _exit(0);
        }
        close(ready[1]);
        close(release[0]);
        char byte;
        assert(read(ready[0], &byte, 1) == 1);
        assert(target->replaceKeyPair(1, old_public.data(), 0, expanded.data(), 96, candidate_public.data()) == Result::Unavailable);
        assert(write(release[1], "1", 1) == 1);
        close(ready[0]);
        close(release[1]);
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        sql(path, "CREATE TRIGGER reject_restore BEFORE UPDATE ON linux_reticulum_identity BEGIN SELECT RAISE(ABORT,'injected'); END;");
        assert(target->replaceKeyPair(1, old_public.data(), 0, expanded.data(), 96, candidate_public.data()) == Result::StorageError);
        assert(target->ready());
        uint8_t signature[64];
        const uint8_t message[]{1, 2, 3};
        assert(target->sign(message, sizeof(message), signature) && Identity::verify(old_public.data() + 32, message, sizeof(message), signature));
        sql(path, "DROP TRIGGER reject_restore;");
        assert(target->replaceKeyPair(1, old_public.data(), 0, expanded.data(), 96, candidate_public.data()) == Result::CommittedRebootRequired);
        assert(!target->ready() && !target->begin() && !target->sign(message, sizeof(message), signature));
        Identity blocked(path);
        assert(!blocked.begin()); // Retired writer excludes fresh readers until released.
        target.reset();
        assert(blocked.begin() && blocked.generation() == 2 && blocked.privateFormat() == 0 && blocked.publicKey() == candidate_public);
        assert(blocked.copyPrivateKey(2, copied.data(), copied.size(), size) && size == 96 && copied == expanded);
        assert(blocked.sign(message, sizeof(message), signature) && Identity::verify(candidate_public.data() + 32, message, sizeof(message), signature));
        uint8_t forward[32], reverse[32];
        assert(blocked.sharedSecret(source.publicKey().data(), forward) && source.sharedSecret(blocked.publicKey().data(), reverse));
        assert(!std::memcmp(forward, reverse, 32));
        // Restoring the same key still advances generation; seed storage stays supported.
        assert(blocked.replaceKeyPair(2, candidate_public.data(), 1, seed.data(), 64, candidate_public.data()) == Result::CommittedRebootRequired);
    }
    {
        Identity reopened(path);
        assert(reopened.begin() && reopened.generation() == 3 && reopened.privateFormat() == 1);
    }
    sql(path, "UPDATE linux_reticulum_identity SET generation=4294967295;");
    {
        Identity exhausted(path);
        assert(exhausted.begin());
        uint8_t bytes[96];
        size_t size = 0;
        assert(exhausted.copyPrivateKey(UINT32_MAX, bytes, sizeof(bytes), size));
        assert(exhausted.replaceKeyPair(UINT32_MAX, exhausted.publicKey().data(), 1, bytes, size, exhausted.publicKey().data()) == Result::InvalidInput);
    }
    struct stat status
    {
    };
    assert(stat((path.string() + ".identity-owner").c_str(), &status) == 0 && (status.st_mode & 0777) == 0600);
    std::filesystem::remove_all(root);
}
