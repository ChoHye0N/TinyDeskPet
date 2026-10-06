#pragma once

// 렌더 패스에 넘기는 디바이스 묶음. 소유하지 않는 포인터입니다 (수명은 D3D11Renderer가 관리).

#include "core/Math.h"
#include "core/Math3D.h"

#include <d2d1_1.h>
#include <d3d11.h>

namespace deskpet::renderer::d3d11 {

struct D3D11Context {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;  // 프레임 텍스처 (끝나면 백버퍼로 복사)
    // 3D 패스가 그리는 곳: 장면 영역(sceneRegion) 크기의 텍스처 (MSAA면 멀티샘플).
    // 끝나면 렌더러가 프레임 텍스처의 영역 자리에 복사
    ID3D11RenderTargetView* sceneTarget = nullptr;
    // 장면의 투영(화면 전체 기준) 뒤에 곱해 3D 영역 기준으로 바꾸는 클립 공간 변환
    core::Mat4 clipTransform = core::Mat4::identity();
    unsigned sampleCount = 1;           // sceneTarget의 샘플 수. 깊이 버퍼도 같아야 함
    ID2D1DeviceContext* d2d = nullptr;  // 같은 백버퍼를 타깃으로 하는 D2D
    core::SizeI viewport;
};

}  // namespace deskpet::renderer::d3d11
