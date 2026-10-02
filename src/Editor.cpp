#include "Editor.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>

//以下提示均可用于 Basic 部分的实现, 部分函数在 Advanced 部分中需要修改
//在该文件中, 有些函数我们完整保留了正确的实现, 有些函数去掉了一些, 其余的完全需要你自己填写
namespace sjtu {

namespace {
//匿名 namespace, 提供了只给当前文件使用的辅助函数

std::string Trim(std::string value) {
    //去掉字符串两端的空白, 保留中间的内容; 全部是空白时返回空字符串
    //你可以分别从两端找到第一个非空白字符, 注意反向迭代器转回正向迭代器时的边界
    auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

//判断是否为 ASCII 可打印字符, Tab 由插入模式另外处理
bool IsPrintable(char value) { return value >= 0x20U && value < 0x7FU; }

} // namespace

//用 path 初始化文件缓冲区, Terminal 构造时会准备好终端输入环境
Editor::Editor(const std::filesystem::path& path) : buffer_(path), terminal_() {}

void Editor::Run() {
    //每轮先刷新画面, 再读取并处理一个按键; 退出循环后清屏
    while (running_) {
        RefreshScreen();
        ProcessKey(terminal_.ReadKey());
    }
    terminal_.ClearScreen();
}

//返回编辑器是否还需要继续运行
bool Editor::IsRunning() const noexcept { return running_; }

void Editor::RefreshScreen() {
    //1. 获取终端大小 (GetScreenSize), 更新窗口可显示的范围, 并让光标落在可见区域内
    ScreenSize terminal_size = terminal_.GetScreenSize(); 
    window_.Resize(terminal_size);
    window_.EnsureCursorVisible(buffer_);
    //2. 把当前模式、命令和提示打包成 RenderState
    RenderState states = {mode_, command_, message_};
    //3. 让 Renderer 生成一帧字符串, 再交给 Terminal 输出
    std::string display_text = renderer_.Render(buffer_, window_, states);
    //这里是你唯一需要调用 Terminal 中的接口的地方, 请调用 WriteOutput
    terminal_.WriteOutput(display_text);
}

void Editor::ProcessKey(KeyEvent key) {
    //1. (可选) 先处理所有模式都能使用的 Ctrl-Q, 用于紧急退出
    if (key.IsControl('Q')) {
        running_ = false;
        return;
    }
    //2. 按当前模式分发给命令行或插入模式的处理函数
    if (mode_ == Mode::Insert) {
        HandleInsert(key);
        return;
    } 
    if (mode_ == Mode::CommandLine) {
        HandleCommandLine(key);
        return;
    }
    //3. Normal 模式下清除旧提示, 将按键交给 parser, 再执行生成的 Action
    if (mode_ == Mode::Normal) {
        message_.clear();
        EditorAction action = normal_parser_.Feed(key);
        Execute(action);
        return;
    }
}

void Editor::Execute(const EditorAction& action) {
    //根据 Action 的种类调用对应模块
    switch (action.kind_) {
    case ActionKind::None:
        return;
    case ActionKind::Move:
        //交给 Window 吧
        window_.ApplyMotion(buffer_, *action.motion_);
        return;
    case ActionKind::InsertBefore:
        //进入 InsertMode, Editor 自己就有对应方法
        EnterInsert(window_.GetCursor());
        return;
    case ActionKind::InsertAfter: {
        Position cursor = window_.GetCursor();
        const std::string& cursor_line = buffer_.GetLineAt(cursor.row_);
        //特判: 如果 Buffer 表示当前 Cursor 所在的行是空的怎么办?
        if (cursor_line.empty()) {
            EnterInsert(cursor);
            return;
        }
        //从当前 Char 之后进入 InsertMode
        cursor.column_ += 1;
        EnterInsert(cursor);
        return;
    }
    case ActionKind::EnterCommandLine:
        //进入Command Mode
        mode_ = Mode::CommandLine;
        //记得清空当前的message之类的遗留状态
        command_.clear();
        message_.clear();
        return;
    }
}

// Insert 模式下 Editor 对于 KeyEvent 的处理.
void Editor::HandleInsert(KeyEvent key) {
    //在该函数中你需要同时照顾 Buffer 和 Window 的状态
    Position cursor = window_.GetCursor();
    //1. 若是 Escape 退出插入模式
    if (key.code_ == KeyCode::Escape) {
        LeaveInsert();
        return;
    }
    //2. Enter 在光标处分行, 光标移动到新行开头
    if (key.code_ == KeyCode::Enter) {
        buffer_.SplitLine(cursor.row_, cursor.column_);
        cursor.row_ += 1;
        cursor.column_ = 0;
        window_.SetCursor(buffer_, cursor, true);
        return;
    }
    //3. Backspace 删除前一个字符; 若在行首且不是第一行, 则与上一行合并
    if (key.code_ == KeyCode::Backspace) {
        if (cursor.row_ == 0 && cursor.column_ == 0) return;
        if (cursor.column_ == 0) {
            std::size_t last_row_length = buffer_.GetLineAt(cursor.row_ - 1).size();
            buffer_.JoinLine(cursor.row_ - 1);
            cursor.row_ -= 1;
            cursor.column_ = last_row_length;
            window_.SetCursor(buffer_, cursor, true);
            return;
        }
        buffer_.EraseCharacter(cursor.row_, cursor.column_ - 1);
        cursor.column_ -= 1;
        window_.SetCursor(buffer_, cursor, true);
        return;
    }
    //4. 可打印字符和 Tab 插入当前位置, 光标向后移动一列
    if (key.code_ == KeyCode::Character && (IsPrintable(key.value_) || key.value_ == '\t')) {
        buffer_.InsertCharacter(cursor.row_, cursor.column_, key.value_);
        cursor.column_ += 1;
        window_.SetCursor(buffer_, cursor, true);
        return;
    }
    //修改内容后记得同步 Window 中的光标, 插入模式允许光标位于 line.size()
}

void Editor::EnterInsert(Position position) {
    //切换到 Insert 模式, 设置插入位置并清除旧提示; 允许光标停在行尾字符之后
    mode_ = Mode::Insert;
    message_.clear();
    command_.clear();
    window_.SetCursor(buffer_, position, true);
}

void Editor::LeaveInsert() {
    //从插入位置回到 Normal 模式的字符位置: 不在行首时先左移一列, 再限制光标范围
    Position cursor = window_.GetCursor();
    if (cursor.column_ > 0) cursor.column_ -= 1;
    window_.SetCursor(buffer_, cursor, false);
    mode_ = Mode::Normal;
}



void Editor::HandleCommandLine(KeyEvent key) {
    //命令内容保存在 command_ 中, 不修改 Buffer
    // Escape 取消命令
    if (key.code_ == KeyCode::Escape) {
        LeaveCommandLine();
        return;
    }
    // Enter 执行命令
    if (key.code_ == KeyCode::Enter) {
        ExecuteCommandLine();
        return;
    }
    // Backspace/Delete 删除末尾字符, 注意空命令不能再删除字符
    if (key.code_ == KeyCode::Backspace || key.code_ == KeyCode::Delete) {
        if (command_.empty()) return;
        command_.pop_back();
        return;
    }
    // 可打印字符追加到末尾
    if (key.code_ == KeyCode::Character && IsPrintable(key.value_)) {
        command_.push_back(key.value_);
        return;
    }
}

void Editor::ExecuteCommandLine() {
    //1. 保存去掉首尾空白后的命令 (用 trim), 再退出命令行模式, 因为退出会清空 command_
    std::string exec_command = Trim(command_);
    LeaveCommandLine();
    //2. 空命令直接返回, 否则按第一个空格或 Tab 拆成命令名和参数
    if (exec_command.empty()) return;
    std::size_t mark = exec_command.find_first_of(" \t");
    std::string command_name = (mark == std::string::npos)? exec_command: exec_command.substr(0, mark);
    std::string command_arg = (mark == std::string::npos)? "": Trim(exec_command.substr(mark + 1));
    //3. 在 Basic 部分中你会发现最后命令就一个命令名, 直接根据要求的命令名执行
    if (command_name == "wq") {
        if (SaveBuffer(command_arg)) running_ = false;
        return;
    }
    if (command_name == "q!" || command_name == "quit!") {
        running_ = false;
        return;
    }
    //4. 无法识别的命令写入 message_, 供下一次刷新显示
    message_ = "Invalid command: " + command_name;
    return;
}

void Editor::LeaveCommandLine() {
    //恢复 Normal 模式并清空正在输入的命令
    command_.clear();
    mode_ = Mode::Normal;
}


bool Editor::SaveBuffer(const std::filesystem::path& path) {
    //1. path 为空时调用 Save, 否则调用 SaveAs
    try {
        if (path.empty()) buffer_.Save();
        else buffer_.SaveAs(path);
    }
    //2. 捕获保存时的异常, 把错误写入 message_ 并返回 false
    catch (const std::exception& e) {
        message_ = e.what();
        return false;
    }
    //3. 成功后生成包含文件名和行数的提示, 返回 true, 供 wq 判断是否可以退出
    message_ = "Saved to \"" + buffer_.GetDisplayName() + "\" with " + std::to_string(buffer_.GetLineCount()) + " lines written.";
    return true;
}

} // namespace sjtu
