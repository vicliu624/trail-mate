/**
 * @file protocol_factory.h
 * @brief Factory for creating protocol-specific mesh adapters
 */

#pragma once

#include "../domain/chat_types.h"
#include "../ports/i_mesh_adapter.h"
#include "../../board/LoraBoard.h"
#include <memory>

namespace chat {

/**
 * @brief Factory class for creating mesh adapters based on protocol selection
 */
class ProtocolFactory {
public:
    ProtocolFactory() = delete;
    ~ProtocolFactory() = delete;

    /**
     * @brief Create a mesh adapter for the specified protocol
     */
    static std::unique_ptr<IMeshAdapter> createAdapter(MeshProtocol protocol,
                                                       LoraBoard& board);
};

} // namespace chat
