/*
Motion.hpp
MotionEngine 用纯函数计算各种光标移动的结果: 只读 Buffer, 不保存状态, 不直接修改 Window.
同一套计算供三类调用方复用:
  - Normal 模式移动光标 (Window::ApplyMotion);
  - 范围操作 d/c/y/</> 计算 range (后续章节, 由 Editor 调用, 只取 target 与 kind, 不动光标);
  - Visual 模式扩展选区 (后续章节).
结果中的 kind_ 是该 motion 的范围类型, 供 operator 章节构造 Range 使用.
*/
#ifndef MINIVIM_MOTION_HPP
#define MINIVIM_MOTION_HPP

#include <cstddef>
#include <optional>

#include "Buffer.hpp"
#include "Types.hpp"

namespace sjtu {

//motion 完成后 curswant 的更新策略 (README 各命令的 curswant 要求)
enum class CurswantPolicy {
    Keep,        // 保留原目标列: j/k 上下移动, 经短行时钳制但不丢失
    SetToTarget, // 更新为终点的显示列: h/l/w/b/e/ge/0/^/G/gg
    SetMax,      // 设为极大值, 之后 j/k 始终钳到行尾: $
};

struct MotionResult {
    bool ok_{false};  // false 表示 motion 失败: 光标与视口不动, 整条命令取消
    Position target_{}; // 成功时的目标位置
    RangeKind kind_{RangeKind::CharacterwiseExclusive}; // 该 motion 的范围类型 (operator 章节使用)
    CurswantPolicy curswant_{CurswantPolicy::Keep};     // 失败时也可能需要更新 curswant
};

class MotionEngine {
public:
    // origin: 移动前的光标位置; curswant: 当前的期望显示列 (上下移动定位目标列用)
    // spec.count_ 为 nullopt 表示未输入 count; G/gg 以此区分"无 count"与"count=1"
    static MotionResult Compute(const Buffer& buffer, Position origin,
                                size_t curswant, MotionSpec spec);

private:
    // ---- 光标的移动(一) ----
    static MotionResult ComputeLineStart(const Buffer& buffer, Position origin);            // 0
    static MotionResult ComputeFirstNonBlank(const Buffer& buffer, Position origin);        // ^
    static MotionResult ComputeLineEnd(const Buffer& buffer, Position origin, MotionSpec spec); // $ / [count]$
    static MotionResult ComputeWordForward(const Buffer& buffer, Position origin, bool cap, size_t count);      // w / W
    static MotionResult ComputeWordBackward(const Buffer& buffer, Position origin, bool cap, size_t count);     // b / B
    static MotionResult ComputeWordEnd(const Buffer& buffer, Position origin, bool cap, size_t count);          // e / E
    static MotionResult ComputeWordBackwardEnd(const Buffer& buffer, Position origin, bool cap, size_t count);  // ge / gE
    static MotionResult ComputeGotoLine(const Buffer& buffer, Position origin, std::optional<size_t> count);     // G
    static MotionResult ComputeGotoFirstLine(const Buffer& buffer, Position origin, std::optional<size_t> count);// gg

    // ---- 后续章节扩展点 ----
    // 搜索 (/,?,n,N)、文本对象 (a/i + 对象字符) 等在此扩展; 文本对象建议直接产出 Range
};

} // namespace sjtu

#endif // MINIVIM_MOTION_HPP
