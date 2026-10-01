#pragma once

// 렌더 패스 인터페이스. D3D11Renderer는 등록된 패스를 순서대로 실행합니다.
// 디바이스 손실 시 release() → create() 순서로 다시 만들어집니다.
// 명세: docs/03-detailed-design/renderer.md §4.3

#include "renderer/RenderScene.h"
#include "renderer/d3d11/D3D11Context.h"

#include <string_view>

namespace deskpet::renderer::d3d11 {

class IRenderPass {
public:
    IRenderPass() = default;
    virtual ~IRenderPass() = default;

    IRenderPass(const IRenderPass&) = delete;
    IRenderPass& operator=(const IRenderPass&) = delete;
    IRenderPass(IRenderPass&&) = delete;
    IRenderPass& operator=(IRenderPass&&) = delete;

    [[nodiscard]] virtual std::string_view name() const = 0;

    // 디바이스 종속 리소스(브러시, 셰이더, 버퍼 등)를 만듭니다.
    [[nodiscard]] virtual bool create(const D3D11Context& ctx) = 0;

    // 한 프레임을 그립니다. false는 디바이스 손실 등으로 리소스 재생성이 필요하다는 뜻입니다.
    [[nodiscard]] virtual bool execute(const D3D11Context& ctx, const RenderScene& scene) = 0;

    virtual void release() = 0;
};

}  // namespace deskpet::renderer::d3d11
