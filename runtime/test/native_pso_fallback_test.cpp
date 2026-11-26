// The slot 57 factory's blend fallback (shims/d3d12_xs_native_pso_fallback.h): only a pipeline that blends a
// B4G4R4A4 target is retried, with blending off on those targets alone. Any other E_NOTIMPL refusal keeps the blend.
#include "../shims/d3d12_xs_native_pso_fallback.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC two_targets(DXGI_FORMAT first, DXGI_FORMAT second) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.NumRenderTargets = 2;
    desc.RTVFormats[0] = first;
    desc.RTVFormats[1] = second;
    for (auto& rt : desc.BlendState.RenderTarget) {
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    desc.BlendState.IndependentBlendEnable = FALSE;
    return desc;
}

}  // namespace

int main() {
    using rt::d3d12_xs::native_pipeline_detail::without_stand_in_blend;

    {
        auto desc = two_targets(DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM);
        expect(without_stand_in_blend(desc), "a blended B4G4R4A4 target is the stand-in's refusal");
        expect(!desc.BlendState.RenderTarget[0].BlendEnable, "blending goes off on the B4G4R4A4 target");
        expect(desc.BlendState.RenderTarget[1].BlendEnable, "the other target keeps its blend");
        expect(desc.BlendState.IndependentBlendEnable, "the targets now differ, so blending is per target");
    }
    {
        // No B4G4R4A4 target: an E_NOTIMPL here is some other refusal (a rect list, mesh limits, a dual-source output
        // that does not fit Location 0 Index 1), and the pipeline must not be rebuilt without its blend.
        const auto before = two_targets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto desc = before;
        expect(!without_stand_in_blend(desc), "no B4G4R4A4 target, no retry");
        expect(std::memcmp(&desc, &before, sizeof(desc)) == 0, "the description is left as it was");
    }
    {
        auto before = two_targets(DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM);
        before.BlendState.RenderTarget[0].BlendEnable = FALSE;
        auto desc = before;
        expect(!without_stand_in_blend(desc), "a B4G4R4A4 target that does not blend, no retry");
        expect(std::memcmp(&desc, &before, sizeof(desc)) == 0, "the description is left as it was");
    }
    {
        auto desc = two_targets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM);
        desc.NumRenderTargets = 1;  // the B4G4R4A4 format past the target count is not a target
        expect(!without_stand_in_blend(desc), "a format past NumRenderTargets is not looked at");
    }
    {
        // A B4G4R4A4 target that is not the first one still blends: under D3D12's non-independent rule
        // RenderTarget[0] describes every target, and the entry at this target's own index is ignored, so its
        // BlendEnable = FALSE here must not be read as "this target does not blend".
        auto desc = two_targets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM);
        desc.BlendState.RenderTarget[1].BlendEnable = FALSE;  // ignored: RenderTarget[0] applies
        expect(without_stand_in_blend(desc), "a blended B4G4R4A4 target at index 1 is still the stand-in's refusal");
        expect(desc.BlendState.IndependentBlendEnable, "the targets now differ, so blending is per target");
        expect(desc.BlendState.RenderTarget[0].BlendEnable, "the target that is not a stand-in keeps its blend");
        expect(!desc.BlendState.RenderTarget[1].BlendEnable, "blending goes off on the B4G4R4A4 target");
    }
    {
        // The same, with RenderTarget[0] not blending: the entry at the stand-in's own index is ignored, so a set
        // BlendEnable there is not a reason to rebuild the pipeline without its blend.
        auto before = two_targets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM);
        before.BlendState.RenderTarget[0].BlendEnable = FALSE;
        before.BlendState.RenderTarget[1].BlendEnable = TRUE;  // ignored
        auto desc = before;
        expect(!without_stand_in_blend(desc), "a B4G4R4A4 target that does not blend, no retry");
        expect(std::memcmp(&desc, &before, sizeof(desc)) == 0, "the description is left as it was");
    }
    {
        // Switching to independent blending must not drop the state the other targets inherited from
        // RenderTarget[0]: they keep it, field for field, and only the stand-in target loses its blend.
        auto desc = two_targets(DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM);
        desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
        for (unsigned i = 1; i < 8; ++i) desc.BlendState.RenderTarget[i] = D3D12_RENDER_TARGET_BLEND_DESC{};
        const auto inherited = desc.BlendState.RenderTarget[0];
        expect(without_stand_in_blend(desc), "a blended B4G4R4A4 first target is the stand-in's refusal");
        expect(!desc.BlendState.RenderTarget[0].BlendEnable, "blending goes off on the B4G4R4A4 target");
        for (unsigned i = 1; i < 8; ++i) {
            const auto& target = desc.BlendState.RenderTarget[i];
            expect(target.SrcBlend == inherited.SrcBlend && target.DestBlend == inherited.DestBlend &&
                       target.BlendOp == inherited.BlendOp && target.RenderTargetWriteMask == inherited.RenderTargetWriteMask,
                   "a target that inherited RenderTarget[0] keeps that state");
            expect(target.BlendEnable, "a target that is not a stand-in keeps its blend");
        }
    }
    {
        // Independent blending already: only the stand-in target changes, and the others keep their own state.
        auto desc = two_targets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM);
        desc.BlendState.IndependentBlendEnable = TRUE;
        desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        const auto first = desc.BlendState.RenderTarget[0];
        expect(without_stand_in_blend(desc), "a blended B4G4R4A4 target under independent blending is the refusal");
        expect(std::memcmp(&desc.BlendState.RenderTarget[0], &first, sizeof(first)) == 0,
               "the other target keeps its own state");
        expect(!desc.BlendState.RenderTarget[1].BlendEnable, "blending goes off on the B4G4R4A4 target");
    }
    {
        // Logic ops are the other thing the stand-in refuses; they go off with the blend on the stand-in target, and a
        // target that inherited the same state keeps it.
        auto desc = two_targets(DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM);
        desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
        desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
        expect(without_stand_in_blend(desc), "a stand-in target that blends through a logic op is the refusal");
        expect(!desc.BlendState.RenderTarget[0].LogicOpEnable, "the logic op goes off on the B4G4R4A4 target");
        expect(desc.BlendState.RenderTarget[1].LogicOpEnable, "the other target keeps the state it inherited");
        expect(!desc.BlendState.RenderTarget[1].BlendEnable, "the other target keeps the blend it inherited");
    }
    if (failures) return 1;
    std::printf("native_pso_fallback: ok\n");
    return 0;
}
