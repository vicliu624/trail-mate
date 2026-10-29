#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "platform/ui/reticulum_receive_runtime.h"

#include "chat/infra/mesh_peer_directory_core.h"
#include "platform/linux/peer_directory.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/storage_monitor.h"
#include "platform/ui/device_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <string_view>
#include <vector>
#if defined(__linux__)
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace platform::ui::reticulum_directory
{
namespace
{

constexpr const char* kRelativeDir = "trailmate/reticulum";
constexpr const char* kAnnouncesRelativePath = "trailmate/reticulum/announces.tsv";
constexpr const char* kAnnouncesRelativeTempPath = "trailmate/reticulum/announces.tmp";
constexpr const char* kMeshPeerDirectoryRelativePath = "mesh/peers.bin";
constexpr const char* kAnnouncesLogicalPath = "/trailmate/reticulum/announces.tsv";
constexpr const char* kMeshPeerDirectoryLogicalPath = "/mesh/peers.bin";
constexpr std::size_t kMaxDirectoryEntries = 1024;
constexpr std::size_t kMaxLineBytes = 4096;
constexpr std::size_t kMaxTsvFields = 14;

struct TsvFields
{
    std::string_view values[kMaxTsvFields];
    std::size_t count = 0;
};

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void copy_view(char* out, std::size_t out_len, std::string_view text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const std::size_t len = text.size() < out_len - 1U ? text.size() : out_len - 1U;
    if (len != 0)
    {
        std::memcpy(out, text.data(), len);
    }
    out[len] = '\0';
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

std::filesystem::path sd_root()
{
    return ::platform::linux_runtime::resolve_paths().sd_root;
}

std::filesystem::path path_under_sd(const char* relative)
{
    return sd_root() / (relative ? relative : "");
}

bool ensure_directory()
{
    return ::platform::linux_runtime::ensure_directory(sd_root() / kRelativeDir);
}

class LinuxMeshPeerDirectoryBlobStore final
    : public chat::IMeshPeerDirectoryBlobStore
{
  public:
    LinuxMeshPeerDirectoryBlobStore() : path_(path_under_sd(kMeshPeerDirectoryRelativePath)) {}
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>& out) override
    {
        out.clear();
        const auto& path = path_;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Missing;
        }
        if (ec)
        {
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
        {
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < 0)
        {
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }
        in.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(size));
        if (!out.empty())
        {
            in.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            if (!in)
            {
                out.clear();
                return chat::MeshPeerDirectoryBlobLoadResult::IoError;
            }
        }
        return chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }

    bool saveBlob(const uint8_t* data, std::size_t len) override
    {
        if (!data && len != 0)
        {
            return false;
        }

        const auto& path = path_;
        if (!::platform::linux_runtime::ensure_directory(path.parent_path()))
        {
            return false;
        }

#if defined(__linux__)
        struct Descriptor
        {
            int value = -1;
            ~Descriptor()
            {
                if (value >= 0) ::close(value);
            }
        } directory, file;
        static std::atomic<uint64_t> sequence{0};
        const auto parent = path.parent_path().string();
        const auto filename = path.filename().string();
        char temporary[96];
        const auto count = std::snprintf(temporary, sizeof(temporary), ".%s.tmp.%lu.%llu", filename.c_str(),
                                         static_cast<unsigned long>(::getpid()), static_cast<unsigned long long>(++sequence));
        if (count <= 0 || static_cast<size_t>(count) >= sizeof(temporary)) return false;
        directory.value = ::open(parent.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (directory.value < 0) return false;
        file.value = ::openat(directory.value, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (file.value < 0) return false;
        bool saved = true;
        size_t offset = 0;
        while (offset < len)
        {
            const auto written = ::write(file.value, data + offset, len - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0)
            {
                saved = false;
                break;
            }
            offset += static_cast<size_t>(written);
        }
        saved = saved && ::fsync(file.value) == 0;
        const auto closed = ::close(file.value);
        file.value = -1;
        saved = saved && closed == 0;
        if (saved) saved = ::renameat(directory.value, temporary, directory.value, filename.c_str()) == 0;
        if (saved) saved = ::fsync(directory.value) == 0;
        if (!saved) ::unlinkat(directory.value, temporary, 0);
        return saved;
#else

        const std::filesystem::path temp_path = path.string() + ".tmp";
        {
            std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
            {
                return false;
            }
            if (len != 0)
            {
                out.write(reinterpret_cast<const char*>(data),
                          static_cast<std::streamsize>(len));
            }
            if (!out.good())
            {
                out.close();
                std::error_code remove_ec;
                std::filesystem::remove(temp_path, remove_ec);
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp_path, path, ec);
        if (ec)
        {
            std::filesystem::remove(temp_path, ec);
            return false;
        }
        return true;
#endif
    }

    void clearBlob() override
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

  private:
    std::filesystem::path path_; // Pinned before the storage thread starts.
};

LinuxMeshPeerDirectoryBlobStore& local_mesh_peer_directory_blob_store()
{
    static LinuxMeshPeerDirectoryBlobStore store;
    return store;
}

::platform::linux_runtime::LinuxPeerDirectory& local_mesh_peer_directory()
{
    static ::platform::linux_runtime::LinuxPeerDirectory directory(
        local_mesh_peer_directory_blob_store());
    return directory;
}

chat::IMeshPeerDirectory* s_bound_mesh_peer_directory = nullptr;
bool s_local_mesh_peer_directory_begun = false;

bool zero_hash(const uint8_t* hash, std::size_t len)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

chat::MeshPeerSource mesh_source_from_entry_source(EntrySource source)
{
    switch (source)
    {
    case EntrySource::RuntimeRx:
        return chat::MeshPeerSource::RuntimeRx;
    case EntrySource::PathResponse:
        return chat::MeshPeerSource::DiscoveryResponse;
    case EntrySource::Manual:
        return chat::MeshPeerSource::Manual;
    case EntrySource::Import:
        return chat::MeshPeerSource::Import;
    case EntrySource::Unknown:
    default:
        return chat::MeshPeerSource::Unknown;
    }
}

EntrySource entry_source_from_mesh_source(chat::MeshPeerSource source)
{
    switch (source)
    {
    case chat::MeshPeerSource::RuntimeRx:
        return EntrySource::RuntimeRx;
    case chat::MeshPeerSource::DiscoveryResponse:
        return EntrySource::PathResponse;
    case chat::MeshPeerSource::Manual:
        return EntrySource::Manual;
    case chat::MeshPeerSource::Import:
        return EntrySource::Import;
    case chat::MeshPeerSource::Unknown:
    default:
        return EntrySource::Unknown;
    }
}

void init_lxmf_directory_status(Status& out)
{
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    out.file_present = true;
}

void set_mesh_peer_directory_failure(Status& out,
                                     chat::MeshPeerDirectoryStatusCode code,
                                     const char* not_found_message,
                                     const char* invalid_message)
{
    switch (code)
    {
    case chat::MeshPeerDirectoryStatusCode::NotFound:
        set_status(out, not_found_message, kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::InvalidArgument:
        set_status(out, invalid_message, kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::StorageUnavailable:
        set_status(out,
                   "Mesh peer directory storage unavailable",
                   kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Busy:
        set_status(out,
                   "Mesh peer directory busy",
                   kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::DeviceUnavailable:
        set_status(out,
                   "Mesh peer directory device unavailable",
                   kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::IoError:
        set_status(out,
                   "Cannot access mesh peer directory",
                   kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::CapacityExceeded:
        set_status(out, "Mesh peer directory full", kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Unsupported:
        set_status(out,
                   "Mesh peer directory unsupported",
                   kMeshPeerDirectoryLogicalPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Ok:
    default:
        set_status(out, "Mesh peer directory failed", kMeshPeerDirectoryLogicalPath);
        break;
    }
}

chat::IMeshPeerDirectory* require_mesh_peer_directory(Status& out)
{
    init_lxmf_directory_status(out);
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kMeshPeerDirectoryLogicalPath);
        return nullptr;
    }
    if (s_bound_mesh_peer_directory)
    {
        return s_bound_mesh_peer_directory;
    }

    auto& directory = local_mesh_peer_directory();
    if (!s_local_mesh_peer_directory_begun)
    {
        const chat::MeshPeerDirectoryStatus status = directory.begin();
        if (!status.succeeded())
        {
            set_mesh_peer_directory_failure(out,
                                            status.code,
                                            "No LXMF addresses",
                                            "LXMF address storage unavailable");
            return nullptr;
        }
        s_local_mesh_peer_directory_begun = true;
    }
    return &directory;
}

bool lxmf_address_to_mesh_peer_record(const LxmfAddressRecord& record,
                                      chat::MeshPeerRecord& out_record)
{
    out_record = chat::MeshPeerRecord{};
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize) ||
        zero_hash(record.enc_pub, kReticulumPublicKeySize) ||
        zero_hash(record.sig_pub, kReticulumPublicKeySize))
    {
        return false;
    }

    const chat::ReticulumPeerIdentity identity =
        chat::makeReticulumPeerIdentity(record.destination_hash,
                                        record.identity_hash);
    out_record.valid = true;
    out_record.identity = chat::makeMeshPeerReticulumIdentity(identity);
    out_record.source = mesh_source_from_entry_source(record.source);
    out_record.first_seen_s =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    out_record.last_seen_s =
        record.last_seen_s != 0 ? record.last_seen_s : out_record.first_seen_s;
    chat::copyMeshPeerText(out_record.display_name,
                           sizeof(out_record.display_name),
                           record.display_name);
    out_record.flags.favorite = record.favorite;
    out_record.flags.ignored = record.ignored;
    out_record.flags.trusted = record.trusted;
    out_record.reticulum.identity = identity;
    out_record.reticulum.has_public_keys = true;
    std::memcpy(out_record.reticulum.enc_pub,
                record.enc_pub,
                sizeof(out_record.reticulum.enc_pub));
    std::memcpy(out_record.reticulum.sig_pub,
                record.sig_pub,
                sizeof(out_record.reticulum.sig_pub));
    out_record.reticulum.delivery = true;
    return chat::meshPeerRecordIsValid(out_record);
}

bool mesh_peer_record_to_lxmf_address(const chat::MeshPeerRecord& record,
                                      LxmfAddressRecord& out_record)
{
    out_record = LxmfAddressRecord{};
    if (!chat::meshPeerRecordIsValid(record) ||
        !chat::meshPeerIsReticulumProtocol(record.identity.protocol))
    {
        return false;
    }

    chat::ReticulumPeerIdentity identity = record.reticulum.identity.valid
                                               ? record.reticulum.identity
                                               : record.identity.reticulum;
    if (!identity.valid ||
        zero_hash(identity.destination_hash, kReticulumHashSize))
    {
        return false;
    }

    out_record.valid = true;
    std::memcpy(out_record.destination_hash,
                identity.destination_hash,
                sizeof(out_record.destination_hash));
    std::memcpy(out_record.identity_hash,
                identity.identity_hash,
                sizeof(out_record.identity_hash));
    if (record.reticulum.has_public_keys)
    {
        std::memcpy(out_record.enc_pub,
                    record.reticulum.enc_pub,
                    sizeof(out_record.enc_pub));
        std::memcpy(out_record.sig_pub,
                    record.reticulum.sig_pub,
                    sizeof(out_record.sig_pub));
    }
    copy_text(out_record.display_name,
              sizeof(out_record.display_name),
              record.display_name);
    out_record.favorite = record.flags.favorite;
    out_record.ignored = record.flags.ignored;
    out_record.trusted = record.flags.trusted;
    out_record.source = entry_source_from_mesh_source(record.source);
    out_record.first_seen_s = record.first_seen_s;
    out_record.last_seen_s = record.last_seen_s;
    return true;
}

void append_hex(std::string& out, const uint8_t* data, std::size_t len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!data || len == 0)
    {
        return;
    }
    out.reserve(out.size() + (len * 2U));
    for (std::size_t index = 0; index < len; ++index)
    {
        out.push_back(kHex[(data[index] >> 4U) & 0x0FU]);
        out.push_back(kHex[data[index] & 0x0FU]);
    }
}

std::string hex_text(const uint8_t* data, std::size_t len)
{
    std::string out;
    append_hex(out, data, len);
    return out;
}

uint8_t hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    return 0xFF;
}

bool parse_hex(std::string_view text, uint8_t* out, std::size_t out_len)
{
    if (!out || text.size() != out_len * 2U)
    {
        return false;
    }
    for (std::size_t index = 0; index < out_len; ++index)
    {
        const uint8_t hi = hex_nibble(text[index * 2U]);
        const uint8_t lo = hex_nibble(text[(index * 2U) + 1U]);
        if (hi == 0xFF || lo == 0xFF)
        {
            return false;
        }
        out[index] = static_cast<uint8_t>((hi << 4U) | lo);
    }
    return true;
}

bool copy_text_app_data_display_name_from_hex(std::string_view text,
                                              char* out,
                                              std::size_t out_len)
{
    if (!out || out_len == 0 || text.empty() || (text.size() % 2U) != 0)
    {
        return false;
    }
    const std::size_t byte_count = text.size() / 2U;
    if (byte_count > 96)
    {
        return false;
    }

    std::size_t used = 0;
    bool has_visible = false;
    for (std::size_t index = 0; index < byte_count; ++index)
    {
        const uint8_t hi = hex_nibble(text[index * 2U]);
        const uint8_t lo = hex_nibble(text[(index * 2U) + 1U]);
        if (hi == 0xFF || lo == 0xFF)
        {
            out[0] = '\0';
            return false;
        }
        uint8_t byte = static_cast<uint8_t>((hi << 4U) | lo);
        if (byte == '\t' || byte == '\r' || byte == '\n')
        {
            byte = ' ';
        }
        else if (byte == 0 || byte < 0x20 || byte == 0x7F)
        {
            out[0] = '\0';
            return false;
        }
        if (used + 1U < out_len)
        {
            out[used++] = static_cast<char>(byte);
        }
        if (byte != ' ')
        {
            has_visible = true;
        }
    }
    while (used != 0 && out[used - 1U] == ' ')
    {
        --used;
    }
    out[used] = '\0';
    return has_visible && used != 0;
}

std::string sanitize_field(const char* text)
{
    std::string out = text ? text : "";
    for (char& ch : out)
    {
        if (ch == '\t' || ch == '\r' || ch == '\n')
        {
            ch = ' ';
        }
    }
    return out;
}

std::string_view trim_view(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ' || line.back() == '\t'))
    {
        line.remove_suffix(1);
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    line.remove_prefix(start);
    return line;
}

bool read_line(std::istream& stream, std::string& out)
{
    out.clear();
    if (!std::getline(stream, out))
    {
        return false;
    }
    if (out.size() > kMaxLineBytes)
    {
        out.clear();
    }
    return true;
}

TsvFields split_tsv(std::string_view line)
{
    TsvFields out{};
    std::size_t start = 0;
    while (start <= line.size() && out.count < kMaxTsvFields)
    {
        const std::size_t end = line.find('\t', start);
        out.values[out.count++] =
            line.substr(start,
                        end == std::string::npos ? std::string::npos : end - start);
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1U;
    }
    return out;
}

bool data_line(std::string_view line)
{
    return !line.empty() && line[0] != '#' && line.rfind("version\t", 0) != 0;
}

bool first_field_matches(std::string_view line, std::string_view key)
{
    const std::size_t tab = line.find('\t');
    const std::string_view first =
        tab == std::string::npos ? line : line.substr(0, tab);
    return first == key;
}

uint32_t parse_u32(std::string_view text, uint32_t fallback = 0)
{
    if (text.empty())
    {
        return fallback;
    }
    uint32_t value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            return fallback;
        }
        value = (value * 10U) + static_cast<uint32_t>(ch - '0');
    }
    return value;
}

bool truthy(std::string_view text)
{
    return text == "1" || text == "true" || text == "yes" || text == "enabled";
}

std::string bool_text(bool value)
{
    return value ? "1" : "0";
}

std::string source_text(EntrySource source)
{
    switch (source)
    {
    case EntrySource::RuntimeRx:
        return "runtime_rx";
    case EntrySource::PathResponse:
        return "path_response";
    case EntrySource::Manual:
        return "manual";
    case EntrySource::Import:
        return "import";
    case EntrySource::Unknown:
    default:
        return "unknown";
    }
}

EntrySource parse_source(std::string_view source)
{
    if (source == "runtime_rx")
    {
        return EntrySource::RuntimeRx;
    }
    if (source == "path_response")
    {
        return EntrySource::PathResponse;
    }
    if (source == "manual")
    {
        return EntrySource::Manual;
    }
    if (source == "import")
    {
        return EntrySource::Import;
    }
    return EntrySource::Unknown;
}

std::string aspect_text(AnnounceAspect aspect)
{
    switch (aspect)
    {
    case AnnounceAspect::LxmfDelivery:
        return "lxmf.delivery";
    case AnnounceAspect::LxmfPropagation:
        return "lxmf.propagation";
    case AnnounceAspect::CallAudio:
        return "call.audio";
    case AnnounceAspect::NomadNetworkNode:
        return "nomadnetwork.node";
    case AnnounceAspect::Unknown:
    default:
        return "unknown";
    }
}

AnnounceAspect parse_aspect(std::string_view aspect)
{
    if (aspect == "lxmf.delivery")
    {
        return AnnounceAspect::LxmfDelivery;
    }
    if (aspect == "lxmf.propagation")
    {
        return AnnounceAspect::LxmfPropagation;
    }
    if (aspect == "call.audio")
    {
        return AnnounceAspect::CallAudio;
    }
    if (aspect == "nomadnetwork.node")
    {
        return AnnounceAspect::NomadNetworkNode;
    }
    return AnnounceAspect::Unknown;
}

template <typename Record>
void append_latest_record(Record* records,
                          std::size_t max_records,
                          std::size_t& count,
                          const Record& record)
{
    if (!records || max_records == 0)
    {
        return;
    }
    if (count < max_records)
    {
        records[count++] = record;
        return;
    }
    for (std::size_t index = 1; index < max_records; ++index)
    {
        records[index - 1U] = records[index];
    }
    records[max_records - 1U] = record;
}

template <typename Record>
void reverse_records(Record* records, std::size_t count)
{
    if (!records || count < 2)
    {
        return;
    }
    for (std::size_t left = 0, right = count - 1U; left < right; ++left, --right)
    {
        Record temp = records[left];
        records[left] = records[right];
        records[right] = temp;
    }
}

uint32_t find_existing_first_seen(const char* relative_path,
                                  const std::string& destination,
                                  std::size_t first_seen_field,
                                  uint32_t fallback)
{
    std::ifstream in(path_under_sd(relative_path), std::ios::binary);
    if (!in.is_open())
    {
        return fallback;
    }
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            const TsvFields fields = split_tsv(view);
            return fields.count > first_seen_field
                       ? parse_u32(fields.values[first_seen_field], fallback)
                       : fallback;
        }
    }
    return fallback;
}

bool parse_announce_line(std::string_view line, AnnounceRecord& out)
{
    const TsvFields fields = split_tsv(line);
    if (fields.count < 12)
    {
        return false;
    }
    AnnounceRecord parsed{};
    if (!parse_hex(fields.values[0], parsed.destination_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[1], parsed.identity_hash, kReticulumHashSize))
    {
        return false;
    }
    parsed.aspect = parse_aspect(fields.values[2]);
    parsed.source = parse_source(fields.values[3]);
    parsed.first_seen_s = parse_u32(fields.values[4]);
    parsed.last_seen_s = parse_u32(fields.values[5]);
    parsed.hops = static_cast<uint8_t>(parse_u32(fields.values[6], 0xFF) & 0xFFU);
    parsed.path_response = truthy(fields.values[7]);
    parsed.local_destination = truthy(fields.values[8]);
    parsed.delivery = truthy(fields.values[9]);
    parsed.propagation = truthy(fields.values[10]);
    copy_view(parsed.display_name, sizeof(parsed.display_name), fields.values[11]);
    if (parsed.display_name[0] == '\0' && fields.count >= 14)
    {
        (void)copy_text_app_data_display_name_from_hex(fields.values[13],
                                                       parsed.display_name,
                                                       sizeof(parsed.display_name));
    }
    parsed.valid = true;
    out = parsed;
    return true;
}

bool write_line(std::ofstream& out, std::string_view line)
{
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    out.put('\n');
    return out.good();
}

Status stream_upsert_line(const char* relative_path,
                          const char* temp_relative_path,
                          const char* logical_path,
                          const char* title,
                          const char* success_message,
                          const std::string& destination,
                          const std::string& new_line)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::linux_runtime::probe_storage_root(
                         ::platform::linux_runtime::resolve_paths().sd_root)
                         .available;
    if (!out.sd_present)
    {
        set_status(out, "SD card required", logical_path);
        return out;
    }
    if (!ensure_directory())
    {
        set_status(out, "Cannot create Reticulum directory", "/trailmate/reticulum");
        return out;
    }

    const auto path = path_under_sd(relative_path);
    const auto temp_path = path_under_sd(temp_relative_path);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    std::filesystem::remove(temp_path, ec);

    std::ofstream out_file(temp_path, std::ios::binary | std::ios::trunc);
    if (!out_file.is_open())
    {
        set_status(out, "Cannot open Reticulum directory temp", logical_path);
        return out;
    }

    out_file << "# Trail Mate Reticulum " << title << "\nversion\t1\n";
    bool ok = out_file.good();
    std::size_t existing_data_lines = 0;
    if (ok && out.file_present)
    {
        std::ifstream count_file(path, std::ios::binary);
        if (!count_file.is_open())
        {
            out_file.close();
            std::filesystem::remove(temp_path, ec);
            set_status(out, "Cannot read Reticulum directory", logical_path);
            return out;
        }
        std::string old_line;
        while (read_line(count_file, old_line))
        {
            const std::string_view view = trim_view(old_line);
            if (data_line(view) && !first_field_matches(view, destination))
            {
                ++existing_data_lines;
            }
        }
    }

    const std::size_t skip_oldest =
        existing_data_lines + 1U > kMaxDirectoryEntries
            ? (existing_data_lines + 1U - kMaxDirectoryEntries)
            : 0;
    std::size_t kept = 0;
    std::size_t skipped = 0;
    if (ok && out.file_present)
    {
        std::ifstream in_file(path, std::ios::binary);
        if (!in_file.is_open())
        {
            out_file.close();
            std::filesystem::remove(temp_path, ec);
            set_status(out, "Cannot read Reticulum directory", logical_path);
            return out;
        }
        std::string old_line;
        while (read_line(in_file, old_line))
        {
            const std::string_view view = trim_view(old_line);
            if (data_line(view) && !first_field_matches(view, destination))
            {
                if (skipped < skip_oldest)
                {
                    ++skipped;
                    continue;
                }
                ok = write_line(out_file, view);
                if (!ok)
                {
                    break;
                }
                ++kept;
            }
        }
    }

    ok = ok && write_line(out_file, new_line);
    out_file.close();
    if (!ok)
    {
        std::filesystem::remove(temp_path, ec);
        set_status(out, "Cannot write Reticulum directory", logical_path);
        return out;
    }

    std::filesystem::remove(path, ec);
    std::filesystem::rename(temp_path, path, ec);
    if (ec)
    {
        std::filesystem::remove(temp_path, ec);
        set_status(out, "Cannot replace Reticulum directory", logical_path);
        return out;
    }

    out.file_present = true;
    out.saved = true;
    set_status(out, success_message, logical_path);
    return out;
}

std::string announce_line(const AnnounceRecord& record, uint32_t first_seen_s)
{
    std::string line;
    line.reserve(256U + (record.raw_packet_len * 2U) + (record.app_data_len * 2U));
    line += hex_text(record.destination_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.identity_hash, kReticulumHashSize);
    line += "\t";
    line += aspect_text(record.aspect);
    line += "\t";
    line += source_text(record.source);
    line += "\t";
    line += std::to_string(first_seen_s);
    line += "\t";
    line += std::to_string(record.last_seen_s);
    line += "\t";
    line += std::to_string(static_cast<unsigned>(record.hops));
    line += "\t";
    line += bool_text(record.path_response);
    line += "\t";
    line += bool_text(record.local_destination);
    line += "\t";
    line += bool_text(record.delivery);
    line += "\t";
    line += bool_text(record.propagation);
    line += "\t";
    line += sanitize_field(record.display_name);
    line += "\t";
    append_hex(line, record.raw_packet, record.raw_packet_len);
    line += "\t";
    append_hex(line, record.app_data, record.app_data_len);
    return line;
}

} // namespace

const char* announces_path()
{
    return kAnnouncesLogicalPath;
}

const char* lxmf_addresses_path()
{
    return kMeshPeerDirectoryLogicalPath;
}

void bind_mesh_peer_directory(chat::IMeshPeerDirectory* directory)
{
    s_bound_mesh_peer_directory = directory;
}

chat::IMeshPeerDirectory& mesh_peer_directory()
{
    return local_mesh_peer_directory();
}

Status record_announce(const AnnounceRecord& record)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::linux_runtime::probe_storage_root(
                         ::platform::linux_runtime::resolve_paths().sd_root)
                         .available;
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid Reticulum announce", kAnnouncesLogicalPath);
        return out;
    }

    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    const uint32_t fallback_first_seen =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    const uint32_t first_seen_s =
        out.sd_present
            ? find_existing_first_seen(kAnnouncesRelativePath,
                                       destination,
                                       4,
                                       fallback_first_seen)
            : fallback_first_seen;
    return stream_upsert_line(kAnnouncesRelativePath,
                              kAnnouncesRelativeTempPath,
                              kAnnouncesLogicalPath,
                              "announces",
                              "Reticulum announce saved",
                              destination,
                              announce_line(record, first_seen_s));
}

Status record_lxmf_address(const LxmfAddressRecord& record)
{
    Status out{};
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }

    chat::MeshPeerRecord mesh_record{};
    if (!lxmf_address_to_mesh_peer_record(record, mesh_record))
    {
        set_status(out, "Invalid LXMF address", kMeshPeerDirectoryLogicalPath);
        return out;
    }

    chat::MeshPeerUserFlags merged_flags = mesh_record.flags;
    chat::MeshPeerRecord existing{};
    if (directory->find(mesh_record.identity, existing).succeeded())
    {
        merged_flags.favorite = existing.flags.favorite || record.favorite;
        merged_flags.ignored = existing.flags.ignored || record.ignored;
        merged_flags.trusted = existing.flags.trusted || record.trusted;
    }

    const chat::MeshPeerDirectoryStatus status = directory->record(mesh_record);
    if (!status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }

    if (record.favorite || record.ignored || record.trusted)
    {
        const chat::MeshPeerDirectoryStatus flag_status =
            directory->setUserFlags(mesh_record.identity, merged_flags);
        if (!flag_status.succeeded())
        {
            set_mesh_peer_directory_failure(out,
                                            flag_status.code,
                                            "LXMF address not found",
                                            "Invalid LXMF address");
            return out;
        }
    }

    out.saved = true;
    out.loaded = true;
    set_status(out, "LXMF address saved", kMeshPeerDirectoryLogicalPath);
    return out;
}

Status record_lxmf_address_now(const LxmfAddressRecord& record)
{
    return record_lxmf_address(record);
}

Status set_lxmf_address_favorite_now(
    const uint8_t destination_hash[kReticulumHashSize],
    bool favorite)
{
    Status out{};
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!destination_hash || zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kMeshPeerDirectoryLogicalPath);
        return out;
    }

    const chat::ReticulumPeerIdentity reticulum_identity =
        chat::makeReticulumDestinationIdentity(destination_hash);
    const chat::MeshPeerIdentity identity =
        chat::makeMeshPeerReticulumIdentity(reticulum_identity);
    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus find_status =
        directory->find(identity, record);
    if (!find_status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        find_status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }

    chat::MeshPeerUserFlags flags = record.flags;
    flags.favorite = favorite;
    const chat::MeshPeerDirectoryStatus flag_status =
        directory->setUserFlags(record.identity, flags);
    if (!flag_status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        flag_status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }

    out.saved = true;
    out.loaded = true;
    set_status(out, "LXMF address saved", kMeshPeerDirectoryLogicalPath);
    return out;
}

Status load_announces(AnnounceRecord* out_records,
                      std::size_t max_records,
                      std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::linux_runtime::probe_storage_root(
                         ::platform::linux_runtime::resolve_paths().sd_root)
                         .available;
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "Reticulum announce storage unavailable", kAnnouncesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kAnnouncesLogicalPath);
        return out;
    }

    const auto path = path_under_sd(kAnnouncesRelativePath);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No Reticulum announces", kAnnouncesLogicalPath);
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot read Reticulum announces", kAnnouncesLogicalPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view))
        {
            AnnounceRecord parsed{};
            if (parse_announce_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }

    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No Reticulum announces" : "Reticulum announces loaded",
               kAnnouncesLogicalPath);
    return out;
}

Status load_lxmf_addresses(LxmfAddressRecord* out_records,
                           std::size_t max_records,
                           std::size_t* out_count)
{
    Status out{};
    if (out_count)
    {
        *out_count = 0;
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out,
                   "LXMF address storage unavailable",
                   kMeshPeerDirectoryLogicalPath);
        return out;
    }

    chat::MeshPeerRecord* records =
        new (std::nothrow) chat::MeshPeerRecord[max_records];
    if (!records)
    {
        set_status(out,
                   "LXMF address buffer unavailable",
                   kMeshPeerDirectoryLogicalPath);
        return out;
    }

    std::size_t loaded_count = 0;
    const chat::MeshPeerDirectoryStatus status =
        directory->loadRecent(chat::MeshProtocol::Reticulum,
                              records,
                              max_records,
                              &loaded_count);
    if (!status.succeeded())
    {
        delete[] records;
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "No LXMF addresses",
                                        "LXMF address storage unavailable");
        return out;
    }

    std::size_t count = 0;
    for (std::size_t index = 0; index < loaded_count && count < max_records; ++index)
    {
        LxmfAddressRecord converted{};
        if (mesh_peer_record_to_lxmf_address(records[index], converted))
        {
            out_records[count++] = converted;
        }
    }
    delete[] records;
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No LXMF addresses" : "LXMF addresses loaded",
               kMeshPeerDirectoryLogicalPath);
    return out;
}

Status load_lxmf_addresses_matching(const char* query,
                                    LxmfAddressRecord* out_records,
                                    std::size_t max_records,
                                    std::size_t* out_count)
{
    Status out{};
    if (out_count)
    {
        *out_count = 0;
    }
    if (!query || trim_view(query).empty())
    {
        return load_lxmf_addresses(out_records, max_records, out_count);
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out,
                   "LXMF address storage unavailable",
                   kMeshPeerDirectoryLogicalPath);
        return out;
    }

    chat::MeshPeerRecord* records =
        new (std::nothrow) chat::MeshPeerRecord[max_records];
    if (!records)
    {
        set_status(out,
                   "LXMF address buffer unavailable",
                   kMeshPeerDirectoryLogicalPath);
        return out;
    }

    std::size_t loaded_count = 0;
    const chat::MeshPeerDirectoryStatus status =
        directory->search(chat::MeshProtocol::Reticulum,
                          query,
                          records,
                          max_records,
                          &loaded_count);
    if (!status.succeeded())
    {
        delete[] records;
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "No LXMF address matches",
                                        "LXMF address storage unavailable");
        return out;
    }

    std::size_t count = 0;
    for (std::size_t index = 0; index < loaded_count && count < max_records; ++index)
    {
        LxmfAddressRecord converted{};
        if (mesh_peer_record_to_lxmf_address(records[index], converted))
        {
            out_records[count++] = converted;
        }
    }
    delete[] records;
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No LXMF address matches" : "LXMF address matches loaded",
               kMeshPeerDirectoryLogicalPath);
    return out;
}

Status find_lxmf_address_by_destination(
    const uint8_t destination_hash[kReticulumHashSize],
    LxmfAddressRecord* out_record)
{
    Status out{};
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!destination_hash || !out_record ||
        zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kMeshPeerDirectoryLogicalPath);
        return out;
    }

    const chat::ReticulumPeerIdentity reticulum_identity =
        chat::makeReticulumDestinationIdentity(destination_hash);
    const chat::MeshPeerIdentity identity =
        chat::makeMeshPeerReticulumIdentity(reticulum_identity);
    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus status = directory->find(identity, record);
    if (!status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF address");
        return out;
    }
    if (!mesh_peer_record_to_lxmf_address(record, *out_record))
    {
        set_status(out, "Invalid LXMF address", kMeshPeerDirectoryLogicalPath);
        return out;
    }

    out.loaded = true;
    set_status(out, "LXMF address loaded", kMeshPeerDirectoryLogicalPath);
    return out;
}

Status find_lxmf_address_by_node_id(uint32_t node_id,
                                    LxmfAddressRecord* out_record)
{
    Status out{};
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (node_id == 0 || !out_record)
    {
        set_status(out, "Invalid LXMF node", kMeshPeerDirectoryLogicalPath);
        return out;
    }

    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus status =
        directory->findByNodeId(chat::MeshProtocol::Reticulum, node_id, record);
    if (!status.succeeded())
    {
        set_mesh_peer_directory_failure(out,
                                        status.code,
                                        "LXMF address not found",
                                        "Invalid LXMF node");
        return out;
    }
    if (!mesh_peer_record_to_lxmf_address(record, *out_record))
    {
        set_status(out, "Invalid LXMF address", kMeshPeerDirectoryLogicalPath);
        return out;
    }

    out.loaded = true;
    set_status(out, "LXMF address loaded", kMeshPeerDirectoryLogicalPath);
    return out;
}

} // namespace platform::ui::reticulum_directory

namespace platform::ui::reticulum_page
{
namespace
{

constexpr const char* kPagesRelativeDir = "trailmate/reticulum/pages";
constexpr const char* kPagesLogicalDir = "/trailmate/reticulum/pages";
constexpr const char* kDefaultPagePath = "/page/index.mu";

RequestStartHandler s_request_handler = nullptr;
void* s_request_context = nullptr;
RequestCancelHandler s_request_cancel_handler = nullptr;
void* s_request_cancel_context = nullptr;
constexpr std::size_t kPageProgressDepth = 4;

struct PageCacheLoadSnapshot
{
    bool completed = false;
    char destination[kReticulumPageDestinationTextSize] = {};
    char path[kReticulumPagePathSize] = {};
    Status status{};
    std::size_t body_len = 0;
    char body[kReticulumPageBodyMaxBytes + 1U] = {};
};

struct PageRequestProgressSlot
{
    bool occupied = false;
    uint32_t order = 0;
    char destination[kReticulumPageDestinationTextSize] = {};
    char path[kReticulumPagePathSize] = {};
    RequestProgress progress{};
};

PageCacheLoadSnapshot s_page_cache_snapshot{};
PageRequestProgressSlot s_request_progress[kPageProgressDepth]{};
uint32_t s_request_progress_order = 1;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

bool same_page_request(const char* lhs_destination,
                       const char* lhs_path,
                       const char* rhs_destination,
                       const char* rhs_path)
{
    return lhs_destination && lhs_path && rhs_destination && rhs_path &&
           std::strcmp(lhs_destination, rhs_destination) == 0 &&
           std::strcmp(lhs_path, rhs_path) == 0;
}

std::filesystem::path page_path_under_sd(const std::string& relative)
{
    return ::platform::linux_runtime::resolve_paths().sd_root / relative;
}

bool is_hex_char(char ch)
{
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

char uppercase_hex(char ch)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

bool normalize_destination(const char* destination_hash,
                           char* out_hash,
                           std::size_t out_len)
{
    if (!destination_hash || !out_hash ||
        out_len < kReticulumPageDestinationTextSize)
    {
        return false;
    }
    for (std::size_t i = 0; i < kReticulumPageDestinationTextSize - 1U; ++i)
    {
        if (!is_hex_char(destination_hash[i]))
        {
            return false;
        }
        out_hash[i] = uppercase_hex(destination_hash[i]);
    }
    out_hash[kReticulumPageDestinationTextSize - 1U] = '\0';
    return destination_hash[kReticulumPageDestinationTextSize - 1U] == '\0';
}

bool destination_to_bytes(
    const char* destination_hash,
    uint8_t out_hash[kReticulumPageDestinationTextSize / 2U])
{
    if (!destination_hash || !out_hash)
    {
        return false;
    }
    for (std::size_t i = 0; i < kReticulumPageDestinationTextSize / 2U; ++i)
    {
        const int hi = hex_value(destination_hash[i * 2U]);
        const int lo = hex_value(destination_hash[i * 2U + 1U]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        out_hash[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool allowed_path_char(char ch)
{
    const auto value = static_cast<unsigned char>(ch);
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || ch == '/' || ch == '-' ||
           ch == '_' || ch == '.';
}

bool path_has_parent_segment(const char* path)
{
    if (!path)
    {
        return true;
    }
    const char* segment = path;
    while (*segment != '\0')
    {
        while (*segment == '/')
        {
            ++segment;
        }
        const char* end = segment;
        while (*end != '\0' && *end != '/')
        {
            ++end;
        }
        if ((end - segment) == 2 && segment[0] == '.' && segment[1] == '.')
        {
            return true;
        }
        segment = end;
    }
    return false;
}

std::string cache_relative_path(const char* destination_hash, const char* path)
{
    std::string relative = kPagesRelativeDir;
    relative += "/";
    relative += destination_hash;
    relative += path;
    return relative;
}

bool ensure_parent_dir(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec;
}

void set_request_status(Status& out,
                        const RequestStartResult& result,
                        const char* normalized_path)
{
    switch (result.code)
    {
    case RequestStartCode::Started:
        out.supported = true;
        out.request_started = true;
        set_status(out, "Nomad page request started", normalized_path);
        break;
    case RequestStartCode::AlreadyPending:
        out.supported = true;
        out.request_started = true;
        set_status(out, "Nomad page request already pending", normalized_path);
        break;
    case RequestStartCode::InvalidInput:
        out.supported = true;
        set_status(out, "Invalid Nomad page request", normalized_path);
        break;
    case RequestStartCode::Unsupported:
        set_status(out, "Nomad page fetch unavailable", normalized_path);
        break;
    case RequestStartCode::NotReady:
        out.supported = true;
        set_status(out, "Nomad page requester not ready", normalized_path);
        break;
    case RequestStartCode::Busy:
        out.supported = true;
        set_status(out, "Nomad page requester busy", normalized_path);
        break;
    case RequestStartCode::EncodeFailed:
        out.supported = true;
        set_status(out, "Cannot encode Nomad page request", normalized_path);
        break;
    case RequestStartCode::RadioTxFailed:
        out.supported = true;
        set_status(out, "Nomad page request TX failed", normalized_path);
        break;
    case RequestStartCode::StorageUnavailable:
        out.supported = true;
        set_status(out, "SD card required", kPagesLogicalDir);
        break;
    case RequestStartCode::Unknown:
    default:
        out.supported = true;
        set_status(out, "Nomad page request failed", normalized_path);
        break;
    }
}

PageRequestProgressSlot* find_request_progress_slot(
    const char* destination,
    const char* path,
    bool create)
{
    PageRequestProgressSlot* empty = nullptr;
    PageRequestProgressSlot* oldest = nullptr;
    for (auto& slot : s_request_progress)
    {
        if (slot.occupied &&
            same_page_request(slot.destination, slot.path, destination, path))
        {
            return &slot;
        }
        if (!slot.occupied && !empty)
        {
            empty = &slot;
        }
        if (slot.occupied && (!oldest || slot.order < oldest->order))
        {
            oldest = &slot;
        }
    }
    if (!create)
    {
        return nullptr;
    }
    PageRequestProgressSlot* slot = empty ? empty : oldest;
    if (slot)
    {
        *slot = {};
        slot->occupied = true;
        slot->order = s_request_progress_order++;
        copy_text(slot->destination, sizeof(slot->destination), destination);
        copy_text(slot->path, sizeof(slot->path), path);
    }
    return slot;
}

} // namespace

const char* cache_root_path()
{
    return kPagesLogicalDir;
}

void bind_request_start_handler(RequestStartHandler handler, void* context)
{
    s_request_handler = handler;
    s_request_context = context;
}

void bind_request_cancel_handler(RequestCancelHandler handler, void* context)
{
    s_request_cancel_handler = handler;
    s_request_cancel_context = context;
}

bool normalize_path(const char* path, char* out_path, std::size_t out_len)
{
    if (!out_path || out_len == 0)
    {
        return false;
    }
    out_path[0] = '\0';

    const char* source = (path && path[0] != '\0') ? path : kDefaultPagePath;
    if (std::strcmp(source, "/") == 0)
    {
        source = kDefaultPagePath;
    }

    std::size_t written = 0;
    if (source[0] != '/')
    {
        if (written + 1U >= out_len)
        {
            return false;
        }
        out_path[written++] = '/';
    }

    bool previous_slash = false;
    for (std::size_t i = 0; source[i] != '\0'; ++i)
    {
        const char ch = source[i];
        if (!allowed_path_char(ch) || ch == '\\')
        {
            out_path[0] = '\0';
            return false;
        }
        if (ch == '/' && previous_slash)
        {
            continue;
        }
        if (written + 1U >= out_len)
        {
            out_path[0] = '\0';
            return false;
        }
        out_path[written++] = ch;
        previous_slash = ch == '/';
    }
    if (written == 0)
    {
        if (out_len <= std::strlen(kDefaultPagePath))
        {
            return false;
        }
        copy_text(out_path, out_len, kDefaultPagePath);
        return true;
    }
    if (written > 1U && out_path[written - 1U] == '/')
    {
        --written;
    }
    out_path[written] = '\0';
    if (path_has_parent_segment(out_path))
    {
        out_path[0] = '\0';
        return false;
    }
    return true;
}

Status load_cached_page(const char* destination_hash,
                        const char* path,
                        char* out_body,
                        std::size_t body_capacity,
                        std::size_t* out_body_len)
{
    Status out{};
    out.supported = true;
    out.sd_present = true;
    if (out_body_len)
    {
        *out_body_len = 0;
    }
    if (out_body && body_capacity != 0)
    {
        out_body[0] = '\0';
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        !out_body || body_capacity == 0)
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    const auto cache_path = page_path_under_sd(
        cache_relative_path(destination, normalized_path));
    out.file_present = std::filesystem::exists(cache_path);
    out.cache_checked = true;
    if (!out.file_present)
    {
        set_status(out, "Nomad page cache miss", cache_path.string().c_str());
        return out;
    }

    std::ifstream in(cache_path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot open Nomad page cache", cache_path.string().c_str());
        return out;
    }
    in.read(out_body, static_cast<std::streamsize>(body_capacity - 1U));
    const std::streamsize read = in.gcount();
    const std::size_t used = read > 0 ? static_cast<std::size_t>(read) : 0;
    out_body[used] = '\0';
    if (out_body_len)
    {
        *out_body_len = used;
    }
    out.truncated = in.peek() != std::char_traits<char>::eof();
    out.loaded = true;
    out.progress_percent = 100;
    set_status(out, "Nomad page cache loaded", cache_path.string().c_str());
    return out;
}

Status request_cached_page_load(const char* destination_hash,
                                const char* path,
                                bool force)
{
    Status out{};
    out.supported = true;
    out.sd_present = true;

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    if (!force && s_page_cache_snapshot.completed &&
        same_page_request(s_page_cache_snapshot.destination,
                          s_page_cache_snapshot.path,
                          destination,
                          normalized_path))
    {
        return s_page_cache_snapshot.status;
    }

    std::size_t body_len = 0;
    Status status = load_cached_page(destination,
                                     normalized_path,
                                     s_page_cache_snapshot.body,
                                     sizeof(s_page_cache_snapshot.body),
                                     &body_len);
    status.busy = false;
    s_page_cache_snapshot.completed = true;
    copy_text(s_page_cache_snapshot.destination,
              sizeof(s_page_cache_snapshot.destination),
              destination);
    copy_text(s_page_cache_snapshot.path,
              sizeof(s_page_cache_snapshot.path),
              normalized_path);
    s_page_cache_snapshot.status = status;
    s_page_cache_snapshot.body_len = body_len;
    return status;
}

Status poll_cached_page_load(const char* destination_hash,
                             const char* path,
                             char* out_body,
                             std::size_t body_capacity,
                             std::size_t* out_body_len)
{
    Status out{};
    out.supported = true;
    out.sd_present = true;
    if (out_body_len)
    {
        *out_body_len = 0;
    }
    if (out_body && body_capacity != 0)
    {
        out_body[0] = '\0';
    }

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        !out_body || body_capacity == 0)
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    if (!s_page_cache_snapshot.completed ||
        !same_page_request(s_page_cache_snapshot.destination,
                           s_page_cache_snapshot.path,
                           destination,
                           normalized_path))
    {
        set_status(out, "Nomad page cache idle", normalized_path);
        return out;
    }

    out = s_page_cache_snapshot.status;
    const std::size_t copy_len =
        s_page_cache_snapshot.body_len < (body_capacity - 1U)
            ? s_page_cache_snapshot.body_len
            : body_capacity - 1U;
    if (copy_len != 0)
    {
        std::memcpy(out_body, s_page_cache_snapshot.body, copy_len);
    }
    out_body[copy_len] = '\0';
    if (out_body_len)
    {
        *out_body_len = copy_len;
    }
    out.truncated = out.truncated || s_page_cache_snapshot.body_len > copy_len;
    return out;
}

Status store_cached_page_now(const char* destination_hash,
                             const char* path,
                             const char* body,
                             std::size_t body_len)
{
    Status out{};
    out.supported = true;
    out.sd_present = true;

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        (!body && body_len != 0))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    const auto cache_path = page_path_under_sd(
        cache_relative_path(destination, normalized_path));
    if (!ensure_parent_dir(cache_path))
    {
        set_status(out, "Cannot create Nomad page cache directory",
                   cache_path.string().c_str());
        return out;
    }

    std::ofstream file(cache_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        set_status(out, "Cannot write Nomad page cache", cache_path.string().c_str());
        return out;
    }
    if (body_len != 0)
    {
        file.write(body, static_cast<std::streamsize>(body_len));
    }
    out.saved = static_cast<bool>(file);
    out.file_present = out.saved;
    set_status(out,
               out.saved ? "Nomad page cache saved"
                         : "Nomad page cache write failed",
               cache_path.string().c_str());
    return out;
}

Status clear_cached_page(const char* destination_hash, const char* path)
{
    Status out{};
    out.supported = true;
    out.sd_present = true;

    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    if (s_page_cache_snapshot.completed &&
        same_page_request(s_page_cache_snapshot.destination,
                          s_page_cache_snapshot.path,
                          destination,
                          normalized_path))
    {
        s_page_cache_snapshot = {};
    }

    const auto cache_path = page_path_under_sd(
        cache_relative_path(destination, normalized_path));
    out.cache_checked = true;
    out.file_present = std::filesystem::exists(cache_path);
    if (!out.file_present)
    {
        set_status(out,
                   "Nomad page cache already clear",
                   cache_path.string().c_str());
        return out;
    }

    std::error_code ec;
    const bool removed = std::filesystem::remove(cache_path, ec);
    out.file_present = !removed;
    set_status(out,
               removed && !ec ? "Nomad page cache cleared"
                              : "Cannot clear Nomad page cache",
               cache_path.string().c_str());
    return out;
}

Status request_page(const char* destination_hash, const char* path)
{
    return request_page_with_data(destination_hash, path, nullptr, 0);
}

Status request_page_with_data(const char* destination_hash,
                              const char* path,
                              const uint8_t* request_data,
                              std::size_t request_data_len)
{
    Status out{};
    out.supported = s_request_handler != nullptr;
    out.sd_present = true;
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        (request_data_len != 0 && !request_data))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    if (!s_request_handler)
    {
        set_status(out, "Nomad page fetch unavailable", normalized_path);
        return out;
    }

    uint8_t destination_bytes[kReticulumPageDestinationTextSize / 2U] = {};
    if (!destination_to_bytes(destination, destination_bytes))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }

    const RequestStartResult result =
        s_request_handler(destination_bytes,
                          normalized_path,
                          request_data,
                          request_data_len,
                          s_request_context);
    set_request_status(out, result, normalized_path);
    return out;
}

Status cancel_request(const char* destination_hash, const char* path)
{
    Status out{};
    out.supported = s_request_cancel_handler != nullptr;
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }
    if (!s_request_cancel_handler)
    {
        set_status(out, "Nomad page cancel unavailable", normalized_path);
        return out;
    }
    uint8_t destination_bytes[kReticulumPageDestinationTextSize / 2U] = {};
    if (!destination_to_bytes(destination, destination_bytes))
    {
        set_status(out, "Invalid Nomad page address", kPagesLogicalDir);
        return out;
    }
    out.cancelled = s_request_cancel_handler(
        destination_bytes, normalized_path, s_request_cancel_context);
    set_status(out,
               out.cancelled ? "Nomad page request cancelled"
                             : "Nomad page request not active",
               normalized_path);
    if (out.cancelled)
    {
        clear_request_progress(destination, normalized_path);
    }
    return out;
}

RequestProgress get_request_progress(const char* destination_hash,
                                     const char* path)
{
    RequestProgress out{};
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        copy_text(out.message, sizeof(out.message), "Invalid Nomad page address");
        return out;
    }

    const PageRequestProgressSlot* slot =
        find_request_progress_slot(destination, normalized_path, false);
    if (slot)
    {
        out = slot->progress;
    }
    return out;
}

void update_request_progress(const char* destination_hash,
                             const char* path,
                             int progress_percent,
                             const char* message,
                             const char* detail,
                             bool active,
                             bool complete,
                             RequestProgress::FailureKind failure)
{
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        return;
    }
    if (progress_percent > 100)
    {
        progress_percent = 100;
    }
    if (progress_percent < -1)
    {
        progress_percent = -1;
    }

    PageRequestProgressSlot* slot =
        find_request_progress_slot(destination, normalized_path, true);
    if (!slot)
    {
        return;
    }
    slot->progress.active = active;
    slot->progress.complete = complete;
    slot->progress.failure = failure;
    slot->progress.progress_percent = progress_percent;
    copy_text(slot->progress.message, sizeof(slot->progress.message), message);
    copy_text(slot->progress.detail, sizeof(slot->progress.detail), detail);
    slot->order = s_request_progress_order++;
}

void clear_request_progress(const char* destination_hash, const char* path)
{
    char destination[kReticulumPageDestinationTextSize] = {};
    char normalized_path[kReticulumPagePathSize] = {};
    if (!normalize_destination(destination_hash, destination, sizeof(destination)) ||
        !normalize_path(path, normalized_path, sizeof(normalized_path)))
    {
        return;
    }

    PageRequestProgressSlot* slot =
        find_request_progress_slot(destination, normalized_path, false);
    if (slot)
    {
        *slot = {};
    }
}

} // namespace platform::ui::reticulum_page

namespace platform::ui::reticulum_receive
{
namespace
{
Snapshot s_receive_snapshot{};
CancelHandler s_receive_cancel_handler = nullptr;
void* s_receive_cancel_context = nullptr;
} // namespace

void bind_cancel_handler(CancelHandler handler, void* context)
{
    s_receive_cancel_handler = handler;
    s_receive_cancel_context = context;
}

void begin(const uint8_t link_id[kHashSize],
           const uint8_t resource_hash[32],
           uint32_t total_parts,
           uint32_t total_bytes)
{
    if (!link_id || !resource_hash || total_parts == 0)
    {
        return;
    }
    s_receive_snapshot = Snapshot{};
    s_receive_snapshot.active = true;
    s_receive_snapshot.cancellable = true;
    std::memcpy(s_receive_snapshot.link_id, link_id, kHashSize);
    std::memcpy(s_receive_snapshot.resource_hash, resource_hash, 32);
    s_receive_snapshot.total_parts = total_parts;
    s_receive_snapshot.total_bytes = total_bytes;
    s_receive_snapshot.progress_percent = 0;
}

void update(const uint8_t resource_hash[32],
            uint32_t received_parts,
            uint32_t received_bytes)
{
    if (resource_hash && s_receive_snapshot.active &&
        std::memcmp(s_receive_snapshot.resource_hash, resource_hash, 32) == 0)
    {
        s_receive_snapshot.received_parts = received_parts;
        s_receive_snapshot.received_bytes = received_bytes;
        s_receive_snapshot.progress_percent = static_cast<int>(
            (received_parts * 100U) / s_receive_snapshot.total_parts);
    }
}

void complete(const uint8_t resource_hash[32])
{
    if (resource_hash && s_receive_snapshot.active &&
        std::memcmp(s_receive_snapshot.resource_hash, resource_hash, 32) == 0)
    {
        s_receive_snapshot = Snapshot{};
    }
}

Snapshot snapshot()
{
    return s_receive_snapshot;
}

bool cancel()
{
    Snapshot current{};
    CancelHandler handler = nullptr;
    void* context = nullptr;
    current = s_receive_snapshot;
    handler = s_receive_cancel_handler;
    context = s_receive_cancel_context;
    if (!current.active || !handler ||
        !handler(current.link_id, current.resource_hash, context))
    {
        return false;
    }
    complete(current.resource_hash);
    return true;
}
} // namespace platform::ui::reticulum_receive
