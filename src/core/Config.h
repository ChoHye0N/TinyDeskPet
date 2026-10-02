#pragma once

// deskpet.ini 설정. 형식과 규칙은 docs/03-detailed-design/core.md §3.6 참고.

#include "core/Log.h"
#include "core/Math.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deskpet::core {

struct WindowConfig {
    int width = 200;
    int height = 200;
    int marginRight = 40;
    bool alwaysOnTop = true;
};

struct CharacterConfig {
    float gravity = 2400.0f;       // px/s²
    float jumpSpeed = 900.0f;      // px/s
    float dragThreshold = 4.0f;    // px
    float maxFallSpeed = 4000.0f;  // px/s
};

struct RendererConfig {
    bool vsync = true;
    int msaa = 4;  // MSAA 샘플 수 (1 = 끔, 2, 4, 8). 장치가 지원하지 않으면 렌더러가 낮춤
#ifdef NDEBUG
    bool debugLayer = false;
#else
    bool debugLayer = true;
#endif
};

struct ModelConfig {
    std::string path;  // VRM 파일. 상대 경로는 실행 파일 폴더 기준. 비면 슬라임 (ADR-0008)
};

struct AnimationConfig {
    bool idleMotion =
        true;  // 대기 중 숨쉬기 동작. 끄면 대기 중 화면이 멈춰 GPU를 쉬게 함 (DEBT-02)
};

// 프로그램이 직접 기록하는 값 (사용자가 고칠 필요 없음)
struct StateConfig {
    std::optional<int> lastX;  // 마지막 발 위치 (화면 좌표, 물리 px)
    std::optional<int> lastY;
    std::optional<int> scale;  // 캐릭터 크기 (%, 50 ~ 200). 메뉴의 크게/작게로 바뀜 (FR-06)

    // 둘 다 있을 때만 유효
    [[nodiscard]] std::optional<PointI> lastPosition() const {
        if (lastX && lastY) {
            return PointI{*lastX, *lastY};
        }
        return std::nullopt;
    }
};

struct LogConfig {
    logging::Level level = logging::Level::Info;
    bool toFile = true;
};

struct AppConfig {
    WindowConfig window;
    CharacterConfig character;
    RendererConfig renderer;
    ModelConfig model;
    AnimationConfig animation;
    LogConfig log;
    StateConfig state;
};

struct ConfigLoadResult {
    AppConfig config;
    std::vector<std::string> warnings;  // 사람이 읽을 수 있는 경고 메시지
};

// INI 텍스트를 해석합니다. 잘못된 항목은 경고를 남기고 기본값을 유지합니다.
[[nodiscard]] ConfigLoadResult parseConfig(std::string_view text);

// 파일을 읽어 parseConfig를 호출합니다. 파일이 없으면 기본값 + 경고를 반환합니다.
[[nodiscard]] ConfigLoadResult loadConfigFile(const std::filesystem::path& path);

// INI 텍스트에서 [section] key 값을 바꾼 텍스트를 반환합니다. 주석·순서·줄바꿈 형식은 유지하고,
// 키가 없으면 섹션 끝에, 섹션이 없으면 파일 끝에 추가합니다. 섹션·키는 대소문자 구분 없음.
[[nodiscard]] std::string setIniValue(std::string_view text, std::string_view section,
                                      std::string_view key, std::string_view value);

struct IniValue {
    std::string section;
    std::string key;
    std::string value;
};

// 파일을 읽어 setIniValue를 적용하고 다시 씁니다 (파일이 없으면 새로 만듦). 실패하면 false.
[[nodiscard]] bool saveConfigValues(const std::filesystem::path& path,
                                    const std::vector<IniValue>& values);

}  // namespace deskpet::core
