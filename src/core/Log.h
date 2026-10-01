#pragma once

// 레벨별 로그. 출력 대상(싱크)은 main에서 정합니다.
//
//   logging::info("창 생성: {}x{}", width, height);
//
// 꺼진 레벨은 std::format을 호출하지 않으므로 문자열 생성 비용이 없습니다.
// write()는 내부 뮤텍스로 보호되어 여러 스레드에서 호출해도 안전합니다.

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deskpet::core::logging {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

// line은 formatLine()으로 만들어진 한 줄 (줄바꿈 없음)
using Sink = std::function<void(Level level, std::string_view line)>;

void setLevel(Level level);
[[nodiscard]] Level level();
[[nodiscard]] bool isEnabled(Level level);

// nullptr를 넘기면 기본 싱크(stderr)로 되돌립니다.
void setSink(Sink sink);

void write(Level level, std::string_view message);

[[nodiscard]] std::string_view toString(Level level);
[[nodiscard]] std::optional<Level> parseLevel(std::string_view text);

// "[   1.234] [INFO ] message" 형태. 시간은 프로그램 시작 후 경과 초.
[[nodiscard]] std::string formatLine(Level level, std::string_view message);

template <class... Args>
void log(Level level, std::format_string<Args...> fmt, Args&&... args) {
    if (isEnabled(level)) {
        write(level, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <class... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Trace, fmt, std::forward<Args>(args)...);
}

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Debug, fmt, std::forward<Args>(args)...);
}

template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Info, fmt, std::forward<Args>(args)...);
}

template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Warn, fmt, std::forward<Args>(args)...);
}

template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Error, fmt, std::forward<Args>(args)...);
}

}  // namespace deskpet::core::logging
