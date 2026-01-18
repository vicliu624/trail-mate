/**
 * @file pinyin_ime.h
 * @brief Lightweight pinyin IME engine (fixed dict, 8-char buffer, 50 candidates)
 */

#pragma once

#include <string>
#include <vector>

namespace ui {
namespace widgets {

class PinyinIme {
public:
    void setEnabled(bool enabled);
    bool isEnabled() const;

    void reset();
    bool hasBuffer() const;
    const std::string& buffer() const;
    const std::vector<std::string>& candidates() const;
    int candidateIndex() const;

    bool appendLetter(char c);
    bool backspace();
    bool moveCandidate(int delta);

    bool commitCandidate(int index, std::string& out);
    bool commitActive(std::string& out);

private:
    void updateCandidates();
    void updateCandidatesFromBuiltin();

    bool enabled_ = false;
    std::string buffer_;
    std::vector<std::string> candidates_;
    int candidate_index_ = 0;

    static constexpr size_t kMaxBufferLen = 8;
};

} // namespace widgets
} // namespace ui
