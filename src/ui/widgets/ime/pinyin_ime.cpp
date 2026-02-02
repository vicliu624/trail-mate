/**
 * @file pinyin_ime.cpp
 * @brief Lightweight pinyin IME engine (fixed dict, 8-char buffer, 50 candidates)
 */

#include "pinyin_ime.h"
#include "pinyin_data.h"

#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <pgmspace.h>

namespace ui
{
namespace widgets
{

static void trim_in_place(std::string& s)
{
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
    {
        ++start;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
    {
        --end;
    }
    if (start == 0 && end == s.size())
    {
        return;
    }
    s = s.substr(start, end - start);
}

static void append_candidate_unique(std::vector<std::string>& list, const std::string& cand)
{
    if (cand.empty()) return;
    if (std::find(list.begin(), list.end(), cand) != list.end())
    {
        return;
    }
    list.push_back(cand);
}

static void append_candidates(std::vector<std::string>& list, const std::string& candidates, size_t max_count)
{
    size_t p = 0;
    while (p < candidates.size() && list.size() < max_count)
    {
        while (p < candidates.size() && candidates[p] == ' ')
        {
            ++p;
        }
        size_t start = p;
        while (p < candidates.size() && candidates[p] != ' ')
        {
            ++p;
        }
        if (p > start)
        {
            append_candidate_unique(list, candidates.substr(start, p - start));
        }
    }
}

void PinyinIme::setEnabled(bool enabled)
{
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_)
    {
        reset();
    }
}

bool PinyinIme::isEnabled() const
{
    return enabled_;
}

void PinyinIme::reset()
{
    buffer_.clear();
    candidates_.clear();
    candidate_index_ = 0;
}

bool PinyinIme::hasBuffer() const
{
    return !buffer_.empty();
}

const std::string& PinyinIme::buffer() const
{
    return buffer_;
}

const std::vector<std::string>& PinyinIme::candidates() const
{
    return candidates_;
}

int PinyinIme::candidateIndex() const
{
    return candidate_index_;
}

bool PinyinIme::appendLetter(char c)
{
    if (!enabled_) return false;
    if (buffer_.size() >= kMaxBufferLen) return false;
    if (!(c >= 'a' && c <= 'z')) return false;
    buffer_.push_back(c);
    updateCandidates();
    return true;
}

bool PinyinIme::backspace()
{
    if (!enabled_) return false;
    if (buffer_.empty()) return false;
    buffer_.pop_back();
    updateCandidates();
    return true;
}

bool PinyinIme::moveCandidate(int delta)
{
    if (!enabled_) return false;
    if (candidates_.empty()) return false;
    int size = static_cast<int>(candidates_.size());
    candidate_index_ = (candidate_index_ + delta) % size;
    if (candidate_index_ < 0)
    {
        candidate_index_ += size;
    }
    return true;
}

bool PinyinIme::commitCandidate(int index, std::string& out)
{
    if (!enabled_) return false;
    if (buffer_.empty()) return false;

    if (index >= 0 && index < static_cast<int>(candidates_.size()))
    {
        out = candidates_[index];
    }
    else if (!candidates_.empty())
    {
        out = candidates_[0];
    }
    else
    {
        out = buffer_;
    }
    reset();
    return true;
}

bool PinyinIme::commitActive(std::string& out)
{
    return commitCandidate(candidate_index_, out);
}

void PinyinIme::updateCandidates()
{
    candidates_.clear();
    candidate_index_ = 0;
    if (!enabled_) return;
    if (buffer_.empty()) return;
    updateCandidatesFromBuiltin();
}

void PinyinIme::updateCandidatesFromBuiltin()
{
    static constexpr size_t kMaxCandidates = 50;
    std::vector<std::string> exact_candidates;
    std::vector<std::string> prefix_candidates;

    std::string line;
    const char* ptr = kPinyinDict;
    while (true)
    {
        char c = static_cast<char>(pgm_read_byte(ptr++));
        if (c == '\0')
        {
            if (!line.empty())
            {
                std::string tmp = line;
                trim_in_place(tmp);
                if (!tmp.empty() && tmp[0] != '#')
                {
                    size_t tab_pos = tmp.find('\t');
                    size_t split_pos = (tab_pos != std::string::npos) ? tab_pos : tmp.find(' ');
                    if (split_pos != std::string::npos && split_pos > 0)
                    {
                        std::string pinyin = tmp.substr(0, split_pos);
                        std::string candidates = tmp.substr(split_pos + 1);
                        trim_in_place(candidates);
                        if (!candidates.empty())
                        {
                            if (pinyin == buffer_)
                            {
                                append_candidates(exact_candidates, candidates, kMaxCandidates);
                            }
                            else if (pinyin.rfind(buffer_, 0) == 0)
                            {
                                append_candidates(prefix_candidates, candidates, kMaxCandidates);
                            }
                        }
                    }
                }
            }
            break;
        }
        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            if (!line.empty())
            {
                std::string tmp = line;
                trim_in_place(tmp);
                if (!tmp.empty() && tmp[0] != '#')
                {
                    size_t tab_pos = tmp.find('\t');
                    size_t split_pos = (tab_pos != std::string::npos) ? tab_pos : tmp.find(' ');
                    if (split_pos != std::string::npos && split_pos > 0)
                    {
                        std::string pinyin = tmp.substr(0, split_pos);
                        std::string candidates = tmp.substr(split_pos + 1);
                        trim_in_place(candidates);
                        if (!candidates.empty())
                        {
                            if (pinyin == buffer_)
                            {
                                append_candidates(exact_candidates, candidates, kMaxCandidates);
                            }
                            else if (pinyin.rfind(buffer_, 0) == 0)
                            {
                                append_candidates(prefix_candidates, candidates, kMaxCandidates);
                            }
                        }
                    }
                }
            }
            line.clear();
            continue;
        }
        line.push_back(c);
    }

    for (const auto& cand : exact_candidates)
    {
        if (candidates_.size() >= kMaxCandidates) break;
        candidates_.push_back(cand);
    }
    for (const auto& cand : prefix_candidates)
    {
        if (candidates_.size() >= kMaxCandidates) break;
        if (std::find(candidates_.begin(), candidates_.end(), cand) == candidates_.end())
        {
            candidates_.push_back(cand);
        }
    }
}

} // namespace widgets
} // namespace ui
