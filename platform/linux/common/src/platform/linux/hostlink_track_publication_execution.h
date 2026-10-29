#pragma once
#include "platform/linux/hostlink_track_publication_admission.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/track_publication_transport.h"
#include "track_sharing/publication_network_session.h"
#include <algorithm>
#include <functional>
#include <memory>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include "platform/linux/track_publication_assets.h"
#include <openssl/rand.h>
#endif

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
// Protocol-worker owner: advance and receive are serialized on that worker.
// The enclosing service retains the storage lease and Reticulum identity.
class HostlinkTrackPublicationExecution
{
  public:
    using Transport = TrackPublicationTransport;
    using MessageSource = TrackPublicationMessageSource;
    using Jobs = track_sharing::PublicationJournalStore<UploadIo>;
    using Receipts = track_sharing::SignedManifestStore<UploadIo>;
    struct Lease
    {
        HostlinkTrackPublicationExecution& owner;
        bool holds(const track_sharing::PublicationJournal& job) const { return owner.holds(job); }
    };
    struct Sender
    {
        HostlinkTrackPublicationExecution& owner;
        chat::MeshSendResult sendTrackData(const uint8_t* destination, chat::lxmf::ByteSpan data, const uint8_t* author,
                                           uint32_t generation, bool response, std::array<uint8_t, 32>*)
        {
            if (response || !owner.holds(owner.input_->job_) || generation != owner.input_->request_.generation ||
                !author || std::memcmp(author, owner.input_->author_, 64) || !destination ||
                std::memcmp(destination, owner.input_->directory_.delivery, 16) || !owner.transport_ || !owner.message_source_)
                return chat::MeshSendResult::fail(chat::MeshOperationFailure::InvalidInput);
            const auto id = owner.message_source_();
            return id && owner.transport_(destination, owner.input_->directory_.public_key, data, id)
                       ? chat::MeshSendResult::success(id)
                       : chat::MeshSendResult::fail(chat::MeshOperationFailure::Busy);
        }
    };
    struct Nonce
    {
        bool fill(uint8_t* output, size_t bytes) { return output && bytes == 16 && RAND_bytes(output, bytes) == 1; }
    };
    using Session = track_sharing::PublicationNetworkSession<UploadIo, Jobs, Receipts, LinuxTrackReceiptCrypto, Lease, Sender, Nonce>;
    HostlinkTrackPublicationExecution(std::unique_ptr<HostlinkTrackPublicationAdmission> input, const LinuxReticulumIdentity& identity,
                                      Transport transport, MessageSource messages)
        : input_(std::move(input)), identity_(identity), transport_(std::move(transport)), message_source_(std::move(messages)),
          receipts_(input_->io_, receipt_temporary_.data(), receipt_path_.data()), lease_{*this}, sender_{*this},
          network_(input_->io_, input_->jobs_, receipts_, input_->receipt_crypto_, lease_, sender_, nonce_, input_->job_, input_->desired_, input_->job_verify_,
                   input_->manifest_, input_->receipt_, {input_->directory_.public_key, 64}, identity.destinationHash().data(), input_->author_, input_->kml_path_,
                   wire_.data(), wire_.size(), input_->workspace_, sizeof(input_->workspace_), file_.data(), file_.size(), 30000, 3000, 8)
    {
        initialized_ = input_->ready() && track_sharing::resolveObjectPath(input_->request_.object, track_sharing::ObjectFile::ReceiptTemporary, input_->request_.operation, receipt_temporary_.data(), receipt_temporary_.size()) &&
                       track_sharing::resolveObjectPath(input_->request_.object, track_sharing::ObjectFile::Receipt, input_->request_.operation, receipt_path_.data(), receipt_path_.size());
        if (initialized_ && input_->job_.phase == track_sharing::PublicationPhase::Accepted && input_->manifest_.manifest.version == 2)
        {
            track_sharing::ManifestExtensionsView extensions;
            initialized_ = track_sharing::getManifestExtensions(input_->manifest_.manifest, extensions);
            if (initialized_ && extensions.assets.count)
                assets_ = std::make_unique<TrackPublicationAssets>(input_->io_, input_->manifest_.manifest, input_->job_.object, input_->directory_,
                                                                   identity.destinationHash().data(), transport_, message_source_, [this]
                                                                   { return holds(input_->job_); });
        }
    }
    bool ready() const { return initialized_ && holds(input_->job_); }
    const track_sharing::PublicationJournal* result() const { return ready() ? &input_->job_ : nullptr; }
    track_sharing::NetworkSessionResult advance(uint64_t now)
    {
        if (!ready()) return track_sharing::NetworkSessionResult::Conflict;
        if (paused_) return track_sharing::NetworkSessionResult::Paused;
        if (assets_ && !assets_->done()) return assets_->advance(now);
        return network_.advance(now);
    }
    bool receive(chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view)
    {
        if (!ready() || !chat::lxmf::isTrackCustomType(type) || !view.message_hash.data || view.message_hash.size != 32) return false;
        for (size_t i = 0; i < accepted_count_; ++i)
            if (!std::memcmp(accepted_[i].data(), view.message_hash.data, 32)) return true;
        if (paused_) return false;
        // The native LXMF backend has already authenticated the envelope.
        const bool accepted = assets_ && !assets_->done() ? assets_->receive(view) : chat::lxmf::deliverVerifiedTrack(&network_.binding(), false, false, view);
        if (!accepted) return false;
        std::copy_n(view.message_hash.data, 32, accepted_[accepted_cursor_].begin());
        accepted_cursor_ = (accepted_cursor_ + 1) % accepted_.size();
        accepted_count_ = std::min(accepted_count_ + 1, accepted_.size());
        return true;
    }
    track_sharing::PublicationStoreResult cancel(uint32_t generation, geocaching::ByteView actor)
    {
        const auto result = ready() ? network_.cancel(generation, actor) : track_sharing::PublicationStoreResult::Conflict;
        if (result == track_sharing::PublicationStoreResult::Ready)
        {
            paused_ = true;
            if (assets_) assets_->pause();
        }
        return result;
    }
    void pause()
    {
        paused_ = true;
        network_.pause();
        if (assets_) assets_->pause();
    }
    bool resume(uint64_t now)
    {
        if (!ready() || !network_.resume(now) || (assets_ && !assets_->resume(now))) return false;
        paused_ = false;
        return true;
    }

  private:
    bool holds(const track_sharing::PublicationJournal& job) const
    {
        return input_->ready() && identity_.ready() && identity_.generation() == input_->request_.generation &&
               !std::memcmp(identity_.publicKey().data(), input_->author_, 64) && job.generation == input_->request_.generation &&
               !std::memcmp(job.operation, input_->request_.operation.data, 16) && !std::memcmp(job.author_hash, input_->author_hash_, 32) &&
               track_sharing::publicationManifestMatches(job, input_->manifest_) &&
               track_sharing::publicationDirectoryMatches(input_->directory_, job);
    }
    std::unique_ptr<HostlinkTrackPublicationAdmission> input_;
    const LinuxReticulumIdentity& identity_;
    Transport transport_;
    MessageSource message_source_;
    std::array<char, 160> receipt_temporary_{}, receipt_path_{};
    std::array<uint8_t, 8192> wire_{};
    std::array<uint8_t, 1024> file_{};
    Receipts receipts_;
    Lease lease_;
    Sender sender_;
    Nonce nonce_;
    Session network_;
    bool initialized_ = false;
    bool paused_ = false;
    std::unique_ptr<TrackPublicationAssets> assets_;
    std::array<std::array<uint8_t, 32>, 32> accepted_{};
    size_t accepted_count_ = 0, accepted_cursor_ = 0;
};
#endif
} // namespace platform::linux_runtime
