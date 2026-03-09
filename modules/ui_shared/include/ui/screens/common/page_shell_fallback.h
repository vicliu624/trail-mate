#pragma once

#include "ui/screens/common/placeholder_page.h"

namespace ui::page_shell_fallback
{

inline placeholder_page::State make_unavailable_state(const char* title, const char* body)
{
    placeholder_page::State state{};
    state.title = title;
    state.body = body;
    return state;
}

template <typename Host, typename EnterFn>
inline void enter(placeholder_page::State& state,
                  void* user_data,
                  lv_obj_t* parent,
                  bool runtime_available,
                  EnterFn&& enter_fn)
{
    const Host* host = static_cast<const Host*>(user_data);
    if (!runtime_available)
    {
        state.host = host;
        placeholder_page::show(state, parent);
        return;
    }

    enter_fn(host, parent);
}

template <typename ExitFn>
inline void exit(placeholder_page::State& state,
                 lv_obj_t* parent,
                 bool runtime_available,
                 ExitFn&& exit_fn)
{
    if (!runtime_available)
    {
        placeholder_page::hide(state);
        return;
    }

    exit_fn(parent);
}

} // namespace ui::page_shell_fallback
