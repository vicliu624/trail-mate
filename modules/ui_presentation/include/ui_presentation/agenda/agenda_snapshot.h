#pragma once

#include "agenda/usecase/agenda_query.h"
#include "agenda/usecase/agenda_service.h"

namespace ui::agenda
{

struct AgendaRequest
{
    int64_t day_start = 0;
    uint8_t visible_rows = 5;
    ::agenda::QueryCursor after;
};

struct AgendaSnapshot
{
    ::agenda::AgendaPage page;
    int64_t today_start = 0;
    uint32_t revision = 0;
    bool clock_valid = false;
    bool storage_ready = false;
    ::agenda::StoreResult result = ::agenda::StoreResult::Ok;
};

enum class CommandState : uint8_t
{
    Idle,
    Pending,
    Succeeded,
    Failed,
};

struct CommandResult
{
    CommandState state = CommandState::Idle;
    ::agenda::AgendaResult result = ::agenda::AgendaResult::Ok;
    uint32_t event_id = 0;
    uint32_t sequence = 0;
};

struct MonthSnapshot
{
    uint32_t occupied_days = 0;
    uint16_t year = 1970;
    uint8_t month = 1;
    ::agenda::StoreResult result = ::agenda::StoreResult::Ok;
};

static_assert(sizeof(AgendaSnapshot) < 2048, "Agenda presentation budget exceeded");
static_assert(sizeof(MonthSnapshot) < 256, "Month picker budget exceeded");
} // namespace ui::agenda
