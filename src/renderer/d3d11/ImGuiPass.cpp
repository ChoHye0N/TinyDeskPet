#include "renderer/d3d11/ImGuiPass.h"

#include "core/Log.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace deskpet::renderer::d3d11 {

ImGuiPass::~ImGuiPass() {
    release();
}

bool ImGuiPass::create(const D3D11Context& ctx) {
    // ImGui 컨텍스트는 앱(ui 모듈)이 만듦. 없으면(설정 창을 쓰지 않는 구성) 이 패스는 건너뜀
    if (ImGui::GetCurrentContext() == nullptr) {
        return true;
    }
    if (!ImGui_ImplDX11_Init(ctx.device, ctx.context)) {
        core::logging::error("ImGui D3D11 백엔드 초기화 실패");
        return false;
    }
    initialized_ = true;
    // 셰이더·버퍼는 보통 ImGui_ImplDX11_NewFrame에서 만들지만, 프레임은 ui 모듈이 시작하므로
    // 여기서 미리 만듦 (셰이더는 실행할 때 d3dcompiler로 컴파일됨)
    if (!ImGui_ImplDX11_CreateDeviceObjects()) {
        core::logging::error("ImGui D3D11 리소스 생성 실패");
        return false;
    }
    return true;
}

bool ImGuiPass::execute(const D3D11Context& /*ctx*/, const RenderScene& scene) {
    if (initialized_ && scene.ui != nullptr) {
        // 렌더 타깃·뷰포트는 렌더러가 프레임 텍스처 전체로 맞춰 둠. 백엔드가 상태를 저장·복원함.
        // 블렌드가 (SrcAlpha, InvSrcAlpha) + 알파 (One, InvSrcAlpha)라 결과가 premultiplied alpha로
        // 쌓여 DirectComposition 합성과 맞음
        ImGui_ImplDX11_RenderDrawData(scene.ui);
    }
    return true;
}

void ImGuiPass::release() {
    if (initialized_) {
        ImGui_ImplDX11_Shutdown();  // 텍스처(글꼴 아틀라스)는 다시 만들도록 표시됨
        initialized_ = false;
    }
}

}  // namespace deskpet::renderer::d3d11
