/*
Editor.hpp
Editor负责协调各个模块,保存当前模式,并根据输入调用Buffer和Window完成编辑.
你可以沿着Run -> ProcessKey -> 各模式的处理函数阅读程序,再看RefreshScreen如何把结果显示出来.
*/
#ifndef MINIVIM_EDITOR_HPP
#define MINIVIM_EDITOR_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Buffer.hpp"
#include "Command.hpp"
#include "Edit.hpp"
#include "Register.hpp"
#include "Renderer.hpp"
#include "Search.hpp"
#include "Terminal.hpp"
#include "Types.hpp"
#include "Window.hpp"


namespace sjtu {

//插入会话的有效操作记录 ([count]i 回放用, README:1401-1405):
//只记录"有效"操作 (过程上有效, 不是净结果有效: iX<BS> 记录 Type+Backspace, i<Esc> 不记录)
struct InsertOp {
    enum class Kind {
        Type,              // 在光标前插入 value, 光标右移一列
        Backspace,         // 删除光标前字符, 光标左移一列
        BackspaceJoin,     // 行首退格: 合并到上一行, 光标到上一行原行末
        ForwardDelete,     // 删除光标所指字符, 光标不动
        ForwardDeleteJoin, // 行末删除: 合并下一行, 光标停在原行末 (拼接点)
        Split,             // 在光标处切分当前行, 光标到下一行行首
        OpenBelow,         // 当前行下方插入空行, 光标到新行 (o 的进入动作)
        OpenAbove,         // 当前行上方插入空行, 光标到新行 (O 的进入动作)
    };
    Kind kind_{Kind::Type};
    char value_{0};
};

class Editor {

public:
    explicit Editor(const std::filesystem::path& path = {});

    //编辑器的主循环,直到用户要求退出
    void Run();
    bool IsRunning() const noexcept;

private:
    //屏幕刷新,生成新的一帧,在Run循环中第一个调用
    void RefreshScreen();

    //总执行逻辑,根据Key和Mode分发给下属函数来处理
    void ProcessKey(KeyEvent key);

    //ProcessKey的下属函数之一,Editor对NormalMode下的Action的执行,这个action是从parser传进来的
    void Execute(const EditorAction& action);

    //ProcessKey的下属函数之一,Editor在InsertMode下对于KeyEvent的处理逻辑
    //FAQ:为啥InsertMode和CommandMode的处理直接属于Editor.hpp/cpp,而NormalMode则分出去一个单独的文件(Command.hpp/cpp)?
    //NormalMode的命令解析部分比较复杂所以我们单独解析,而这两部分逻辑需要Editor直接统筹全局其他模块且比较简明
    void EnterInsert(Position position, std::optional<size_t> count = std::nullopt);
    void LeaveInsert();
    void HandleInsert(KeyEvent key);

    //文本的修正与移动(一) 的执行入口 (单指令逻辑留给实现, 契约见 Editor.cpp 中各函数的注释)
    void ExecuteDelete(const EditorAction& action); // d{motion}/dd/D (D 由 parser 传 Motion::LineEnd)
    void ExecuteChange(const EditorAction& action); // c{motion}/cc/C
    void ExecuteDeleteChar(bool backward, std::optional<size_t> count); // x / X
    void ExecuteReplace(const EditorAction& action); // r / [count]r
    void ExecuteJoin(std::optional<size_t> count);   // J / [count]J
    void ExecuteFlipCase(std::optional<size_t> count); // ~ / [count]~
    //插入会话机制 (接口联动, 已实现): 记录有效操作与 [count] 回放
    void RecordInsertOp(InsertOp op);
    void ApplyInsertOp(const InsertOp& op); // 在光标处重放一个操作 (不重复记录)
    void ReplayInsertSession();             // Esc 时回放 (count-1) 次
    //I/A/o/O 的进入入口 (已实现)
    void EnterInsertLineStart(std::optional<size_t> count);
    void EnterInsertLineEnd(std::optional<size_t> count);
    void EnterInsertBelow(std::optional<size_t> count);
    void EnterInsertAbove(std::optional<size_t> count);

    //ProcessKey的下属函数之一,Editor在CommandMode下对于KeyEvent的处理逻辑
    void HandleCommandLine(KeyEvent key);
    void ExecuteCommandLine();
    void LeaveCommandLine();

    //ProcessKey的下属函数之一,Editor在搜索输入模式下对于KeyEvent的处理逻辑
    //(/ 或 ? 打开的输入框; 输入期间正文与光标静止, <CR> 提交搜索, <Esc> 打断)
    //count: [count]/ [count]? 取该方向循环序第 count 个匹配 (测试点 113/114)
    void EnterSearch(bool forward, std::optional<size_t> count = std::nullopt);
    void LeaveSearch();
    void HandleSearch(KeyEvent key);

    bool SaveBuffer(const std::filesystem::path& path = {});

    Buffer buffer_; //文件的实际内容
    Terminal terminal_; //读取按键,向终端输出,管理终端设置
    Window window_; //光标和当前可见区域
    Renderer renderer_; //根据内容和状态生成要输出的画面
    NormalModeParser normal_parser_; //把Normal模式的按键转换成EditorAction
    SearchEngine search_engine_; // / ? n N 的搜索串、方向与匹配缓存
    Register register_; // unnamed 寄存器 (d/c/x/X 写入; 命名寄存器由后续章节扩展)

    //插入会话 ([count]i 回放) 的状态:
    std::vector<InsertOp> insert_ops_; // 本次插入会话的有效操作序列 (README:1405 "有效"的定义)
    std::optional<size_t> pending_insert_count_; // 进入插入时的 [count]; Esc 时回放 count-1 次

    Mode mode_{Mode::Normal}; //当前编辑模式
    std::string command_; //命令行模式下已经输入的内容,不含开头的冒号
    std::string message_; //底部的提示或错误信息
    std::string search_input_; //搜索输入框的内容,不含前缀 / 或 ?
    bool search_direction_{true}; //本次搜索输入的方向: true = / 向前
    std::optional<size_t> search_count_; //[count]/ 或 [count]? 的 count
    bool running_{true}; //设为false后结束主循环
};

} // namespace sjtu

#endif // MINIVIM_EDITOR_HPP
