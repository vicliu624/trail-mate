#pragma once

#include <stddef.h>

#include "ui/app_screen.h"

namespace ui
{

struct AppCatalog
{
    void* user_data = nullptr;
    size_t (*count)(void* user_data) = nullptr;
    AppScreen* (*at)(void* user_data, size_t index) = nullptr;
};

inline size_t catalogCount(const AppCatalog& catalog)
{
    return catalog.count ? catalog.count(catalog.user_data) : 0;
}

inline AppScreen* catalogAt(const AppCatalog& catalog, size_t index)
{
    return catalog.at ? catalog.at(catalog.user_data, index) : nullptr;
}

struct StaticAppCatalogState
{
    AppScreen** apps = nullptr;
    size_t count = 0;
};

inline size_t staticAppCatalogCount(void* user_data)
{
    auto* state = static_cast<StaticAppCatalogState*>(user_data);
    return state ? state->count : 0;
}

inline AppScreen* staticAppCatalogAt(void* user_data, size_t index)
{
    auto* state = static_cast<StaticAppCatalogState*>(user_data);
    if (!state || !state->apps || index >= state->count)
    {
        return nullptr;
    }
    return state->apps[index];
}

template <size_t N>
inline StaticAppCatalogState makeStaticAppCatalogState(AppScreen* (&apps)[N])
{
    StaticAppCatalogState state{};
    state.apps = apps;
    state.count = N;
    return state;
}

inline AppCatalog makeStaticAppCatalog(StaticAppCatalogState* state)
{
    AppCatalog catalog{};
    catalog.user_data = state;
    catalog.count = staticAppCatalogCount;
    catalog.at = staticAppCatalogAt;
    return catalog;
}

} // namespace ui
