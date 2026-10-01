#pragma once

// HRESULT 검사 도우미.
//   if (!check(device->CreateBuffer(...), "정점 버퍼 생성")) return false;
// 실패하면 "정점 버퍼 생성 실패: 0x887A0005 (DXGI_ERROR_DEVICE_REMOVED) [파일:줄]" 로그를 남깁니다.

#include <windows.h>

#include <source_location>
#include <string>
#include <string_view>

namespace deskpet::renderer::d3d11 {

[[nodiscard]] bool check(HRESULT hr, std::string_view what,
                         std::source_location where = std::source_location::current());

[[nodiscard]] std::string hresultToString(HRESULT hr);

}  // namespace deskpet::renderer::d3d11
