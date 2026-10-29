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

    //Feed 按当前状态分发的三个子函数; 返回 nullopt 表示命令尚未完成或已取消
    std::optional<EditorAction> FeedNormal(char value);
    std::optional<EditorAction> FeedCount(char value);
    std::optional<EditorAction> FeedPrefix(char value);

    enum class State {
        Normal,
        Count,             // 已输入 [1-9]+ 数字
        PrefixPending,     // g (ge/gE/gg) → 后续 z (zt/zz/zb) 复用
        OperatorPending,   // d/c/y/</> 等 motion (含 doubled 形式)
        RegisterPending,   // "
        CharPending,       // r/m/'/f/t/q/@ 等"等一个字符"的命令
        TextObjectPending, // operator 后的 a/i (文本对象章节)
    };
    State state_{State::Normal};
    std::optional<size_t> pending_count_;
    std::optional<ActionKind> pending_operator_;
    char pending_prefix_{0};
    std::optional<char> pending_register_;    
};

} // namespace sjtu

#endif // MINIVIM_COMMAND_HPP
