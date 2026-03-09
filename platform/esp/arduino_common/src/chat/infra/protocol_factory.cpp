/**
 * @file protocol_factory.cpp
 * @brief Factory for creating protocol-specific mesh adapters
 */

#include "platform/esp/arduino_common/chat/infra/protocol_factory.h"
#include "board/LoraBoard.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"

namespace chat
{

std::unique_ptr<IMeshAdapter> ProtocolFactory::createAdapter(MeshProtocol protocol,
                                                             LoraBoard& board)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return std::unique_ptr<IMeshAdapter>(new chat::meshcore::MeshCoreAdapter(board));
    case MeshProtocol::Meshtastic:
    default:
        return std::unique_ptr<IMeshAdapter>(new chat::meshtastic::MtAdapter(board));
    }
}

} // namespace chat
