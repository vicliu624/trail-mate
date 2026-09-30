#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace gps::gpx
{
// Reads attributes of one opening tag, not arbitrary text or nested tags.
inline bool readDoubleAttribute(std::string_view tag, std::string_view key, double& out)
{
    const auto space = [](char c)
    { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    std::size_t p = tag.find('<');
    if (p == std::string_view::npos || ++p == tag.size() || tag[p] == '/' || tag[p] == '!' || tag[p] == '?')
        return false;
    while (p < tag.size() && !space(tag[p]) && tag[p] != '>' && tag[p] != '/')
        ++p;
    bool found = false;
    double value = 0;
    while (p < tag.size())
    {
        while (p < tag.size() && space(tag[p])) ++p;
        if (p == tag.size()) return false;
        if (tag[p] == '>' || (tag[p] == '/' && p + 1 < tag.size() && tag[p + 1] == '>'))
        {
            if (found) out = value;
            return found;
        }
        const auto start = p;
        while (p < tag.size() && !space(tag[p]) && tag[p] != '=' && tag[p] != '>') ++p;
        const auto name = tag.substr(start, p - start);
        if (name.empty()) return false;
        while (p < tag.size() && space(tag[p])) ++p;
        if (p == tag.size() || tag[p++] != '=') return false;
        while (p < tag.size() && space(tag[p])) ++p;
        if (p == tag.size() || (tag[p] != '\'' && tag[p] != '"')) return false;
        const char quote = tag[p++];
        const auto begin = p;
        while (p < tag.size() && tag[p] != quote && tag[p] != '<') ++p;
        if (p == tag.size() || tag[p] != quote) return false;
        if (name == key)
        {
            if (found || p == begin || p - begin > 64) return false;
            const std::string text(tag.substr(begin, p - begin));
            char* end = nullptr;
            value = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || !std::isfinite(value)) return false;
            while (*end && space(*end)) ++end;
            if (*end) return false;
            found = true;
        }
        ++p;
        if (p < tag.size() && !space(tag[p]) && tag[p] != '/' && tag[p] != '>') return false;
    }
    return false;
}
} // namespace gps::gpx
