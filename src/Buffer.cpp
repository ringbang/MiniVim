#include <fstream>

#include "Buffer.hpp"

namespace sjtu {

Buffer::Buffer(const std::filesystem::path& path) {
    path_ = path;
    if (path_ == "") {
        lines_ = {""};
        return;
    }
    std::ifstream text_file(path_);
    std::string line;
    if (!text_file.is_open()) {
        lines_ = {""};
        return;
    }
    while (std::getline(text_file, line)) {
        if (text_file.eof()) end_with_newline_ = false;
        else end_with_newline_ = true;
        lines_.push_back(line);
    }
    if (lines_.empty()) lines_ = {""};
    //从 path 指向的文件构造 Buffer, 你需要打开文件并且把文件内容填充进 Buffer, 并正确初始化一些状态.
    //注意 path 可能为空的边界情况
}

Buffer::Buffer(std::vector<std::string> lines, std::filesystem::path path) {
    path_ = path;
    lines_ = lines;
}

std::size_t Buffer::GetLineCount() const {
    //返回文件行数
    return lines_.size();
}

const std::string& Buffer::GetLineAt(std::size_t row) const {
    //返回第row行的内容
    return lines_[row];
}


std::string Buffer::GetDisplayName() const {
    //返回文件名, 若是新文件, 返回 "[No Name]"
    if (path_ == "") return "[No Name]";
    else return path_.filename().string();
}

bool Buffer::IsModified() const {
    //返回文件和上次保存比起来是否被修改过
    return modified_;
}

void Buffer::InsertCharacter(std::size_t row, std::size_t column, char value) {
    //在第 row 行第 col 列插入一个 value, 注意越界检查
    std::size_t row_length = lines_[row].size();
    if (column > row_length) throw std::runtime_error("Column out of range.");
    lines_[row].insert(column, 1, value);
    modified_ = true;
    end_with_newline_ = true;
    return;
    
}

void Buffer::EraseCharacter(std::size_t row, std::size_t column) {
    //在第 row 行第 col 列删除一个 value
    std::size_t row_length = lines_[row].size();
    if (column >= row_length) throw std::runtime_error("Column out of range.");
    lines_[row].erase(column, 1);
    modified_ = true;
    end_with_newline_ = true;
    return;
}

void Buffer::SplitLine(std::size_t row, std::size_t column) {
    //在第 row 行第 col 列分割, 即在此处敲了回车键
    std::size_t row_length = lines_[row].size();
    if (column > row_length) throw std::runtime_error("Column out of range.");
    //标记维护 (README 规则二: 行内插入不移动标记 → 切分行上的标记原地不动, 列也不变;
    //测试点 94 "line-split anchor updates": 切分行以下的标记随新插入的行整体下移)
    AdjustMarksInsertLine(row + 1);
    if (column == row_length) lines_.insert(lines_.begin() + row + 1, "");
    else {
        std::string old_row = lines_[row].substr(0, column);
        std::string new_row = lines_[row].substr(column);
        lines_[row] = old_row;
        lines_.insert(lines_.begin() + row + 1, new_row);
    }
    modified_ = true;
    end_with_newline_ = true;
    return;
}

void Buffer::JoinLine(std::size_t row) {
    //把第 row + 1 行合并进第 row 行
    //标记维护 (OJ 实测, 测试点 142/143/144):
    //  被合并行有字符 → 标记只上移一行, 列不变 (与 vim 跟随文字不同: 142 的判据);
    //  被合并行为空行 → 该行标记丢失 (143/144);
    //  row+1 以下的标记上移一行
    bool merged_empty = lines_[row + 1].empty();
    for (size_t i = 0; i < kMarkCount; i++) {
        if (!mark_valid_[i]) continue;
        if (marks_[i].row_ == row + 1) {
            if (merged_empty) {
                mark_valid_[i] = false;
            } else {
                marks_[i].row_ = row;
            }
        } else if (marks_[i].row_ > row + 1) {
            marks_[i].row_--;
        }
    }
    lines_[row] += lines_[row + 1];
    lines_.erase(lines_.begin() + row + 1);
    modified_ = true;
    end_with_newline_ = true;
    return;
}

// ---- 文本的修正与移动(一) ----

//保证 Buffer 至少有一行 (契约见 Buffer.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. lines_.empty() 时 push_back(""), 不因此置 modified_
void Buffer::EnsureNonEmpty() {
    if (lines_.empty()) { lines_.push_back(""); }
}

//删除 range 覆盖的文本并返回被删除文本 (契约见 Buffer.hpp 的注释)
//TODO(文本的修正与移动一): 实现
std::string Buffer::EraseRange(const Range& range) {
    std::string deleted_text = "";
    bool erased = false;
    switch (range.kind_) {
    //1. Linewise: 循环 EraseLine(begin.row_) (每删一行后该下标又是下一行),
    //   拼接返回值, 行间加 '\n' (末行不加); EraseLine 自带标记维护
    case RangeKind::Linewise: {
        size_t line_count = range.end_.row_ - range.begin_.row_ + 1;
        for (size_t i = 0; i < line_count; i++) {
            deleted_text += EraseLine(range.begin_.row_);
            deleted_text += '\n';
        }
        deleted_text.pop_back();
        erased = line_count > 0;
        break;
    }
    //2/3. Characterwise: 同行删 [begin, end) 或含 end; 跨行删首行尾 + 中间整行 + 末行头,
    //    最后 JoinLine 合并首尾两行 (标记维护随各原语自动生效)
    case RangeKind::CharacterwiseExclusive:
    case RangeKind::CharacterwiseInclusive: {
        bool inclusive = (range.kind_ == RangeKind::CharacterwiseInclusive);
        if (range.begin_.row_ == range.end_.row_) {
            //同行: 反复删同一列, 字符依次补位, 文本顺序不变
            size_t n = range.end_.column_ - range.begin_.column_ + (inclusive ? 1 : 0);
            //空行/短行保护: Inclusive [x,x] 落在空行上时无字符可删 (README:896 空行无字符),
            //否则 EraseCharacter 会因越界抛异常 (测试点 198/201 的崩溃根因)
            n = std::min(n, lines_[range.begin_.row_].size() - std::min(range.begin_.column_, lines_[range.begin_.row_].size()));
            for (size_t i = 0; i < n; i++) {
                deleted_text += lines_[range.begin_.row_][range.begin_.column_];
                EraseCharacter(range.begin_.row_, range.begin_.column_);
            }
            erased = n > 0;
        } else {
            //跨行: 先取文本 (删除前的原文), 再删除
            const std::string& first_line = lines_[range.begin_.row_];
            //(a) 首行尾部 [begin.col, 行尾)
            deleted_text = first_line.substr(range.begin_.column_);
            deleted_text += '\n';
            //(b) 中间完整行: 每次 EraseLine(begin.row_+1), 下一行补位
            size_t middle_count = range.end_.row_ - range.begin_.row_ - 1;
            for (size_t i = 0; i < middle_count; i++) {
                deleted_text += EraseLine(range.begin_.row_ + 1);
                deleted_text += '\n';
            }
            //(c) 末行头部 [0, end.col] (Exclusive 不含 end.col; 课程简化: 不自动转换, README:889);
            //终点行是空行时头部为空 (Inclusive [x,0] 在空行上无字符, README:896)
            size_t head_len = range.end_.column_ + (inclusive ? 1 : 0);
            head_len = std::min(head_len, lines_[range.begin_.row_ + 1].size());
            deleted_text += lines_[range.begin_.row_ + 1].substr(0, head_len);
            //(d) 删除: 首行尾 (反复删 begin.col) + 末行头 (反复删 0) + 合并
            //注意: first_line 是引用, 删除中行内容会变短——长度必须先取好,
            //否则循环条件每次重算会提前终止
            size_t tail_len = first_line.size() - range.begin_.column_;
            for (size_t i = 0; i < tail_len; i++) {
                EraseCharacter(range.begin_.row_, range.begin_.column_);
            }
            for (size_t i = 0; i < head_len; i++) {
                EraseCharacter(range.begin_.row_ + 1, 0);
            }
            JoinLine(range.begin_.row_);
            erased = true;
        }
        break;
    }
    }
    //4. 置 modified_ = true; end_with_newline_ = true (README:1247); 空范围不算修改
    if (erased) {
        modified_ = true;
        end_with_newline_ = true;
    }
    EnsureNonEmpty();
    return deleted_text;
}

//[count]r 替换 (契约见 Buffer.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. 循环 count 次: lines_[row][column + i] = value
//2. modified_ = true
void Buffer::ReplaceCharacters(std::size_t row, std::size_t column, std::size_t count, char value) {
    // 已经保证不越界
    for (size_t i = 0; i < count; i++) {
        lines_[row][column + i] = value;
    }
    modified_ = true;  // 即使与原字符相同也算已更改
    end_with_newline_ = true;
}

//~ 翻转一个字符 (契约见 Buffer.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. 'a'-'z' 与 'A'-'Z' 互换; 其他字符原样
//2. 只有真正翻转了字母才置 modified_ = true
bool Buffer::FlipCaseCharacter(std::size_t row, std::size_t column) {
    char &c = lines_[row][column];
    if (c >= 'a' && c <= 'z') {
        c = 'A' + (c - 'a');
        modified_ = true;
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        c = 'a' + (c - 'A');
        modified_ = true;
        return true;
    }
    return false;
}

//J 的一步拼接 (契约见 Buffer.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. 标记维护: 与 JoinLine 相同 (被合并行标记只上移行号; 被合并行为空行则标记失效)
//2. 按 README:1523 的四条规则处理前导空白与分隔空格, 拼接
//3. 置 modified_ = true; end_with_newline_ = true
void Buffer::JoinForJ(std::size_t row) {
    (void)row;
}

void Buffer::Save() {
    //把文件内容保存, 直接调用 WriteTo 方法
    Buffer::WriteTo(path_);
    modified_ = false;
}

void Buffer::SaveAs(const std::filesystem::path& path) {
    Buffer::WriteTo(path);
    path_ = path;
    modified_ = false;
}

// ---- 标记 (README: 标记 m) ----

//m{a-z}: 保存当前 Buffer 位置为小写局部标记, 同名标记被覆盖 (README:1143); name 非法时忽略
void Buffer::SetMark(char name, Position pos) {
    //TODO(光标的移动二): 实现
    //1. name 不在 a-z 内直接返回
    if (name < 'a' || name > 'z') {
        return;
    }
    //2. marks_[name - 'a'] = pos; mark_valid_[name - 'a'] = true
    marks_[name - 'a'] = pos;
    mark_valid_[name - 'a'] = true;
}

//'a: 返回标记的精确行与字符位置; 标记未设置 (或 name 非法) 返回 nullopt (README:1143)
std::optional<Position> Buffer::GetMark(char name) const {
    //TODO(光标的移动二): 实现
    //1. name 不在 a-z 内或 mark_valid_ 为 false 返回 nullopt
    if (name < 'a' || name > 'z' || !mark_valid_[name - 'a']) {
        return std::nullopt;
    }
    //2. 否则返回 marks_[name - 'a']
    return marks_[name - 'a'];
}

//丢弃一个标记 (EraseLine 内部使用)
void Buffer::ClearMark(char name) {
    //TODO(光标的移动二): 实现
    //name 合法时 mark_valid_[name - 'a'] = false
    if (name >= 'a' && name <= 'z') {
        mark_valid_[name - 'a'] = false;
    }
}

//在 row 处插入一个空行 (后续 o/O 与粘贴章节的行插入入口); row 处及以下的标记整体下移一行
void Buffer::InsertLine(std::size_t row) {
    //TODO(光标的移动二): 实现
    //1. 越界检查; 
    if (row > lines_.size()) {
        throw std::runtime_error("Row out of range.");
    }
    // 在 lines_ 的 row 处插入空串, 并置 modified_ / end_with_newline_
    lines_.insert(lines_.begin() + row, "");
    modified_ = true;
    end_with_newline_ = true;
    //2. AdjustMarksInsertLine(row)
    AdjustMarksInsertLine(row);
}

//删除第 row 行并返回其内容 (寄存器章节取文本用); 该行标记丢失, 其下标记上移一行
std::string Buffer::EraseLine(std::size_t row) {
    //TODO(光标的移动二): 实现
    //1. 取出 lines_[row] 的内容作为返回值
    const std::string res = lines_[row];
    //2. 删除该行并置 modified_ / end_with_newline_ (保持 Buffer 至少有一行, 全删光时留一个空行)
    lines_.erase(lines_.begin() + row);
    if (lines_.empty()) {
        lines_.push_back("");
    }
    modified_ = true;
    end_with_newline_ = true;
    //3. AdjustMarksEraseLine(row)
    AdjustMarksEraseLine(row);
    return res;
}

void Buffer::AdjustMarksInsertLine(std::size_t row) {
    //TODO(光标的移动二): 实现
    //位置在 row 处及以下的标记整体下移一行 (README: 在标记位置及之前插入完整行, 标记随原文本后移)
    for (size_t i = 0; i < kMarkCount; i++) {
        if (mark_valid_[i] && marks_[i].row_ >= row) {
            marks_[i].row_++;
        }
    }
}

void Buffer::AdjustMarksEraseLine(std::size_t row) {
    //TODO(光标的移动二): 实现
    for (size_t i = 0; i < kMarkCount; i++) {
        if (mark_valid_[i]) {
            //row 行上的标记全部丢失 (mark_valid_ = false, README: 标记所在行被删除则标记丢失);
            if (marks_[i].row_ == row) {
                mark_valid_[i] = false;
            }
            //row 以下的标记上移一行
            else if (marks_[i].row_ > row) {
                marks_[i].row_--;
            }
        }
    }
}


void Buffer::WriteTo(const std::filesystem::path& path) const {
    //实际将缓冲区中的内容写入 path 指向的文件中
    if (path == "") throw std::runtime_error("No path.");
    std::ofstream text_file(path);
    if (!text_file.is_open()) throw std::runtime_error("The file can't be opened.");
    for (std::size_t i = 0; i < lines_.size(); i++) {
        text_file << lines_[i];
        if (i < lines_.size() - 1 || end_with_newline_) text_file << '\n';
    }
}

} // namespace sjtu
