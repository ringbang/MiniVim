/*
Edit.hpp
EditEngine 是文本修正命令的纯计算层 (与 MotionEngine 对应): 只读 Buffer, 不修改任何状态.
  - 范围计算: 把 motion 转成按文本序排列的 Range, 供 d/c 使用;
    MotionEngine 为每个 motion 计算的 kind_ (RangeKind) 就是该 motion 的范围类型,
    这正是 MotionResult 里设计 kind_ 字段的目的.
  - 光标放置: 各删除/替换/合并命令执行后的光标位置规则 (README 各条).
所有函数都是静态纯函数; 需要"删除后的 Buffer"的地方请传入已删除后的 Buffer.
*/
#ifndef MINIVIM_EDIT_HPP
#define MINIVIM_EDIT_HPP

#include <cstddef>
#include <optional>

#include "Buffer.hpp"
#include "Types.hpp"

namespace sjtu {

class EditEngine {
public:
    // motion → 按文本序排列的 Range (begin <= end, 按 (row, column) 文本序).
    // motion 失败 (MotionResult.ok_ == false) 返回 nullopt → 整条 d/c 命令取消:
    // 不修改文本/寄存器, 光标与视口不动, c 不进入 Insert (README:897).
    static std::optional<Range> ComputeRange(const Buffer& buffer, Position origin,
                                             size_t curswant, MotionSpec spec);

    // d 的自动转 linewise (README:1475): 当动作不面向行 (charwise)、起止不在同一行、
    // 起始位置之前只有空白 (空格/Tab)、且结束位置之后没有非空白字符时, 范围转为 Linewise
    // (begin 行到 end 行的完整行). 只有 d 需要调用, c 不做该转换.
    static Range NormalizeDeleteRange(const Buffer& buffer, Range range);

    // characterwise 删除后的光标 (README:1467): 停在 range 起点;
    // 若起点之后已无字符 (删到了行末) 则停该行最后一个字符; 该行成为空行则停唯一合法位置.
    // 注意传入的是删除完成后的 Buffer.
    static Position CursorAfterCharacterwiseDelete(const Buffer& buffer_after, const Range& range);

    // linewise 删除后的光标 (README:1469): 原范围下方仍有行 → 该行第一个非空白字符;
    // 删到文件末尾 → 新的最后一行的第一个非空白字符; 全部行被删 (Buffer 只剩一个空行) → 唯一合法位置.
    // 注意传入的是删除完成后的 Buffer (此时 range.begin_.row_ 就是"原范围起点行").
    static Position CursorAfterLinewiseDelete(const Buffer& buffer_after, const Range& range);

    // [count]x 删除后的光标 (README:1441-1447): 未删到行尾 → 保留原字节列;
    // 删到行末 → 新行末字符; 行被删成空行 → 唯一合法位置.
    // count 是"实际删除的字符数" (已按行内剩余字符数钳制).
    static Position CursorAfterX(const Buffer& buffer_after, Position origin, size_t count);

    // [count]X 删除后的光标 (README:1459): 向左移动实际删除的字符数, 停在原 cursor 所指字符上.
    static Position CursorAfterXBack(const Buffer& buffer_after, Position origin, size_t count);

    // J 合并后的光标 (README:1533): 定位在"最后一次拼接之前累计结果的字节长度"处,
    // 即最后一次新增分隔空格或新接入内容的开头; 超出合法范围钳到行末; 结果为空停唯一位置.
    // length_before_last_join 是调用方在最后一次 JoinForJ 之前记录的累计结果长度.
    static Position CursorAfterJoin(const Buffer& buffer_after, size_t length_before_last_join);

    // [count]~ 后的光标 (README:1545): 停在处理范围之后的字符;
    // 已处理到行末则停本行最后一个字符. count 是实际处理的字符数.
    static Position CursorAfterFlip(const Buffer& buffer_after, Position origin, size_t count);
};

} // namespace sjtu

#endif // MINIVIM_EDIT_HPP
