#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/route_image_tasks.h"
#include "platform/linux/route_package_images.h"
#include "platform/ui/route_storage.h"
#include "track_sharing/installed_package_binding.h"
#include "track_sharing/installed_package_path.h"
#include "track_sharing/manifest_hash.h"
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace ts = track_sharing;
namespace routes = platform::ui::route_storage;
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
static Bytes read(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}
static void write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
static std::array<uint8_t, 32> hash(const Bytes& bytes)
{
    std::array<uint8_t, 32> result{};
    unsigned size = 0;
    assert(EVP_Digest(bytes.data(), bytes.size(), result.data(), &size, EVP_sha256(), nullptr) == 1 && size == 32);
    return result;
}
static std::string hex(const uint8_t* bytes)
{
    const char* alphabet = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < 32; ++i)
    {
        result += alphabet[bytes[i] >> 4];
        result += alphabet[bytes[i] & 15];
    }
    return result;
}
template <class Encode>
static Bytes encode(Encode encode)
{
    Bytes bytes(8192);
    Writer writer(bytes.data(), bytes.size());
    encode(writer);
    assert(writer.good());
    bytes.resize(writer.size());
    return bytes;
}
struct StreamHash
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    ~StreamHash() { EVP_MD_CTX_free(context); }
    bool start() { return EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1; }
    bool update(const uint8_t* bytes, size_t size) { return EVP_DigestUpdate(context, bytes, size) == 1; }
    bool finish(uint8_t* output)
    {
        unsigned size = 0;
        return EVP_DigestFinal_ex(context, output, &size) == 1 && size == 32;
    }
    void reset() { EVP_MD_CTX_reset(context); }
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto root = fs::temp_directory_path() / ("trailmate-package-images-" + std::to_string(getpid()));
    fs::create_directories(root);
    setenv("TRAIL_MATE_SD_ROOT", root.c_str(), 1);
    const Bytes image = read(argv[1]);
    const std::string text = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></Document></kml>";
    const Bytes kml(text.begin(), text.end());
    const std::string reference = "photos/example.jpg";
    const auto kml_hash = hash(kml), image_hash = hash(image);
    const auto index = encode([&](Writer& w)
                              {
        w.array(4); w.unsignedInteger(1); w.binary({kml_hash.data(), 32}); w.unsignedInteger(kml.size()); w.array(1);
        w.array(7); w.text("photo"); w.binary({image_hash.data(), 32}); w.unsignedInteger(image.size());
        w.text("image/jpeg"); w.text(reference); w.unsignedInteger(0); w.unsignedInteger(1); });
    const auto index_hash = hash(index);
    std::array<uint8_t, 32> seed{};
    seed[0] = 41;
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
    assert(key);
    uint8_t author[64]{1}, nonce[16]{2};
    size_t key_size = 32;
    assert(EVP_PKEY_get_raw_public_key(key, author + 32, &key_size) == 1 && key_size == 32);
    auto manifest = encode([&](Writer& w)
                           {
        w.array(16); w.unsignedInteger(2); w.binary({author, 64}); w.binary({nonce, 16}); w.unsignedInteger(0);
        w.unsignedInteger(1); w.nil(); w.unsignedInteger(0); w.unsignedInteger(1); w.unsignedInteger(1);
        w.text("Package image fixture"); w.text("");
        w.array(4); w.signedInteger(10000000); w.signedInteger(20000000); w.signedInteger(10000000); w.signedInteger(20000000);
        w.array(4); w.unsignedInteger(0); w.nil(); w.nil(); w.unsignedInteger(1);
        w.array(4); w.binary({kml_hash.data(), 32}); w.unsignedInteger(kml.size()); w.text("application/vnd.google-earth.kml+xml"); w.unsignedInteger(1);
        w.array(8); w.unsignedInteger(0); w.array(0); w.unsignedInteger(0); w.unsignedInteger(1); w.unsignedInteger(0); w.unsignedInteger(0); w.unsignedInteger(0); w.nil();
        w.array(4); w.binary({index_hash.data(), 32}); w.unsignedInteger(index.size()); w.unsignedInteger(1); w.unsignedInteger(1); });
    constexpr char domain[] = "TrailMate.Track.Manifest.v2";
    Bytes message(reinterpret_cast<const uint8_t*>(domain), reinterpret_cast<const uint8_t*>(domain) + sizeof(domain));
    message.insert(message.end(), manifest.begin(), manifest.end());
    uint8_t signature[64]{};
    size_t signature_size = 64;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    assert(context && EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1);
    assert(EVP_DigestSign(context, signature, &signature_size, message.data(), message.size()) == 1 && signature_size == 64);
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    const auto signed_manifest = encode([&](Writer& w)
                                        { w.array(2); w.binary({manifest.data(), manifest.size()}); w.binary({signature, 64}); });
    ts::ManifestView view;
    StreamHash stream_hash;
    ts::InstalledPackageBinding binding;
    assert(ts::deriveManifestHashes({manifest.data(), manifest.size()}, view, stream_hash, binding.object, binding.revision));
    binding.count = 1;
    binding.availability = 1;
    binding.kml_bytes = kml.size();
    binding.index_bytes = index.size();
    std::copy(kml_hash.begin(), kml_hash.end(), binding.kml_hash);
    std::copy(index_hash.begin(), index_hash.end(), binding.index_hash);
    Bytes marker(ts::kInstalledPackageBindingBytes);
    assert(ts::encodeInstalledPackageBinding(binding, marker.data(), marker.size()));
    const auto asset = "p-" + hex(binding.revision), route = "/routes/" + asset + ".kml";
    const auto package = root / "trailmate/packages" / hex(binding.revision);
    const auto resource = package / (hex(image_hash.data()) + ".ast");
    write(root / route.substr(1), kml);
    write(root / "routes" / (asset + ".rpk"), marker);
    write(package / "manifest.cmp", signed_manifest);
    write(package / "index.cmp", index);
    write(resource, image);
    std::string directory, error;
    assert(routes::ensure_route_asset_dir(asset, directory));
    routes::RouteImageCacheItem item;
    item.source_path = (fs::path(directory) / "images/img-0001.jpg").string();
    item.source_hash_valid = routes::route_image_reference_hash(reference, item.source_hash);
    const auto materialize = [&]
    { return platform::linux_runtime::materialize_route_package_images(asset, route, {item}, nullptr); };
    assert(materialize() == 1);
    assert(read(item.source_path) == image && routes::route_image_source_matches(item.source_path, item.source_hash));
    assert(ts::isInstalledPackageRouteAtRoot((root / route.substr(1)).string(), (root / "routes").string()));
    assert(ts::isInstalledPackageRouteAtRoot(route, "/routes/"));
    assert(!ts::isInstalledPackageRouteAtRoot("/other" + route, "/routes"));
    assert(!ts::isInstalledPackageRouteAtRoot(route, "/route"));
    assert(routes::start_route_image_cache_build(asset, {item}, error, routes::RouteImageTaskPresentation::PageOnly, (root / route.substr(1)).string()));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (routes::route_image_download_status().busy && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(routes::route_image_download_status().phase == routes::RouteImageDownloadPhase::Done);
    assert(fs::file_size(fs::path(directory) / "views/view-0001.bmp") > 54);
    // Every altered proof fails without publishing different source bytes.
    const auto reject_corrupt = [&](const fs::path& path, const Bytes& original)
    {
        auto corrupt = original;
        corrupt.back() ^= 1;
        write(path, corrupt);
        assert(materialize() == 0 && read(item.source_path) == image);
        write(path, original);
    };
    reject_corrupt(package / "manifest.cmp", signed_manifest);
    reject_corrupt(package / "index.cmp", index);
    reject_corrupt(resource, image);
    reject_corrupt(root / route.substr(1), kml);
    reject_corrupt(root / "routes" / (asset + ".rpk"), marker);
    // A valid CRC does not turn a locator into author or availability proof.
    const auto reject_marker = [&]
    {
        Bytes changed(ts::kInstalledPackageBindingBytes);
        assert(ts::encodeInstalledPackageBinding(binding, changed.data(), changed.size()));
        write(root / "routes" / (asset + ".rpk"), changed);
        assert(materialize() == 0 && read(item.source_path) == image);
        write(root / "routes" / (asset + ".rpk"), marker);
    };
    binding.availability = 0;
    reject_marker();
    binding.availability = 1;
    binding.object[0] ^= 1;
    reject_marker();
    binding.object[0] ^= 1;
    binding.index_hash[0] ^= 1;
    reject_marker();
    binding.index_hash[0] ^= 1;
    item.source_hash[0] ^= 1;
    assert(materialize() == 0);
    item.source_hash[0] ^= 1;
    std::atomic_bool cancelled{true};
    assert(platform::linux_runtime::materialize_route_package_images(asset, route, {item}, &cancelled) == 0);
    assert(platform::linux_runtime::materialize_route_package_images("other", route, {item}, nullptr) == 0);
    const auto original_path = item.source_path;
    item.source_path = (root / "escape.jpg").string();
    assert(materialize() == 0 && !fs::exists(item.source_path));
    item.source_path = original_path;
    fs::remove(resource);
    fs::create_symlink(root / route.substr(1), resource);
    assert(materialize() == 0);
    fs::remove(resource);
    write(resource, image);
    assert(materialize() == 1);
    platform::linux_runtime::stop_route_image_tasks();
    fs::remove_all(root);
}
