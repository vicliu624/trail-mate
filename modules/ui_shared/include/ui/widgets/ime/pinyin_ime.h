/**
 * @file pinyin_ime.h
 * @brief Lightweight pinyin IME engine (fixed dict, 8-char buffer, 50 candidates)
 */

#pragma once

#include <string>
#include <vector>

namespace ui
{
namespace widgets
{

class PinyinIme
{
  public:
#if defined(GAT562_NO_PINYIN_IME) && GAT562_NO_PINYIN_IME
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return false; }

    void reset()
    {
        buffer_.clear();
        candidates_.clear();
        candidate_index_ = 0;
    }
    bool hasBuffer() const { return false; }
    const std::string& buffer() const { return buffer_; }
    const std::vector<std::string>& candidates() const { return candidates_; }
    int candidateIndex() const { return 0; }

    bool appendLetter(char c)
    {
        (void)c;
        return false;
    }
    bool backspace() { return false; }
    bool moveCandidate(int delta)
    {
        (void)delta;
        return false;
    }

    bool commitCandidate(int index, std::string& out)
    {
        (void)index;
        out.clear();
        return false;
    }
    bool commitActive(std::string& out)
    {
        out.clear();
        return false;
    }
#else
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
#endif

  private:
#if !(defined(GAT562_NO_PINYIN_IME) && GAT562_NO_PINYIN_IME)
    void updateCandidates();
    void updateCandidatesFromBuiltin();
#endif

    bool enabled_ = false;
    std::string buffer_;
    std::vector<std::string> candidates_;
    int candidate_index_ = 0;

    static constexpr size_t kMaxBufferLen = 8;
};

} // namespace widgets
} // namespace ui
