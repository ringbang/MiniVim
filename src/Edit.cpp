#include "Edit.hpp"

#include "Motion.hpp"

namespace sjtu {

//motion → Range (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. MotionEngine::Compute(buffer, origin, curswant, spec) 得到 MotionResult
//2. ok_ == false → 返回 nullopt
//3. 按文本序组装 Range: begin = min(origin, target), end = max(origin, target)
//   (Position::operator< 已按文本序定义), kind = result.kind_
std::optional<Range> EditEngine::ComputeRange(const Buffer& buffer, Position origin,
                                             size_t curswant, MotionSpec spec) {
    (void)buffer;
    (void)origin;
    (void)curswant;
    (void)spec;
    return std::nullopt;
}

//d 的自动转 linewise (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. range.kind_ == Linewise 或 begin.row_ == end.row_ → 原样返回
//2. 检查 begin 行 [0, begin.column_) 是否全为空格/Tab
//3. 检查 end 行结束位置之后到行尾是否没有非空白字符
//   (CharacterwiseInclusive 从 end.column_ + 1 起, Exclusive 从 end.column_ 起——注意按字面核对)
//4. 两个条件都满足 → kind_ = Linewise (行号保持 begin.row_ 到 end.row_)
Range EditEngine::NormalizeDeleteRange(const Buffer& buffer, Range range) {
    (void)buffer;
    return range;
}

//characterwise 删除后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterCharacterwiseDelete(const Buffer& buffer_after, const Range& range) {
    (void)buffer_after;
    return range.begin_;
}

//linewise 删除后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterLinewiseDelete(const Buffer& buffer_after, const Range& range) {
    (void)buffer_after;
    return range.begin_;
}

//[count]x 后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterX(const Buffer& buffer_after, Position origin, size_t count) {
    (void)buffer_after;
    (void)count;
    return origin;
}

//[count]X 后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterXBack(const Buffer& buffer_after, Position origin, size_t count) {
    (void)buffer_after;
    (void)count;
    return origin;
}

//J 后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterJoin(const Buffer& buffer_after, size_t length_before_last_join) {
    (void)buffer_after;
    return {0, length_before_last_join};
}

//[count]~ 后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterFlip(const Buffer& buffer_after, Position origin, size_t count) {
    const std::string &line = buffer_after.GetLineAt(origin.row_);
    if (line.empty()) { return origin; }
    Position cursor = origin;
    cursor.column_ = std::min(line.length() - 1, cursor.column_ + count);
    return cursor;
}

} // namespace sjtu
