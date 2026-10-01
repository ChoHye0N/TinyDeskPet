#pragma once

// M0 임시 캐릭터를 Direct2D로 그리는 패스.
// M2 이후 MeshPass(D3D11 3D)가 추가되면 디버그 용도로만 남깁니다.

#include "renderer/d3d11/IRenderPass.h"

#include <wrl/client.h>

namespace deskpet::renderer::d3d11 {

class PlaceholderPass final : public IRenderPass {
public:
    [[nodiscard]] std::string_view name() const override { return "PlaceholderPass"; }
    [[nodiscard]] bool create(const D3D11Context& ctx) override;
    [[nodiscard]] bool execute(const D3D11Context& ctx, const RenderScene& scene) override;
    void release() override;

private:
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bodyBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> outlineBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> eyeBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cheekBrush_;
};

}  // namespace deskpet::renderer::d3d11
