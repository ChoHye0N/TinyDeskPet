#pragma once

// 기본 수학 타입. float 좌표는 도메인(캐릭터), int 좌표는 OS(창, 픽셀)에서 사용합니다.

#include <cmath>

namespace deskpet::core {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2 operator+(Vec2 other) const noexcept { return {x + other.x, y + other.y}; }
    constexpr Vec2 operator-(Vec2 other) const noexcept { return {x - other.x, y - other.y}; }
    constexpr Vec2 operator*(float scalar) const noexcept { return {x * scalar, y * scalar}; }
    constexpr Vec2& operator+=(Vec2 other) noexcept {
        x += other.x;
        y += other.y;
        return *this;
    }
    constexpr Vec2& operator-=(Vec2 other) noexcept {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    constexpr bool operator==(const Vec2&) const noexcept = default;

    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y); }
};

struct PointI {
    int x = 0;
    int y = 0;
    constexpr bool operator==(const PointI&) const noexcept = default;
};

struct SizeI {
    int width = 0;
    int height = 0;
    constexpr bool operator==(const SizeI&) const noexcept = default;
};

// Win32 RECT와 같은 반열린 구간: [left, right) × [top, bottom)
struct RectI {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] constexpr int width() const noexcept { return right - left; }
    [[nodiscard]] constexpr int height() const noexcept { return bottom - top; }
    constexpr bool operator==(const RectI&) const noexcept = default;
};

}  // namespace deskpet::core
