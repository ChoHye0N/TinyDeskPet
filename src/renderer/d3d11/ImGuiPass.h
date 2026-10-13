#pragma once

// 설정 창(Dear ImGui)을 프레임 텍스처에 그리는 패스 (ADR-0016). 공식 D3D11 백엔드
// (imgui_impl_dx11)를 감쌈. 3D·Direct2D 뒤에 실행해 맨 위에 그림

#include "renderer/d3d11/IRenderPass.h"

namespace deskpet::renderer::d3d11 {

class ImGuiPass final : public IRenderPass {
public:
    ImGuiPass() = default;
    ~ImGuiPass() override;

    ImGuiPass(const ImGuiPass&) = delete;
    ImGuiPass& operator=(const ImGuiPass&) = delete;
    ImGuiPass(ImGuiPass&&) = delete;
    ImGuiPass& operator=(ImGuiPass&&) = delete;

    [[nodiscard]] std::string_view name() const override { return "ImGuiPass"; }
    [[nodiscard]] bool create(const D3D11Context& ctx) override;
    [[nodiscard]] bool execute(const D3D11Context& ctx, const RenderScene& scene) override;
    void release() override;

private:
    bool initialized_ = false;  // 백엔드를 초기화했는지 (ImGui 컨텍스트가 없으면 건너뜀)
};

}  // namespace deskpet::renderer::d3d11
