/*
Register.hpp
寄存器: 保存被删除/复制文本及其范围类型 (Characterwise/Linewise).
本阶段 (文本的修正与移动一) 只需要 unnamed register (name = '"'):
  d/c/x/X 把删除的文本写入它 (README:1471/1489), r/J/~ 不写 (README:1509/1535/1549).
接口按 name 预留, 供后续章节 (三) 的 yank/paste 与命名寄存器 ("a-"z) 扩展.
空范围 (位移为 0) 的 d/y 不覆盖寄存器内容 (README:897), 该判定由调用方负责.
*/
#ifndef MINIVIM_REGISTER_HPP
#define MINIVIM_REGISTER_HPP

#include <string>

#include "Types.hpp"

namespace sjtu {

class Register {
public:
    // unnamed register 的名字 (vim 用 '"' 指代)
    static constexpr char kUnnamed = '"';

    // 写入文本与类型 (同名覆盖; 空文本也覆盖, "不覆盖"的判定在调用方)
    void Set(char name, std::string text, RangeType type);
    // 该寄存器是否有内容 (d/c 写 unnamed 后粘贴的前提)
    bool Has(char name) const;
    // 取文本; 未写入时返回空串
    const std::string& GetText(char name) const;
    // 取类型; 未写入时返回 Characterwise
    RangeType GetType(char name) const;

private:
    // 本阶段只需实现 unnamed 一个槽位;
    // 后续章节扩展为 unnamed + 'a'-'z' 命名寄存器 (接口不变)
    std::string text_;
    RangeType type_{RangeType::Characterwise};
    bool has_{false};
};

} // namespace sjtu

#endif // MINIVIM_REGISTER_HPP
