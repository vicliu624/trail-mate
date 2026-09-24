#include "ui/screens/agenda/agenda_text_snapshot.h"
#include <cassert>
#include <string>

using ui::agenda::page::TextSnapshot;
template <std::size_t N>
std::string decode(const TextSnapshot<N>& value)
{
    std::string result;
    assert(value.forEachUtf8([&](const char* text)
                             { result += text; return true; }));
    return result;
}

int main()
{
    TextSnapshot<79> snapshot;
    assert(decode(snapshot).empty());
    const std::string boundary = "A\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    assert(snapshot.assign(boundary.c_str()));
    assert(decode(snapshot) == boundary);
    std::string maximum;
    for (unsigned i = 0; i < 79; ++i) maximum += "\xF0\x9F\x98\x80";
    assert(snapshot.assign(maximum.c_str()));
    assert(decode(snapshot) == maximum);
    assert(!snapshot.assign((maximum + "x").c_str()));
    assert(decode(snapshot) == maximum);
    const char* invalid[] = {nullptr, "\x80", "\xC0\x80", "\xC1\xBF", "\xF5\x80\x80\x80",
                             "\xE0\x80\x80", "\xF0\x80\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                             "\xC2", "\xE1\x80", "\xF1\x80\x80", "\xC2x"};
    for (const char* text : invalid)
    {
        assert(!snapshot.assign(text));
        assert(decode(snapshot) == maximum);
    }
    unsigned calls = 0;
    assert(!snapshot.forEachUtf8([&](const char*)
                                 { ++calls; return false; }));
    assert(calls == 1);
    assert(snapshot.assign(""));
    assert(decode(snapshot).empty());

    // Exercise every non-NUL Unicode scalar, independently encoding the input.
    TextSnapshot<1> scalar;
    for (uint32_t code = 1; code <= 0x10FFFF; ++code)
    {
        if (code >= 0xD800 && code <= 0xDFFF) continue;
        std::string text;
        if (code < 128) text += static_cast<char>(code);
        else
        {
            const unsigned count = code < 0x800 ? 2 : code < 0x10000 ? 3
                                                                     : 4;
            text += static_cast<char>((count == 2 ? 0xC0 : count == 3 ? 0xE0
                                                                      : 0xF0) |
                                      (code >> (6 * (count - 1))));
            for (unsigned i = count - 1; i; --i) text += static_cast<char>(0x80 | ((code >> (6 * (i - 1))) & 63));
        }
        assert(scalar.assign(text.c_str()));
        assert(decode(scalar) == text);
    }
}
