#include "Register.hpp"

namespace sjtu {

//写入文本与类型 (契约见 Register.hpp 的注释)
//TODO(文本的修正与移动一): 实现
//1. 本阶段只支持 kUnnamed ('"'); 其他 name 先忽略, 后续章节扩展
//2. 保存 text_/type_ 并置 has_ = true
void Register::Set(char name, std::string text, RangeType type) {
    if (name != kUnnamed) {
        return;
    }
    text_ = text;
    type_ = type;
    has_ = true;
}

//是否有内容 (契约见 Register.hpp 的注释)
//TODO(文本的修正与移动一): 实现
bool Register::Has(char name) const {
    if (name != kUnnamed) { return false; }
    else { return has_; }
}

//取文本 (契约见 Register.hpp 的注释)
//TODO(文本的修正与移动一): 实现
const std::string& Register::GetText(char name) const {
    static const std::string empty;
    if (name != kUnnamed || !has_) { return empty; }
    return text_;
}


//取类型 (契约见 Register.hpp 的注释)
//TODO(文本的修正与移动一): 实现
RangeType Register::GetType(char name) const {
    if (name != kUnnamed) { return RangeType::Characterwise; }
    if (has_) { return type_; }
    else { return RangeType::Characterwise; }
}

} // namespace sjtu
