#pragma once

// 렌더 패스에 넘기는 디바이스 묶음. 소유하지 않는 포인터입니다 (수명은 D3D11Renderer가 관리).

#include "core/Math.h"

#include <d2d1_1.h>
#include <d3d11.h>

namespace deskpet::renderer::d3d11 {

struct D3D11Context {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;  // 스왑체인 백버퍼
    // 3D 패스가 그리는 곳. MSAA를 켜면 멀티샘플 텍스처(나중에 백버퍼로 resolve), 끄면 백버퍼
    ID3D11RenderTargetView* sceneTarget = nullptr;
    unsigned sampleCount = 1;           // sceneTarget의 샘플 수. 깊이 버퍼도 같아야 함
    ID2D1DeviceContext* d2d = nullptr;  // 같은 백버퍼를 타깃으로 하는 D2D
    core::SizeI viewport;
};

}  // namespace deskpet::renderer::d3d11
