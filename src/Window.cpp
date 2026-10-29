#include "Window.hpp"
#include "TextLayout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace sjtu {


void Window::Resize(ScreenSize terminal_size) {
    //底部留一行给命令或提示, 其余作为正文区域; 正文行数和列数都至少取 1
    //修改视口即可
    viewport_.rows_ = terminal_size.rows_ > 1? terminal_size.rows_ - 1: 1;
    viewport_.columns_ = std::max<std::size_t>(terminal_size.columns_, 1);
}

void Window::ApplyMotion(const Buffer& buffer, MotionSpec spec) {
    //1. 根据 motion 调用移动: hjkl 走原有的 Move* 函数 (已支持 count 与 curswant 钳制,
    //   保持 Basic 路径不变); 其余 motion 委托 MotionEngine 纯函数计算
    //2. 将行列限制在 Normal 模式的合法范围内 (Buffer 应始终至少有一行)
    //3. 调整视口, 让移动后的光标可见 (EnsureCursorVisible)
    size_t count = spec.count_.value_or(1);
    switch (spec.motion_) {
        case Motion::Left: {
            MoveLeft(buffer, count);
            break;
        }
        case Motion::Right: {
            MoveRight(buffer, count);
            break;
        }
        case Motion::Up: {
            MoveUp(buffer, count);
            break;
        }
        case Motion::Down: {
            MoveDown(buffer, count);
            break;
        }
        default: {
            MotionResult result = MotionEngine::Compute(buffer, cursor_, desired_column_, spec);
            if (!result.ok_) {
                //命令失败: 光标与视口不动, 但某些命令仍要更新 curswant
                //(如末行 [count]$ 失败时 curswant 仍设为极大值)
                ApplyCurswant(buffer, result.curswant_);
                return;
            }
            cursor_ = result.target_;
            ApplyCurswant(buffer, result.curswant_);
            break;
        }
    }

    // 限制行列范围
    size_t max_row = buffer.GetLineCount() - 1;
    cursor_.row_ = std::min(cursor_.row_, max_row);
    size_t total_columns = buffer.GetLineAt(cursor_.row_).size();
    size_t max_column = total_columns == 0? 0: total_columns - 1;
    cursor_.column_ = std::min(cursor_.column_, max_column);

    // 令光标可见
    EnsureCursorVisible(buffer);
}

void Window::ApplyCurswant(const Buffer& buffer, CurswantPolicy policy) {
    switch (policy) {
    case CurswantPolicy::Keep:
        //上下移动保留原目标列 (MoveUp/MoveDown 已实现"经短行钳制但不丢失")
        return;
    case CurswantPolicy::SetToTarget:
        //把期望列更新为光标当前所在位置的显示列
        desired_column_ = BufferColumnToRenderColumn(buffer.GetLineAt(cursor_.row_), cursor_.column_);
        return;
    case CurswantPolicy::SetMax:
        //$: 设为极大值, 之后的 j/k 会始终钳到行尾
        desired_column_ = std::numeric_limits<size_t>::max();
        return;
    }
}

void Window::EnsureCursorVisible(const Buffer& buffer) {
    //1. 光标高于或低于可见区域时, 调整 top_, 使光标刚好进入区域
    //2. 把光标的字符下标换算成显示列, 再用相同思路调整 left_
    // 显示列换算
    const std::string& text_line = buffer.GetLineAt(cursor_.row_);
    std::size_t render_column = BufferColumnToRenderColumn(text_line, cursor_.column_);
    // 调整 top_
    if (cursor_.row_ < viewport_.top_) viewport_.top_ = cursor_.row_;
    if (cursor_.row_ > viewport_.top_ + viewport_.rows_ - 1) viewport_.top_ = cursor_.row_ - viewport_.rows_ + 1;
    // 调整 left_
    if (render_column < viewport_.left_) viewport_.left_ = render_column;
    if (render_column > viewport_.left_ + viewport_.columns_ - 1) viewport_.left_ = render_column - viewport_.columns_ + 1;
}

const Position& Window::GetCursor() const {
    //返回当前光标位置的只读引用
    return cursor_;
}

const Viewport& Window::GetViewport() const {
    //返回当前可见区域的只读引用, 供 Renderer 绘制
    return viewport_;
}


void Window::SetCursor(const Buffer& buffer, Position position, bool allow_line_end) {
    //1. 先限制行号, 再根据该行长度和 allow_line_end 限制列号
    //2. 用新位置更新上下移动时的目标显示列
    //3. 调整视口, 保证光标可见
    // 限制行号
    cursor_.row_ = std::min(position.row_, buffer.GetLineCount() - 1);
    // 限制列号
    const std::string& text_line = buffer.GetLineAt(cursor_.row_);
    std::size_t max_column = text_line.size();
    if (!allow_line_end && max_column > 0) max_column--;
    cursor_.column_ = std::min(position.column_, max_column);
    // 更新目标显示列和视口
    desired_column_ = BufferColumnToRenderColumn(text_line, cursor_.column_);
    EnsureCursorVisible(buffer);
}


void Window::MoveLeft(const Buffer& buffer, std::size_t count) {
    //向左移动 count 个字符, 最多到行首, 并更新目标显示列
    if (count <= cursor_.column_) cursor_.column_ -= count;
    else cursor_.column_ = 0;
    desired_column_ = BufferColumnToRenderColumn(buffer.GetLineAt(cursor_.row_), cursor_.column_);
}

void Window::MoveRight(const Buffer& buffer, std::size_t count) {
    //向右移动 count 个字符, 最多到最后一个字符, 并更新目标显示列
    std::size_t total_columns = buffer.GetLineAt(cursor_.row_).size();
    std::size_t max_column = total_columns > 0? total_columns - 1: 0;
    if (count <= max_column - cursor_.column_) cursor_.column_ += count;
    else cursor_.column_ = max_column;
    desired_column_ = BufferColumnToRenderColumn(buffer.GetLineAt(cursor_.row_), cursor_.column_);   
}


void Window::MoveUp(const Buffer& buffer, std::size_t count) {
    //先算目标行, 最多到第一行, 再将期望的显示列换算成目标行的字符下标
    //经过短行时不要更新 desired_column_, 这样继续移动到长行时能回到原来的列
    // 确定移动的行数
    if (count <= cursor_.row_) cursor_.row_ -= count;
    else cursor_.row_ = 0;

    // 确定该行是否足够长, 使得显示列可以为期望的显示列
    const std::string& text_line = buffer.GetLineAt(cursor_.row_);
    std::size_t total_columns = text_line.size();
    std::size_t max_column = total_columns > 0? total_columns - 1: 0;
    std::size_t max_render_column = BufferColumnToRenderColumn(text_line, max_column);
    if (desired_column_ <= max_render_column) cursor_.column_ = RenderColumnToBufferColumn(text_line, desired_column_);
    else cursor_.column_ = max_column;
}

void Window::MoveDown(const Buffer& buffer, std::size_t count) {
    //先算目标行, 最多到最后一行, 再根据 desired_column_ 寻找目标字符
    //与向上移动一样, 保留期望显示列
    // 确定移动的行数
    std::size_t max_row = buffer.GetLineCount() - 1;
    if (count <= max_row - cursor_.row_) cursor_.row_ += count;
    else cursor_.row_ = max_row;

    // 确定该行是否足够长, 使得显示列可以为期望的显示列
    const std::string& text_line = buffer.GetLineAt(cursor_.row_);
    std::size_t total_columns = text_line.size();
    std::size_t max_column = total_columns > 0? total_columns - 1: 0;
    std::size_t max_render_column = BufferColumnToRenderColumn(text_line, max_column);
    if (desired_column_ <= max_render_column) cursor_.column_ = RenderColumnToBufferColumn(text_line, desired_column_);
    else cursor_.column_ = max_column;
}

} // namespace sjtu
