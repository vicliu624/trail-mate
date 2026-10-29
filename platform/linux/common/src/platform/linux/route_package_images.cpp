#include "platform/linux/route_package_images.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/track_manifest_crypto.h"
#include "platform/linux/runtime_paths.h"
#include "route_image_source_io.h"
#include "track_sharing/attachment_package_file_check.h"
#include "track_sharing/image_source_binding_store.h"
#include "track_sharing/installed_package_binding.h"
#include "track_sharing/installed_package_path.h"
#include "track_sharing/manifest_file_check.h"
#include "track_sharing/package_cache_paths.h"
#include "track_sharing/signed_manifest_reader.h"
#include <algorithm>
#include <array>
#include <memory>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace
{
namespace ts = track_sharing;
class PackageIo : public RouteSourceIo
{
  public:
    explicit PackageIo(std::filesystem::path root) : root_(std::move(root)) {}
    OpenResult open(const char* path, Mode mode)
    {
        std::filesystem::path resolved;
        if (!path || mode != Mode::Read || path[0] != '/' ||
            !resolve_child_under_root(root_, std::string_view(path + 1), resolved))
            return {nullptr, OpenStatus::Unavailable};
        return RouteSourceIo::open(resolved.c_str(), mode);
    }

  private:
    std::filesystem::path root_;
};
struct ManifestCrypto : LinuxTrackManifestCrypto {};
struct ImageEntry
{
    std::array<uint8_t, 32> reference{}, hash{};
    uint32_t bytes = 0;
};
// Candidate entries remain private until signature, index/resources and KML all pass.
class PackageProof
{
  public:
    explicit PackageProof(const std::atomic_bool* cancelled)
        : io_(resolve_paths().sd_root), reader_(io_), kml_(io_, hash_, names_, sizeof(names_)), cancelled_(cancelled) {}
    ts::AttachmentResolveResult resolve(uint8_t ordinal, const ts::AttachmentEntryView& entry, char* path, size_t capacity)
    {
        if (ordinal >= binding_.count) return ts::AttachmentResolveResult::Invalid;
        if (!(binding_.availability & (uint64_t(1) << ordinal))) return ts::AttachmentResolveResult::Missing;
        if (!ts::resolvePackageCachePath({revision_, 32}, ts::ObjectFile::AttachmentResource, entry.hash, path, capacity))
            return ts::AttachmentResolveResult::Invalid;
        if (ts::attachmentIdsEqual(entry.mime, "image/jpeg") || ts::attachmentIdsEqual(entry.mime, "image/png"))
        {
            if (entries_.size() >= 64 || entry.bytes > ts::kImageSourceByteLimit) return ts::AttachmentResolveResult::Invalid;
            ImageEntry output;
            if (!reference_hash_.start() || !reference_hash_.update(reinterpret_cast<const uint8_t*>(entry.reference.data()), entry.reference.size()) ||
                !reference_hash_.finish(output.reference.data())) return ts::AttachmentResolveResult::Invalid;
            std::copy_n(entry.hash.data, 32, output.hash.begin());
            output.bytes = entry.bytes;
            entries_.push_back(output);
        }
        return ts::AttachmentResolveResult::Ready;
    }
    bool matchesAttachments(const ts::ManifestView& candidate) const
    {
        return authenticated_ && candidate.raw.data == verified_.manifest.raw.data && candidate.raw.size == verified_.manifest.raw.size &&
               (!binding_.count || (package_ && package_->ready()));
    }
    bool verify(const std::string& route)
    {
        if (stopped() || !ts::installedPackageRevision(route, revision_) ||
            !ts::routePackageTarget({revision_, 32}, ts::PackageRouteFile::Binding, path_, sizeof(path_))) return false;
        const auto marker = io_.open(path_, PackageIo::Mode::Read);
        uint64_t size = 0;
        const bool loaded = marker.handle && io_.size(marker.handle, size) && size == ts::kInstalledPackageBindingBytes &&
                            io_.read(marker.handle, scratch_, size) == size;
        const bool closed = marker.handle && io_.close(marker.handle);
        if (!loaded || !closed || !ts::decodeInstalledPackageBinding(scratch_, size, binding_) ||
            std::memcmp(binding_.revision, revision_, 32) ||
            !ts::resolvePackageCachePath({revision_, 32}, ts::ObjectFile::SignedManifest, {}, path_, sizeof(path_)) ||
            reader_.begin(path_, raw_, sizeof(raw_)) != ts::FileIntegrityResult::Pending) return false;
        auto read = ts::FileIntegrityResult::Pending;
        while (!stopped() && read == ts::FileIntegrityResult::Pending) read = reader_.step();
        if (stopped() || read != ts::FileIntegrityResult::Ready ||
            ts::verifyManifest(reader_.bytes(), crypto_, hash_, workspace_, sizeof(workspace_), verified_) != ts::ManifestVerification::Valid ||
            std::memcmp(verified_.object_id, binding_.object, 32) || std::memcmp(verified_.revision_hash, revision_, 32) ||
            verified_.manifest.file_bytes != binding_.kml_bytes || std::memcmp(verified_.manifest.file_hash.data, binding_.kml_hash, 32)) return false;
        if (verified_.manifest.version == 1)
        {
            if (binding_.count) return false;
        }
        else if (!ts::getManifestExtensions(verified_.manifest, extensions_) || extensions_.assets.count != binding_.count ||
                 extensions_.assets.bytes != binding_.index_bytes ||
                 (binding_.count && std::memcmp(extensions_.assets.hash.data, binding_.index_hash, 32))) return false;
        authenticated_ = true;
        if (binding_.count)
        {
            package_ = std::make_unique<Package>(io_, hash_, resource_hash_, *this, window_, sizeof(window_), fingerprints_, 64,
                                                 extensions_.assets, verified_.manifest.file_hash, verified_.manifest.file_bytes);
            if (!ts::resolvePackageCachePath({revision_, 32}, ts::ObjectFile::AttachmentIndex, {}, path_, sizeof(path_)) ||
                package_->begin(path_) != ts::AttachmentPackageResult::Pending) return false;
            auto result = ts::AttachmentPackageResult::Pending;
            while (!stopped() && result == ts::AttachmentPackageResult::Pending) result = package_->step();
            if (stopped() || result != ts::AttachmentPackageResult::Ready || !package_->ready()) return false;
        }
        auto result = verified_.manifest.version == 2 ? kml_.beginVerifiedPackage(route.c_str(), verified_, *this)
                                                      : kml_.begin(route.c_str(), verified_);
        while (!stopped() && result == ts::ManifestFileResult::Pending) result = kml_.step(scratch_, sizeof(scratch_));
        return ready_ = !stopped() && result == ts::ManifestFileResult::Ready && kml_.ready();
    }
    const ImageEntry* find(const uint8_t reference[32]) const
    {
        if (!ready_ || !reference) return nullptr;
        for (const auto& entry : entries_)
            if (!std::memcmp(entry.reference.data(), reference, 32)) return &entry;
        return nullptr;
    }
    bool resource(const ImageEntry& entry, std::filesystem::path& output) const
    {
        char path[160]{};
        return ready_ && ts::resolvePackageCachePath({revision_, 32}, ts::ObjectFile::AttachmentResource, {entry.hash.data(), 32}, path, sizeof(path)) &&
               resolve_child_under_root(resolve_paths().sd_root, path + 1, output);
    }

  private:
    bool stopped() const { return cancelled_ && cancelled_->load(); }
    using Package = ts::AttachmentPackageFileCheck<PackageIo, RouteSourceHash, PackageProof, 160>;
    PackageIo io_;
    RouteSourceHash hash_, resource_hash_, reference_hash_;
    ManifestCrypto crypto_;
    ts::SignedManifestReader<PackageIo> reader_;
    char names_[2048]{}, path_[160]{};
    ts::ManifestFileCheck<PackageIo, RouteSourceHash> kml_;
    uint8_t revision_[32]{}, raw_[4166]{}, workspace_[4200]{}, window_[4352]{}, scratch_[320]{};
    ts::AttachmentFingerprint fingerprints_[64]{};
    ts::InstalledPackageBinding binding_;
    ts::VerifiedManifestView verified_;
    ts::ManifestExtensionsView extensions_;
    std::unique_ptr<Package> package_;
    std::vector<ImageEntry> entries_;
    const std::atomic_bool* cancelled_;
    bool authenticated_ = false, ready_ = false;
};

bool publish_image(const PackageProof& proof, const ImageEntry& entry, const std::filesystem::path& image,
                   const std::atomic_bool* cancelled)
{
    const auto stopped = [&]
    { return cancelled && cancelled->load(); };
    std::filesystem::path resource;
    if (stopped() || !proof.resource(entry, resource)) return false;
    RouteImageLease lease((image.parent_path().parent_path() / ".route-images.lock").c_str());
    if (!lease) return false;
    static std::atomic_uint64_t sequence{0};
    const auto temporary = image.string() + ".package-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
    const auto temporary_binding = temporary + ".source", binding = image.string() + ".source";
    struct Cleanup
    {
        const std::string& file;
        const std::string& binding;
        ~Cleanup()
        {
            ::unlink(file.c_str());
            ::unlink(binding.c_str());
        }
    } cleanup{temporary, temporary_binding};
    RouteSourceIo input, output;
    RouteSourceHash hash;
    if (!input.open(resource.c_str(), RouteSourceIo::Mode::Read).handle) return false;
    uint64_t size = 0;
    if (!input.size(&input, size) || size != entry.bytes || !hash.start() ||
        !output.open(temporary.c_str(), RouteSourceIo::Mode::CreateTemporary).handle) return false;
    uint8_t scratch[320]{}, digest[32]{};
    uint32_t consumed = 0;
    while (consumed < entry.bytes)
    {
        const auto wanted = std::min<size_t>(sizeof(scratch), entry.bytes - consumed);
        if (stopped() || input.read(&input, scratch, wanted) != wanted || !hash.update(scratch, wanted) ||
            output.write(&output, scratch, wanted) != wanted) return false;
        consumed += wanted;
    }
    if (!input.close(&input) || !output.sync(&output) || !output.close(&output) || !hash.finish(digest) ||
        std::memcmp(digest, entry.hash.data(), 32)) return false;
    ts::FileIntegrityCheck<RouteSourceIo, RouteSourceHash> check(output, hash);
    if (check.begin(temporary.c_str(), entry.bytes, digest) != ts::FileIntegrityResult::Pending) return false;
    while (check.active())
    {
        if (stopped()) return false;
        check.step(scratch, sizeof(scratch));
    }
    if (stopped()) return false;
    const auto ordinal = image.filename().string().substr(4, 4);
    std::filesystem::path preview, view;
    const auto asset_root = image.parent_path().parent_path();
    if (!resolve_child_under_root(asset_root, "thumbs/thumb-" + ordinal + ".bmp", preview) ||
        !resolve_child_under_root(asset_root, "views/view-" + ordinal + ".bmp", view)) return false;
    const ts::ImageBindingPaths paths{temporary.c_str(), image.c_str(), temporary_binding.c_str(), binding.c_str(), preview.c_str(), view.c_str()};
    ts::ImageSourceBindingStore<RouteSourceIo> store(output);
    return store.commit(paths, {{entry.reference.data(), 32}, {digest, 32}, entry.bytes}, check, scratch, sizeof(scratch)) == ts::ImageBindingResult::Ready;
}
} // namespace
#endif

uint64_t materialize_route_package_images(const std::string& asset_id, const std::string& route,
                                          const std::vector<platform::ui::route_storage::RouteImageCacheItem>& items,
                                          const std::atomic_bool* cancelled)
try
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!track_sharing::isInstalledPackageRoute(route) || route.substr(8, 66) != asset_id || items.empty() || items.size() > 64) return 0;
    auto proof = std::make_unique<PackageProof>(cancelled);
    if (!proof->verify(route)) return 0;
    uint64_t result = 0;
    const auto root = resolve_paths().sd_root / "routes";
    for (size_t index = 0; index < items.size() && !(cancelled && cancelled->load()); ++index)
    {
        const auto& item = items[index];
        const auto* entry = item.source_hash_valid ? proof->find(item.source_hash) : nullptr;
        if (!entry) continue;
        const std::filesystem::path input(item.source_path);
        const auto relative = input.is_absolute() ? input.lexically_relative(root) : input;
        std::filesystem::path image;
        if (relative.parent_path() != std::filesystem::path(".trailmate") / asset_id / "images" ||
            !resolve_child_under_root(root, relative.generic_string(), image)) continue;
        const auto name = relative.filename().string();
        if (name.size() != 12 || name.substr(0, 4) != "img-" || name.substr(8) != ".jpg" ||
            !std::all_of(name.begin() + 4, name.begin() + 8, [](char c)
                         { return c >= '0' && c <= '9'; }) ||
            name.substr(4, 4) < "0001" || name.substr(4, 4) > "0064") continue;
        std::error_code error;
        std::filesystem::create_directories(image.parent_path(), error);
        if (!error && publish_image(*proof, *entry, image, cancelled)) result |= uint64_t(1) << index;
    }
    return result;
#else
    return 0;
#endif
}
catch (...)
{
    // Allocation/filesystem failures on the owned worker must remain a failed
    // batch, rather than terminate the application or admit partial mappings.
    return 0;
}
} // namespace platform::linux_runtime
