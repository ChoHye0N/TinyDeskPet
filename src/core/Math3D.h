#pragma once

// 3D 수학 (모델 로딩·카메라). 명세: docs/03-detailed-design/core.md §3.7
//
// 규약 (DirectXMath와 같음):
//   - 행 벡터: p' = p * M. 변환 합성은 왼쪽부터 적용 순서 (A * B = A 다음 B).
//   - 행 우선 저장: m[행][열]. 이동 성분은 m[3][0..2].
//   - 오른손 좌표계, +Y 위 (glTF와 같음). 투영 후 깊이는 D3D 규약 0 ~ 1.
// 참고: glTF의 열 우선 float[16]을 그대로 m[4][4]에 복사하면 이 규약의 행렬이 됩니다
//       (열 벡터 행렬 M을 열 우선으로 저장 = Mᵀ를 행 우선으로 저장).

#include <array>
#include <cmath>
#include <numbers>

namespace deskpet::core {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3 operator+(Vec3 o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr bool operator==(const Vec3&) const noexcept = default;

    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y + z * z); }
};

[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// 길이가 0이면 그대로 반환
[[nodiscard]] inline Vec3 normalize(Vec3 v) noexcept {
    const float len = v.length();
    return len > 0.0f ? v * (1.0f / len) : v;
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr bool operator==(const Vec4&) const noexcept = default;
};

struct Mat4 {
    std::array<std::array<float, 4>, 4> m{};

    constexpr bool operator==(const Mat4&) const noexcept = default;

    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        Mat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    // glTF/cgltf의 열 우선 배열 → 이 규약의 행렬 (위 머리 주석 참고)
    [[nodiscard]] static constexpr Mat4 fromColumnMajor(const float* values) noexcept {
        Mat4 r;
        for (int i = 0; i < 16; ++i) {
            r.m[static_cast<std::size_t>(i / 4)][static_cast<std::size_t>(i % 4)] = values[i];
        }
        return r;
    }

    [[nodiscard]] static constexpr Mat4 translation(Vec3 t) noexcept {
        Mat4 r = identity();
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    [[nodiscard]] static Mat4 rotationY(float radians) noexcept {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Mat4 r = identity();
        r.m[0][0] = c;
        r.m[0][2] = -s;
        r.m[2][0] = s;
        r.m[2][2] = c;
        return r;
    }

    constexpr Mat4 operator*(const Mat4& o) const noexcept {
        Mat4 r;
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (std::size_t k = 0; k < 4; ++k) {
                    sum += m[i][k] * o.m[k][j];
                }
                r.m[i][j] = sum;
            }
        }
        return r;
    }

    constexpr Mat4 operator*(float s) const noexcept {
        Mat4 r;
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                r.m[i][j] = m[i][j] * s;
            }
        }
        return r;
    }

    constexpr Mat4 operator+(const Mat4& o) const noexcept {
        Mat4 r;
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                r.m[i][j] = m[i][j] + o.m[i][j];
            }
        }
        return r;
    }
};

// (p, 1) * M 의 결과 (동차 좌표)
[[nodiscard]] constexpr Vec4 transform(Vec3 p, const Mat4& a) noexcept {
    const auto& m = a.m;
    return {
        p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0],
        p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1],
        p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2] + m[3][2],
        p.x * m[0][3] + p.y * m[1][3] + p.z * m[2][3] + m[3][3],
    };
}

// 점 변환 (이동 포함, w 나눗셈 없음 — 아핀 행렬 전용)
[[nodiscard]] constexpr Vec3 transformPoint(Vec3 p, const Mat4& a) noexcept {
    const Vec4 r = transform(p, a);
    return {r.x, r.y, r.z};
}

// 방향 변환 (이동 무시)
[[nodiscard]] constexpr Vec3 transformDirection(Vec3 d, const Mat4& a) noexcept {
    const auto& m = a.m;
    return {
        d.x * m[0][0] + d.y * m[1][0] + d.z * m[2][0],
        d.x * m[0][1] + d.y * m[1][1] + d.z * m[2][1],
        d.x * m[0][2] + d.y * m[1][2] + d.z * m[2][2],
    };
}

// 회전 쿼터니언 (단위 길이). 곱셈은 해밀턴 곱: (a * b).rotate(v) == a.rotate(b.rotate(v)) — b 먼저.
// 행렬(행 벡터 규약)과 순서가 반대이니 주의: q.toMat4()는 p * M == q.rotate(p)
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    [[nodiscard]] static Quat axisAngle(Vec3 axis, float radians) noexcept {
        const Vec3 n = normalize(axis);
        const float s = std::sin(radians * 0.5f);
        return {n.x * s, n.y * s, n.z * s, std::cos(radians * 0.5f)};
    }

    // 단위 벡터 from을 to로 돌리는 최소 회전
    [[nodiscard]] static Quat fromTo(Vec3 from, Vec3 to) noexcept {
        const float d = dot(from, to);
        if (d < -0.9999f) {
            // 정반대: from에 수직인 아무 축으로 180°
            Vec3 axis = cross({1.0f, 0.0f, 0.0f}, from);
            if (dot(axis, axis) < 1e-6f) {
                axis = cross({0.0f, 1.0f, 0.0f}, from);
            }
            return axisAngle(axis, std::numbers::pi_v<float>);
        }
        // q = (from × to, 1 + from·to)를 정규화하면 두 벡터 사이 각의 절반 회전이 됨
        const Vec3 c = cross(from, to);
        return Quat{c.x, c.y, c.z, 1.0f + d}.normalized();
    }

    // 켤레 = 단위 쿼터니언의 역회전
    [[nodiscard]] constexpr Quat conjugate() const noexcept { return {-x, -y, -z, w}; }

    [[nodiscard]] Quat normalized() const noexcept {
        const float len = std::sqrt(x * x + y * y + z * z + w * w);
        return len > 0.0f ? Quat{x / len, y / len, z / len, w / len} : Quat{};
    }

    constexpr Quat operator*(const Quat& o) const noexcept {
        return {
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z,
        };
    }

    // v' = v + 2w(u × v) + 2u × (u × v), u = (x, y, z)
    [[nodiscard]] constexpr Vec3 rotate(Vec3 v) const noexcept {
        const Vec3 u{x, y, z};
        const Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }

    [[nodiscard]] constexpr Mat4 toMat4() const noexcept {
        Mat4 r = Mat4::identity();
        // 행 i = 기저 벡터 e_i를 회전한 결과 (행 벡터 규약)
        const Vec3 ex = rotate({1.0f, 0.0f, 0.0f});
        const Vec3 ey = rotate({0.0f, 1.0f, 0.0f});
        const Vec3 ez = rotate({0.0f, 0.0f, 1.0f});
        r.m[0] = {ex.x, ex.y, ex.z, 0.0f};
        r.m[1] = {ey.x, ey.y, ey.z, 0.0f};
        r.m[2] = {ez.x, ez.y, ez.z, 0.0f};
        return r;
    }

    constexpr bool operator==(const Quat&) const noexcept = default;
};

// 구면 선형 보간: t에 비례해 회전 각도가 바뀜 (t = 0 → a, 1 → b)
[[nodiscard]] inline Quat slerp(Quat a, Quat b, float t) noexcept {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        // q와 -q는 같은 회전. 부호를 맞춰야 짧은 쪽(180° 이하)으로 돎
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    float wa = 1.0f - t;
    float wb = t;
    if (d < 0.9995f) {  // 거의 같으면 sin(θ) ≈ 0으로 나누게 되므로 선형 보간 후 정규화
        const float theta = std::acos(d);
        const float s = std::sin(theta);
        wa = std::sin(wa * theta) / s;
        wb = std::sin(wb * theta) / s;
    }
    return Quat{wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w}
        .normalized();
}

// 오른손 좌표계 뷰 행렬 (XMMatrixLookAtRH와 같음). 카메라는 -Z 방향을 봅니다.
[[nodiscard]] inline Mat4 lookAtRH(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    const Vec3 zAxis = normalize(eye - target);  // 카메라 뒤쪽
    const Vec3 xAxis = normalize(cross(up, zAxis));
    const Vec3 yAxis = cross(zAxis, xAxis);

    Mat4 r = Mat4::identity();
    r.m[0] = {xAxis.x, yAxis.x, zAxis.x, 0.0f};
    r.m[1] = {xAxis.y, yAxis.y, zAxis.y, 0.0f};
    r.m[2] = {xAxis.z, yAxis.z, zAxis.z, 0.0f};
    r.m[3] = {-dot(xAxis, eye), -dot(yAxis, eye), -dot(zAxis, eye), 1.0f};
    return r;
}

// 오른손 원근 투영 (XMMatrixPerspectiveFovRH와 같음). 깊이 near→0, far→1.
[[nodiscard]] inline Mat4 perspectiveFovRH(float fovY, float aspect, float nearZ,
                                           float farZ) noexcept {
    const float yScale = 1.0f / std::tan(fovY * 0.5f);
    const float xScale = yScale / aspect;
    const float range = farZ / (nearZ - farZ);

    Mat4 r;
    r.m[0][0] = xScale;
    r.m[1][1] = yScale;
    r.m[2][2] = range;
    r.m[2][3] = -1.0f;  // w' = -z (오른손: 카메라 앞이 -Z)
    r.m[3][2] = range * nearZ;
    return r;
}

}  // namespace deskpet::core
