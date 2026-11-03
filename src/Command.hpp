/*
Command.hpp
用来将NormalMode下的用户输入转化成Editor可以看懂的EditorAction
*/
#ifndef MINIVIM_COMMAND_HPP
#define MINIVIM_COMMAND_HPP

#include <optional>

#include "Key.hpp"
#include "Types.hpp"
#include "Motion.hpp"

namespace sjtu {

//ActionKind是给NormalMode的用户命令的简单枚举类
enum class ActionKind {
    //这些是Basic部分需要的ActionKind,None是占位符,Move表示包括hjkl在内的移动,后面分别对应i,a,:
    None,
    Move,
    InsertBefore,
    InsertAfter,
    EnterCommandLine,
    Delete,
    Change,
    Yank,
    ShiftLeft,
    ShiftRight,
    FlipCase,
    ReplaceChar,
    JoinLines,
    MarkSet,
    MarkJump,
    PasteAfter,
    PasteBefore,
    Undo,
    Redo,
    Repeat,
    EnterVisual,
    EnterVisualLine,
    Scroll,
    EnterSearchForward,  // / : 进入向前搜索输入框
    EnterSearchBackward, // ? : 进入向后搜索输入框
    SearchNext,          // n / [count]n: 按上次方向重复搜索
    SearchPrev,          // N / [count]N: 按上次方向的反方向重复搜索
    EnterInsertLineStart, // I : 当前行第一个非空白字符处进入 Insert (全空白行在第 0 列)
    EnterInsertLineEnd,   // A : 当前行行末进入 Insert
    EnterInsertBelow,     // o : 当前行下方插入空行并进入 Insert
    EnterInsertAbove,     // O : 当前行上方插入空行并进入 Insert
    DeleteCharForward,    // x / [count]x : 删除光标处起的字符
    DeleteCharBackward,   // X / [count]X : 删除光标左侧的字符
    //在Advanced部分中你很可能需要添加别的ActionKind
};

//EditorAction是在NormalMode下我们的Editor真正收到的指令,在Editor中会根据传入的Action的不同让各模块做出对应的处理
struct EditorAction {
    ActionKind kind_{ActionKind::None};
    std::optional<MotionSpec> motion_spec_{};  // 原 motion_ 升级为 MotionSpec
    std::optional<size_t> count_{};       // x/X/D/C/r/~/J 等"单键+count"命令用
    std::optional<char> register_{};      // "前缀 (README:1603)
    std::optional<char> arg_{};           // r{char}、m{char}、'{char}、f/t{char}、q{char}、@{char}

    //当kind_ = Move时, motion_spec_ 才有意义; 后续 operator 章节也会用到它 (如 dw 构造 kind_=Delete + motion=w)
    //EditorAction 和 ActionKind 分层设计是为了可扩展性: Basic 的命令都是单个按键,
    //Advanced 中会遇到按键 pending, 此时 parser 内部先保存状态, 待命令完整后一次性生成 EditorAction
    //count_ 给 x/X/D/C/r/~/J 等"单键+count"命令用; register_/arg_ 给寄存器与等字符命令预留
};


class NormalModeParser {
//这个类用来处理用户按键的输入,对于Basic部分,每传入一个key你都可以让它生成一个完整的EditorAction(因为我们目前只支持单按键指令)
//在Advanced部分你很可能需要给已有接口添加多按键的逻辑/添加新的接口
public:
//把用户传入的新的key喂给parser,从parser生成当前的EditorAction,稍后转给Editor执行
//这是核心接口,适合扩展和沿用
    std::optional<EditorAction> Feed(KeyEvent key);
    // 返回 nullopt = 命令尚未完成, Editor 不执行、只等下一个键
    // 返回 action  = 命令完整, 交给 Editor::Execute
    void Reset();  // <Esc> 或非法键时清空 pending

private:
    //从 motion/actionkind 直接生成对应 EditorAction; count 为 nullopt 表示用户没输入数字前缀
    EditorAction GenerateMotion(Motion motion, std::optional<size_t> count = std::nullopt);
    EditorAction GenerateCommand(ActionKind kind, std::optional<size_t> count = std::nullopt);
    //生成 operator 动作: kind = Delete/Change, motion_spec_ = {motion, count}
    EditorAction GenerateOperator(ActionKind kind, Motion motion,
                                  std::optional<size_t> count = std::nullopt);
    //d/c 收尾共用的开关表: value 是 motion 键/d/c/D/C, count 是相乘后的最终 count;
    //返回 nullopt 表示该键不完成命令 (调用方再决定取消)
    std::optional<EditorAction> CompleteOperator(char value, std::optional<size_t> count);

    //Feed 按当前状态分发的子函数; 返回 nullopt 表示命令尚未完成或已取消
    std::optional<EditorAction> FeedNormal(char value);
    std::optional<EditorAction> FeedCount(char value);
    std::optional<EditorAction> FeedPrefix(char value);
    //CharPending 状态的收尾: m/`/r 等"等一个字符"的命令
    std::optional<EditorAction> FeedChar(char value);
    //OperatorPending 状态的收尾 (d/c 之后):
    //  - 数字 1-9 开始收集 motion 的 count (进入 OperatorCount 状态);
    //  - motion 键完成 d{motion}/c{motion} (两侧 count 相乘: [count1]d[count2]w);
    //  - 再按 d/c 完成 dd/cc (linewise 整行, count 为 operator count);
    //  - 'D'/'C' 完成 D/C (等价 d$/c$, motion count 即 operator count);
    //  - 'g' 进入 OperatorPrefixPending (dge/dgg 等);
    //  - 其余键取消整条命令并丢弃该键 (README: Parser 状态)
    std::optional<EditorAction> FeedOperator(char value);
    //OperatorCount 状态的收尾: 数字继续拼 count; 其余同 FeedOperator (count 相乘)
    std::optional<EditorAction> FeedOperatorCount(char value);
    //OperatorPrefixPending 的收尾: d/c 之后 g 前缀的第二个键 (ge/gE/gg)
    std::optional<EditorAction> FeedOperatorPrefix(char value);

    enum class State {
        Normal,
        Count,             // 已输入 [1-9]+ 数字
        PrefixPending,     // g (ge/gE/gg) → 后续 z (zt/zz/zb) 复用
        OperatorPending,   // d/c/y/</> 等 motion (含 doubled 形式)
        OperatorCount,     // operator 之后又输入了数字 (motion 的 count, 与 operator count 相乘)
        OperatorPrefixPending, // operator 之后接 g 前缀 (dge/dgg 等)
        RegisterPending,   // "
        CharPending,       // r/m/'/f/t/q/@ 等"等一个字符"的命令
        TextObjectPending, // operator 后的 a/i (文本对象章节)
    };
    State state_{State::Normal};
    std::optional<size_t> pending_count_;
    std::optional<ActionKind> pending_operator_;
    std::optional<size_t> pending_operator_count_; // d/c 之前的 count, 与 motion count 相乘
    char pending_prefix_{0};
    std::optional<char> pending_register_;
    std::optional<ActionKind> pending_char_kind_; // CharPending 状态要生成的动作 (目前是 MarkSet/MarkJump)    
};

} // namespace sjtu

#endif // MINIVIM_COMMAND_HPP
