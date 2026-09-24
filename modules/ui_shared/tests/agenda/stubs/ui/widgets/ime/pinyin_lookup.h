#pragma once
#include <cstring>
namespace ui::widgets::ime
{
// Tiny deterministic dictionary fixture; production composition/rendering run.
template <typename Add>
void collectPinyinCandidates(const char* text, Add add)
{
    if (std::strcmp(text, "ni") == 0)
    {
        add("\xE4\xBD\xA0");
        add("\xE6\xB3\xA5");
    }
}
} // namespace ui::widgets::ime
