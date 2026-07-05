#pragma once

#if defined(TRAILMATE_TARGET_T_IMPULSE_PLUS)
#include "boards/t_impulse_plus/settings_store.h"
#include "boards/t_impulse_plus/t_impulse_plus_board.h"
#elif defined(TRAILMATE_TARGET_T_ECHO_LITE)
#include "boards/t_echo_lite/settings_store.h"
#include "boards/t_echo_lite/t_echo_lite_board.h"
#else
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#endif

#include <cstdint>

namespace trailmate::apps::nrf52_node::target_board
{

#if defined(TRAILMATE_TARGET_T_IMPULSE_PLUS)
using Board = ::boards::t_impulse_plus::TImpulsePlusBoard;
using BoardInputEvent = ::boards::t_impulse_plus::BoardInputEvent;
using BoardInputKey = ::boards::t_impulse_plus::BoardInputKey;
namespace settings_store = ::boards::t_impulse_plus::settings_store;
constexpr const char* kLogTag = "[t-impulse-plus]";
constexpr const char* kTargetId = "t-impulse-plus";
constexpr uint32_t kFsTotalBytes = 7U * 4096U;
#elif defined(TRAILMATE_TARGET_T_ECHO_LITE)
using Board = ::boards::t_echo_lite::TEchoLiteBoard;
using BoardInputEvent = ::boards::t_echo_lite::BoardInputEvent;
using BoardInputKey = ::boards::t_echo_lite::BoardInputKey;
namespace settings_store = ::boards::t_echo_lite::settings_store;
constexpr const char* kLogTag = "[t-echo-lite]";
constexpr const char* kTargetId = "t-echo-lite";
constexpr uint32_t kFsTotalBytes = 7U * 4096U;
#else
using Board = ::boards::gat562_mesh_evb_pro::Gat562Board;
using BoardInputEvent = ::boards::gat562_mesh_evb_pro::BoardInputEvent;
using BoardInputKey = ::boards::gat562_mesh_evb_pro::BoardInputKey;
namespace settings_store = ::boards::gat562_mesh_evb_pro::settings_store;
constexpr const char* kLogTag = "[gat562]";
constexpr const char* kTargetId = "gat562_mesh_evb_pro";
constexpr uint32_t kFsTotalBytes = 7U * 4096U;
#endif

inline Board& instance()
{
    return Board::instance();
}

} // namespace trailmate::apps::nrf52_node::target_board
