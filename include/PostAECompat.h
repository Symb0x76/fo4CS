#pragma once

// ============================================================
// PostAECompat.h
// 跨 CommonLibF4 版本的 API 兼容层
// ============================================================
// 解决两类差异：
//   1. RendererData 访问方式：
//      - PostNG  / PreNG : RE::BSGraphics::RendererData::GetSingleton()
//      - PostAE         : RE::BSGraphics::GetRendererData()  (自由函数，通过 REL::ID 全局指针)
//   2. logger 命名空间：
//      - PostNG  / PreNG : F4SE::log  (或 spdlog)
//      - PostAE         : spdlog (F4SE::log 已移除)
//
// 使用方式：在 PCH 之后 #include "PostAECompat.h"，
// 然后通过 fo4cs::GetRendererData() 访问 RendererData*。
// ============================================================

#include "RE/Fallout.h"

namespace fo4cs
{
    /// 返回当前 BSGraphics::RendererData*，跨三个 CommonLib 版本均可用
    [[nodiscard]] inline RE::BSGraphics::RendererData* GetRendererData() noexcept
    {
#if defined(FALLOUT_POST_AE)
        // PostAE: GetRendererData() 是 BSGraphics 命名空间内的自由函数，
        // 内部通过 REL::Relocation<RendererData**> 读取全局指针。
        // 注意：PostAE 头文件中该函数定义在 RE::BSGraphics:: 命名空间下，
        // 但不属于任何类，直接调用即可。
        return RE::BSGraphics::GetRendererData();
#else
        // PreNG / PostNG: RendererData 是单例类
        return RE::BSGraphics::RendererData::GetSingleton();
#endif
    }
}
