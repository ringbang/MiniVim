#ifndef MINIVIM_BUFFER_HPP
#define MINIVIM_BUFFER_HPP

#include <filesystem>
#include <optional>
#include <vector>

#include "Types.hpp"

namespace sjtu {

class Buffer {
/*
Buffer.cpp
Buffer 即缓冲区.在我们的 MiniVim 中, Buffer 是用来存储文件的实际内容的
我们保证在 Buffer 中只含有可打印字符, 即 [0x20, 0x7E] 以及 \t
我们约定, 当你 MiniVim 打开一个不含有内容的文件时, 不应该让lines_.empty(), 而应该让 lines 含有一个空的 string 作为占位.即表明文件当前只有一行且是空行
*/
public:
    explicit Buffer(const std::filesystem::path& path = {});
    explicit Buffer(std::vector<std::string> lines, std::filesystem::path path = {});

    std::size_t GetLineCount() const;
    const std::string& GetLineAt(std::size_t row) const;
    std::string GetDisplayName() const;
    bool IsModified() const ;

    void InsertCharacter(std::size_t row, std::size_t column, char value);
    void EraseCharacter(std::size_t row, std::size_t column);
    void SplitLine(std::size_t row, std::size_t column);
    void JoinLine(std::size_t row);
    void Save();
    void SaveAs(const std::filesystem::path& path);

    // ---- 标记 (README: 标记 m) ----
    //m{a-z}: 保存当前 Buffer 位置为小写局部标记, 同名标记被覆盖; name 不在 a-z 内时忽略
    void SetMark(char name, Position pos);
    //'a: 返回标记的精确行与字符位置; 标记未设置 (或 name 非法) 返回 nullopt
    std::optional<Position> GetMark(char name) const;
    //丢弃一个标记; EraseLine 内部使用
    void ClearMark(char name);

    // ---- 行级编辑原语 (标记维护的入口) ----
    //后续插入/删除/粘贴章节 (o/O/dd/p 等) 的行结构修改请经由这两个函数, 标记会自动维护:
    //在 row 处插入一个空行; row 处及以下的标记整体下移一行 (README: 标记位置及之前插入完整行)
    void InsertLine(std::size_t row);
    //删除第 row 行并返回其内容 (寄存器章节取文本用); 该行上的标记丢失
    //(README: 标记所在行被删除则标记丢失), 其下标记上移一行
    std::string EraseLine(std::size_t row);

private:
    void WriteTo(const std::filesystem::path& path) const;

    std::vector<std::string> lines_; //文件每行的字符内容,不包含末尾换行符
    std::filesystem::path path_;    //打开文件的路径
    bool modified_ = false; //文件是否被修改
    bool end_with_newline_ = false; //文件是否以回车结尾

    //标记维护: 行内插入/删除字符不移动标记 (README: 标记不会随原文本后移), 无需代码;
    //SplitLine/JoinLine 按 vim 跟随原字符 (README 未规定, 见 Buffer.cpp 中的说明)
    static constexpr std::size_t kMarkCount = 26;
    Position marks_[kMarkCount]{};  //'a' 存下标 0 ... 'z' 存下标 25
    bool mark_valid_[kMarkCount]{}; //对应标记是否已设置
    void AdjustMarksInsertLine(std::size_t row); //InsertLine 的标记维护
    void AdjustMarksEraseLine(std::size_t row);  //EraseLine 的标记维护
};

} // namespace sjtu

#endif // MINIVIM_BUFFER_HPP
