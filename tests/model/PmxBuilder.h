#pragma once

// 테스트용 PMX 2.x 바이너리 생성기. 필드 순서는 PMX 사양(PmxEditor 동봉 "PMX仕様.txt")을 따릅니다.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace deskpet::test {

class PmxWriter {
public:
    // encoding: 0 = UTF-16LE, 1 = UTF-8. indexSize: 정점·본 등 인덱스 크기(1, 2, 4)
    PmxWriter(std::uint8_t encoding, std::uint8_t indexSize, std::uint8_t additionalVec4 = 0,
              float version = 2.0f)
        : encoding_(encoding), indexSize_(indexSize), additionalVec4_(additionalVec4) {
        bytes_ = {'P', 'M', 'X', ' '};
        f32(version);
        u8(8);  // 전역 설정 개수
        for (std::uint8_t value : {encoding, additionalVec4, indexSize, indexSize, indexSize,
                                   indexSize, indexSize, indexSize}) {
            u8(value);
        }
    }

    void u8(std::uint8_t v) { bytes_.push_back(v); }
    void i32(std::int32_t v) { raw(&v, 4); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void f32(float v) { raw(&v, 4); }
    void vec(std::initializer_list<float> values) {
        for (float v : values) {
            f32(v);
        }
    }

    // 정점 인덱스는 부호 없음, 그 외(본·텍스처 등)는 부호 있음 (-1 = 없음)
    void vertexIndex(std::uint32_t v) {
        if (indexSize_ == 1) {
            u8(static_cast<std::uint8_t>(v));
        } else if (indexSize_ == 2) {
            u16(static_cast<std::uint16_t>(v));
        } else {
            i32(static_cast<std::int32_t>(v));
        }
    }
    void index(std::int32_t v) {
        if (indexSize_ == 1) {
            u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(v)));
        } else if (indexSize_ == 2) {
            u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v)));
        } else {
            i32(v);
        }
    }

    // UTF-8 문자열을 설정된 인코딩으로 기록 (길이 int32 + 바이트)
    void text(std::string_view utf8) {
        if (encoding_ == 1) {
            i32(static_cast<std::int32_t>(utf8.size()));
            raw(utf8.data(), utf8.size());
            return;
        }
        const std::vector<std::uint16_t> utf16 = toUtf16(utf8);
        i32(static_cast<std::int32_t>(utf16.size() * 2));
        for (std::uint16_t unit : utf16) {
            u16(unit);
        }
    }

    // 추가 UV(vec4)는 0으로 채움
    void additionalUvs() {
        for (int i = 0; i < additionalVec4_; ++i) {
            vec({0, 0, 0, 0});
        }
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    void raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
    }

    // 테스트 문자열은 BMP 범위(3바이트 이하 UTF-8)만 사용
    static std::vector<std::uint16_t> toUtf16(std::string_view utf8) {
        std::vector<std::uint16_t> out;
        for (std::size_t i = 0; i < utf8.size();) {
            const auto c = static_cast<std::uint8_t>(utf8[i]);
            if (c < 0x80) {
                out.push_back(c);
                i += 1;
            } else if ((c & 0xE0U) == 0xC0U) {
                out.push_back(static_cast<std::uint16_t>(
                    ((c & 0x1FU) << 6U) | (static_cast<std::uint8_t>(utf8[i + 1]) & 0x3FU)));
                i += 2;
            } else {
                out.push_back(static_cast<std::uint16_t>(
                    ((c & 0x0FU) << 12U) |
                    ((static_cast<std::uint8_t>(utf8[i + 1]) & 0x3FU) << 6U) |
                    (static_cast<std::uint8_t>(utf8[i + 2]) & 0x3FU)));
                i += 3;
            }
        }
        return out;
    }

    std::vector<std::uint8_t> bytes_;
    std::uint8_t encoding_;
    std::uint8_t indexSize_;
    std::uint8_t additionalVec4_;
};

// 삼각형 1개, 텍스처 1개, 머티리얼 1개, 본 4개짜리 표준 테스트 모델.
// 정점 가중치는 BDEF1 / BDEF2 / SDEF를 섞어 건너뛰기 처리를 검증합니다.
inline std::vector<std::uint8_t> buildTestPmx(std::uint8_t encoding, std::uint8_t indexSize,
                                              std::uint8_t additionalVec4 = 0) {
    PmxWriter w(encoding, indexSize, additionalVec4);
    w.text("テスト");
    w.text("test");
    w.text("");
    w.text("");

    // 정점 3개 (MMD 단위, 왼손 좌표계, 정면 -Z)
    w.i32(3);
    w.vec({0, 0, 0});   // 위치
    w.vec({0, 0, -1});  // 법선
    w.vec({0, 0});      // UV
    w.additionalUvs();
    w.u8(0);  // BDEF1
    w.index(0);
    w.f32(1.0f);  // 에지 배율

    w.vec({10, 0, 0});
    w.vec({0, 0, -1});
    w.vec({1, 0});
    w.additionalUvs();
    w.u8(1);  // BDEF2
    w.index(0);
    w.index(1);
    w.f32(0.5f);
    w.f32(1.0f);

    w.vec({0, 20, 5});
    w.vec({0, 0, -1});
    w.vec({0, 1});
    w.additionalUvs();
    w.u8(3);  // SDEF: 본 2개 + 가중치 + C, R0, R1
    w.index(0);
    w.index(1);
    w.f32(0.5f);
    w.vec({0, 0, 0});
    w.vec({0, 0, 0});
    w.vec({0, 0, 0});
    w.f32(1.0f);

    // 면 (인덱스 개수)
    w.i32(3);
    w.vertexIndex(0);
    w.vertexIndex(1);
    w.vertexIndex(2);

    // 텍스처
    w.i32(1);
    w.text("tex\\body.tga");

    // 머티리얼
    w.i32(1);
    w.text("体");
    w.text("body");
    w.vec({1.0f, 0.5f, 0.25f, 1.0f});  // 확산색
    w.vec({0, 0, 0});                  // 반사색
    w.f32(5.0f);                       // 반사 강도
    w.vec({0.5f, 0.5f, 0.5f});         // 환경색
    w.u8(0x01 | 0x10);                 // 양면 그리기 + 에지
    w.vec({0, 0, 0, 1});               // 에지 색
    w.f32(1.0f);                       // 에지 크기
    w.index(0);                        // 텍스처
    w.index(-1);                       // 스피어 텍스처
    w.u8(0);                           // 스피어 모드
    w.u8(1);                           // 공유 툰
    w.u8(0);                           // 공유 툰 번호
    w.text("memo");
    w.i32(3);  // 이 머티리얼의 인덱스 수

    // 본 4개. 세 번째 본은 플래그를 모두 켜서 가변 길이 필드 건너뛰기를 검증
    w.i32(4);
    w.text("センター");
    w.text("center");
    w.vec({0, 8, 0});
    w.index(-1);
    w.i32(0);
    w.u16(0x0000);
    w.vec({0, 1, 0});  // 꼬리 위치 (오프셋)

    w.text("下半身");
    w.text("lower body");
    w.vec({0, 12, 1});
    w.index(0);
    w.i32(0);
    w.u16(0x0001);  // 꼬리 = 본 인덱스
    w.index(-1);

    w.text("左腕");
    w.text("arm_L");
    w.vec({2, 15, 0});
    w.index(1);
    w.i32(0);
    w.u16(0x0001 | 0x0020 | 0x0100 | 0x0400 | 0x0800 | 0x2000);
    w.index(-1);       // 꼬리
    w.index(1);        // 부여 부모
    w.f32(0.5f);       // 부여율
    w.vec({1, 0, 0});  // 고정 축
    w.vec({1, 0, 0});  // 로컬 X
    w.vec({0, 0, 1});  // 로컬 Z
    w.i32(0);          // 외부 부모 키
    w.index(0);        // IK 타깃
    w.i32(10);         // 반복
    w.f32(0.1f);       // 각도 제한
    w.i32(1);          // 링크 수
    w.index(1);        // 링크 본
    w.u8(1);           // 각도 제한 있음
    w.vec({-1, -1, -1});
    w.vec({1, 1, 1});

    w.text("右目");
    w.text("eye_R");
    w.vec({-1, 18, -1});
    w.index(2);
    w.i32(0);
    w.u16(0x0000);
    w.vec({0, 0, 0});

    // 모프 3개: 정점 모프(まばたき), 본 모프(건너뛰기 검증), 정점 모프(びっくり)
    w.i32(3);
    w.text("まばたき");
    w.text("blink");
    w.u8(1);  // 패널: 눈
    w.u8(1);  // 종류: 정점
    w.i32(2);
    w.vertexIndex(0);
    w.vec({0, -1, 2});  // MMD 단위 오프셋
    w.vertexIndex(2);
    w.vec({0, -2, 0});

    w.text("ボーン");
    w.text("bone");
    w.u8(4);
    w.u8(2);  // 종류: 본 (본 인덱스 + 이동 vec3 + 회전 vec4)
    w.i32(1);
    w.index(1);
    w.vec({0, 0, 0});
    w.vec({0, 0, 0, 1});

    w.text("びっくり");
    w.text("surprised");
    w.u8(1);
    w.u8(1);
    w.i32(1);
    w.vertexIndex(1);
    w.vec({0, 1, 0});

    // 표시 틀 이후는 로더가 읽지 않으므로 생략
    return w.bytes();
}

}  // namespace deskpet::test
