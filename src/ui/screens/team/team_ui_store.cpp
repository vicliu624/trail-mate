/**
 * @file team_ui_store.cpp
 * @brief Team UI snapshot store (stub implementation)
 */

#include "team_ui_store.h"

#include <SD.h>
#include <vector>


namespace team
{
namespace ui
{

bool TeamUiStoreStub::has_snapshot_ = false;
TeamUiSnapshot TeamUiStoreStub::snapshot_{};

namespace
{
constexpr uint32_t kMagic = 0x544D5350; // 'TMSP'
constexpr uint16_t kVersion = 2;
constexpr const char* kSdPath = "/team_ui.bin";

struct PersistedTeamKeysV1
{
    uint32_t magic = kMagic;
    uint16_t version = 1;
    uint16_t reserved = 0;
    uint32_t security_round = 0;
    uint8_t has_team_id = 0;
    uint8_t has_team_psk = 0;
    uint8_t pad[2] = {0, 0};
    TeamId team_id{};
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
};

struct PersistedTeamKeysV2
{
    uint32_t magic = kMagic;
    uint16_t version = kVersion;
    uint16_t reserved = 0;
    uint32_t security_round = 0;
    uint8_t in_team = 0;
    uint8_t pending_join = 0;
    uint8_t kicked_out = 0;
    uint8_t self_is_leader = 0;
    uint8_t has_team_id = 0;
    uint8_t has_join_target = 0;
    uint8_t has_team_psk = 0;
    uint8_t pad[1] = {0};
    uint32_t pending_join_started_s = 0;
    TeamId team_id{};
    TeamId join_target_id{};
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
};

class TeamUiStorePersisted : public ITeamUiStore
{
  public:
    bool load(TeamUiSnapshot& out) override
    {
        if (SD.cardType() == CARD_NONE || !SD.exists(kSdPath))
        {
            return false;
        }

        File f = SD.open(kSdPath, FILE_READ);
        if (!f)
        {
            return false;
        }
        size_t file_size = f.size();
        std::vector<uint8_t> buf(file_size);
        bool loaded = (f.read(buf.data(), buf.size()) == buf.size());
        f.close();

        if (!loaded)
        {
            return false;
        }

        if (buf.size() >= sizeof(PersistedTeamKeysV2))
        {
            const PersistedTeamKeysV2* data =
                reinterpret_cast<const PersistedTeamKeysV2*>(buf.data());
            if (data->magic != kMagic || data->version != kVersion)
            {
                return false;
            }
            out.security_round = data->security_round;
            out.in_team = (data->in_team != 0);
            out.pending_join = (data->pending_join != 0);
            out.kicked_out = (data->kicked_out != 0);
            out.self_is_leader = (data->self_is_leader != 0);
            out.pending_join_started_s = data->pending_join_started_s;
            out.has_team_id = (data->has_team_id != 0);
            out.team_id = data->team_id;
            out.has_join_target = (data->has_join_target != 0);
            out.join_target_id = data->join_target_id;
            out.has_team_psk = (data->has_team_psk != 0);
            out.team_psk = data->team_psk;
            return true;
        }

        if (buf.size() >= sizeof(PersistedTeamKeysV1))
        {
            const PersistedTeamKeysV1* data =
                reinterpret_cast<const PersistedTeamKeysV1*>(buf.data());
            if (data->magic != kMagic || data->version != 1)
            {
                return false;
            }
            out.security_round = data->security_round;
            out.has_team_id = (data->has_team_id != 0);
            out.team_id = data->team_id;
            out.has_team_psk = (data->has_team_psk != 0);
            out.team_psk = data->team_psk;
            if (out.has_team_id && out.has_team_psk)
            {
                out.in_team = true;
            }
            return true;
        }

        return false;
    }

    void save(const TeamUiSnapshot& in) override
    {
        PersistedTeamKeysV2 data{};
        data.security_round = in.security_round;
        data.in_team = in.in_team ? 1 : 0;
        data.pending_join = in.pending_join ? 1 : 0;
        data.kicked_out = in.kicked_out ? 1 : 0;
        data.self_is_leader = in.self_is_leader ? 1 : 0;
        data.pending_join_started_s = in.pending_join_started_s;
        data.has_team_id = in.has_team_id ? 1 : 0;
        data.has_join_target = in.has_join_target ? 1 : 0;
        data.has_team_psk = in.has_team_psk ? 1 : 0;
        data.team_id = in.team_id;
        data.join_target_id = in.join_target_id;
        data.team_psk = in.team_psk;

        if (SD.cardType() == CARD_NONE)
        {
            return;
        }
        File f = SD.open(kSdPath, FILE_WRITE);
        if (!f)
        {
            return;
        }
        f.write((const uint8_t*)&data, sizeof(data));
        f.flush();
        f.close();
    }

    void clear() override
    {
        if (SD.cardType() != CARD_NONE && SD.exists(kSdPath))
        {
            SD.remove(kSdPath);
        }
    }
};

TeamUiStorePersisted s_persisted_store;
} // namespace

bool TeamUiStoreStub::load(TeamUiSnapshot& out)
{
    if (!has_snapshot_)
    {
        return false;
    }
    out = snapshot_;
    return true;
}

void TeamUiStoreStub::save(const TeamUiSnapshot& in)
{
    snapshot_ = in;
    has_snapshot_ = true;
}

void TeamUiStoreStub::clear()
{
    has_snapshot_ = false;
}

namespace
{
TeamUiStoreStub s_stub_store;
ITeamUiStore* s_store = &s_persisted_store;
} // namespace

ITeamUiStore& team_ui_get_store()
{
    return *s_store;
}

void team_ui_set_store(ITeamUiStore* store)
{
    if (store)
    {
        s_store = store;
    }
    else
    {
        s_store = &s_stub_store;
    }
}

} // namespace ui
} // namespace team
