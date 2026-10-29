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
    lines_[row] += lines_[row + 1];
    lines_.erase(lines_.begin() + row + 1);
    modified_ = true;
    end_with_newline_ = true;
    return;
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
