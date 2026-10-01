#include "renderer/d3d11/PlaceholderPass.h"

#include "renderer/d3d11/DxCheck.h"

namespace deskpet::renderer::d3d11 {

bool PlaceholderPass::create(const D3D11Context& ctx) {
    ID2D1DeviceContext* d2d = ctx.d2d;
    return check(d2d->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.82f, 1.00f), &bodyBrush_),
                 "몸통 브러시 생성") &&
           check(d2d->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.50f, 0.78f), &outlineBrush_),
                 "외곽선 브러시 생성") &&
           check(d2d->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.12f, 0.20f), &eyeBrush_),
                 "눈 브러시 생성") &&
           check(d2d->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.55f, 0.65f, 0.6f), &cheekBrush_),
                 "볼 브러시 생성");
}

bool PlaceholderPass::execute(const D3D11Context& ctx, const RenderScene& scene) {
    const PlaceholderCharacter& character = scene.placeholder;
    if (!character.visible) {
        return true;
    }

    ID2D1DeviceContext* d2d = ctx.d2d;
    const float cx = character.center.x;
    const float cy = character.center.y;
    const float rx = character.radiusX;
    const float ry = character.radiusY;

    d2d->BeginDraw();
    d2d->SetTransform(D2D1::Matrix3x2F::Identity());
    // 배경은 D3D11Renderer가 이미 투명하게 지웠으므로 Clear하지 않습니다.

    // 몸통
    const D2D1_ELLIPSE body = D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry);
    d2d->FillEllipse(body, bodyBrush_.Get());
    d2d->DrawEllipse(body, outlineBrush_.Get(), 2.0f);

    // 눈
    const float eyeY = cy - ry * 0.15f;
    const float eyeDx = rx * 0.34f;
    for (const float eyeX : {cx - eyeDx, cx + eyeDx}) {
        if (character.eyesClosed) {
            const float half = rx * 0.11f;
            d2d->DrawLine(D2D1::Point2F(eyeX - half, eyeY), D2D1::Point2F(eyeX + half, eyeY),
                          eyeBrush_.Get(), 3.0f);
        } else {
            d2d->FillEllipse(D2D1::Ellipse(D2D1::Point2F(eyeX, eyeY), rx * 0.10f, ry * 0.18f),
                             eyeBrush_.Get());
        }
    }

    // 볼
    const float cheekY = cy + ry * 0.18f;
    const float cheekDx = rx * 0.58f;
    for (const float cheekX : {cx - cheekDx, cx + cheekDx}) {
        d2d->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cheekX, cheekY), rx * 0.15f, ry * 0.10f),
                         cheekBrush_.Get());
    }

    const HRESULT hr = d2d->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        return false;  // 렌더러가 디바이스 리소스를 재생성
    }
    return check(hr, "PlaceholderPass EndDraw");
}

void PlaceholderPass::release() {
    bodyBrush_.Reset();
    outlineBrush_.Reset();
    eyeBrush_.Reset();
    cheekBrush_.Reset();
}

}  // namespace deskpet::renderer::d3d11
