#pragma once

namespace chat
{
class ChatService;

namespace contacts
{
class ContactService;
} // namespace contacts
} // namespace chat

namespace product_composition
{

struct AppServicesBundle
{
    chat::ChatService* chat = nullptr;
    chat::contacts::ContactService* contacts = nullptr;

    // Keep this intentionally small. Add services only when a composition root
    // actually wires them; this must not become a new AppContext.
};

inline bool hasChatServices(const AppServicesBundle& bundle)
{
    return bundle.chat != nullptr && bundle.contacts != nullptr;
}

} // namespace product_composition
