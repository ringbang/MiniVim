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
        //把期望列更新为光标所在格子的显示列; 光标落在 Tab 上时取 Tab 的最后一格 (与 vim 一致)
        desired_column_ = CursorCellColumn(buffer.GetLineAt(cursor_.row_), cursor_.column_);
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

void Window::ScrollCursorLine(const Buffer& buffer, char kind, std::optional<size_t> count) {
    //  [count] 给定 → 光标先跳到第 count 行 (钳到末行; 列保持并钳到目标行末, vim 实测),
    //                 curswant 更新为落地字符的显示列;
    //  无 count → 光标与 curswant 完全不变.
    //  然后只调整视口 top_, 使当前行对齐正文顶部/中间/底部; 不碰 left_, 不调 EnsureCursorVisible
    //  (构造保证光标在新视口内).
    if (count) {
        size_t target = (*count > 0) ? *count - 1 : 0;
        cursor_.row_ = std::min(target, buffer.GetLineCount() - 1);
        const std::string& line = buffer.GetLineAt(cursor_.row_);
        size_t max_col = line.empty() ? 0 : line.size() - 1;
        cursor_.column_ = std::min(cursor_.column_, max_col);
        //curswant 保留不重算 (vim 实测: [count]zt 只钳列, 不动 curswant; OJ 测试点 "retained desired column")
    }

    //对齐偏移: t → 0; z → (H-1)/2 (偶数高度取靠上的中间行); b → H-1
    size_t H = viewport_.rows_;
    size_t offset = 0;
    if (kind == 'z') {
        offset = (H - 1) / 2;
    }
    else if (kind == 'b') {
        offset = H - 1;
    } 

    //top_ 钳到 [0, N-1]
    size_t N = buffer.GetLineCount();
    viewport_.top_ = (cursor_.row_ >= offset) ? cursor_.row_ - offset : 0;
    viewport_.top_ = std::min(viewport_.top_, N - 1);
}

//<C-e>/<C-y>/<C-d>/<C-u> (README:1799-1803, vim 9.1 实测):
//  kind: 'e' = 下滚单行, 'y' = 上滚单行, 'd' = 下滚半屏, 'u' = 上滚半屏
//  C-e/C-y: top 按 min(N, top+count) / max(1, top-count) (1 基); 光标保护:
//    被挤出可见区间 [top, min(N, top+H-1)] 时移到最近行, 列按原 curswant 选 (README:1803)
//  C-d/C-u: S = max(1, H/2) 或 count (钳到 H, vim 实测); top 下滚 min(S, room) (room =
//    底部剩余空行), 光标同步下移 S (钳到末行); 上滚对称; 光标已在边界 → 整条命令无效果;
//    完成后光标列归 0、curswant 归 0 (vim halfpage 的 beginline(BL_SOL|BL_FIX))
void Window::ScrollView(const Buffer& buffer, char kind, std::optional<size_t> count) {
    size_t N = buffer.GetLineCount();
    size_t H = viewport_.rows_;
    bool down = (kind == 'e' || kind == 'd');
    bool half = (kind == 'd' || kind == 'u');
    size_t S = count.value_or(0);
    if (half) {
        if (S == 0) S = std::max<size_t>(1, H / 2);
        else S = std::min(S, H);
        //已在边界: 无效果 (vim nv_halfpage 实测: C-d 在末行 / C-u 在首行直接 beep 返回)
        if ((kind == 'd' && cursor_.row_ == N - 1) || (kind == 'u' && cursor_.row_ == 0)) {
            return;
        }
    } else {
        if (S == 0) S = 1;
    }

    if (down) {
        if (half) {
            //room = 底部到文件末尾的空行数 (1 基 botline = top_+H, 0 基换算)
            size_t bot = viewport_.top_ + H;
            size_t room = (N > bot) ? N - bot : 0;
            viewport_.top_ += std::min(S, room);
            cursor_.row_ = std::min(cursor_.row_ + S, N - 1);
        } else {
            //C-e: top = min(N, top + count) (1 基)
            viewport_.top_ = std::min(viewport_.top_ + S, N - 1);
        }
    } else {
        if (half) {
            viewport_.top_ = (viewport_.top_ >= S) ? viewport_.top_ - S : 0;
            cursor_.row_ = (cursor_.row_ >= S) ? cursor_.row_ - S : 0;
        } else {
            viewport_.top_ = (viewport_.top_ >= S) ? viewport_.top_ - S : 0;
        }
    }

    if (half) {
        //光标列归 0, curswant 归 0 (vim halfpage 的 beginline)
        cursor_.column_ = 0;
        desired_column_ = 0;
        return;
    }
    //C-e/C-y 光标保护 (README:1803): 挤出可见区间 → 移到最近行
    size_t lo = viewport_.top_;
    size_t hi = std::min(N - 1, viewport_.top_ + H - 1);
    if (cursor_.row_ < lo) cursor_.row_ = lo;
    else if (cursor_.row_ > hi) cursor_.row_ = hi;
    //按原 curswant 选列 (短行停在行末, 空行唯一合法位置)
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    size_t col = RenderColumnToBufferColumn(line, desired_column_);
    if (!line.empty() && col >= line.size()) col = line.size() - 1;
    cursor_.column_ = col;
}

const Position& Window::GetCursor() const {
    //返回当前光标位置的只读引用
    return cursor_;
}

const Viewport& Window::GetViewport() const {
    //返回当前可见区域的只读引用, 供 Renderer 绘制
    return viewport_;
}

const size_t& Window::GetDesiredColumn() const {
    // 返回期望显示列的只读引用
    return desired_column_;
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


//搜索/标记等跳转命令的通用入口: 钳制行列后更新光标与 curswant (见 Window.hpp 的说明)
void Window::JumpTo(const Buffer& buffer, Position position) {
    //TODO(光标的移动二): 实现
    //与 SetCursor(buffer, position, false) 类似 (钳制行号与列号 + EnsureCursorVisible),
    SetCursor(buffer, position, false);
    //但 curswant 用 CursorCellColumn 计算 (落在 Tab 上取 Tab 的最后一格, SetToTarget 语义)
    desired_column_ = CursorCellColumn(buffer.GetLineAt(cursor_.row_), cursor_.column_);
}

void Window::MoveLeft(const Buffer& buffer, std::size_t count) {
    //向左移动 count 个字符, 最多到行首, 并更新目标显示列
    if (count <= cursor_.column_) cursor_.column_ -= count;
    else cursor_.column_ = 0;
    //空行上 h 不移动, 也不更新 curswant (与 vim 一致, 否则会丢掉之前的目标列)
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    if (!line.empty()) desired_column_ = CursorCellColumn(line, cursor_.column_);
}

void Window::MoveRight(const Buffer& buffer, std::size_t count) {
    //向右移动 count 个字符, 最多到最后一个字符, 并更新目标显示列
    std::size_t total_columns = buffer.GetLineAt(cursor_.row_).size();
    std::size_t max_column = total_columns > 0? total_columns - 1: 0;
    if (count <= max_column - cursor_.column_) cursor_.column_ += count;
    else cursor_.column_ = max_column;
    //空行上 l 不移动, 也不更新 curswant (与 vim 一致, 否则会丢掉之前的目标列)
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    if (!line.empty()) desired_column_ = CursorCellColumn(line, cursor_.column_);
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
