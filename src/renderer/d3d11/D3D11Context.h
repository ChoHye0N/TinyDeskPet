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
    ID2D1DeviceContext* d2d = nullptr;               // 같은 백버퍼를 타깃으로 하는 D2D
    core::SizeI viewport;
};

}  // namespace deskpet::renderer::d3d11
