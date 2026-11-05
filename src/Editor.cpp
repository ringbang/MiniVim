#include "Editor.hpp"
#include <cstdio>

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
    RenderState states = {mode_, command_, message_, search_input_, search_direction_};
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
    if (mode_ == Mode::Search) {
        HandleSearch(key);
        return;
    }
    //3. Normal 模式下清除旧提示, 将按键交给 parser, 再执行生成的 Action
    if (mode_ == Mode::Normal) {
        message_.clear();
        auto action = normal_parser_.Feed(key);
        //parser 返回 nullopt 表示命令尚未完成 (如已输入 count 或 g 前缀), 继续等下一个键
        if (action) {
            Execute(*action);
        }
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
        window_.ApplyMotion(buffer_, *action.motion_spec_);
        return;
    case ActionKind::InsertBefore:
        //进入 InsertMode; [count] 供 Esc 时回放
        EnterInsert(window_.GetCursor(), action.count_);
        return;
    case ActionKind::InsertAfter: {
        Position cursor = window_.GetCursor();
        const std::string& cursor_line = buffer_.GetLineAt(cursor.row_);
        //特判: 如果 Buffer 表示当前 Cursor 所在的行是空的怎么办?
        if (cursor_line.empty()) {
            EnterInsert(cursor, action.count_);
            return;
        }
        //从当前 Char 之后进入 InsertMode
        cursor.column_ += 1;
        EnterInsert(cursor, action.count_);
        return;
    }
    case ActionKind::Delete:
        //d{motion}/dd/D (D 由 parser 传 Motion::LineEnd 的 motion_spec_)
        ExecuteDelete(action);
        return;
    case ActionKind::Change:
        //c{motion}/cc/C
        ExecuteChange(action);
        return;
    case ActionKind::Yank:
        //y{motion}/yy/Y (parser 已接通, 执行体见 ExecuteYank 的 TODO)
        ExecuteYank(action);
        return;
    case ActionKind::PasteAfter:
        //p / [count]p
        ExecutePaste(false, action.count_);
        return;
    case ActionKind::PasteBefore:
        //P / [count]P
        ExecutePaste(true, action.count_);
        return;
    case ActionKind::ShiftLeft:
        //<{motion} / <<
        ExecuteShift(false, action);
        return;
    case ActionKind::ShiftRight:
        //>{motion} / >>
        ExecuteShift(true, action);
        return;
    case ActionKind::DeleteCharForward:
        ExecuteDeleteChar(false, action.count_);
        return;
    case ActionKind::DeleteCharBackward:
        ExecuteDeleteChar(true, action.count_);
        return;
    case ActionKind::ReplaceChar:
        ExecuteReplace(action);
        return;
    case ActionKind::JoinLines:
        ExecuteJoin(action.count_);
        return;
    case ActionKind::FlipCase:
        ExecuteFlipCase(action.count_);
        return;
    case ActionKind::EnterInsertLineStart:
        EnterInsertLineStart(action.count_);
        return;
    case ActionKind::EnterInsertLineEnd:
        EnterInsertLineEnd(action.count_);
        return;
    case ActionKind::EnterInsertBelow:
        EnterInsertBelow(action.count_);
        return;
    case ActionKind::EnterInsertAbove:
        EnterInsertAbove(action.count_);
        return;
    case ActionKind::EnterCommandLine:
        //进入Command Mode
        mode_ = Mode::CommandLine;
        //记得清空当前的message之类的遗留状态
        command_.clear();
        message_.clear();
        return;
    case ActionKind::EnterSearchForward:
        EnterSearch(true, action.count_);
        return;
    case ActionKind::EnterSearchBackward:
        EnterSearch(false, action.count_);
        return;
    case ActionKind::SearchNext: {
        //n/[count]n: 按上次方向取第 count 个匹配; 失败时光标与视口不动
        auto target = search_engine_.Repeat(buffer_, true, action.count_, window_.GetCursor());
        if (target) window_.JumpTo(buffer_, *target);
        else message_ = "Pattern not found"; //TODO: 与 OJ 参考实现的提示文本核对, 必要时调整
        return;
    }
    case ActionKind::SearchPrev: {
        //N/[count]N: 按上次方向的反方向
        auto target = search_engine_.Repeat(buffer_, false, action.count_, window_.GetCursor());
        if (target) window_.JumpTo(buffer_, *target);
        else message_ = "Pattern not found"; //TODO: 同上
        return;
    }
    case ActionKind::MarkSet:
        //m{a-z}: 把当前光标保存为标记, 同名覆盖; 非法字符由 parser 拦截
        if (action.arg_) buffer_.SetMark(*action.arg_, window_.GetCursor());
        return;
    case ActionKind::MarkJump:
        //'a: 跳到标记的精确行列; 标记不存在时命令失败, 光标不动
        if (action.arg_) {
            auto mark = buffer_.GetMark(*action.arg_);
            if (mark) window_.JumpTo(buffer_, *mark);
        }
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
        RecordInsertOp({InsertOp::Kind::Split, 0});
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
            RecordInsertOp({InsertOp::Kind::BackspaceJoin, 0});
            window_.SetCursor(buffer_, cursor, true);
            return;
        }
        buffer_.EraseCharacter(cursor.row_, cursor.column_ - 1);
        cursor.column_ -= 1;
        RecordInsertOp({InsertOp::Kind::Backspace, 0});
        window_.SetCursor(buffer_, cursor, true);
        return;
    }
    //4. 可打印字符和 Tab 插入当前位置, 光标向后移动一列
    if (key.code_ == KeyCode::Character && (IsPrintable(key.value_) || key.value_ == '\t')) {
        buffer_.InsertCharacter(cursor.row_, cursor.column_, key.value_);
        cursor.column_ += 1;
        RecordInsertOp({InsertOp::Kind::Type, key.value_});
        window_.SetCursor(buffer_, cursor, true);
        return;
    }
    //5. <Del> 删除光标所指字符 (README:1409-1423)
    if (key.code_ == KeyCode::Delete) {
        const std::string& line = buffer_.GetLineAt(cursor.row_);
        if (cursor.column_ < line.size()) {
            //删除光标所指字符, 光标保持不动 (因此指向原字符的下一个字符;
            //删掉行末字符时光标停在新的行末位置)
            buffer_.EraseCharacter(cursor.row_, cursor.column_);
            RecordInsertOp({InsertOp::Kind::ForwardDelete, 0});
            window_.SetCursor(buffer_, cursor, true);
            return;
        }
        //行末 (含空行) 且存在下一行: 删除换行, 把下一行拼接上来;
        //光标保持在原来的行末位置 = 拼接后下一行原来第一个字符之前 (README:1419/1421)
        if (cursor.row_ + 1 < buffer_.GetLineCount()) {
            buffer_.JoinLine(cursor.row_);
            RecordInsertOp({InsertOp::Kind::ForwardDeleteJoin, 0});
            window_.SetCursor(buffer_, cursor, true);
            return;
        }
        //文件最后一个合法位置: 无效果 (不记录)
        return;
    }
    //修改内容后记得同步 Window 中的光标, 插入模式允许光标位于 line.size()
}

void Editor::EnterInsert(Position position, std::optional<size_t> count) {
    //切换到 Insert 模式, 设置插入位置并清除旧提示; 允许光标停在行尾字符之后
    mode_ = Mode::Insert;
    message_.clear();
    command_.clear();
    window_.SetCursor(buffer_, position, true);
    //[count] 回放会话初始化: 清空操作记录, 保存回放次数
    insert_ops_.clear();
    pending_insert_count_ = count;
}

void Editor::LeaveInsert() {
    //[count] 回放 (README:1401): 收到 Esc 后, 先完成 (count-1) 次有效操作序列的回放,
    //再执行"左移一列"的退出动作; 回放中产生的修改同样计 modified
    if (pending_insert_count_ && *pending_insert_count_ > 1) {
        ReplayInsertSession();
    }
    //从插入位置回到 Normal 模式的字符位置: 不在行首时先左移一列, 再限制光标范围
    Position cursor = window_.GetCursor();
    if (cursor.column_ > 0) cursor.column_ -= 1;
    window_.SetCursor(buffer_, cursor, false);
    mode_ = Mode::Normal;
    //清空会话状态
    insert_ops_.clear();
    pending_insert_count_.reset();
    //插入期间 Buffer 已被修改, 搜索匹配缓存失效 (搜索串与方向保留);
    //后续 Normal 模式的编辑命令 (x/d/p 等) 也要在修改 Buffer 后调用 Invalidate
    search_engine_.Invalidate();
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

//进入搜索输入模式 (/ 或 ?): 正文与光标保持静止, 底部显示 / 或 ? 加输入内容
void Editor::EnterSearch(bool forward, std::optional<size_t> count) {
    mode_ = Mode::Search;
    search_input_.clear();
    search_direction_ = forward;
    search_count_ = count;
    message_.clear();
}

void Editor::LeaveSearch() {
    //回到 Normal 模式, 丢弃正在输入的内容 (搜索记录由 SearchEngine 管理, 这里不碰)
    search_input_.clear();
    search_count_.reset();
    mode_ = Mode::Normal;
}

void Editor::HandleSearch(KeyEvent key) {
    //README:1097 只响应可打印 ASCII (/、?、反斜杠都是普通字符), 其余控制键忽略,
    //但 <BS> 删最后一个字符 (与 OJ 参考一致; 空提示时 <BS> 无效果, 不取消——v3/v4 分歧位置反推).
    //<CR> 提交搜索, <Esc> 打断并完全恢复进入前的位置 (输入期间光标本就没动, 不修改搜索记录)
    if (key.code_ == KeyCode::Escape) {
        LeaveSearch();
        return;
    }
    if (key.code_ == KeyCode::Enter) {
        //提交: 非空串保存串与方向 (即使本次找不到); 空串复用旧串 + 本次方向, 没有旧串则失败;
        //[count]/ [count]? 取循环序第 count 个匹配 (测试点 113/114)
        auto target = search_engine_.Commit(buffer_, search_input_, search_direction_,
                                            window_.GetCursor(), search_count_);
        bool no_old_record = search_input_.empty() && !search_engine_.HasPattern();
        LeaveSearch();
        if (target) {
            window_.JumpTo(buffer_, *target); //成功: 光标停在匹配的第一个字符
        } else if (!no_old_record) {
            message_ = "Pattern not found"; //TODO: 与 OJ 参考实现的提示文本核对, 必要时调整
        }
        //空提示且无旧串: 静默失败 (与测试点 101/105 的 "empty prompt" 一致)
        return;
    }
    if (key.code_ == KeyCode::Backspace) {
        if (!search_input_.empty()) search_input_.pop_back(); // 空提示时无效果, 不取消
        return;
    }
    if (key.code_ == KeyCode::Character && IsPrintable(key.value_)) {
        search_input_.push_back(key.value_);
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


// ---- 文本的修正与移动(一) ----

//记录一条有效操作 (README:1405: 只记"过程上有效"的操作; 调用点见 HandleInsert)
void Editor::RecordInsertOp(InsertOp op) {
    insert_ops_.push_back(op);
}

//在光标处重放一条操作 (契约见 InsertOp 各 Kind 的注释; 不回写 insert_ops_)
void Editor::ApplyInsertOp(const InsertOp& op) {
    Position cursor = window_.GetCursor();
    switch (op.kind_) {
    case InsertOp::Kind::Type:
        buffer_.InsertCharacter(cursor.row_, cursor.column_, op.value_);
        cursor.column_ += 1;
        break;
    case InsertOp::Kind::Backspace:
        buffer_.EraseCharacter(cursor.row_, cursor.column_ - 1);
        cursor.column_ -= 1;
        break;
    case InsertOp::Kind::BackspaceJoin: {
        std::size_t last_row_length = buffer_.GetLineAt(cursor.row_ - 1).size();
        buffer_.JoinLine(cursor.row_ - 1);
        cursor.row_ -= 1;
        cursor.column_ = last_row_length;
        break;
    }
    case InsertOp::Kind::ForwardDelete:
        buffer_.EraseCharacter(cursor.row_, cursor.column_);
        break; // 光标不动
    case InsertOp::Kind::ForwardDeleteJoin:
        buffer_.JoinLine(cursor.row_);
        break; // 光标不动 (停在原行末 = 拼接点)
    case InsertOp::Kind::Split:
        buffer_.SplitLine(cursor.row_, cursor.column_);
        cursor.row_ += 1;
        cursor.column_ = 0;
        break;
    case InsertOp::Kind::OpenBelow:
        buffer_.InsertLine(cursor.row_ + 1);
        cursor.row_ += 1;
        cursor.column_ = 0;
        break;
    case InsertOp::Kind::OpenAbove:
        buffer_.InsertLine(cursor.row_);
        cursor.column_ = 0;
        break;
    }
    window_.SetCursor(buffer_, cursor, true);
}

//Esc 时的 [count] 回放 (README:1401-1405): 在 LeaveInsert 的左移之前调用
void Editor::ReplayInsertSession() {
    if (!pending_insert_count_) return;
    std::size_t times = *pending_insert_count_ - 1;
    for (std::size_t t = 0; t < times; ++t) {
        for (const InsertOp& op : insert_ops_) {
            ApplyInsertOp(op);
        }
    }
    //insert_ops_ 为空 (如 i<Esc>) 时上面的循环自然什么都不做, 也不产生 modified
}

//I: 当前行第一个非空白字符处进入 Insert (README:1363-1369)
void Editor::EnterInsertLineStart(std::optional<size_t> count) {
    Position cursor = window_.GetCursor();
    const std::string& line = buffer_.GetLineAt(cursor.row_);
    cursor.column_ = line.size();
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != ' ' && line[i] != '\t') {
            cursor.column_ = i;
            break;
        }
    }
    //行内只有空格/Tab 或空行 → 保持最后一列
    EnterInsert(cursor, count);
}

//A: 当前行行末进入 Insert (README:1379-1383; 空行即 (row, 0))
void Editor::EnterInsertLineEnd(std::optional<size_t> count) {
    Position cursor = window_.GetCursor();
    cursor.column_ = buffer_.GetLineAt(cursor.row_).size();
    EnterInsert(cursor, count);
}

//o: 当前行下方插入空行并进入 Insert (README:1385-1389; 最后一行也可用)
void Editor::EnterInsertBelow(std::optional<size_t> count) {
    Position cursor = window_.GetCursor();
    buffer_.InsertLine(cursor.row_ + 1); // 标记随原语维护
    EnterInsert({cursor.row_ + 1, 0}, count);
    RecordInsertOp({InsertOp::Kind::OpenBelow, 0}); // 进入动作计入回放序列 ([count]o 回放出多行)
}

//O: 当前行上方插入空行并进入 Insert (README:1391-1395; 第一行也可用)
void Editor::EnterInsertAbove(std::optional<size_t> count) {
    Position cursor = window_.GetCursor();
    buffer_.InsertLine(cursor.row_); // 标记随原语维护
    EnterInsert({cursor.row_, 0}, count);
    //进入动作记为 Split 而不是 OpenAbove (OJ 实测, 测试点 549):
    //vim 的 count-O 回放 = "在光标处换行"模型——每个副本插在前一个副本下方、光标跟随下移,
    //3O<Esc> 后光标停在最下面的空行; 记为 Split 后回放恰好复现该行为
    RecordInsertOp({InsertOp::Kind::Split, 0});
}

//d{motion}/dd/D
//TODO(文本的修正与移动一): 实现
void Editor::ExecuteDelete(const EditorAction& action) {
    Position cursor = window_.GetCursor();
    size_t desired_column = window_.GetDesiredColumn();
    Range range;
    size_t count = action.count_.value_or(1);
    //1. 组装 range:
    //   - motion_spec_ 有值 → EditEngine::ComputeRange(buffer_, cursor, desired_column, spec);
    //     nullopt (motion 失败) → 整条命令取消: 不修改文本/寄存器, 光标与视口不动, 直接返回 (README:897)
    //   - motion_spec_ 无值 (dd) → Linewise 当前行, count = action.count_.value_or(1),
    //     end.row_ = min(begin.row_ + count - 1, 末行)
    if (action.motion_spec_) {
        auto pre_range = EditEngine::ComputeRange(buffer_, cursor, desired_column, *action.motion_spec_);
        if (!pre_range) { return; }
        else { range = *pre_range; }
    }
    else {
        range.kind_ = RangeKind::Linewise;
        range.begin_ = {cursor.row_, 0};
        range.end_ = {std::min(cursor.row_ + count - 1, buffer_.GetLineCount() - 1), 0};
    }
    //2. d 专用: EditEngine::NormalizeDeleteRange (README:1475 自动转 linewise)
    range = EditEngine::NormalizeDeleteRange(buffer_, range);
    //3. 空范围 (CharacterwiseExclusive 且 begin == end) → 
    // 不删除、不写 register, 直接返回 (README:897)
    if (range.kind_ == RangeKind::CharacterwiseExclusive && range.begin_ == range.end_) { return; }
    //4. std::string text = buffer_.EraseRange(range) → 
    // register_.Set('"', text, 类型)
    //   (Linewise → RangeType::Linewise; 其余 → 
    // RangeType::Characterwise; README:1471)
    std::string deleted_text = buffer_.EraseRange(range);
    register_.Set('"', deleted_text, (range.kind_ == RangeKind::Linewise) ? 
                  RangeType::Linewise : RangeType::Characterwise);
    //5. 光标: Linewise → EditEngine::CursorAfterLinewiseDelete(buffer_, range);
    //   Characterwise → EditEngine::CursorAfterCharacterwiseDelete(buffer_, range);
    //   window_.SetCursor(buffer_, pos, false) (同时更新 curswant 与视口)
    Position new_cursor = (range.kind_ == RangeKind::Linewise)
        ? EditEngine::CursorAfterLinewiseDelete(buffer_, range)
        : EditEngine::CursorAfterCharacterwiseDelete(buffer_, range);
    window_.SetCursor(buffer_, new_cursor, false);
    //6. 修改了 Buffer → search_engine_.Invalidate() (搜索缓存失效, 串与方向保留)
    search_engine_.Invalidate();
}

//c{motion}/cc/C
//TODO(文本的修正与移动一): 实现
void Editor::ExecuteChange(const EditorAction& action) {
    //1. 组装 range: 与 ExecuteDelete 相同, 但【不】调用 NormalizeDeleteRange
    Position cursor = window_.GetCursor();
    size_t desired_column = window_.GetDesiredColumn();
    Range range;
    size_t count = action.count_.value_or(1);
    if (action.motion_spec_) {
        //README:951 课程特例 1: 光标在非空白字符上时, cw/cW 等价于 ce/cE (不包含后方空白);
        //若光标已在空白上 (或空行唯一位置), 则按普通 w 处理 (与 vim nv_wordcmd 一致).
        //count 语义: [count1]c[count2]w 两侧 count 相乘后作为 e/E 的 count (vim 2cw == 2ce).
        MotionSpec spec = *action.motion_spec_;
        bool cw_special = false;
        if (spec.motion_ == Motion::WordForward || spec.motion_ == Motion::WordForwardCap) {
            const std::string& line = buffer_.GetLineAt(cursor.row_);
            if (cursor.column_ < line.size() && line[cursor.column_] != ' '
                && line[cursor.column_] != '\t') {
                cw_special = true;
                bool cap = (spec.motion_ == Motion::WordForwardCap);
                range.kind_ = RangeKind::CharacterwiseInclusive;
                range.begin_ = cursor;
                range.end_ = MotionEngine::ComputeChangeWordEnd(buffer_, cursor,
                                                                cap, spec.count_.value_or(1));
            }
        }
        if (!cw_special) {
            auto pre_range = EditEngine::ComputeRange(buffer_, cursor, desired_column, spec);
            if (!pre_range) { return; }
            else { range = *pre_range; }
        }
    }
    else {
        range.kind_ = RangeKind::Linewise;
        range.begin_ = {cursor.row_, 0};
        range.end_ = {std::min(cursor.row_ + count - 1, buffer_.GetLineCount() - 1), 0};
    }
    //2. 空范围 → 不删除文本, 直接在当前位置进入 Insert (README:1485)
    //   注意不传 count: [count] 属于 operator 范围, 不参与插入回放
    //   (vim 实测 3c$foo<Esc> 的 foo 只出现一次; README:1403 的回放只限 i/I/a/A/o/O)
    if (range.kind_ == RangeKind::CharacterwiseExclusive && range.begin_ == range.end_) {
        EnterInsert(range.begin_);
        return;
    }
    //3. 删除并写 register (与 d 相同, README:1489)
    size_t line_count_before = buffer_.GetLineCount();
    std::string deleted_text = buffer_.EraseRange(range);
    register_.Set('"', deleted_text, (range.kind_ == RangeKind::Linewise) ? 
                  RangeType::Linewise : RangeType::Characterwise);
    //4. 进入 Insert:
    //   - Characterwise: 在 range.begin_ 进入 (EnterInsert(begin))
    //   - Linewise: 删除后 buffer_.InsertLine(begin.row_) 在原范围位置保留一个空行,
    //     在该空行唯一位置进入 Insert (README:1487);
    //     例外: 删掉的范围覆盖整个文件时 EnsureNonEmpty 留下的空行就是"保留的空行",
    //     不再补插 (vim 实测 cc 唯一行 → 一个空行)
    if (range.kind_ == RangeKind::Linewise) {
        bool whole_file = (range.begin_.row_ == 0 && range.end_.row_ == line_count_before - 1);
        if (!whole_file) {
            buffer_.InsertLine(range.begin_.row_);
        }
    }
    EnterInsert(range.begin_);
    //5. 修改了 Buffer → search_engine_.Invalidate()
    search_engine_.Invalidate();
}

//x/[count]x (backward = false) 与 X/[count]X (backward = true)
//TODO(文本的修正与移动一): 实现 — README:1433-1459
void Editor::ExecuteDeleteChar(bool backward, std::optional<size_t> count) {
    Position cursor = window_.GetCursor();
    const std::string line = buffer_.GetLineAt(cursor.row_);
    std::string deleted_text = "";
    //1. 空行 → 无效果, 直接返回
    if (line.empty()) { return; }
    if (!backward) {
        //2. 实际删除数 n = min(count, 可删字符数)
        //   (x: 光标到行末的字符数)
        size_t n = std::min(count.value_or(1), line.size() - cursor.column_);
        if (n == 0) { return; }
        //3. 删除并收集文本 (x 反复删同一列: 后面的字符依次补上来)
        for (size_t i = 0; i < n; i++) {
            deleted_text += buffer_.GetLineAt(cursor.row_)[cursor.column_];
            buffer_.EraseCharacter(cursor.row_, cursor.column_);
        }
        //4. 写 register (charwise, vim 默认行为: x/X 写入 unnamed)
        register_.Set(Register::kUnnamed, deleted_text, RangeType::Characterwise);
        //5. 光标: EditEngine::CursorAfterX; 
        // window_.SetCursor 更新 curswant
        Position new_cursor = EditEngine::CursorAfterX(buffer_, cursor, n);
        window_.SetCursor(buffer_, new_cursor, false);
        //6. 修改了 Buffer → search_engine_.Invalidate()
        search_engine_.Invalidate();
    }
    else {
        //2. 实际删除数 n = min(count, 可删字符数)
        // X: 光标左侧的字符数; X 在行首时 n = 0 直接返回
        size_t n = std::min(count.value_or(1), cursor.column_);
        if (n == 0) { return; }
        //3. 删除并收集文本
        deleted_text = line.substr(cursor.column_ - n, n);
        for (size_t i = 0; i < n; i++) {
            buffer_.EraseCharacter(cursor.row_, cursor.column_ - n);
        }
        //4. 写 register (charwise, vim 默认行为: x/X 写入 unnamed)
        register_.Set(Register::kUnnamed, deleted_text, RangeType::Characterwise);
        //5. 光标: EditEngine::CursorAfterXBack; 
        // window_.SetCursor 更新 curswant
        Position new_cursor = EditEngine::CursorAfterXBack(buffer_, cursor, n);
        window_.SetCursor(buffer_, new_cursor, false);
        //6. 修改了 Buffer → search_engine_.Invalidate()
        search_engine_.Invalidate();
    }
}

//r/[count]r
void Editor::ExecuteReplace(const EditorAction& action) {
    Position cursor = window_.GetCursor();
    const std::string &line = buffer_.GetLineAt(cursor.row_);
    //1. 空行 → 无效果
    if (line.empty()) { return; }
    //2. 行内从光标到行末的字符数 < count → 整条命令失败: 不修改任何字符,
    //   光标与 curswant 不变, 不写 register (README:1501)
    size_t count = action.count_.value_or(1);
    if (line.size() - cursor.column_ < count || !action.arg_) { return; }
    //3. buffer_.ReplaceCharacters(row, col, count, char)
    // (与原字符相同也算修改, README:1507)
    buffer_.ReplaceCharacters(cursor.row_, cursor.column_, count, action.arg_.value());
    //4. 光标停在最后一个被替换的字符上; curswant 更新为其显示列
    cursor.column_ += count - 1;
    window_.JumpTo(buffer_, cursor);
    //5. 不写 register (README:1509)
    //6. 修改了 Buffer → search_engine_.Invalidate()
    search_engine_.Invalidate();
}

//J/[count]J
//TODO(文本的修正与移动一): 实现 — README:1515-1533
//1. 合并行数 = max(2, count); 行数不足时按实际 (当前行已是末行 → 无效果)
//2. 循环: 每次 buffer_.JoinForJ(row) 之前记录累计结果长度 (lines_[row].size());
//   最后一次拼接前的那个长度就是光标位置
//3. 光标 = EditEngine::CursorAfterJoin (钳到合法范围); window_.SetCursor
//4. 不写 register (README:1535)
//5. 修改了 Buffer → search_engine_.Invalidate()
void Editor::ExecuteJoin(std::optional<size_t> count) {
    (void)count;
}

//y{motion}/yy/Y
//TODO(拷贝与粘贴): 实现 — README:1685-1691
//1. 组装 range: 与 ExecuteDelete 完全相同的两路 (motion_spec_ → EditEngine::ComputeRange,
//   motion 失败 → 整条命令取消; 无 motion_spec_ (yy/Y) → Linewise 当前行起 count 行,
//   count = action.count_.value_or(1), 超末行截断)
//2. 空范围 (CharacterwiseExclusive 且 begin == end) → 不覆盖寄存器, 直接返回 (README:897)
//3. 取出 range 文本 (【注意】Buffer 目前只有 EraseRange 没有"只读取文本"的接口,
//   需新增 Buffer::GetRangeText(range) 或用行级/字符级原语拼接; Linewise 行间以 '\n' 连接),
//   写入 register_.Set('"', text, Linewise/Characterwise), 不修改 Buffer
//4. 光标 (README:1689-1691):
//   - Characterwise: 停在 range.begin_ (文本序起点)
//   - Linewise 向后复制 (range.end_.row_ 在原光标之后): 光标保持原位
//   - Linewise 向前复制 (如 yk): 光标移到对应 motion 的目标位置,
//     列遵循该 motion 的规则并钳到目标行合法字符位置
//   - yy/[count]Y: 光标保持原位
//   window_.SetCursor 更新 curswant; yank 不修改 Buffer → 不调用 search_engine_.Invalidate
void Editor::ExecuteYank(const EditorAction& action) {
    (void)action;
}

//p / P (before = false → 光标之后插入, true → 光标之前插入)
//TODO(拷贝与粘贴): 实现 — README:1697-1709
//1. 寄存器无内容 → 无效果; 取 register_.GetText('"') 与 GetType('"'),
//   [count] 指把文本重复拼接 count 次后粘贴 (不改变寄存器内容, README:1679)
//2. Characterwise: p 从 cursor.column_ + 1 插入, P 从 cursor.column_ 插入
//   (从该行唯一合法位置开始插入); 光标停在最后一个被插入的字符上,
//   最后插入的位置是空行 → 光标停在该空行唯一合法位置
//3. Linewise: p 插入当前行之后, P 插入当前行之前 (用 buffer_.InsertLine 逐行插入,
//   Buffer 空 (只有一行空行) 时先把 '\n' 加在文本前 (P) / 后 (p), README:1707);
//   光标位于新插入文本第一行的第一个非空白字符 (全空白行停最后一个字符, 空行停唯一位置)
//4. 修改了 Buffer → search_engine_.Invalidate(); [count] 拼接: charwise 直接拼接,
//   linewise 在拼接过程中加入必要换行符 (README:1709)
void Editor::ExecutePaste(bool before, std::optional<size_t> count) {
    (void)before;
    (void)count;
}

//</>{motion} 与 << //>> (right = false → 左缩进, true → 右缩进)
//TODO(缩进): 实现 — README:1637-1663
//1. 组装 range: 与 ExecuteDelete 相同 (motion_spec_ → ComputeRange, 失败取消;
//   doubled (<<//>>) → Linewise 当前行起 count 行, 两侧 count 乘积是【行数】不是缩进级数)
//2. motion 失败或 Characterwise range 为空 → 无效果 (README:1649)
//3. 处理 range 中实际包含字符或换行的所有文本行:
//   - Exclusive range 终点恰为较后行的第 0 列 → 不包含该终点行; Linewise 包含所有行 (含空行)
//   - 每行前导空白 (空格/Tab) 的宽度 W: 右移 → W + 4 个 space; 左移 → max(0, W - 4) 个 space
//     (用 TextLayout 的显示宽度辅助函数换算, 处理 Tab 展开)
//   - 真正空行保持为空; 只有空白的非空行同样按宽度规则处理
//   - 非前导空白与正文完全不变
//4. 成功得到至少包含一行的范围后: 光标移到第一条受影响行, 列遵循 ^ 的规则
//   (即使该次没有改变文本); 不写 register (README:1663)
//5. 修改了 Buffer → search_engine_.Invalidate()
void Editor::ExecuteShift(bool right, const EditorAction& action) {
    (void)right;
    (void)action;
}

//~/[count]~
void Editor::ExecuteFlipCase(std::optional<size_t> count) {
    Position cursor = window_.GetCursor();
    const std::string &line = buffer_.GetLineAt(cursor.row_);
    //1. 空行 → 无效果
    if (line.empty()) { return; }
    //2. 实际处理数 n = min(count, 光标到行末的字符数); 逐字符 buffer_.FlipCaseCharacter
    //   (非字母也消耗计数但不翻转; 全是非字母 → 不置 modified, 但光标照常移动, README:1551)
    size_t n = std::min(count.value_or(1), line.length() - cursor.column_);
    bool flipped = false;
    for (size_t i = 0; i < n; i++) {
        if (buffer_.FlipCaseCharacter(cursor.row_, cursor.column_ + i)) { flipped = true; };
    }
    //3. 光标 = EditEngine::CursorAfterFlip; window_.SetCursor
    Position cursor_after = EditEngine::CursorAfterFlip(buffer_, cursor, n);
    window_.SetCursor(buffer_, cursor_after, false);
    //4. 不写 register (README:1549)
    //5. 若有字母被翻转 (Buffer 已修改) → search_engine_.Invalidate()
    if (flipped) { search_engine_.Invalidate(); }
}

} // namespace sjtu