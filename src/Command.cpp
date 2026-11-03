#include "Command.hpp"

namespace sjtu {

namespace {

bool IsDigit(char value) {
    return value >= '0' && value <= '9';
}

} // namespace

std::optional<EditorAction> NormalModeParser::Feed(KeyEvent key) {
    //Esc 在任何 pending 状态下都取消命令; 与 Basic 中"Esc 不产生动作"一致
    if (key.code_ == KeyCode::Escape) {
        Reset();
        return std::nullopt;
    }
    //方向键等特殊键: 取消 pending 并丢弃该键, 不当作新命令执行 (README: Parser 状态)
    if (key.code_ != KeyCode::Character) {
        Reset();
        return std::nullopt;
    }
    switch (state_) {
    case State::Normal:
        return FeedNormal(key.value_);
    case State::Count:
        return FeedCount(key.value_);
    case State::PrefixPending:
        return FeedPrefix(key.value_);
    case State::CharPending:
        return FeedChar(key.value_);
    case State::OperatorPending:
        return FeedOperator(key.value_);
    case State::OperatorCount:
        return FeedOperatorCount(key.value_);
    case State::OperatorPrefixPending:
        return FeedOperatorPrefix(key.value_);
    default:
        Reset();
        return std::nullopt;
    }
}

std::optional<EditorAction> NormalModeParser::FeedNormal(char value) {
    //数字前缀: 1-9 开始收集 count; 裸 0 是移动到行首的 motion (README:1048)
    if (value >= '1' && value <= '9') {
        pending_count_ = static_cast<size_t>(value - '0');
        state_ = State::Count;
        return std::nullopt;
    }
    switch (value) {
    case '0': return GenerateMotion(Motion::LineStart);
    case 'h': return GenerateMotion(Motion::Left);
    case 'j': return GenerateMotion(Motion::Down);
    case 'k': return GenerateMotion(Motion::Up);
    case 'l': return GenerateMotion(Motion::Right);
    case 'w': return GenerateMotion(Motion::WordForward);
    case 'b': return GenerateMotion(Motion::WordBackward);
    case 'e': return GenerateMotion(Motion::WordEnd);
    case 'W': return GenerateMotion(Motion::WordForwardCap);
    case 'B': return GenerateMotion(Motion::WordBackwardCap);
    case 'E': return GenerateMotion(Motion::WordEndCap);
    case '^': return GenerateMotion(Motion::FirstNonBlank);
    case '$': return GenerateMotion(Motion::LineEnd);
    case 'G': return GenerateMotion(Motion::GotoLine);
    case 'g':
        //g 前缀, 等待第二个键 (ge/gE/gg); z 前缀 (zt/zz/zb) 在窗口操作章节复用本状态
        pending_prefix_ = 'g';
        state_ = State::PrefixPending;
        return std::nullopt;
    case '/': return GenerateCommand(ActionKind::EnterSearchForward);
    case '?': return GenerateCommand(ActionKind::EnterSearchBackward);
    case 'n': return GenerateCommand(ActionKind::SearchNext);
    case 'N': return GenerateCommand(ActionKind::SearchPrev);
    case '%': return GenerateMotion(Motion::MatchPercent);
    case 'm':
    case '`':
        //m/` 等一个字符 (m{a-z} / `a), 具体字符由 FeedChar 收尾;
        //跳转键只绑反引号 ` (README 大纲行、vim 语义与通过 OJ 的旁证实现一致; 单引号不绑)
        pending_char_kind_ = (value == 'm') ? ActionKind::MarkSet : ActionKind::MarkJump;
        state_ = State::CharPending;
        return std::nullopt;
    case 'd':
    case 'c':
        //operator 前缀: 等待 motion (含 dd/cc 与 D/C)
        pending_operator_ = (value == 'd') ? ActionKind::Delete : ActionKind::Change;
        state_ = State::OperatorPending;
        return std::nullopt;
    case 'x': return GenerateCommand(ActionKind::DeleteCharForward);
    case 'X': return GenerateCommand(ActionKind::DeleteCharBackward);
    case 'D': return GenerateOperator(ActionKind::Delete, Motion::LineEnd);  // D = d$
    case 'C': return GenerateOperator(ActionKind::Change, Motion::LineEnd);  // C = c$
    case 'I': return GenerateCommand(ActionKind::EnterInsertLineStart);
    case 'A': return GenerateCommand(ActionKind::EnterInsertLineEnd);
    case 'o': return GenerateCommand(ActionKind::EnterInsertBelow);
    case 'O': return GenerateCommand(ActionKind::EnterInsertAbove);
    case 'r':
        pending_char_kind_ = ActionKind::ReplaceChar;
        state_ = State::CharPending;
        return std::nullopt;
    case 'J': return GenerateCommand(ActionKind::JoinLines);
    case '~': return GenerateCommand(ActionKind::FlipCase);
    case 'i': return GenerateCommand(ActionKind::InsertBefore);
    case 'a': return GenerateCommand(ActionKind::InsertAfter);
    case ':': return GenerateCommand(ActionKind::EnterCommandLine);
    default:
        //未绑定的键: 与 Basic 一样不产生任何动作
        return std::nullopt;
    }
}

std::optional<EditorAction> NormalModeParser::FeedCount(char value) {
    //已开始输入 count 时, 后续的 0 也属于 count (README:1048)
    if (IsDigit(value)) {
        *pending_count_ = *pending_count_ * 10 + static_cast<size_t>(value - '0');
        return std::nullopt;
    }
    //count 后接 g 前缀时保留 count, 等待第二个键 (如 2ge、3gg)
    if (value == 'g') {
        pending_prefix_ = 'g';
        state_ = State::PrefixPending;
        return std::nullopt;
    }
    std::optional<EditorAction> action;
    switch (value) {
    case 'h': action = GenerateMotion(Motion::Left, pending_count_); break;
    case 'j': action = GenerateMotion(Motion::Down, pending_count_); break;
    case 'k': action = GenerateMotion(Motion::Up, pending_count_); break;
    case 'l': action = GenerateMotion(Motion::Right, pending_count_); break;
    case 'w': action = GenerateMotion(Motion::WordForward, pending_count_); break;
    case 'b': action = GenerateMotion(Motion::WordBackward, pending_count_); break;
    case 'e': action = GenerateMotion(Motion::WordEnd, pending_count_); break;
    case 'W': action = GenerateMotion(Motion::WordForwardCap, pending_count_); break;
    case 'B': action = GenerateMotion(Motion::WordBackwardCap, pending_count_); break;
    case 'E': action = GenerateMotion(Motion::WordEndCap, pending_count_); break;
    case '^': action = GenerateMotion(Motion::FirstNonBlank, pending_count_); break;
    case '$': action = GenerateMotion(Motion::LineEnd, pending_count_); break;
    case 'G': action = GenerateMotion(Motion::GotoLine, pending_count_); break;
    case 'i': action = GenerateCommand(ActionKind::InsertBefore, pending_count_); break;
    case 'a': action = GenerateCommand(ActionKind::InsertAfter, pending_count_); break;
    case ':': action = GenerateCommand(ActionKind::EnterCommandLine, pending_count_); break;
    case 'n': action = GenerateCommand(ActionKind::SearchNext, pending_count_); break;
    case 'N': action = GenerateCommand(ActionKind::SearchPrev, pending_count_); break;
    case '%': action = GenerateMotion(Motion::PercentLine, pending_count_); break;
    case 'm':
    case '`':
        //count 对 m/` 无效, 按"忽略 count"处理 (测试点 93/96), 与 FeedNormal 一样进入 CharPending;
        //pending_count_ 保留到 FeedChar 收尾时一并清掉, 值本身被忽略
        pending_char_kind_ = (value == 'm') ? ActionKind::MarkSet : ActionKind::MarkJump;
        state_ = State::CharPending;
        return std::nullopt;
    case '/': action = GenerateCommand(ActionKind::EnterSearchForward, pending_count_); break;
    case '?': action = GenerateCommand(ActionKind::EnterSearchBackward, pending_count_); break;
    case 'd':
    case 'c':
        //operator 前缀带 count: 保存为 operator count, 等待 motion
        pending_operator_ = (value == 'd') ? ActionKind::Delete : ActionKind::Change;
        pending_operator_count_ = pending_count_;
        pending_count_.reset();
        state_ = State::OperatorPending;
        return std::nullopt; // 注意不走下面的 Reset
    case 'x': action = GenerateCommand(ActionKind::DeleteCharForward, pending_count_); break;
    case 'X': action = GenerateCommand(ActionKind::DeleteCharBackward, pending_count_); break;
    case 'D': action = GenerateOperator(ActionKind::Delete, Motion::LineEnd, pending_count_); break; // [count]D = [count]d$
    case 'C': action = GenerateOperator(ActionKind::Change, Motion::LineEnd, pending_count_); break; // [count]C = [count]c$
    case 'I': action = GenerateCommand(ActionKind::EnterInsertLineStart, pending_count_); break;
    case 'A': action = GenerateCommand(ActionKind::EnterInsertLineEnd, pending_count_); break;
    case 'o': action = GenerateCommand(ActionKind::EnterInsertBelow, pending_count_); break;
    case 'O': action = GenerateCommand(ActionKind::EnterInsertAbove, pending_count_); break;
    case 'r':
        pending_char_kind_ = ActionKind::ReplaceChar;
        state_ = State::CharPending;
        return std::nullopt; // count 保留给 FeedChar 收尾时取用
    case 'J': action = GenerateCommand(ActionKind::JoinLines, pending_count_); break;
    case '~': action = GenerateCommand(ActionKind::FlipCase, pending_count_); break;
    default:
        //不支持的键: 取消命令并丢弃该键
        action = std::nullopt;
        break;
    }
    Reset();
    return action;
}

std::optional<EditorAction> NormalModeParser::FeedPrefix(char value) {
    //目前只有 g 前缀 (ge/gE/gg); pending 的 count 一并生效 (如 2ge 是执行两次 ge, 3gg 是跳到第 3 行)
    std::optional<EditorAction> action;
    switch (value) {
    case 'e': action = GenerateMotion(Motion::WordBackwardEnd, pending_count_); break;
    case 'E': action = GenerateMotion(Motion::WordBackwardEndCap, pending_count_); break;
    case 'g': action = GenerateMotion(Motion::GotoFirstLine, pending_count_); break;
    default:
        //g 后跟其他键: 取消并丢弃
        action = std::nullopt;
        break;
    }
    Reset();
    return action;
}

//CharPending 状态的收尾: m/'/r/f/t/q/@ 等"等一个字符"的命令.
//目前只有 m/' (光标的移动二): value 为 a-z 时生成 MarkSet/MarkJump (arg_ = value),
//否则取消整个命令 (与非法键一致); 后续章节的 r/f/t/q/@ 在这里扩展
std::optional<EditorAction> NormalModeParser::FeedChar(char value) {
    //r 收尾 (先于 m/` 判断, 因为 r 的 {char} 也可能是 a-z):
    //{char} 必须是可打印 ASCII (0x20-0x7e), 否则取消整个命令 (README:1511);
    //[count]r 的 count 由 pending_count_ 带入动作
    if (pending_char_kind_ && *pending_char_kind_ == ActionKind::ReplaceChar) {
        if (value >= 0x20 && value <= 0x7e) {
            EditorAction act;
            act.kind_ = ActionKind::ReplaceChar;
            act.count_ = pending_count_;
            act.arg_ = value;
            Reset();
            return act;
        }
        Reset();
        return std::nullopt;
    }
    //m/` 收尾: 要求 a-z
    if (pending_char_kind_ && value >= 'a' && value <= 'z') {
        EditorAction act;
        act.kind_ = *pending_char_kind_;
        act.arg_ = value;
        Reset();
        return act;
    }
    //其余情况 Reset 后返回 nullopt (取消并丢弃)
    Reset();
    return std::nullopt;
}

EditorAction NormalModeParser::GenerateMotion(Motion motion, std::optional<size_t> count) {
    return {ActionKind::Move, MotionSpec{motion, count}, std::nullopt, std::nullopt, std::nullopt};
}

EditorAction NormalModeParser::GenerateCommand(ActionKind kind, std::optional<size_t> count) {
    //count 目前不参与 i/a/: 的执行, 先随动作传出, 供 [count]i 等章节使用
    return {kind, std::nullopt, count, std::nullopt, std::nullopt};
}

EditorAction NormalModeParser::GenerateOperator(ActionKind kind, Motion motion, std::optional<size_t> count) {
    //operator 动作: kind = Delete/Change, motion_spec_ = {motion, count}
    return {kind, MotionSpec{motion, count}, std::nullopt, std::nullopt, std::nullopt};
}

void NormalModeParser::Reset() {
    state_ = State::Normal;
    pending_count_.reset();
    pending_operator_.reset();
    pending_prefix_ = 0;
    pending_register_.reset();
    pending_char_kind_.reset();
    pending_operator_count_.reset();
}


//d/c 收尾共用的开关表 (契约见 Command.hpp 的声明注释)
//TODO 说明: 本函数是"接口联动"已实现; [count] 语义 (相乘) 在 FeedOperator/FeedOperatorCount 里完成
std::optional<EditorAction> NormalModeParser::CompleteOperator(char value, std::optional<size_t> count) {
    switch (value) {
    case 'h': return GenerateOperator(*pending_operator_, Motion::Left, count);
    case 'j': return GenerateOperator(*pending_operator_, Motion::Down, count);
    case 'k': return GenerateOperator(*pending_operator_, Motion::Up, count);
    case 'l': return GenerateOperator(*pending_operator_, Motion::Right, count);
    case 'w': return GenerateOperator(*pending_operator_, Motion::WordForward, count);
    case 'b': return GenerateOperator(*pending_operator_, Motion::WordBackward, count);
    case 'e': return GenerateOperator(*pending_operator_, Motion::WordEnd, count);
    case 'W': return GenerateOperator(*pending_operator_, Motion::WordForwardCap, count);
    case 'B': return GenerateOperator(*pending_operator_, Motion::WordBackwardCap, count);
    case 'E': return GenerateOperator(*pending_operator_, Motion::WordEndCap, count);
    case '0': return GenerateOperator(*pending_operator_, Motion::LineStart, count);
    case '^': return GenerateOperator(*pending_operator_, Motion::FirstNonBlank, count);
    case '$': return GenerateOperator(*pending_operator_, Motion::LineEnd, count);
    case 'G': return GenerateOperator(*pending_operator_, Motion::GotoLine, count);
    case '%': return GenerateOperator(*pending_operator_, Motion::MatchPercent, count);
    case 'D':
        //d 后接 D: 按 d$ 处理 (D = d$, 见 FeedNormal 的注释)
        return GenerateOperator(*pending_operator_, Motion::LineEnd, count);
    case 'C':
        return GenerateOperator(*pending_operator_, Motion::LineEnd, count);
    case 'd':
    case 'c': {
        //doubled 形式 (dd/cc): 整行 linewise, motion_spec_ 缺省, count 为最终 count
        EditorAction act;
        act.kind_ = *pending_operator_;
        act.count_ = count;
        return act;
    }
    default:
        //不完成命令的键: 由调用方决定取消
        return std::nullopt;
    }
}

//OperatorPending 的收尾 (契约见 Command.hpp 的声明注释)
std::optional<EditorAction> NormalModeParser::FeedOperator(char value) {
    //motion 的第二个 count 开始 (与 operator count 相乘)
    if (value >= '1' && value <= '9') {
        pending_count_ = static_cast<size_t>(value - '0');
        state_ = State::OperatorCount;
        return std::nullopt;
    }
    //g 前缀 (dge/dgg 等)
    if (value == 'g') {
        pending_prefix_ = 'g';
        state_ = State::OperatorPrefixPending;
        return std::nullopt;
    }
    std::optional<EditorAction> action = CompleteOperator(value, pending_operator_count_);
    Reset();
    return action; // nullopt = 取消整条命令并丢弃该键
}

//OperatorCount 的收尾: 数字继续拼 motion count, 其余同 FeedOperator (count 相乘)
std::optional<EditorAction> NormalModeParser::FeedOperatorCount(char value) {
    if (IsDigit(value)) {
        *pending_count_ = *pending_count_ * 10 + static_cast<size_t>(value - '0');
        return std::nullopt;
    }
    if (value == 'g') {
        pending_prefix_ = 'g';
        state_ = State::OperatorPrefixPending;
        return std::nullopt;
    }
    //两侧 count 相乘 ([count1]d[count2]w)
    std::optional<size_t> count;
    if (pending_count_ || pending_operator_count_) {
        count = pending_count_.value_or(1) * pending_operator_count_.value_or(1);
    }
    std::optional<EditorAction> action = CompleteOperator(value, count);
    Reset();
    return action;
}

//OperatorPrefixPending 的收尾: d/c 之后 g 前缀的第二个键 (ge/gE/gg)
std::optional<EditorAction> NormalModeParser::FeedOperatorPrefix(char value) {
    std::optional<size_t> count;
    if (pending_count_ || pending_operator_count_) {
        count = pending_count_.value_or(1) * pending_operator_count_.value_or(1);
    }
    std::optional<EditorAction> action;
    switch (value) {
    case 'e': action = GenerateOperator(*pending_operator_, Motion::WordBackwardEnd, count); break;
    case 'E': action = GenerateOperator(*pending_operator_, Motion::WordBackwardEndCap, count); break;
    case 'g': action = GenerateOperator(*pending_operator_, Motion::GotoFirstLine, count); break;
    default:
        //g 后跟其他键: 取消并丢弃
        action = std::nullopt;
        break;
    }
    Reset();
    return action;
}

} // namespace sjtu