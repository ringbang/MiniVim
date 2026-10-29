#include "Motion.hpp"

#include <string>
#include <algorithm>

namespace sjtu {

namespace {

// word 字符分类: 字母/数字/下划线为 word 字符,
// 其他非空格字符构成标点 word, 空格分隔, 换行即行边界 (word 不跨行).
// 本项目只考虑可打印 ASCII, 不处理中文与 Tab 的 word 划分.
bool IsWordChar(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')
        || (value >= '0' && value <= '9') || value == '_';
}

//判断一个字符是否是空白分隔符; word/WORD 均只被空格与换行分开
bool IsSpaceChar(char value) {
    return value == ' ';
}

//判断一个字符是否为制表符
bool IsTabChar(char value) {
    return value == '\t';
}

} // namespace

MotionResult MotionEngine::Compute(const Buffer& buffer, Position origin,
                                   size_t curswant, MotionSpec spec) {
    //curswant 目前只被上下移动使用; hjkl 不经过本函数 (Window 直接调用 Move*),
    //其余 motion 的列计算不依赖 curswant. 参数先保留给后续扩展.
    (void)curswant;
    switch (spec.motion_) {
    case Motion::Left:
    case Motion::Down:
    case Motion::Up:
    case Motion::Right:
        //hjkl 由 Window::ApplyMotion 直接调用 MoveLeft/Right/Up/Down 完成
        //(这些函数已支持 count 与 curswant 钳制, 保持 Basic 路径不变);
        //operator 章节需要 hjkl 的范围计算时再补充此分支
        return {};
    case Motion::LineStart:
        return ComputeLineStart(buffer, origin);
    case Motion::FirstNonBlank:
        return ComputeFirstNonBlank(buffer, origin);
    case Motion::LineEnd:
        return ComputeLineEnd(buffer, origin, spec);
    case Motion::WordForward:
        return ComputeWordForward(buffer, origin, false, spec.count_.value_or(1));
    case Motion::WordBackward:
        return ComputeWordBackward(buffer, origin, false, spec.count_.value_or(1));
    case Motion::WordEnd:
        return ComputeWordEnd(buffer, origin, false, spec.count_.value_or(1));
    case Motion::WordBackwardEnd:
        return ComputeWordBackwardEnd(buffer, origin, false, spec.count_.value_or(1));
    case Motion::WordForwardCap:
        return ComputeWordForward(buffer, origin, true, spec.count_.value_or(1));
    case Motion::WordBackwardCap:
        return ComputeWordBackward(buffer, origin, true, spec.count_.value_or(1));
    case Motion::WordEndCap:
        return ComputeWordEnd(buffer, origin, true, spec.count_.value_or(1));
    case Motion::WordBackwardEndCap:
        return ComputeWordBackwardEnd(buffer, origin, true, spec.count_.value_or(1));
    case Motion::GotoLine:
        return ComputeGotoLine(buffer, origin, spec.count_);
    case Motion::GotoFirstLine:
        return ComputeGotoFirstLine(buffer, origin, spec.count_);
    }
    return {};
}

//0: 移动到当前行第一个字符 (第 0 列), 包括行首空格; 空行停在该行唯一位置 (第 0 列).
//不接受 count (parser 保证: 裸 0 生成无 count 的 LineStart, 已开始输入 count 时 0 并入 count).
//完成后 curswant 更新为到达位置的显示列 (SetToTarget).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeLineStart(const Buffer& buffer, Position origin) {
    MotionResult res;
    res.ok_ = true;
    res.target_ = origin;
    res.target_.column_ = 0;
    res.kind_ = RangeKind::CharacterwiseExclusive;
    res.curswant_ = CurswantPolicy::SetToTarget;
    return res;
}

//^: 移动到当前行第一个非空白字符
//当前行全部为空白字符时停在该行最后一个字符; 空行停在该行唯一位置.
//[count] 与其本身等价, 直接忽略 count. 完成后 curswant = 到达位置的显示列 (SetToTarget).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeFirstNonBlank(const Buffer& buffer, Position origin) {
    MotionResult res;
    res.ok_ = true;
    res.target_ = origin;
    res.curswant_ = CurswantPolicy::SetToTarget;
    res.kind_ = RangeKind::CharacterwiseExclusive;
    const std::string& line = buffer.GetLineAt(origin.row_);
    if (line.empty()) {
        res.target_.column_ = 0;
        return res;
    }
    else {
        for (size_t i = 0; i < line.length(); i++) {
            if (!IsSpaceChar(line[i]) && !IsTabChar(line[i])) {
                res.target_.column_ = i;
                return res;
            }
        }
    res.target_.column_ = line.length() - 1;
    return res;
    }
}

//$: 移动到当前行最后一个字符 (包括行尾空格); 空行停在该行唯一位置.
//curswant 设为极大值 (SetMax), 使之后 j/k 始终钳到行尾.
//[count]$: 先向下移动 count-1 行, 再移动到该行行末; 下方行数不足时停在最后一行行末.
//  若执行前已在文件最后一行且 count > 1, 命令失败: ok_ = false, 光标不动,
//  curswant 仍设为极大值 (SetMax).
//  注意 count=1 等价于不带 count; 只有 count > 1 才可能失败.
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeLineEnd(const Buffer& buffer, Position origin, MotionSpec spec) {
    MotionResult res;
    res.kind_ = RangeKind::CharacterwiseInclusive;
    res.curswant_ = CurswantPolicy::SetMax;
    res.target_ = origin;
    // 在行内移动
    if (!spec.count_ || spec.count_.value() == 1) {
        res.ok_ = true;
        const std::string& line = buffer.GetLineAt(origin.row_);
        if (line.empty()) {
            res.target_.column_ = 0;
        }
        else {
            res.target_.column_ = line.length() - 1;
        }
        return res;
    }
    else {
        // 已经在最后一行且 count > 1
        if (origin.row_ == buffer.GetLineCount() - 1) {
            res.ok_ = false;
            return res;
        }
        // 不在最后一行且 count > 1
        else {
            res.ok_ = true;
            size_t last_row = buffer.GetLineCount() - 1;
            size_t steps = spec.count_.value() - 1;
            res.target_.row_ = (steps >= last_row - origin.row_) ? last_row : origin.row_ + steps;
            const std::string& line = buffer.GetLineAt(res.target_.row_);
            if (line.empty()) {
                res.target_.column_ = 0;
            }
            else {
                res.target_.column_ = line.length() - 1;
            }
            return res;
        }
    }
}

//w: 移动到下一个可以作为 word 开头的位置; 没有这样的位置时移到文件最后一行的最后一个字符.
//  word = 连续英文字母/数字/下划线, 或连续的其他非空白字符; 空格和换行分隔; 不跨行.
//  空行算一个 word (停在该行唯一位置), 连续空行分别计算; 只含空格的行没有 word, 跳过.
//  count 表示连续执行 count 次; 已在边界时不再移动, count 再大也一样.
//  完成后 curswant = 终点显示列 (SetToTarget).
//W: 同 w, 但 WORD = 连续非空白字符, 不区分字母数字标点 (cap = true 走同一函数).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeWordForward(const Buffer& buffer, Position origin, bool cap, size_t count) {
    return {};
}

//b: 移动到上一个可以作为 word 开头的位置; 没有这样的位置时移到文件第一行的第一个字符.
//  空行算一个 word, 其余规则与 w 对称. count/curswant 规则同 w.
//B: 同 b 但按 WORD 划分 (cap = true).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeWordBackward(const Buffer& buffer, Position origin, bool cap, size_t count) {
    return {};
}

//e: 移动到下一个可以作为 word 结尾的位置; 没有这样的位置时移到文件最后一行的最后一个字符.
//  例外: e 跳过空行, 继续寻找后面的非空 word (w/b/ge 都会把空行当作 word 停下来).
//  count/curswant 规则同 w.
//E: 同 e 但按 WORD 划分 (cap = true).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeWordEnd(const Buffer& buffer, Position origin, bool cap, size_t count) {
    return {};
}

//ge: 移动到上一个可以作为 word 结尾的位置; 没有这样的位置时移到文件第一行的第一个字符.
//  空行算一个 word (停在该行唯一位置), 连续空行分别计算.
//  count/curswant 规则同 w.
//gE: 同 ge 但按 WORD 划分 (cap = true).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeWordBackwardEnd(const Buffer& buffer, Position origin, bool cap, size_t count) {
    return {};
}

//G: 无 count 跳到文件最后一行; [count]G 跳到第 count 行 (行号从 1 开始, 1G 是第一行).
//  行号超过文件总行数时都到文件最后一行.
//  到目标行后光标停在第一个非空白字符; 行内全为空格停在最后一个字符; 空行停在该行唯一位置;
//  即使目标行就是当前行也按上述规则重新定位. 完成后 curswant = 光标所在显示列 (SetToTarget).
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeGotoLine(const Buffer& buffer, Position origin, std::optional<size_t> count) {
    return {};
}

//gg: 跳到文件第一行; [count]gg 跳到文件第 count 行. 列定位与 curswant 规则同 G.
//TODO(光标的移动一): 实现
MotionResult MotionEngine::ComputeGotoFirstLine(const Buffer& buffer, Position origin, std::optional<size_t> count) {
    return {};
}

} // namespace sjtu
