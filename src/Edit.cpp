#include "Edit.hpp"

#include "Motion.hpp"

namespace sjtu {

//motion → Range (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
std::optional<Range> EditEngine::ComputeRange(const Buffer& buffer, Position origin,
                                             size_t curswant, MotionSpec spec) {
    //0. hjkl 的 operator 用法 (README 速查表 4.1/4.2): MotionEngine 不计算它们,
    //   需要在这里补出端点与范围类型
    {
        size_t count = spec.count_.value_or(1);
        const std::string& line = buffer.GetLineAt(origin.row_);
        Range range;
        switch (spec.motion_) {
        case Motion::Left:
            //h: 行首时产生空范围 (不失败)
            range.begin_ = {origin.row_, origin.column_ >= count ? origin.column_ - count : 0};
            range.end_ = origin;
            range.kind_ = RangeKind::CharacterwiseExclusive;
            return range;
        case Motion::Right:
            //l: 终点是 min(col+count, 行长) 的插入位置 (可等于行长; 空行范围为空)
            range.begin_ = origin;
            range.end_ = {origin.row_, std::min(origin.column_ + count, line.size())};
            range.kind_ = RangeKind::CharacterwiseExclusive;
            return range;
        case Motion::Up:
            //k: linewise; 执行前已在首行 → motion 失败
            if (origin.row_ == 0) return std::nullopt;
            range.begin_ = {origin.row_ >= count ? origin.row_ - count : 0, 0};
            range.end_ = {origin.row_, 0};
            range.kind_ = RangeKind::Linewise;
            return range;
        case Motion::Down:
            //j: linewise; 执行前已在末行 → motion 失败; 越界延伸到末行
            if (origin.row_ == buffer.GetLineCount() - 1) return std::nullopt;
            range.begin_ = {origin.row_, 0};
            range.end_ = {std::min(origin.row_ + count, buffer.GetLineCount() - 1), 0};
            range.kind_ = RangeKind::Linewise;
            return range;
        default:
            break; // 其余 motion 走 MotionEngine
        }
    }
    //1. MotionEngine::Compute(buffer, origin, curswant, spec) 得到 MotionResult
    MotionResult res = MotionEngine::Compute(buffer, origin, curswant, spec);
    //2. ok_ == false → 返回 nullopt
    if (!res.ok_) { return std::nullopt; }
    //3. 按文本序组装 Range: begin = min(origin, target), end = max(origin, target)
    //   (Position::operator< 已按文本序定义), kind = result.kind_
    Range range;
    range.begin_ = std::min<Position>(origin, res.target_);
    range.end_ = std::max<Position>(origin, res.target_);
    range.kind_ = res.kind_;
    return range;
}

//d 的自动转 linewise (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Range EditEngine::NormalizeDeleteRange(const Buffer& buffer, Range range) {
    //1. range.kind_ == Linewise 或 begin.row_ == end.row_ → 原样返回
    if (range.kind_ == RangeKind::Linewise || range.begin_.row_ == range.end_.row_) { return range; }
    bool toLinewise = true;
    //2. 检查 begin 行 [0, begin.column_) 是否全为空格/Tab
    std::string line1 = buffer.GetLineAt(range.begin_.row_).substr(0, range.begin_.column_);
    if (line1.find_first_not_of(" \t") != std::string::npos) { toLinewise = false; }
    else {
        //3. 检查 end 行结束位置之后到行尾是否没有非空白字符
        //   (CharacterwiseInclusive 从 end.column_ + 1 起, 
        //   Exclusive 从 end.column_ 起——注意按字面核对)
        std::string line2 = "";
        switch (range.kind_) {
            case RangeKind::CharacterwiseExclusive: {
                line2 = buffer.GetLineAt(range.end_.row_).substr(range.end_.column_);
                break;
            }
            case RangeKind::CharacterwiseInclusive: {
                line2 = buffer.GetLineAt(range.end_.row_).substr(range.end_.column_ + 1);
                break;
            }
        }
        if (line2.find_first_not_of(" \t") != std::string::npos) { toLinewise = false; }
    }
    //4. 两个条件都满足 → kind_ = Linewise (行号保持 begin.row_ 到 end.row_)
    if (toLinewise) { range.kind_ = RangeKind::Linewise; }
    return range;
}

//characterwise 删除后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterCharacterwiseDelete(const Buffer& buffer_after, const Range& range) {
    const std::string &line = buffer_after.GetLineAt(range.begin_.row_);
    Position cursor = range.begin_;
    if (line.empty()) {
        cursor.column_ = 0;
    }
    else {
        cursor.column_ = std::min(range.begin_.column_, line.size() - 1);
    }
    return cursor;
}

//linewise 删除后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterLinewiseDelete(const Buffer& buffer_after, const Range& range) {
    Position cursor;
    size_t row = std::min(range.begin_.row_, buffer_after.GetLineCount() - 1);
    cursor.row_ = row;
    cursor.column_ = MotionEngine::LineJumpTarget(buffer_after, row).column_;
    return cursor;
}

//[count]x 后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterX(const Buffer& buffer_after, Position origin, size_t count) {
    Position pos = origin;
    const std::string &line = buffer_after.GetLineAt(origin.row_);
    if (line.empty()) { pos.column_ = 0; }
    else { pos.column_ = std::min(line.size() - 1, origin.column_); }
    return pos;
}

//[count]X 后的光标 (契约见 Edit.hpp 的注释)
//TODO(文本的修正与移动一): 实现
Position EditEngine::CursorAfterXBack(const Buffer& buffer_after, Position origin, size_t count) {
    Position pos = origin;
    pos.column_ = (origin.column_ >= count) ? origin.column_ - count : 0;
    return pos;
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
