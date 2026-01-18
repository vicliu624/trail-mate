/**
 * @file contact_types.h
 * @brief Contact domain types
 */

#pragma once

#include <cstdint>
#include <string>

namespace chat {
namespace contacts {

/**
 * @brief Node information (from mesh network)
 */
struct NodeInfo {
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    uint32_t last_seen;  // Unix timestamp (seconds)
    float snr;           // Signal-to-Noise Ratio
    bool is_contact;     // true if user has assigned a nickname
    std::string display_name;  // nickname if contact, short_name otherwise
};

} // namespace contacts
} // namespace chat
