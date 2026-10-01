#pragma once

// UTF-8(std::string) ↔ UTF-16(std::wstring) 변환.
// 프로젝트 내부 문자열은 UTF-8, Win32 W 함수 호출 직전에만 UTF-16으로 바꿉니다.

#include <string>
#include <string_view>

namespace deskpet::platform::win32 {

[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view utf16);

}  // namespace deskpet::platform::win32
