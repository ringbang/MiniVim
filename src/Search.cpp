#include "Search.hpp"

#include <algorithm>

namespace sjtu {

// / ? 提交搜索 (契约见 Search.hpp 的声明注释)
//1. pattern 非空: 保存 pattern_/forward_/has_pattern_ 并 RebuildMatches
//   (即使 matches_ 为空即"本次找不到", 也要保存, README:1105)
//2. pattern 为空: 无旧串直接失败; 有旧串复用 pattern_ 并更新 forward_ 为本次方向
//3. 在缓存中取循环序第 count 个匹配 (PickMatch, 取模定位); 无匹配返回 nullopt
std::optional<Position> SearchEngine::Commit(const Buffer& buffer, const std::string& pattern,
                                             bool forward, Position origin,
                                             std::optional<std::size_t> count) {
    if (!pattern.empty()) {
        pattern_ = pattern;
        has_pattern_ = true;
        RebuildMatches(buffer);
    }
    else if (!has_pattern_) {
        return std::nullopt;
    }
    else if (!matches_valid_) {
        RebuildMatches(buffer);
    }
    forward_ = forward;
    auto pos = PickMatch(origin, forward, count.value_or(1));
    return pos;
}

// n/N 重复搜索 (契约见 Search.hpp 的声明注释)
//TODO(光标的移动二): 实现
//1. 无旧串 (has_pattern_ == false) 直接失败
//2. matches_valid_ == false 时先 RebuildMatches
//3. direction = same_direction ? forward_ : !forward_
//4. PickMatch(origin, direction, count.value_or(1))
std::optional<Position> SearchEngine::Repeat(const Buffer& buffer, bool same_direction,
                                             std::optional<std::size_t> count, Position origin) {
    if (!has_pattern_) {
        return std::nullopt;
    }
    if (!matches_valid_) {
        RebuildMatches(buffer);
    }
    bool direction = same_direction ? forward_ : !forward_;
    auto pos = PickMatch(origin, direction, count.value_or(1));
    return pos;
}

void SearchEngine::Invalidate() {
    matches_valid_ = false;
}

bool SearchEngine::HasPattern() const {
    return has_pattern_;
}

// 全文件扫描 pattern 的全部匹配起点 (契约见 Search.hpp 的声明注释)
//TODO(光标的移动二): 实现
void SearchEngine::RebuildMatches(const Buffer& buffer) {
    //1. 清空 matches_; pattern_ 为空直接返回
    matches_.clear();
    if (pattern_.empty()) {
        matches_valid_ = true;
        return;
    }
    //2. 逐行 line.find(pattern_) 循环, 每找到一处记录 (row, col) 后只前进 1 个字符
    //   (重叠匹配), 直到 find 返回 npos; 自然得到文本序
    for (size_t i = 0; i < buffer.GetLineCount(); i++) {
        const std::string &line = buffer.GetLineAt(i);
        size_t col = 0;
        size_t found = line.find(pattern_, col);
        while (found != std::string::npos) {
            Position pos;
            pos.row_ = i;
            pos.column_ = found;
            col = pos.column_ + 1;
            matches_.push_back(pos);
            found = line.find(pattern_, col);
        }
    }
    //3. 完成后置 matches_valid_ = true
    matches_valid_ = true;
}

// 从 origin 出发按方向取循环序第 k 个匹配 (契约见 Search.hpp 的声明注释)
//TODO(光标的移动二): 实现
std::optional<Position> SearchEngine::PickMatch(Position origin, bool forward, std::size_t k) const {
    if (k == 0) {
        return std::nullopt;
    }
    //M = matches_.size(); M == 0 返回 nullopt
    size_t M = matches_.size();
    if (M == 0) {
        return std::nullopt;
    }
    size_t step = (k - 1) % M;
    //向前: std::upper_bound 找第一个 > origin 的匹配, step = (k-1) % M,
    //      返回 matches_[(first_after + step) % M]
    if (forward) {
        auto it = std::upper_bound(matches_.begin(), matches_.end(), origin);
        size_t idx = it - matches_.begin();
        return matches_[(idx + step) % M];
    }
    //向后: std::lower_bound 找第一个 >= origin 的匹配, step = (k-1) % M,
    //      返回 matches_[(first_ge - 1 + M - step) % M]  (注意 first_ge == 0 时从 M-1 绕行)
    else {
        auto it = std::lower_bound(matches_.begin(), matches_.end(), origin);
        size_t idx = it - matches_.begin();
        return matches_[(idx + M - 1 - step) % M];
    }
}

} // namespace sjtu
