#pragma once
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/route_image_source_io.h"
#include "platform/linux/track_manifest_crypto.h"
#include "platform/linux/track_publication_transport.h"
#include "track_sharing/read_network_exchange.h"
#include "track_sharing/read_query_binding.h"
#include "track_sharing/read_response_sink.h"
#include "track_sharing/read_usb_request.h"

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
// All methods run on the mesh worker. The shared exchange owns protocol state;
// this adapter lends native identity, transport and descriptor-bound storage.
class HostlinkTrackReadOwner
{
  public:
    using Status = ::hostlink::management::Status;
    using Store = track_sharing::ReadResponseStore<UploadIo>;
    using Validator = track_sharing::ReadResponseValidator<LinuxTrackManifestCrypto, RouteSourceHash>;
    using Sink = track_sharing::ReadResponseSink<Store, Validator, RouteSourceHash>;
    struct Lease
    {
        HostlinkTrackReadOwner& owner;
        bool holds() const { return owner.current(); }
    };
    struct Adapter
    {
        HostlinkTrackReadOwner& owner;
        const chat::lxmf::TrackDeliveryBinding* binding = nullptr;
        chat::TrackBindingChange replaceTrackDelivery(const chat::lxmf::TrackDeliveryBinding* previous, const chat::lxmf::TrackDeliveryBinding* next)
        {
            if (binding != previous) return chat::TrackBindingChange::Conflict;
            binding = next;
            return chat::TrackBindingChange::Applied;
        }
        chat::TrackBindingChange visitTrackDelivery(const chat::lxmf::TrackDeliveryBinding* expected, uint8_t* author, uint32_t& generation, void (*visit)(void*), void* context)
        {
            if (!binding || binding != expected || !owner.current()) return chat::TrackBindingChange::Conflict;
            std::copy_n(owner.identity_.publicKey().data(), 64, author);
            generation = owner.identity_.generation();
            visit(context);
            return chat::TrackBindingChange::Applied;
        }
        chat::MeshSendResult sendTrackData(const uint8_t* target, chat::lxmf::ByteSpan bytes, const uint8_t* author, uint32_t generation, bool response, std::array<uint8_t, 32>*)
        {
            if (!owner.current() || response || generation != owner.decoded_.generation || !author || std::memcmp(author, owner.author_.data(), 64) ||
                !target || std::memcmp(target, owner.directory_.delivery, 16) || !owner.transport_ || !owner.messages_) return chat::MeshSendResult::fail(chat::MeshOperationFailure::InvalidInput);
            const auto id = owner.messages_();
            return id && owner.transport_(target, owner.directory_.public_key, bytes, id) ? chat::MeshSendResult::success(id) : chat::MeshSendResult::fail(chat::MeshOperationFailure::Busy);
        }
    };
    using Exchange = track_sharing::ReadNetworkExchange<Adapter, Sink, Lease>;
    HostlinkTrackReadOwner(std::filesystem::path root, const LinuxReticulumIdentity& identity, const uint8_t session[16], geocaching::ByteView body,
                           const track_sharing::PublicationDirectory& directory, TrackPublicationTransport transport, TrackPublicationMessageSource messages, uint64_t now)
        : identity_(identity), directory_(directory), transport_(std::move(transport)), messages_(std::move(messages)), io_(std::move(root)), author_(identity.publicKey()),
          store_(io_, temporary_.data(), destination_.data()), validator_(decoded_.expected, crypto_, hash_, manifest_, row_, page_, workspace_.data(), workspace_.size(), &regions_),
          sink_(store_, validator_, hash_, stored_, workspace_.data(), workspace_.size()), lease_{*this}, adapter_{*this},
          exchange_(adapter_, sink_, lease_, {author_.data(), 64}, identity.generation()), touched(now)
    {
        using namespace track_sharing;
        if (!networkRequestId({session, 16}) || !body.data || body.size > raw_.size() || !identity.ready() || !publicationDirectoryValid(directory_)) return;
        std::copy_n(session, 16, session_.begin());
        std::copy_n(identity.publicKey().data(), 64, author_.begin());
        bytes_ = body.size;
        std::copy_n(body.data, body.size, raw_.begin());
        if (!decodeReadUsbRequest({raw_.data(), bytes_}, decoded_) || decoded_.generation != identity.generation()) return;
        decoded_.expected.directory_public = {directory_.public_key, 64};
        decoded_.expected.directory_epoch = {directory_.epoch, 16};
        const auto op = decoded_.operation;
        auto& query = decoded_.query;
        if (op == 1 || op == 13)
        {
            if (query.limit > (op == 1 ? directory_.query_limit : 32)) return;
            const bool bound = op == 1 ? deriveReadQueryBinding(query, hash_, workspace_.data(), workspace_.size(), query_binding_.data())
                                       : deriveRegionQueryBinding(regionBytes(decoded_.region.id), decoded_.region.version, query, hash_, workspace_.data(), workspace_.size(), query_binding_.data());
            if (!bound || (query.has_cursor && std::memcmp(query.cursor_binding.data, query_binding_.data(), 32))) return;
            decoded_.expected.query_binding = {query_binding_.data(), 32};
        }
        if (op == 8 && (decoded_.expected.file_bytes > directory_.file_limit || decoded_.expected.length > directory_.chunk_limit)) return;
        if (!io_.current())
        {
            status_ = Status::StorageUnavailable;
            return;
        }
        if (!io_.lock())
        {
            status_ = Status::Busy;
            return;
        }
        if (!io_.mkdirs("/trailmate/track-read"))
        {
            status_ = Status::StorageUnavailable;
            return;
        }
        std::string stem = "/trailmate/track-read/";
        constexpr char hex[] = "0123456789abcdef";
        for (const auto input : {geocaching::ByteView{session_.data(), 16}, decoded_.request})
            for (size_t i = 0; i < input.size; ++i)
            {
                stem += hex[input.data[i] >> 4];
                stem += hex[input.data[i] & 15];
            }
        std::strcpy(temporary_.data(), (stem + ".tmp").c_str());
        std::strcpy(destination_.data(), (stem + ".cmp").c_str());
        for (const auto* path : {temporary_.data(), destination_.data()})
        {
            const auto file = io_.open(path, UploadIo::Mode::Read);
            if (file.handle) io_.close(file.handle);
            if (file.status != UploadIo::OpenStatus::Missing)
            {
                status_ = Status::Conflict;
                return;
            }
        }
        paths_owned_ = live_ = true;
        size_t size = 0;
        bool encoded = false;
        const auto request = decoded_.request;
        auto& expected = decoded_.expected;
        const auto& region = decoded_.region;
        if (op == 0)
        {
            geocaching::protocol::CmpWriter writer(workspace_.data(), workspace_.size());
            encoded = networkRequestHeader(writer, 0, request, 8192, 0);
            size = writer.size();
        }
        else if (op == 1) encoded = encodeTrackReadQuery(request, query, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 2) encoded = encodeTrackReadManifest(request, expected.object, expected.revision, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 8) encoded = encodeTrackReadFile(request, expected.file_hash, expected.offset, expected.length, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 9) encoded = encodeRegionCapabilities(request, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 10) encoded = encodeRegionChildren(request, region.version, regionBytes(region.parent), regionBytes(region.after), query.limit, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 11) encoded = encodeRegionDescriptor(request, regionBytes(region.id), region.version, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 12) encoded = encodeRegionBoundaryChunk(request, regionBytes(region.id), region.version, expected.offset, expected.length, 8192, workspace_.data(), workspace_.size(), size);
        else if (op == 13) encoded = encodeRegionQuery(request, regionBytes(region.id), region.version, query, 8192, workspace_.data(), workspace_.size(), size);
        if (!encoded || exchange_.attach(op, request, {directory_.delivery, 16}, {identity.destinationHash().data(), 16}, {workspace_.data(), size}, 8192, now) != chat::TrackBindingChange::Applied) return;
        status_ = Status::Ok;
    }
    ~HostlinkTrackReadOwner()
    {
        exchange_.detach();
        live_ = false;
        if (paths_owned_ && io_.current())
        {
            io_.remove(temporary_.data());
            io_.remove(destination_.data());
        }
    }
    bool current() const { return live_ && io_.current() && identity_.ready() && identity_.generation() == decoded_.generation && !std::memcmp(author_.data(), identity_.publicKey().data(), 64); }
    Status admission() const { return status_; }
    bool session(const uint8_t* value) const { return value && !std::memcmp(session_.data(), value, 16); }
    bool request(geocaching::ByteView value) const { return value.size == 16 && value.data && !std::memcmp(decoded_.request.data, value.data, 16); }
    bool same(geocaching::ByteView body) const { return body.size == bytes_ && body.data && !std::memcmp(raw_.data(), body.data, bytes_); }
    void advance(uint64_t now)
    {
        if (current()) exchange_.advance(now, state_);
    }
    bool receive(const chat::lxmf::CustomDeliveryView& view) { return current() && chat::lxmf::deliverVerifiedTrack(adapter_.binding, false, false, view); }
    Status read(uint32_t offset, uint16_t count, uint8_t* output, size_t capacity, size_t& written, track_sharing::StoredReadResponse& metadata)
    {
        written = 0;
        if (!current()) return io_.current() ? Status::StaleIdentity : Status::StorageUnavailable;
        if (!stored_.ready) return state_ == track_sharing::ReadExchangeState::Waiting || state_ == track_sharing::ReadExchangeState::Idle ? Status::Busy : Status::Expired;
        metadata = stored_;
        if (!count) return Status::Ok;
        const auto read = store_.read(stored_.bytes, offset, count, output, capacity, written);
        return read == track_sharing::ReadStoreResult::Ready ? Status::Ok : read == track_sharing::ReadStoreResult::Invalid ? Status::InvalidField
                                                                        : read == track_sharing::ReadStoreResult::Conflict  ? Status::Conflict
                                                                                                                            : Status::StorageUnavailable;
    }
    uint8_t operation() const { return decoded_.operation; }
    uint64_t touched;

  private:
    const LinuxReticulumIdentity& identity_;
    track_sharing::PublicationDirectory directory_;
    TrackPublicationTransport transport_;
    TrackPublicationMessageSource messages_;
    UploadIo io_;
    std::array<uint8_t, 471> raw_{};
    size_t bytes_ = 0;
    std::array<uint8_t, 16> session_{};
    std::array<uint8_t, 64> author_{};
    std::array<uint8_t, 32> query_binding_{};
    std::array<char, 96> temporary_{}, destination_{};
    std::array<uint8_t, 4200> workspace_{};
    track_sharing::ReadUsbRequest decoded_;
    RouteSourceHash hash_;
    LinuxTrackManifestCrypto crypto_;
    track_sharing::VerifiedManifestView manifest_;
    track_sharing::ReadQueryRow row_;
    track_sharing::ReadQueryResponse page_;
    track_sharing::RegionReplyWorkspace regions_;
    track_sharing::StoredReadResponse stored_;
    Store store_;
    Validator validator_;
    Sink sink_;
    Lease lease_;
    Adapter adapter_;
    Exchange exchange_;
    track_sharing::ReadExchangeState state_ = track_sharing::ReadExchangeState::Idle;
    Status status_ = Status::InvalidField;
    bool live_ = false, paths_owned_ = false;
};
#endif
} // namespace platform::linux_runtime
