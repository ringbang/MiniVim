/*
Types.hpp
这里放各模块共用的简单类型
注意字符下标和显示列不是一回事,一个Tab在Buffer中只有一个字符,在屏幕上可能占多列.
*/
#ifndef MINIVIM_TYPES_HPP
#define MINIVIM_TYPES_HPP

#include <cstddef>
#include <optional>

namespace sjtu {

struct Position {
    //Buffer 中的行号和字符下标; 插入模式的 column_ 可以等于该行长度
    size_t row_{0};
    size_t column_{0};

    constexpr bool operator==(const Position& other) const noexcept {
        return row_ == other.row_ && column_ == other.column_;
    }
    constexpr bool operator!=(const Position& other) const noexcept {
        return !(*this == other);
    }
};

struct ScreenSize {
    //终端的总行数和总列数, 包括底部用于显示命令或提示的那一行
    size_t rows_{0};
    size_t columns_{0};
};

struct Viewport {
    // top_ 是顶部可见的 Buffer 行号, left_ 是左侧可见的显示列
    // rows_ 和columns_ 是正文区域的高度和宽度, 不包含底部提示行
    size_t top_{0};
    size_t left_{0};
    size_t rows_{1};
    size_t columns_{1};
};

//分别表示普通模式、插入模式和冒号命令行模式
enum class Mode {
    Normal,
    Insert,
    CommandLine,
    Visual,
    VisualLine,  // 新增 Visual 模式
};

//Basic中支持的四个移动方向,可以由hjkl等按键生成
enum class Motion {
    Left,
    Down,
    Up,
    Right,
    WordForward, // w
    WordBackward, // b
    WordEnd, // e
    WordBackwardEnd, // ge
    WordForwardCap, // W
    WordBackwardCap, // B
    WordEndCap, // E
    WordBackwardEndCap, // gE
    LineStart, // 0
    FirstNonBlank, // ^
    LineEnd, // $
    GotoLine, // G
    GotoFirstLine, // gg
};

//motion 与其 count; parser 负责收集数字前缀并填入 count_
//count_ 为 nullopt 表示用户没有输入 count——注意这与 count=1 不同 (G 是最后一行, 1G 是第一行)
//G/gg 把 count 解释为行号参数, 其余 motion 解释为重复次数, 该解释由 MotionEngine 完成
struct MotionSpec {
    Motion motion_{Motion::Left};
    std::optional<size_t> count_{};
};

// 计算用三种类型
enum class RangeKind {
    CharacterwiseInclusive,  // 含终点字符
    CharacterwiseExclusive,  // 不含终点字符
    Linewise,                // 含整行
};

// 写入寄存器/粘贴时的类型
enum class RangeType { 
    Characterwise, 
    Linewise, 
};  

struct Range {
    Position begin_{};  // 文本序起点 (begin <= end)
    Position end_{};    // 文本序终点; CharacterwiseExclusive 时不含 end 所指字符
    RangeKind kind_{RangeKind::CharacterwiseExclusive};
    bool IsEmpty() const;
};

//空范围语义 (README "范围计算核心机制"):
// - Linewise: 永远包含整行, 不为空;
// - CharacterwiseExclusive: 起点终点重合为空 (如行首执行 h);
// - CharacterwiseInclusive: 起点终点重合时包含当前字符, 只有空行上才为空——
//   仅凭 Range 无法判断终点处是否有字符, 留到实现范围操作的章节由计算代码结合 Buffer 判定.
inline bool Range::IsEmpty() const {
    return kind_ == RangeKind::CharacterwiseExclusive && begin_ == end_;
}

} // namespace sjtu

#endif // MINIVIM_TYPES_HPP
