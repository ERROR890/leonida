#pragma once

// The one blend fallback of the slot 57 native graphics factory (RT-SLOT57-BLEND-STATE). Where B4G4R4A4_UNORM is not
// renderable, the Vulkan layer draws it into an RGBA8 stand-in and refuses blending there (E_NOTIMPL): the stand-in
// quantizes the shader output before the blend. A pipeline that blends such a target is retried with blending off on
// those targets alone. E_NOTIMPL also stands for refusals that have nothing to do with blending, so a pipeline with no
// blended B4G4R4A4 target is never retried.
//
// D3D12 lets a pipeline describe its targets two ways: `IndependentBlendEnable = TRUE` gives every target its own
// `RenderTarget[i]`, and `FALSE` makes `RenderTarget[0]` describe all of them, with `RenderTarget[1..7]` ignored. A
// target that blends is therefore decided by the entry that applies to it, not by the array index, and switching the
// flag on here has to make the state every target inherits explicit first, or the targets that relied on
// `RenderTarget[0]` would silently lose their blend.
#include <d3d12.h>

namespace rt::d3d12_xs::native_pipeline_detail {

// Turns blending off on the B4G4R4A4 targets that blend, and only there. False, with `desc` unchanged, when no target
// that applies does: the refusal was not the stand-in's.
inline bool without_stand_in_blend(D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    const D3D12_BLEND_DESC& blend = desc.BlendState;
    bool changed = false;
    for (unsigned i = 0; i < desc.NumRenderTargets && i < 8; ++i) {
        if (desc.RTVFormats[i] != DXGI_FORMAT_B4G4R4A4_UNORM) continue;
        // The entry that applies to target i: its own under independent blending, RenderTarget[0] otherwise. The
        // stand-in refuses blending and logic ops alike, which is what the layer's refusal tests.
        const D3D12_RENDER_TARGET_BLEND_DESC& entry = blend.RenderTarget[blend.IndependentBlendEnable ? i : 0];
        if (entry.BlendEnable || entry.LogicOpEnable) changed = true;
    }
    if (!changed) return false;

    D3D12_BLEND_DESC rewritten = blend;
    if (!blend.IndependentBlendEnable) {
        // Every target inherits RenderTarget[0] today; give each of them that state before they can differ, so turning
        // blending off on a stand-in leaves the others as they were.
        for (auto& target : rewritten.RenderTarget) target = blend.RenderTarget[0];
        rewritten.IndependentBlendEnable = TRUE;
    }
    for (unsigned i = 0; i < desc.NumRenderTargets && i < 8; ++i)
        if (desc.RTVFormats[i] == DXGI_FORMAT_B4G4R4A4_UNORM) {
            // Blending and logic ops are what the stand-in refuses; neither can be kept on it.
            rewritten.RenderTarget[i].BlendEnable = FALSE;
            rewritten.RenderTarget[i].LogicOpEnable = FALSE;
        }
    desc.BlendState = rewritten;
    return true;
}

}  // namespace rt::d3d12_xs::native_pipeline_detail
