/**
 * @file contact_service.cpp
 * @brief Contact and peer-directory use cases.
 */

#include "chat/usecase/contact_service.h"
#include "sys/clock.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

namespace chat
{
namespace contacts
{

#ifndef CONTACT_SERVICE_LOG_ENABLE
#define CONTACT_SERVICE_LOG_ENABLE 0
#endif

#if CONTACT_SERVICE_LOG_ENABLE
#define CONTACT_SERVICE_LOG(...) std::printf(__VA_ARGS__)
#else
#define CONTACT_SERVICE_LOG(...)
#endif

namespace
{

bool has_text(const char* text)
{
    return text && text[0] != '\0';
}

bool same_text(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
    {
        return lhs == rhs;
    }
    return std::strcmp(lhs, rhs) == 0;
}

std::string preferred_node_name(const char* short_name,
                                const char* long_name)
{
    if (has_text(long_name) && !same_text(long_name, short_name))
    {
        return std::string(long_name);
    }
    if (has_text(short_name))
    {
        return std::string(short_name);
    }
    return has_text(long_name) ? std::string(long_name) : std::string();
}

std::string preferred_node_name(const PeerDirectoryItem& peer)
{
    if (!peer.display_name.empty() &&
        !same_text(peer.display_name.c_str(), peer.short_name))
    {
        return peer.display_name;
    }
    return preferred_node_name(peer.short_name, peer.long_name);
}

MeshProtocol normalize_protocol(MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                           : protocol;
}

const MeshPeerNodeFacts* node_facts(const MeshPeerRecord& record)
{
    if (record.identity.protocol == MeshProtocol::Meshtastic)
    {
        return &record.meshtastic.node;
    }
    if (record.identity.protocol == MeshProtocol::MeshCore)
    {
        return &record.meshcore.node;
    }
    return nullptr;
}

MeshPeerNodeFacts* mutable_node_facts(MeshPeerRecord& record)
{
    if (record.identity.protocol == MeshProtocol::Meshtastic)
    {
        return &record.meshtastic.node;
    }
    if (record.identity.protocol == MeshProtocol::MeshCore)
    {
        return &record.meshcore.node;
    }
    return nullptr;
}

void copy_peer_item(PeerDirectoryItem& out, const MeshPeerRecord& record)
{
    out = {};
    out.node_id = meshPeerProjectedNodeId(record);
    out.last_seen = record.last_seen_s;
    const MeshProtocol protocol = normalize_protocol(record.identity.protocol);
    out.protocol = static_cast<NodeProtocolType>(protocol);

    const MeshPeerNodeFacts* facts = node_facts(record);
    if (facts)
    {
        copyMeshPeerText(out.short_name, sizeof(out.short_name),
                         facts->short_name);
        copyMeshPeerText(out.long_name, sizeof(out.long_name),
                         facts->long_name);
        out.role = static_cast<NodeRoleType>(facts->role);
        out.hw_model = facts->hw_model;
        out.channel = facts->channel;
        out.hops_away = facts->hops_away;
        out.has_macaddr = facts->has_macaddr;
        std::memcpy(out.macaddr, facts->macaddr, sizeof(out.macaddr));
        out.via_mqtt = facts->via_mqtt;
    }
    else
    {
        copyMeshPeerText(out.long_name, sizeof(out.long_name),
                         record.display_name);
        out.role = NodeRoleType::Client;
        out.hops_away = 0;
    }

    if (out.long_name[0] == '\0')
    {
        copyMeshPeerText(out.long_name, sizeof(out.long_name),
                         record.display_name);
    }
    if (out.short_name[0] == '\0')
    {
        std::snprintf(out.short_name,
                      sizeof(out.short_name),
                      "%04X",
                      static_cast<unsigned>(out.node_id & 0xFFFFU));
    }

    out.has_snr = record.observations.has_snr;
    out.has_rssi = record.observations.has_rssi;
    out.snr = record.observations.has_snr ? record.observations.snr : 0.0F;
    out.rssi = record.observations.has_rssi ? record.observations.rssi : 0.0F;
    out.is_contact = meshPeerIsContact(record);
    out.is_ignored = record.flags.ignored;
    out.has_device_metrics = record.observations.has_device_metrics;
    if (out.has_device_metrics)
    {
        out.device_metrics = record.observations.device_metrics;
    }
    if (record.observations.has_position)
    {
        out.position = record.observations.position;
    }

    if (protocol == MeshProtocol::Meshtastic)
    {
        out.next_hop = record.meshtastic.has_next_hop
                           ? record.meshtastic.next_hop
                           : 0;
        out.has_public_key = record.meshtastic.has_public_key;
        out.key_manually_verified =
            record.meshtastic.key_manually_verified;
    }
    else if (protocol == MeshProtocol::MeshCore)
    {
        out.next_hop = record.meshcore.has_next_hop
                           ? record.meshcore.next_hop
                           : 0;
        out.has_public_key =
            record.meshcore.has_public_key ||
            record.identity.kind == MeshPeerIdentityKind::PublicKey;
        out.key_manually_verified = record.meshcore.public_key_verified;
    }
    else if (protocol == MeshProtocol::Reticulum)
    {
        out.reticulum_identity = record.identity.reticulum;
        out.has_public_key = record.reticulum.has_public_keys;
        out.key_manually_verified = record.flags.trusted;
    }

    const std::string protocol_name =
        preferred_node_name(out.short_name, out.long_name);
    out.display_name = record.user_alias[0] != '\0'
                           ? std::string(record.user_alias)
                           : protocol_name;
}

class ProjectionVisitor final : public IMeshPeerDirectoryVisitor
{
  public:
    explicit ProjectionVisitor(std::vector<PeerDirectoryItem>& out)
        : out_(out)
    {
    }

    bool visit(const MeshPeerRecord& record) override
    {
        if (meshPeerProjectedNodeId(record) == 0 &&
            !(meshPeerIsReticulumProtocol(record.identity.protocol) && hasReticulumDestinationIdentity(record.identity.reticulum)))
        {
            return true;
        }
        out_.emplace_back();
        copy_peer_item(out_.back(), record);
        return true;
    }

  private:
    std::vector<PeerDirectoryItem>& out_;
};

class ReticulumLookupVisitor final : public IMeshPeerDirectoryVisitor
{
  public:
    ReticulumLookupVisitor(
        const uint8_t destination_hash[kReticulumPeerHashSize],
        uint32_t* out_node_id)
        : destination_hash_(destination_hash), out_node_id_(out_node_id)
    {
    }

    bool visit(const MeshPeerRecord& record) override
    {
        if (sameReticulumDestinationHash(record.identity.reticulum,
                                         destination_hash_))
        {
            *out_node_id_ = meshPeerProjectedNodeId(record);
            found_ = *out_node_id_ != 0;
            return false;
        }
        return true;
    }

    bool found() const { return found_; }

  private:
    const uint8_t* destination_hash_ = nullptr;
    uint32_t* out_node_id_ = nullptr;
    bool found_ = false;
};

} // namespace

ContactService::ContactService(IMeshPeerDirectory& directory)
    : directory_(directory), cache_timestamp_(0)
{
}

void ContactService::begin()
{
    (void)directory_.begin();
    invalidateCache();
}

void ContactService::setActiveProtocol(MeshProtocol protocol)
{
    active_protocol_ = normalize_protocol(protocol);
    invalidateCache();
}

void ContactService::applyNodeUpdate(uint32_t node_id,
                                     const NodeUpdate& update)
{
    MeshProtocol protocol = active_protocol_;
    if (update.has_protocol)
    {
        const MeshProtocol requested =
            normalize_protocol(static_cast<MeshProtocol>(update.protocol));
        if (requested == MeshProtocol::Meshtastic ||
            requested == MeshProtocol::MeshCore ||
            requested == MeshProtocol::Reticulum)
        {
            protocol = requested;
        }
    }

    const bool full_reticulum_identity = protocol == MeshProtocol::Reticulum && hasReticulumDestinationIdentity(update.reticulum_identity);
    if (!node_id && !full_reticulum_identity) return;
    const auto find_record = [&](MeshPeerRecord& out)
    {
        return full_reticulum_identity ? directory_.find(makeMeshPeerReticulumIdentity(update.reticulum_identity), out)
                                       : directory_.findByNodeId(protocol, node_id, out);
    };
    const bool found = find_record(lookup_scratch_).succeeded();
    update_scratch_ = {};
    update_scratch_.valid = true;
    update_scratch_.source = MeshPeerSource::RuntimeRx;
    if (protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(update.reticulum_identity))
    {
        update_scratch_.identity =
            makeMeshPeerReticulumIdentity(update.reticulum_identity);
        update_scratch_.reticulum.identity = update.reticulum_identity;
    }
    else if (found)
    {
        update_scratch_.identity = lookup_scratch_.identity;
    }
    else
    {
        update_scratch_.identity = makeMeshPeerNodeIdentity(protocol, node_id);
    }
    update_scratch_.identity.protocol = protocol;
    if (update.has_last_seen)
    {
        update_scratch_.first_seen_s = update.last_seen;
        update_scratch_.last_seen_s = update.last_seen;
    }
    if (has_text(update.long_name))
    {
        copyMeshPeerText(update_scratch_.display_name,
                         sizeof(update_scratch_.display_name),
                         update.long_name);
    }
    else if (has_text(update.short_name))
    {
        copyMeshPeerText(update_scratch_.display_name,
                         sizeof(update_scratch_.display_name),
                         update.short_name);
    }
    if (update.has_snr)
    {
        update_scratch_.observations.has_snr = true;
        update_scratch_.observations.snr = update.snr;
    }
    if (update.has_rssi)
    {
        update_scratch_.observations.has_rssi = true;
        update_scratch_.observations.rssi = update.rssi;
    }
    if (update.has_device_metrics)
    {
        update_scratch_.observations.has_device_metrics = true;
        update_scratch_.observations.device_metrics = update.device_metrics;
    }
    if (update.has_position)
    {
        update_scratch_.observations.has_position = true;
        update_scratch_.observations.position = update.position;
    }

    MeshPeerNodeFacts* facts = mutable_node_facts(update_scratch_);
    if (facts)
    {
        copyMeshPeerText(facts->short_name,
                         sizeof(facts->short_name),
                         update.short_name);
        copyMeshPeerText(facts->long_name,
                         sizeof(facts->long_name),
                         update.long_name);
        if (update.has_role)
        {
            facts->role = update.role;
        }
        if (update.has_hw_model)
        {
            facts->hw_model = update.hw_model;
        }
        if (update.has_channel)
        {
            facts->channel = update.channel;
        }
        if (update.has_hops_away)
        {
            facts->hops_away = update.hops_away;
        }
        if (update.has_macaddr)
        {
            facts->has_macaddr = true;
            std::memcpy(facts->macaddr,
                        update.macaddr,
                        sizeof(facts->macaddr));
        }
        if (update.has_via_mqtt)
        {
            facts->via_mqtt = update.via_mqtt;
        }
        if (update.has_next_hop)
        {
            if (protocol == MeshProtocol::Meshtastic)
            {
                update_scratch_.meshtastic.has_next_hop = true;
                update_scratch_.meshtastic.next_hop = update.next_hop;
            }
            else
            {
                update_scratch_.meshcore.has_next_hop = true;
                update_scratch_.meshcore.next_hop = update.next_hop;
            }
        }
        if (protocol == MeshProtocol::MeshCore)
        {
            update_scratch_.meshcore.node_id_hint = node_id;
        }
    }

    if (!directory_.record(update_scratch_).succeeded())
    {
        return;
    }
    if (find_record(lookup_scratch_).succeeded())
    {
        if (update.has_is_ignored)
        {
            MeshPeerUserFlags flags = lookup_scratch_.flags;
            flags.ignored = update.is_ignored;
            (void)directory_.setUserFlags(lookup_scratch_.identity, flags);
        }
        if (update.has_key_manually_verified)
        {
            (void)directory_.setKeyManuallyVerified(
                lookup_scratch_.identity,
                update.key_manually_verified);
        }
    }
    invalidateCache();
}

bool ContactService::recordPeer(const MeshPeerRecord& record)
{
    const bool recorded = directory_.record(record).succeeded();
    if (recorded)
    {
        invalidateCache();
    }
    return recorded;
}

void ContactService::updateNodeInfo(uint32_t node_id,
                                    const char* short_name,
                                    const char* long_name,
                                    float snr,
                                    float rssi,
                                    uint32_t now_secs,
                                    uint8_t protocol,
                                    uint8_t role,
                                    uint8_t hops_away,
                                    uint8_t hw_model,
                                    uint8_t channel)
{
    CONTACT_SERVICE_LOG(
        "[ContactService] updateNodeInfo node=%08" PRIX32 "\n", node_id);
    NodeUpdate update{};
    update.short_name = short_name;
    update.long_name = long_name;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    update.has_snr = !std::isnan(snr);
    update.snr = snr;
    update.has_rssi = !std::isnan(rssi);
    update.rssi = rssi;
    update.has_protocol = protocol != 0;
    update.protocol = protocol;
    update.has_role = role != kNodeRoleUnknown;
    update.role = role;
    update.has_hops_away = hops_away != 0xFF;
    update.hops_away = hops_away;
    update.has_hw_model = hw_model != 0;
    update.hw_model = hw_model;
    update.has_channel = channel != 0xFF;
    update.channel = channel;
    applyNodeUpdate(node_id, update);
}

void ContactService::updateNodeProtocol(uint32_t node_id,
                                        uint8_t protocol,
                                        uint32_t now_secs)
{
    NodeUpdate update{};
    update.has_protocol = protocol != 0;
    update.protocol = protocol;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    applyNodeUpdate(node_id, update);
}

void ContactService::updateNodePosition(uint32_t node_id,
                                        const NodePosition& position)
{
    NodeUpdate update{};
    update.has_position = true;
    update.position = position;
    applyNodeUpdate(node_id, update);
}

bool ContactService::setNextHop(uint32_t node_id, uint8_t next_hop)
{
    NodeUpdate update{};
    update.has_next_hop = true;
    update.next_hop = next_hop;
    update.has_last_seen = true;
    update.last_seen = ::sys::epoch_seconds_now();
    applyNodeUpdate(node_id, update);
    return getNextHop(node_id) == next_hop;
}

uint8_t ContactService::getNextHop(uint32_t node_id) const
{
    const PeerDirectoryItem* peer = getPeerByNodeId(node_id);
    return peer ? peer->next_hop : 0;
}

std::string ContactService::getContactName(uint32_t node_id) const
{
    const PeerDirectoryItem* peer = getPeerByNodeId(node_id);
    return peer ? preferred_node_name(*peer) : std::string();
}

std::string ContactService::getReticulumContactName(
    const ReticulumPeerIdentity& identity) const
{
    if (!hasReticulumDestinationIdentity(identity))
    {
        return {};
    }
    buildCache();
    for (const PeerDirectoryItem& peer : cached_nodes_)
    {
        if (sameReticulumDestinationHash(peer.reticulum_identity, identity))
        {
            return preferred_node_name(peer);
        }
    }
    return {};
}

std::vector<PeerDirectoryItem> ContactService::getContacts() const
{
    buildCache();
    std::vector<PeerDirectoryItem> result;
    for (const PeerDirectoryItem& peer : cached_nodes_)
    {
        if (peer.is_contact)
        {
            result.push_back(peer);
        }
    }
    return result;
}

std::vector<PeerDirectoryItem> ContactService::getNearby() const
{
    buildCache();
    std::vector<PeerDirectoryItem> result;
    for (const PeerDirectoryItem& peer : cached_nodes_)
    {
        if (!peer.is_contact && !peer.is_ignored &&
            isNodeVisible(peer.last_seen))
        {
            result.push_back(peer);
        }
    }
    return result;
}

std::vector<PeerDirectoryItem> ContactService::getIgnoredNodes() const
{
    buildCache();
    std::vector<PeerDirectoryItem> result;
    for (const PeerDirectoryItem& peer : cached_nodes_)
    {
        if (!peer.is_contact && peer.is_ignored &&
            isNodeVisible(peer.last_seen))
        {
            result.push_back(peer);
        }
    }
    return result;
}

std::vector<PeerDirectoryItem> ContactService::getAllPeers() const
{
    buildCache();
    return cached_nodes_;
}

MeshPeerDirectoryStatus ContactService::browsePeers(
    MeshPeerDirectoryView view, const char* query,
    std::vector<PeerBrowseEntry>& output) const
{
    class BrowseVisitor final : public IMeshPeerDirectoryVisitor
    {
      public:
        BrowseVisitor(std::vector<PeerBrowseEntry>& output, const char* query, MeshPeerDirectoryView view)
            : output_(output), query_(query ? query : ""), view_(view) {}
        bool visit(const MeshPeerRecord& record) override
        {
            const auto* facts = node_facts(record);
            if (meshPeerIsReticulumProtocol(record.identity.protocol) && view_ != MeshPeerDirectoryView::All)
            {
                const bool contact = meshPeerIsContact(record) || record.source == MeshPeerSource::Manual ||
                                     record.source == MeshPeerSource::Import;
                if (view_ == MeshPeerDirectoryView::Ignored ? !record.flags.ignored : record.flags.ignored || (view_ == MeshPeerDirectoryView::Contacts ? !contact : contact)) return true;
            }
            const uint32_t now = sys::epoch_seconds_now();
            if (view_ == MeshPeerDirectoryView::Nearby && record.identity.protocol != MeshProtocol::Reticulum &&
                now > record.last_seen_s && now - record.last_seen_s >= 6U * 86400U) return true;
            char identity_text[65]{};
            if (record.identity.kind == MeshPeerIdentityKind::NodeId)
                std::snprintf(identity_text, sizeof(identity_text), "!%08lx", static_cast<unsigned long>(record.identity.node_id));
            else
            {
                const uint8_t* bytes = record.identity.kind == MeshPeerIdentityKind::PublicKey ? record.identity.public_key : record.identity.reticulum.destination_hash;
                const size_t count = record.identity.kind == MeshPeerIdentityKind::PublicKey ? std::min<size_t>(record.identity.public_key_len, 32) : kReticulumPeerHashSize;
                for (size_t i = 0; i < count; ++i) std::snprintf(identity_text + i * 2, 3, "%02x", bytes[i]);
            }
            if (!matches(record.user_alias) && !matches(record.display_name) &&
                !matches(identity_text) && !(facts && (matches(facts->long_name) || matches(facts->short_name)))) return true;
            PeerBrowseEntry entry{};
            if (PeerBrowseEntry::fromRecord(record, entry)) output_.push_back(entry);
            return true;
        }

      private:
        bool matches(const char* text) const
        {
            if (!query_[0]) return true;
            for (const char* start = text; *start; ++start)
            {
                size_t i = 0;
                while (query_[i] && start[i] &&
                       std::tolower(static_cast<unsigned char>(query_[i])) ==
                           std::tolower(static_cast<unsigned char>(start[i]))) ++i;
                if (!query_[i]) return true;
            }
            return false;
        }
        std::vector<PeerBrowseEntry>& output_;
        const char* query_;
        MeshPeerDirectoryView view_;
    };
    output.clear();
    BrowseVisitor visitor(output, query, view);
    const auto status = directory_.visit(active_protocol_, meshPeerIsReticulumProtocol(active_protocol_) ? MeshPeerDirectoryView::All : view, visitor);
    if (!status.succeeded())
    {
        output.clear();
        return status;
    }
    std::sort(output.begin(), output.end(), [](const PeerBrowseEntry& lhs, const PeerBrowseEntry& rhs)
              {
                  if (lhs.last_seen != rhs.last_seen) return lhs.last_seen > rhs.last_seen;
                  if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
                  if (lhs.node_id != rhs.node_id) return lhs.node_id < rhs.node_id;
                  return lhs.identity_bytes < rhs.identity_bytes; });
    return status;
}

MeshPeerDirectoryStatus ContactService::readPeer(
    const PeerBrowseEntry& entry, PeerDirectoryItem& output) const
{
    if (normalize_protocol(entry.protocol) != active_protocol_)
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
    const auto identity = entry.identity();
    const auto status = directory_.find(identity, lookup_scratch_);
    if (status.succeeded())
    {
        copy_peer_item(output, lookup_scratch_);
        if (meshPeerIsReticulumProtocol(entry.protocol))
            output.is_contact = output.is_contact || lookup_scratch_.source == MeshPeerSource::Manual ||
                                lookup_scratch_.source == MeshPeerSource::Import;
    }
    else output = {};
    return status;
}

bool ContactService::addContact(uint32_t node_id, const char* nickname)
{
    return editContact(node_id, nickname);
}

bool ContactService::editContact(uint32_t node_id, const char* nickname)
{
    if (!nickname || nickname[0] == '\0' ||
        !ensureNodeExistsForContact(node_id) ||
        !directory_.setUserAlias(lookup_scratch_.identity, nickname).succeeded())
    {
        return false;
    }
    invalidateCache();
    return true;
}

bool ContactService::removeContact(uint32_t node_id)
{
    if (!hasNodeEntry(node_id) ||
        !directory_.setUserAlias(lookup_scratch_.identity, "").succeeded())
    {
        return false;
    }
    invalidateCache();
    return true;
}

bool ContactService::removeNode(uint32_t node_id)
{
    if (!hasNodeEntry(node_id))
    {
        return false;
    }
    const MeshPeerIdentity identity = lookup_scratch_.identity;
    (void)directory_.setUserAlias(identity, "");
    (void)directory_.setUserFlags(identity, MeshPeerUserFlags{});
    const bool removed = directory_.remove(identity).succeeded();
    if (removed)
    {
        invalidateCache();
    }
    return removed;
}

bool ContactService::setNodeIgnored(uint32_t node_id, bool ignored)
{
    if (!hasNodeEntry(node_id))
    {
        return false;
    }
    MeshPeerUserFlags flags = lookup_scratch_.flags;
    flags.ignored = ignored;
    const bool updated =
        directory_.setUserFlags(lookup_scratch_.identity, flags).succeeded();
    if (updated)
    {
        invalidateCache();
    }
    return updated;
}

bool ContactService::setNodeKeyManuallyVerified(uint32_t node_id,
                                                bool verified)
{
    if (!hasNodeEntry(node_id))
    {
        return false;
    }
    const bool updated = directory_.setKeyManuallyVerified(
                                       lookup_scratch_.identity,
                                       verified)
                             .succeeded();
    if (updated)
    {
        invalidateCache();
    }
    return updated;
}

const PeerDirectoryItem* ContactService::getPeerByNodeId(
    uint32_t node_id) const
{
    buildCache();
    for (const PeerDirectoryItem& peer : cached_nodes_)
    {
        if (peer.node_id == node_id)
        {
            return &peer;
        }
    }
    return nullptr;
}

bool ContactService::findNodeIdByReticulumDestinationHash(
    const uint8_t destination_hash[kReticulumPeerHashSize],
    uint32_t* out_node_id) const
{
    if (!destination_hash || !out_node_id)
    {
        return false;
    }
    ReticulumLookupVisitor visitor(destination_hash, out_node_id);
    (void)directory_.visit(MeshProtocol::Reticulum,
                           MeshPeerDirectoryView::All,
                           visitor);
    return visitor.found();
}

void ContactService::clearCache()
{
    invalidateCache();
}

void ContactService::invalidateCache() const
{
    cache_timestamp_ = 0;
    cached_nodes_.clear();
}

void ContactService::buildCache() const
{
    const uint32_t now_ms = sys::millis_now();
    if (cache_timestamp_ != 0 &&
        (now_ms - cache_timestamp_) < kCacheTimeoutMs)
    {
        return;
    }
    std::vector<PeerDirectoryItem> next_nodes;
    ProjectionVisitor visitor(next_nodes);
    const MeshPeerDirectoryStatus status =
        directory_.visit(active_protocol_,
                         MeshPeerDirectoryView::All,
                         visitor);
    if (!status.succeeded())
    {
        // A persistent directory may not have installed its authoritative
        // projection yet. Keep the last visible cache instead of turning a
        // transient retry into a blank contact list.
        cache_timestamp_ = now_ms;
        return;
    }
    cached_nodes_.swap(next_nodes);
    cache_timestamp_ = now_ms;
}

bool ContactService::ensureNodeExistsForContact(uint32_t node_id)
{
    return hasNodeEntry(node_id) &&
           meshPeerIsStableContactIdentity(lookup_scratch_.identity);
}

bool ContactService::hasNodeEntry(uint32_t node_id) const
{
    return node_id != 0 &&
           directory_
               .findByNodeId(active_protocol_, node_id, lookup_scratch_)
               .succeeded();
}

bool ContactService::isNodeVisible(uint32_t last_seen) const
{
    (void)last_seen;
    return true;
}

std::string ContactService::formatTimeStatus(uint32_t last_seen) const
{
    const uint32_t now_secs = sys::epoch_seconds_now();
    if (now_secs < last_seen)
    {
        return "Offline";
    }
    const uint32_t age_secs = now_secs - last_seen;
    if (age_secs <= 120)
    {
        return "Online";
    }
    char buffer[24] = {};
    if (age_secs < 3600)
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "Seen %lum",
                      static_cast<unsigned long>(age_secs / 60));
    }
    else if (age_secs < 86400)
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "Seen %luh",
                      static_cast<unsigned long>(age_secs / 3600));
    }
    else
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "Seen %lud",
                      static_cast<unsigned long>(age_secs / 86400));
    }
    return buffer;
}

} // namespace contacts
} // namespace chat
