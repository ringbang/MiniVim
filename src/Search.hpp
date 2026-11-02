/*
Search.hpp
SearchEngine 负责 / ? n N 的字面串搜索, 由 Editor 持有一个实例:
  - 状态: 上次搜索串与方向 (n/N 复用; 提交空串时复用旧串);
  - 记忆化搜索: 为当前搜索串一次性缓存文件中全部匹配的起点 (按文本序),
    之后任意大的 [count]n/[count]N 都只用一次二分查找 + 取模定位,
    满足 README:1111 "禁止对超大 count 进行低效的数十亿次物理循环";
  - 缓存只依赖搜索串: 光标移动不影响; Buffer 被修改后 Editor 调用 Invalidate()
    丢弃缓存 (串与方向保留), 下次搜索时惰性重建. 所有修改 Buffer 的路径都要记得失效
    (目前是插入模式退出时, 后续 x/d/p 等编辑命令同理).

匹配规则 (README:1083):
  - 区分大小写、单行纯字面匹配, 不解析正则; 匹配允许重叠, 按起始位置排序;
  - 向前搜索从当前字符之后开始, 向后搜索从当前字符之前开始;
  - 循环序: 原光标位置 (及之前的匹配) 排在一周的最后; 全局无匹配则失败.
*/
#ifndef MINIVIM_SEARCH_HPP
#define MINIVIM_SEARCH_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Buffer.hpp"
#include "Types.hpp"

namespace sjtu {

class SearchEngine {
public:
    // / ? 提交搜索: pattern 非空时保存新串并重建缓存 (即使本次找不到也保存, README:1105);
    // pattern 为空时复用旧串、使用本次方向, 没有旧串则失败且不保存任何状态.
    // origin 是搜索起点 (输入期间光标静止, 即进入搜索框时的光标).
    // count 为 nullopt 时按 1 处理; [count]/ [count]? 取该方向循环序第 count 个匹配 (允许多次环绕, 取模定位).
    // 成功返回目标匹配的起点 (光标停在匹配的第一个字符), 失败返回 nullopt (光标与视口不动).
    std::optional<Position> Commit(const Buffer& buffer, const std::string& pattern,
                                   bool forward, Position origin,
                                   std::optional<std::size_t> count = std::nullopt);

    // n/N 重复搜索: same_direction = true 是 n (沿用上次方向), false 是 N (反方向);
    // count 为 nullopt 时按 1 处理; [count] 表示该方向上第 count 个匹配, 允许多次环绕.
    // 没有旧搜索串时失败. 成功返回目标匹配起点, 失败返回 nullopt (光标与视口不动).
    std::optional<Position> Repeat(const Buffer& buffer, bool same_direction,
                                   std::optional<std::size_t> count, Position origin);

    // Buffer 被修改后调用: 只丢弃匹配缓存, 搜索串与方向保留
    void Invalidate();
    // 是否已有可复用的旧搜索串 (n/N 与空串提交的前提)
    bool HasPattern() const;

private:
    // 全文件扫描 pattern 的全部匹配起点, 按 (row, column) 文本序填入 matches_
    // (行内用 find 且每次找到后只前进 1 个字符, 以支持重叠匹配; 空串直接得到空列表)
    void RebuildMatches(const Buffer& buffer);

    // 从 origin 出发按方向取循环序第 k 个匹配 (k >= 1); 无匹配返回 nullopt.
    // 向前: 二分查找第一个 > origin 的匹配, 循环序从它起绕行一周, 第 k 个 =
    //       matches_[(first_after + (k-1) % M) % M];
    // 向后: 二分查找第一个 >= origin 的匹配, 循环序从它前一个起逆序绕行, 第 k 个 =
    //       matches_[(first_ge - 1 + M - (k-1) % M) % M].
    // M = matches_.size(), M == 0 返回 nullopt; 不随 k 做物理循环.
    std::optional<Position> PickMatch(Position origin, bool forward, std::size_t k) const;

    std::string pattern_;     // 上次提交的非空搜索串
    bool forward_{true};      // 上次提交的方向: true = / 向前, false = ? 向后
    bool has_pattern_{false}; // 是否有旧串
    bool matches_valid_{false}; // false = 缓存缺失或已因 Buffer 修改而失效
    std::vector<Position> matches_; // 记忆化缓存: 当前串的全部匹配起点, 文本序
};

} // namespace sjtu

#endif // MINIVIM_SEARCH_HPP
