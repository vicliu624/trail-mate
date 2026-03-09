#pragma once

#include "../../../core_chat/include/chat/domain/chat_types.h"
#include "hostlink/hostlink_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hostlink
{

constexpr uint8_t kAppDataFlagMoreChunks = 1 << 3;

struct AppDataFrameEncodeRequest
{
    uint32_t portnum = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t channel = 0;
    uint8_t flags = 0;
    const uint8_t* team_id = nullptr;
    uint32_t team_key_id = 0;
    uint32_t timestamp_s = 0;
    uint32_t packet_id = 0;
    const chat::RxMeta* rx_meta = nullptr;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

void build_rx_meta_tlvs(const chat::RxMeta& meta,
                        uint32_t packet_id,
                        std::vector<uint8_t>& out);

bool encode_app_data_frames(const AppDataFrameEncodeRequest& request,
                            size_t max_frame_len,
                            size_t team_id_size,
                            std::vector<std::vector<uint8_t>>& out_frames);

} // namespace hostlink
