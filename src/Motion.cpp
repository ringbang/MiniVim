#include "Motion.hpp"

#include <string>
#include <algorithm>
#include <vector>

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

//光标所在位置的类别:
//  0 = 空白分隔符 (空格 + Tab; 与 vim 一致——README 不要求处理 Tab 的 word 划分,
//      OJ 实测参考实现把 Tab 当空白: 含 Tab 的"空白行"对词移动没有 word)
//  1 = word 字符 (字母/数字/下划线); WORD 模式下 = 任意非空白字符
//  2 = 标点字符 (仅 word 模式); 同类连续段是一个 word
//  3 = 空行的唯一位置 (空行对 w/b/ge 是一个 word, 对 e 跳过)
int Classify(const Buffer& buffer, const Position& pos, bool cap) {
    const std::string& line = buffer.GetLineAt(pos.row_);
    if (line.empty()) return 3;
    char c = line[pos.column_];
    if (c == ' ' || c == '\t') return 0;
    if (cap) return 1;
    return IsWordChar(c) ? 1 : 2;
}

//前进一个位置; 到达文件末尾返回 false. 从行尾前进到下一行行首 (空行即其唯一位置).
bool StepForward(const Buffer& buffer, Position& pos) {
    if (pos.column_ + 1 < buffer.GetLineAt(pos.row_).size()) {
        ++pos.column_;
        return true;
    }
    if (pos.row_ + 1 < buffer.GetLineCount()) {
        ++pos.row_;
        pos.column_ = 0;
        return true;
    }
    return false;
}

//后退一个位置; 到达文件开头返回 false. 从行首后退到上一行最后一个字符 (上一行为空行时停其唯一位置).
bool StepBackward(const Buffer& buffer, Position& pos) {
    if (pos.column_ > 0) {
        --pos.column_;
        return true;
    }
    if (pos.row_ > 0) {
        --pos.row_;
        pos.column_ = buffer.GetLineAt(pos.row_).empty() ? 0 : buffer.GetLineAt(pos.row_).size() - 1;
        return true;
    }
    return false;
}

//行内前进/后退一个字符: 同类字符段的扫描不能跨行 (word 不跨行), 与空格扫描区分开
bool StepForwardInLine(const Buffer& buffer, Position& pos) {
    if (pos.column_ + 1 < buffer.GetLineAt(pos.row_).size()) {
        ++pos.column_;
        return true;
    }
    return false;
}

bool StepBackwardInLine(const Buffer& buffer, Position& pos) {
    if (pos.column_ > 0) {
        --pos.column_;
        return true;
    }
    return false;
}

//文件最后一个合法位置 (末行末字符; 末行为空行时是其唯一位置)
Position LastPosition(const Buffer& buffer) {
    size_t row = buffer.GetLineCount() - 1;
    return {row, buffer.GetLineAt(row).empty() ? 0 : buffer.GetLineAt(row).size() - 1};
}

MotionResult MakeResult(Position target, RangeKind kind) {
    MotionResult res;
    res.ok_ = true;
    res.target_ = target;
    res.kind_ = kind;
    res.curswant_ = CurswantPolicy::SetToTarget;
    return res;
}

//w/W 单步: 返回下一个 word/WORD 开头; 没有下一个 word 时返回文件最后位置; 已在边界时返回原位置.
//  1. 光标在 word 上时先跳到当前 word 的末字符 (不跨行);
//  2. 向前越过空格 (可跨行, 只含空格的行整体跳过; 空行是一个 word, 停下来);
//  3. 落在第一个非空格位置 (word 开头或空行唯一位置).
Position NextWordStart(const Buffer& buffer, Position pos, bool cap) {
    int cls = Classify(buffer, pos, cap);
    if (cls == 1 || cls == 2) {
        Position next = pos;
        while (StepForwardInLine(buffer, next) && Classify(buffer, next, cap) == cls) {
            pos = next;
        }
    }
    Position next = pos;
    if (StepForward(buffer, next)) {
        while (Classify(buffer, next, cap) == 0) {
            if (!StepForward(buffer, next)) break;
        }
        pos = next;
    }
    return pos;
}

//e/E 单步: 返回下一个 word/WORD 结尾; 没有时返回文件最后位置; 已在边界时返回原位置.
//  光标在 word 内部时跳到当前 word 的末字符 (不跨行);
//  否则向前找下一个 word: 跳过空格与空行 (e 跳过空行), 跳到下一个 word 的末字符.
Position NextWordEnd(const Buffer& buffer, Position pos, bool cap) {
    int cls = Classify(buffer, pos, cap);
    if (cls == 1 || cls == 2) {
        Position next = pos;
        bool moved = false;
        while (StepForwardInLine(buffer, next) && Classify(buffer, next, cap) == cls) {
            pos = next;
            moved = true;
        }
        if (moved) return pos;
    }
    Position next = pos;
    while (StepForward(buffer, next)) {
        int ncls = Classify(buffer, next, cap);
        if (ncls == 0 || ncls == 3) {
            pos = next; // 空格与空行都跳过
            continue;
        }
        // 找到下一个 word, 跳到它的末字符
        pos = next;
        while (StepForwardInLine(buffer, next) && Classify(buffer, next, cap) == ncls) {
            pos = next;
        }
        return pos;
    }
    return pos;
}

//b/B 单步: 返回上一个 word/WORD 开头; 没有时返回文件第一行第一个字符; 已在边界时返回原位置.
//  光标在 word 内部时先跳到当前 word 的首字符 (不跨行);
//  否则向后找上一个 word: 跳过空格 (可跨行), 遇空行停其唯一位置, 回溯上一个 word 到其首字符.
Position PrevWordStart(const Buffer& buffer, Position pos, bool cap) {
    int cls = Classify(buffer, pos, cap);
    if (cls == 1 || cls == 2) {
        Position prev = pos;
        bool moved = false;
        while (StepBackwardInLine(buffer, prev) && Classify(buffer, prev, cap) == cls) {
            pos = prev;
            moved = true;
        }
        if (moved) return pos;
    }
    Position prev = pos;
    while (StepBackward(buffer, prev)) {
        int pcls = Classify(buffer, prev, cap);
        if (pcls == 0) {
            pos = prev; // 跳过空格
            continue;
        }
        if (pcls == 3) return prev; // 空行是一个 word, 停其唯一位置
        // 上一个 word 的末字符, 回溯到它的首字符
        pos = prev;
        while (StepBackwardInLine(buffer, prev) && Classify(buffer, prev, cap) == pcls) {
            pos = prev;
        }
        return pos;
    }
    return pos;
}

//ge/gE 单步: 返回上一个 word/WORD 结尾; 没有时返回文件第一行第一个字符; 已在边界时返回原位置.
//  与 vim 一致: 光标在 word 的任意位置 (开头/中间/结尾) 时都先整个跳过当前 word (不跨行),
//  再向后找上一个 word 的结尾: 跳过空格 (可跨行), 遇空行停其唯一位置 (README: 空行是 word),
//  遇到的第一个非空格字符就是上一个 word 的末字符.
Position PrevWordEnd(const Buffer& buffer, Position pos, bool cap) {
    int cls = Classify(buffer, pos, cap);
    if (cls == 1 || cls == 2) {
        // 整个跳过当前 word, 回退到它的首字符 (无论光标在 word 的哪个位置)
        Position prev = pos;
        while (StepBackwardInLine(buffer, prev) && Classify(buffer, prev, cap) == cls) {
            pos = prev;
        }
    }
    Position prev = pos;
    while (StepBackward(buffer, prev)) {
        int pcls = Classify(buffer, prev, cap);
        if (pcls == 0) {
            pos = prev; // 跳过空格
            continue;
        }
        if (pcls == 3) return prev; // 空行 word 的结尾 = 唯一位置
        return prev;                // 上一个 word 的末字符
    }
    return pos;
}

// ---- 光标的移动(二): 括号匹配的辅助函数 (定义见文件末尾) ----

//pos 前紧邻奇数个连续反斜杠 (同一行内) → 该字符被转义 (Vim 奇偶规则, 测试点 88);
//引号翻转与括号配对都用它, 串内串外一致
bool EscapedBefore(const Buffer& buffer, Position pos);

//在 origin 所在行从 origin.column_ 起 (含) 向右找第一个 ()[]{} 括号字符; 没有返回 nullopt.
//按 README 字面理解: 候选选择不区分引号与转义, 只找括号字符.
std::optional<Position> FindBracketCandidate(const Buffer& buffer, Position origin);

//从 start 出发找配对括号 (Vim findmatchlimit / search2141.c 在线扫描的忠实移植, 测试点 88):
//  候选由 FindBracketCandidate 按字面选出; 候选前紧邻反斜杠的奇偶记为 match_escaped,
//  配对括号必须与其奇偶一致 (被转义的候选只配被转义的括号);
//  引号: 每行第一次访问时统计引号数 (跳过 \X 与 '"'), 偶数个才启用引号处理 (do_quotes),
//  奇数个时该行引号全部忽略, 除非本行或上一行以反斜杠结尾 (续行);
//  扫描中维护 inquote: 前导反斜杠为偶数的 " 翻转; 行尾不是反斜杠时重置;
//  括号接受条件: (!inquote || start_in_quotes) 且转义奇偶 == match_escaped;
//  单引号: 跳过 'x' 与 '\x' 单字符字面量 (向前向后对称).
//其他种类括号不影响深度; 跨行扫描; 找不到返回 nullopt. 起始括号自身是 depth 种子.
std::optional<Position> ScanBracketMatch(const Buffer& buffer, Position start,
                                         char open, char close, bool forward);

//行跳转命令 (G/gg/[count]%) 共用的目标行列定位: 第一个非空白字符;
//行内全为空白 (空格+Tab, 与 ^ 的实测结论一致) 停在最后一个字符; 空行停在该行唯一位置.
//G/gg 现在直接调用 ComputeFirstNonBlank 完成了同样的事情, 建议实现本函数后顺手把 G/gg 改为复用它.
Position LineJumpTarget(const Buffer& buffer, std::size_t row);

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
    case Motion::MatchPercent:
        return ComputeMatchPercent(buffer, origin);
    case Motion::PercentLine:
        return ComputePercentLine(buffer, origin, spec.count_);
    }
    return {};
}

//0: 移动到当前行第一个字符 (第 0 列), 包括行首空格; 空行停在该行唯一位置 (第 0 列).
//不接受 count (parser 保证: 裸 0 生成无 count 的 LineStart, 已开始输入 count 时 0 并入 count).
//完成后 curswant 更新为到达位置的显示列 (SetToTarget).
MotionResult MotionEngine::ComputeLineStart(const Buffer& buffer, Position origin) {
    MotionResult res;
    res.ok_ = true;
    res.target_ = origin;
    res.target_.column_ = 0;
    res.kind_ = RangeKind::CharacterwiseExclusive;
    res.curswant_ = CurswantPolicy::SetToTarget;
    return res;
}

//^: 移动到当前行第一个非空白字符.
//当前行全部为空白字符时停在该行最后一个字符; 空行停在该行唯一位置.
//[count] 与其本身等价, 直接忽略 count. 完成后 curswant = 到达位置的显示列 (SetToTarget).
//空白 = 空格 + Tab (与真 vim 一致). README:1045 注明"空白不含 Tab"与 OJ 参考实现不符,
//OJ 评测实测只有把 Tab 视为空白才能通过 ^ 的全部测试点.
//若全空白行的最后字符是 Tab, 光标画在 Tab 末格 (Renderer 的 CursorCellColumn 已处理).
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
MotionResult MotionEngine::ComputeWordForward(const Buffer& buffer, Position origin, bool cap, size_t count) {
    Position pos = origin;
    for (size_t i = 0; i < count; ++i) {
        Position next = NextWordStart(buffer, pos, cap);
        if (next == pos) break; // 已到边界, count 再大也不再移动
        pos = next;
    }
    return MakeResult(pos, RangeKind::CharacterwiseExclusive);
}

//b: 移动到上一个可以作为 word 开头的位置; 没有这样的位置时移到文件第一行的第一个字符.
//  空行算一个 word, 其余规则与 w 对称. count/curswant 规则同 w.
//B: 同 b 但按 WORD 划分 (cap = true).
MotionResult MotionEngine::ComputeWordBackward(const Buffer& buffer, Position origin, bool cap, size_t count) {
    Position pos = origin;
    for (size_t i = 0; i < count; ++i) {
        Position next = PrevWordStart(buffer, pos, cap);
        if (next == pos) break; // 已到边界, count 再大也不再移动
        pos = next;
    }
    return MakeResult(pos, RangeKind::CharacterwiseExclusive);
}

//e: 移动到下一个可以作为 word 结尾的位置; 没有这样的位置时移到文件最后一行的最后一个字符.
//  例外: e 跳过空行, 继续寻找后面的非空 word (w/b/ge 都会把空行当作 word 停下来).
//  count/curswant 规则同 w.
//E: 同 e 但按 WORD 划分 (cap = true).
MotionResult MotionEngine::ComputeWordEnd(const Buffer& buffer, Position origin, bool cap, size_t count) {
    Position pos = origin;
    for (size_t i = 0; i < count; ++i) {
        Position next = NextWordEnd(buffer, pos, cap);
        if (next == pos) break; // 已到边界, count 再大也不再移动
        pos = next;
    }
    return MakeResult(pos, RangeKind::CharacterwiseInclusive);
}

//ge: 移动到上一个可以作为 word 结尾的位置; 没有这样的位置时移到文件第一行的第一个字符.
//  空行算一个 word (停在该行唯一位置), 连续空行分别计算.
//  count/curswant 规则同 w.
//gE: 同 ge 但按 WORD 划分 (cap = true).
MotionResult MotionEngine::ComputeWordBackwardEnd(const Buffer& buffer, Position origin, bool cap, size_t count) {
    Position pos = origin;
    for (size_t i = 0; i < count; ++i) {
        Position next = PrevWordEnd(buffer, pos, cap);
        if (next == pos) break; // 已到边界, count 再大也不再移动
        pos = next;
    }
    return MakeResult(pos, RangeKind::CharacterwiseInclusive);
}

//G: 无 count 跳到文件最后一行; [count]G 跳到第 count 行 (行号从 1 开始, 1G 是第一行).
//  行号超过文件总行数时都到文件最后一行.
//  到目标行后光标停在第一个非空白字符; 行内全为空白停在最后一个字符; 空行停在该行唯一位置;
//  即使目标行就是当前行也按上述规则重新定位. 完成后 curswant = 光标所在显示列 (SetToTarget).
//  列定位与 gg/[count]% 共用 LineJumpTarget ("非空白"按空格+Tab, 与 ^ 的 OJ 实测结论一致).
MotionResult MotionEngine::ComputeGotoLine(const Buffer& buffer, Position origin, std::optional<size_t> count) {
    MotionResult res;
    res.ok_ = true;
    res.kind_ = RangeKind::Linewise;
    res.curswant_ = CurswantPolicy::SetToTarget;
    // 无 count 时跳到文件最后一行
    if (!count) {
        res.target_.row_ = buffer.GetLineCount() - 1;
    }
    // 有 count 时跳到文件第 count 行
    else {
        size_t row = std::min<size_t>(buffer.GetLineCount() - 1, count.value() - 1);
        res.target_.row_ = row;
    }
    res.target_ = LineJumpTarget(buffer, res.target_.row_);
    return res;
}

//gg: 跳到文件第一行; [count]gg 跳到文件第 count 行. 列定位与 curswant 规则同 G.
MotionResult MotionEngine::ComputeGotoFirstLine(const Buffer& buffer, Position origin, std::optional<size_t> count) {
    MotionResult res;
    res.ok_ = true;
    res.kind_ = RangeKind::Linewise;
    res.curswant_ = CurswantPolicy::SetToTarget;
    // 无 count 时跳到文件第 1 行
    if (!count) {
        res.target_.row_ = 0;
    }
    // 有 count 时跳到文件第 count 行
    else {
        size_t row = std::min<size_t>(buffer.GetLineCount() - 1, count.value() - 1);
        res.target_.row_ = row;
    }
    res.target_ = LineJumpTarget(buffer, res.target_.row_);
    return res;
}

//% (无 count): 当前行从光标 (含) 向右找第一个 ()[]{} (FindBracketCandidate), 再跳到其配对括号.
//配对扫描跨行, 用同种括号的嵌套深度确定配对, 其他种类括号不影响深度 (README:1117).
//候选没有配对时命令失败 (ok_ = false, 光标与视口不动), 不继续尝试下一个候选 (README:1127).
//成功: 光标停在配对括号上, curswant = 其显示列 (SetToTarget), kind = CharacterwiseInclusive.
//引号/反斜杠/单引号字面量规则由 ScanBracketMatch 在线处理 (Vim findmatchlimit, 测试点 88).
MotionResult MotionEngine::ComputeMatchPercent(const Buffer& buffer, Position origin) {
    MotionResult res;
    res.target_ = origin;
    //1. 当前行从光标 (含) 向右找第一个括号; 没有候选 → 失败 (README:1127)
    std::optional<Position> candidate = FindBracketCandidate(buffer, origin);
    if (!candidate) {
        res.ok_ = false;
        return res;
    }
    //2. 按候选类型确定配对方向: 开括号向右扫, 闭括号向左扫
    char c = buffer.GetLineAt(candidate->row_)[candidate->column_];
    char open = 0;
    char close = 0;
    bool forward = true;
    switch (c) {
    case '(': open = '('; close = ')'; break;
    case '[': open = '['; close = ']'; break;
    case '{': open = '{'; close = '}'; break;
    case ')': open = '('; close = ')'; forward = false; break;
    case ']': open = '['; close = ']'; forward = false; break;
    case '}': open = '{'; close = '}'; forward = false; break;
    }
    std::optional<Position> match = ScanBracketMatch(buffer, *candidate, open, close, forward);
    //3. 候选没有配对 → 整条命令失败, 不继续尝试下一个候选 (README:1127)
    if (!match) {
        res.ok_ = false;
        return res;
    }
    res.ok_ = true;
    res.target_ = *match;
    res.kind_ = RangeKind::CharacterwiseInclusive;
    res.curswant_ = CurswantPolicy::SetToTarget;
    return res;
}

//[count]%: 跳到第 ceil(count * N / 100) 行 (N = 文件行数, 行号从 1 开始).
//count 不在 [1,100] 内时失败 (ok_ = false); 用 (count * N + 99) / 100 避免浮点, count <= 100 不会溢出.
//列定位与 G 相同 (LineJumpTarget): 即使目标行就是当前行也按规则重新定位 (README:1129).
//完成后 curswant = 光标所在的显示列 (SetToTarget), kind = Linewise.
//TODO(光标的移动二): 实现
MotionResult MotionEngine::ComputePercentLine(const Buffer& buffer, Position origin, std::optional<size_t> count) {
    MotionResult res;
    res.kind_ = RangeKind::Linewise;
    res.curswant_ = CurswantPolicy::SetToTarget;
    if (!count || count.value() < 1 || count.value() > 100) {
        res.ok_ = false;
        res.target_ = origin;
        return res;
    }
    size_t N = buffer.GetLineCount();
    size_t row = (count.value() * N + 99) / 100;  // ceil
    res.ok_ = true;
    res.target_ = LineJumpTarget(buffer, row - 1);
    return res;
}

namespace {

//pos 前紧邻奇数个连续反斜杠 (同一行内) → 该字符被转义 (Vim 奇偶规则, 测试点 88);
//引号翻转与括号配对的转义判定都用它, 串内串外一致
bool EscapedBefore(const Buffer& buffer, Position pos) {
    const std::string& line = buffer.GetLineAt(pos.row_);
    size_t count = 0;
    for (size_t j = pos.column_; j > 0 && line[j - 1] == '\\'; --j) {
        ++count;
    }
    return count % 2 == 1;
}

//当前行从光标 (含) 向右找第一个 ()[]{}; 没有返回 nullopt.
//按 README:1121/1127 只在当前行找, 不跨行继续.
std::optional<Position> FindBracketCandidate(const Buffer& buffer, Position origin) {
    const std::string& line = buffer.GetLineAt(origin.row_);
    std::size_t found = line.find_first_of("()[]{}", origin.column_);
    if (found == std::string::npos) {
        return std::nullopt;
    }
    return Position{origin.row_, found};
}

//从 start 出发找配对括号 (契约见文件中部声明注释; Vim findmatchlimit 的忠实移植)
std::optional<Position> ScanBracketMatch(const Buffer& buffer, Position start,
                                         char open, char close, bool forward) {
    const bool backwards = !forward;
    const bool match_escaped = EscapedBefore(buffer, start); // 候选的转义奇偶
    bool inquote = false;
    int start_in_quotes = -1; // -1 = MAYBE, 0 = FALSE, 1 = TRUE
    // 每行第一次访问时计算的"引号处理是否启用": -1 未算, 0 奇数引号(忽略), 1 偶数(启用)
    std::vector<char> do_quotes_cache(buffer.GetLineCount(), -1);

    size_t count = 0; // 深度: 候选自身不计, 遇到第一个配对括号即返回 (与 vim 一致)
    char inc = forward ? open : close; // 沿扫描方向"更内层"的括号
    char dec = forward ? close : open;

    // 向前: 从行尾 NUL 位置进入下一行行首; 向后: 从行首进入上一行的 NUL 位置.
    // 每行的 NUL 位置都会被访问 (行尾重置引号状态用)
    auto advance = [&](Position& p) -> bool {
        if (backwards) {
            if (p.column_ == 0) {
                if (p.row_ == 0) return false;
                --p.row_;
                p.column_ = buffer.GetLineAt(p.row_).size(); // 停在行尾 NUL 位置
            } else {
                --p.column_;
            }
            return true;
        }
        if (p.column_ == buffer.GetLineAt(p.row_).size()) {
            if (p.row_ + 1 >= buffer.GetLineCount()) return false;
            ++p.row_;
            p.column_ = 0;
        } else {
            ++p.column_;
        }
        return true;
    };

    // 进入新行时的 do_quotes 初始化 (vim search2141.c:2546-2610);
    // first_pos 非空表示起始行 (at_start 锚点 = 扫描将要处理的第一个位置)
    auto init_line = [&](Position& p, const Position* first_pos) {
        if (do_quotes_cache[p.row_] >= 0) return;
        const std::string& line = buffer.GetLineAt(p.row_);
        // 注意: 与 vim 一致, 计数从 -1 开始 (do_quotes == -1 是"进入新行"的哨兵值),
        // 这样 &1 之后 1 才表示"引号数为偶数"
        int do_quotes = -1;
        int at_start = 0;
        size_t col = 0;
        while (col < line.size()) {
            if (first_pos && col == first_pos->column_ + (backwards ? 1 : 0)) {
                at_start = do_quotes & 1;
            }
            if (line[col] == '"'
                && (col == 0 || line[col - 1] != '\''
                    || (col + 1 < line.size() && line[col + 1] != '\''))) {
                ++do_quotes;
            }
            if (line[col] == '\\' && col + 1 < line.size()) ++col; // 跳过 \X
            ++col;
        }
        do_quotes &= 1; // 1 = 引号数为偶数
        if (!do_quotes) {
            // 奇数引号行: 默认全部忽略; 行尾反斜杠 (本行或上一行) 提供续行
            inquote = false;
            if (!line.empty() && line.back() == '\\') {
                do_quotes = 1;
                if (start_in_quotes == -1) {
                    inquote = true;
                    start_in_quotes = 1;
                } else if (backwards) {
                    inquote = true;
                }
            }
            if (p.row_ > 0) {
                const std::string& prev = buffer.GetLineAt(p.row_ - 1);
                if (!prev.empty() && prev.back() == '\\') {
                    do_quotes = 1;
                    if (start_in_quotes == -1) {
                        inquote = at_start != 0;
                        if (inquote) start_in_quotes = 1;
                    } else if (!backwards) {
                        inquote = true;
                    }
                }
            }
        }
        do_quotes_cache[p.row_] = (char)do_quotes;
        if (start_in_quotes == -1) start_in_quotes = 0; // MAYBE → FALSE
    };

    init_line(start, &start);

    Position pos = start;
    while (true) {
        if (!advance(pos)) return std::nullopt;
        if (do_quotes_cache[pos.row_] < 0) init_line(pos, nullptr);

        const std::string& line = buffer.GetLineAt(pos.row_);
        char c = (pos.column_ < line.size()) ? line[pos.column_] : '\0';
        if (c == '\0') {
            // 行尾: 非反斜杠结尾则重置引号状态 (vim search2141.c:2625)
            if (pos.column_ == 0 || line[pos.column_ - 1] != '\\') {
                inquote = false;
                start_in_quotes = 0;
            }
            continue;
        }
        if (c == '"') {
            // 前导反斜杠为偶数的引号才翻转 (vim search2141.c:2634)
            if (do_quotes_cache[pos.row_] && !EscapedBefore(buffer, pos)) {
                inquote = !inquote;
                start_in_quotes = 0;
            }
            continue;
        }
        if (c == '\'' && open != '\'' && close != '\'') {
            // 跳过单引号单字符字面量 'x' 或 '\x' (vim search2141.c:2653)
            if (backwards) {
                if (pos.column_ > 1) {
                    if (line[pos.column_ - 2] == '\'') { pos.column_ -= 2; continue; }
                    if (line[pos.column_ - 2] == '\\' && pos.column_ > 2
                        && line[pos.column_ - 3] == '\'') {
                        pos.column_ -= 3;
                        continue;
                    }
                }
            } else if (pos.column_ + 1 < line.size()) {
                if (line[pos.column_ + 1] == '\\' && pos.column_ + 2 < line.size()
                    && pos.column_ + 3 < line.size() && line[pos.column_ + 3] == '\'') {
                    pos.column_ += 3;
                    continue;
                }
                if (pos.column_ + 2 < line.size() && line[pos.column_ + 2] == '\'') {
                    pos.column_ += 2;
                    continue;
                }
            }
            continue; // 普通单引号 (如 jon's)
        }
        // 括号接受条件: 引号外, 或起点也在引号内 (续行); 且转义奇偶与候选一致
        if ((!inquote || start_in_quotes == 1) && (c == inc || c == dec)
            && EscapedBefore(buffer, pos) == match_escaped) {
            if (c == inc) {
                ++count;
            } else {
                if (count == 0) return pos;
                --count;
            }
        }
        // 其他种类的括号不影响深度 (README:1123)
    }
}

//行跳转命令 (G/gg/[count]%) 共用的目标行列定位: 第一个非空白字符 (空格+Tab 均视为空白);
//行内全为空白停在最后一个字符; 空行停在该行唯一位置.
Position LineJumpTarget(const Buffer& buffer, std::size_t row) {
    Position pos;
    pos.row_ = row;
    const std::string& line = buffer.GetLineAt(row);
    if (line.empty()) {
        pos.column_ = 0;
        return pos;
    }
    for (size_t i = 0; i < line.length(); i++) {
        if (!IsSpaceChar(line[i]) && !IsTabChar(line[i])) {
            pos.column_ = i;
            return pos;
        }
    }
    pos.column_ = line.length() - 1;
    return pos;
}

} // namespace

} // namespace sjtu
