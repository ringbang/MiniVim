#include "Renderer.hpp"
#include "TextLayout.hpp"

#include <algorithm>

namespace sjtu {

//我们在 Render 中保留了所有需要用到控制序列的部分逻辑, 你不应该修改它们.
//啥是控制序列? 它们是嵌入在文本流中的 “指令”, 告诉终端执行特定操作 (比如移动光标、改变文字颜色、清屏或清除行内容).   
namespace {

void AppendClearedLine(std::string& frame, std::string_view contents, std::size_t width, bool newline) {
    //追加不超过 width 列的显示内容, 再用 ESC[K 清除这一行余下的旧内容
    //需要换行时同时输出回车和换行, 因为 raw 模式关闭了终端的自动输出转换
    //你在生成帧的时候生成每一行都应该调用这个函数
    frame.append(contents.substr(0, width));
    frame += "\x1b[K";
    if (newline) {
        frame += "\r\n";
    }
}

std::string CursorSequence(std::size_t row, std::size_t column) {
    //生成定位光标的 ANSI 转义序列, 这里接收的行列都从 1 开始
    return "\x1b[" + std::to_string(row) + ';' + std::to_string(column) + 'H';
}

}

std::string Renderer::Render(const Buffer& buffer, const Window& window, const RenderState& state) const {
    //1. 隐藏光标并回到屏幕左上角, 开始拼接新的一帧 (一帧就是一个长 string)
    //2. 按视口逐行取 Buffer 内容, 展开 Tab 后再横向截取; 超出文件的行显示~
    //3. 绘制底部一行, 优先显示命令行, 其次是提示信息, 最后是 Insert 模式标记
    //4. 根据当前模式计算光标的屏幕位置, 追加定位序列并重新显示光标
    //你应该区分我们的 Cursor 光标 (它代表当前用户在修改文件的哪个位置) 和终端显示的白色亮条, 白色亮条在屏幕中的位置是由光标位置和视口位置计算的
    auto& viewport = window.GetViewport();
    auto width = std::max<std::size_t>(viewport.columns_, 1);

    std::string frame;
    frame.reserve((viewport.rows_ + 1) * (width + 8));
    frame += "\x1b[?25l";
    frame += "\x1b[H";

    for (std::size_t screen_row = 0; screen_row < viewport.rows_; ++screen_row) {
       //这里是提示 2 中的部分
       std::size_t buffer_row = screen_row + viewport.top_;
       std::string left_cut_display_line;
       // 未超出文本的部分
       if (buffer_row < buffer.GetLineCount()) {
           std::string full_line = ExpandForDisplay(buffer.GetLineAt(buffer_row));
           left_cut_display_line = full_line.substr(std::min(viewport.left_, full_line.size()));
       }
       // 超出文本的部分
       else left_cut_display_line = "~";
       AppendClearedLine(frame, left_cut_display_line, width, true);
    }

    std::string bottom;
    if (state.mode_ == Mode::CommandLine) {
        bottom = ":" + state.command_;
    } else if (!state.message_.empty()) {
        bottom = state.message_;
    } else if (state.mode_ == Mode::Insert) {
        bottom = "-- INSERT --";
    }
    AppendClearedLine(frame, bottom, width, false);
    //这里是提示 3 中的部分
    //我们只会在 CommandMode 的时候检查一下底部的命令内容, 在 NormalMode 不会看底部, 所以 message 你可以随意写

    std::size_t cursor_row{0};
    std::size_t cursor_column{0};

    //计算 cursor_row 和 cursor_column 即可
    // 命令行模式
    if (state.mode_ == Mode::CommandLine) {
        cursor_row = viewport.rows_;
        cursor_column = state.command_.size() + 1; // 考虑冒号
    }    

    // 其他模式
    else {
        const std::string& text_line = buffer.GetLineAt(window.GetCursor().row_);
        cursor_row = window.GetCursor().row_ - viewport.top_;
        cursor_column = BufferColumnToRenderColumn(text_line, window.GetCursor().column_) - viewport.left_;
    }

    // 转换为 1-based
    cursor_row += 1;
    cursor_column += 1;

    frame += CursorSequence(cursor_row, cursor_column);
    frame += "\x1b[?25h";
    return frame;
}

std::string Renderer::ExpandForDisplay(std::string_view line) {
    //从左到右扫描 buffer 中一整行的实际内容, 并扩展到 render 应该输出的视图
    //你应该在 Render 中调用这个函数, 并把函数返回的结果按照视口剪切用于 Render 的某些行
    std::string display_line = "";
    for (const char& c: line) {
        if (c != '\t') display_line.push_back(c);
        else {
            std::size_t column = display_line.size();
            std::size_t num_of_spaces = NextScreenColumn(column, c) - column;
            display_line.append(num_of_spaces, ' ');
        }
    }
    return display_line;
}
} // namespace sjtu
