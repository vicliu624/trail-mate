#include "chat/linux_reticulum_stamp.h"
#include "chat/infra/lxmf/lxmf_propagation_stamp.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace protocol = chat::lxmf::runtime;
struct LinuxReticulumStamp::Impl
{
    State state = State::Idle;
    std::vector<uint8_t> workblock;
    std::array<uint8_t, 32> transient{}, stamp{};
    uint16_t expanded = 0;
    uint32_t searched = 0;
    uint8_t cost = 0;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_MD_CTX* base = EVP_MD_CTX_new();
    EVP_MD_CTX* attempt = EVP_MD_CTX_new();
    ~Impl()
    {
        EVP_MD_CTX_free(base);
        EVP_MD_CTX_free(attempt);
    }
#endif
    void release()
    {
        std::vector<uint8_t>().swap(workblock);
#if defined(TRAIL_MATE_HAS_OPENSSL)
        if (base) EVP_MD_CTX_reset(base);
        if (attempt) EVP_MD_CTX_reset(attempt);
#endif
    }
};
LinuxReticulumStamp::LinuxReticulumStamp() : impl_(std::make_unique<Impl>()) {}
LinuxReticulumStamp::~LinuxReticulumStamp() = default;
void LinuxReticulumStamp::reset()
{
    impl_->release();
    impl_->state = State::Idle;
    impl_->expanded = 0;
    impl_->searched = 0;
    impl_->cost = 0;
    impl_->transient.fill(0);
    impl_->stamp.fill(0);
}
bool LinuxReticulumStamp::begin(const uint8_t transient[32], uint8_t cost)
{
    if (!transient) return false;
    reset();
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!impl_->base || !impl_->attempt)
    {
        impl_->state = State::Failed;
        return false;
    }
    try
    {
        impl_->workblock.resize(protocol::kPropagationStampWorkblockBytes);
    }
    catch (...)
    {
        impl_->state = State::Failed;
        return false;
    }
    std::memcpy(impl_->transient.data(), transient, 32);
    impl_->cost = cost;
    impl_->state = State::Expanding;
    return true;
#else
    (void)cost;
    impl_->state = State::Failed;
    return false;
#endif
}
LinuxReticulumStamp::State LinuxReticulumStamp::poll(uint16_t expand_budget, uint16_t search_budget)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    auto& work = *impl_;
    bool ok = true;
    while (work.state == State::Expanding && expand_budget--)
    {
        ok = protocol::expandPropagationStampRound(work.transient.data(), work.expanded,
                                                   work.workblock.data() + size_t(work.expanded) * protocol::kPropagationStampRoundBytes);
        if (!ok) break;
        if (++work.expanded == protocol::kPropagationStampExpandRounds)
        {
            ok = EVP_DigestInit_ex(work.base, EVP_sha256(), nullptr) == 1 &&
                 EVP_DigestUpdate(work.base, work.workblock.data(), work.workblock.size()) == 1;
            if (ok)
            {
                std::vector<uint8_t>().swap(work.workblock);
                work.state = State::Searching;
            }
        }
    }
    while (ok && work.state == State::Searching && search_budget--)
    {
        uint8_t hash[32];
        unsigned size = sizeof(hash);
        ok = RAND_bytes(work.stamp.data(), work.stamp.size()) == 1 && EVP_MD_CTX_copy_ex(work.attempt, work.base) == 1 &&
             EVP_DigestUpdate(work.attempt, work.stamp.data(), work.stamp.size()) == 1 &&
             EVP_DigestFinal_ex(work.attempt, hash, &size) == 1 && size == sizeof(hash);
        ++work.searched;
        if (ok && protocol::propagationStampMeetsTarget(hash, work.cost))
        {
            work.state = State::Complete;
            work.release();
        }
    }
    if (!ok)
    {
        work.state = State::Failed;
        work.release();
    }
#else
    (void)expand_budget;
    (void)search_budget;
#endif
    return impl_->state;
}
bool LinuxReticulumStamp::take(uint8_t stamp[32])
{
    if (!stamp || impl_->state != State::Complete) return false;
    std::memcpy(stamp, impl_->stamp.data(), 32);
    reset();
    return true;
}
LinuxReticulumStamp::State LinuxReticulumStamp::state() const { return impl_->state; }
uint16_t LinuxReticulumStamp::expandedRounds() const { return impl_->expanded; }
uint32_t LinuxReticulumStamp::searchRounds() const { return impl_->searched; }
} // namespace trailmate::linux_app
