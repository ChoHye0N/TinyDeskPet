#include "core/Config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <system_error>

namespace deskpet::core {
namespace {

// 값 해석 실패 시 이유를 돌려줍니다. 성공하면 std::nullopt.
using ApplyResult = std::optional<std::string>;
using Applier = std::function<ApplyResult(std::string_view value, AppConfig& config)>;

std::string_view trim(std::string_view text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string toLower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

ApplyResult assignInt(std::string_view value, int minValue, int maxValue, int& out) {
    int parsed = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || end != value.data() + value.size()) {
        return std::string("정수가 아닙니다");
    }
    if (parsed < minValue || parsed > maxValue) {
        return std::format("허용 범위 {} ~ {} 밖입니다", minValue, maxValue);
    }
    out = parsed;
    return std::nullopt;
}

ApplyResult assignSampleCount(std::string_view value, int& out) {
    int parsed = 0;
    if (assignInt(value, 1, 8, parsed).has_value() || (parsed & (parsed - 1)) != 0) {
        return std::string("1, 2, 4, 8 중 하나가 아닙니다");
    }
    out = parsed;
    return std::nullopt;
}

ApplyResult assignFloat(std::string_view value, float minValue, float maxValue, float& out) {
    float parsed = 0.0f;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || end != value.data() + value.size()) {
        return std::string("숫자가 아닙니다");
    }
    if (parsed < minValue || parsed > maxValue) {
        return std::format("허용 범위 {} ~ {} 밖입니다", minValue, maxValue);
    }
    out = parsed;
    return std::nullopt;
}

ApplyResult assignOptionalInt(std::string_view value, int minValue, int maxValue,
                              std::optional<int>& out) {
    int parsed = 0;
    if (ApplyResult error = assignInt(value, minValue, maxValue, parsed)) {
        return error;
    }
    out = parsed;
    return std::nullopt;
}

ApplyResult assignBool(std::string_view value, bool& out) {
    const std::string lower = toLower(value);
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        out = true;
        return std::nullopt;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        out = false;
        return std::nullopt;
    }
    return std::string("true/false 값이 아닙니다");
}

ApplyResult assignLevel(std::string_view value, logging::Level& out) {
    if (const auto parsed = logging::parseLevel(value)) {
        out = *parsed;
        return std::nullopt;
    }
    return std::string("trace/debug/info/warn/error/off 중 하나가 아닙니다");
}

// "섹션.키" → 값을 적용하는 함수. 새 설정 키는 여기에 추가합니다.
const std::vector<std::pair<std::string_view, Applier>>& appliers() {
    static const std::vector<std::pair<std::string_view, Applier>> table = {
        {"window.width",
         [](std::string_view v, AppConfig& c) { return assignInt(v, 32, 2048, c.window.width); }},
        {"window.height",
         [](std::string_view v, AppConfig& c) { return assignInt(v, 32, 2048, c.window.height); }},
        {"window.margin_right",
         [](std::string_view v, AppConfig& c) {
             return assignInt(v, 0, 10000, c.window.marginRight);
         }},
        {"window.always_on_top",
         [](std::string_view v, AppConfig& c) { return assignBool(v, c.window.alwaysOnTop); }},
        {"character.gravity",
         [](std::string_view v, AppConfig& c) {
             return assignFloat(v, 1.0f, 100000.0f, c.character.gravity);
         }},
        {"character.jump_speed",
         [](std::string_view v, AppConfig& c) {
             return assignFloat(v, 0.0f, 100000.0f, c.character.jumpSpeed);
         }},
        {"character.drag_threshold",
         [](std::string_view v, AppConfig& c) {
             return assignFloat(v, 0.0f, 100.0f, c.character.dragThreshold);
         }},
        {"character.max_fall_speed",
         [](std::string_view v, AppConfig& c) {
             return assignFloat(v, 1.0f, 100000.0f, c.character.maxFallSpeed);
         }},
        {"renderer.vsync",
         [](std::string_view v, AppConfig& c) { return assignBool(v, c.renderer.vsync); }},
        {"renderer.debug_layer",
         [](std::string_view v, AppConfig& c) { return assignBool(v, c.renderer.debugLayer); }},
        {"renderer.msaa",
         [](std::string_view v, AppConfig& c) { return assignSampleCount(v, c.renderer.msaa); }},
        {"model.path",
         [](std::string_view v, AppConfig& c) {
             c.model.path = std::string(v);
             return ApplyResult{};
         }},
        {"animation.idle_motion",
         [](std::string_view v, AppConfig& c) { return assignBool(v, c.animation.idleMotion); }},
        {"state.last_x",
         [](std::string_view v, AppConfig& c) {
             return assignOptionalInt(v, -100000, 100000, c.state.lastX);
         }},
        {"state.last_y",
         [](std::string_view v, AppConfig& c) {
             return assignOptionalInt(v, -100000, 100000, c.state.lastY);
         }},
        {"state.scale", [](std::string_view v,
                           AppConfig& c) { return assignOptionalInt(v, 50, 200, c.state.scale); }},
        {"log.level", [](std::string_view v, AppConfig& c) { return assignLevel(v, c.log.level); }},
        {"log.to_file",
         [](std::string_view v, AppConfig& c) { return assignBool(v, c.log.toFile); }},
    };
    return table;
}

// 값 뒤에 붙은 주석(; 또는 #)을 잘라냅니다.
std::string_view stripInlineComment(std::string_view line) {
    const auto pos = line.find_first_of(";#");
    return pos == std::string_view::npos ? line : line.substr(0, pos);
}

// 줄 단위로 나눔. 줄바꿈 문자는 각 줄에 포함된 채로 둠 (원문 그대로 다시 이어 붙이기 위함)
std::vector<std::string_view> splitLinesKeepingEol(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const std::size_t length = newline == std::string_view::npos ? text.size() : newline + 1;
        lines.push_back(text.substr(0, length));
        text.remove_prefix(length);
    }
    return lines;
}

// "키 = 값" 줄의 소문자 키. '='가 없으면 빈 문자열
std::string keyOf(std::string_view line) {
    const auto equals = line.find('=');
    return equals == std::string_view::npos ? std::string{} : toLower(trim(line.substr(0, equals)));
}

// 원본 줄에서 기존 값(oldValue)만 newValue로 바꿈. "키 = " 앞부분과 뒤의 주석·줄바꿈은 유지
std::string replaceLineValue(std::string_view raw, std::string_view oldValue,
                             std::string_view newValue) {
    std::size_t valueBegin = raw.find('=') + 1;
    while (valueBegin < raw.size() && (raw[valueBegin] == ' ' || raw[valueBegin] == '\t')) {
        ++valueBegin;
    }
    std::string result(raw.substr(0, valueBegin));
    result += newValue;
    result += raw.substr(valueBegin + oldValue.size());
    return result;
}

// lines[position] 앞에 newLine을 끼워 넣어 다시 이어 붙임
std::string insertLine(const std::vector<std::string_view>& lines, std::size_t position,
                       std::string_view newLine, std::string_view eol) {
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i == position) {
            result += newLine;
        }
        result += lines[i];
        if (i + 1 == position && !lines[i].ends_with('\n')) {
            result += eol;  // 마지막 줄에 줄바꿈이 없던 경우
        }
    }
    if (position == lines.size()) {
        result += newLine;
    }
    return result;
}

}  // namespace

ConfigLoadResult parseConfig(std::string_view text) {
    ConfigLoadResult result;

    // 메모장 등이 붙이는 UTF-8 BOM 제거
    constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
    if (text.starts_with(kUtf8Bom)) {
        text.remove_prefix(kUtf8Bom.size());
    }

    std::string section;
    int lineNumber = 0;

    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view rawLine = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++lineNumber;

        const std::string_view line = trim(stripInlineComment(rawLine));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                result.warnings.push_back(
                    std::format("{}행: 섹션 형식이 잘못되었습니다: '{}'", lineNumber, line));
                continue;
            }
            section = toLower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            result.warnings.push_back(
                std::format("{}행: '키 = 값' 형식이 아닙니다: '{}'", lineNumber, line));
            continue;
        }

        const std::string key = toLower(trim(line.substr(0, equals)));
        const std::string_view value = trim(line.substr(equals + 1));
        std::string fullKey = section;
        if (!fullKey.empty()) {
            fullKey += '.';
        }
        fullKey += key;

        const auto& table = appliers();
        const auto it = std::find_if(table.begin(), table.end(),
                                     [&](const auto& entry) { return entry.first == fullKey; });
        if (it == table.end()) {
            result.warnings.push_back(
                std::format("{}행: 알 수 없는 설정 '{}'을(를) 무시합니다", lineNumber, fullKey));
            continue;
        }

        if (const ApplyResult error = it->second(value, result.config)) {
            result.warnings.push_back(
                std::format("{}행: '{}' 값 '{}'이(가) 잘못되었습니다 ({}). "
                            "기본값을 유지합니다",
                            lineNumber, fullKey, value, *error));
        }
    }

    return result;
}

std::string setIniValue(std::string_view text, std::string_view section, std::string_view key,
                        std::string_view value) {
    const std::string wantedSection = toLower(section);
    const std::string wantedKey = toLower(key);
    const std::string_view eol = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    const std::vector<std::string_view> lines = splitLinesKeepingEol(text);

    std::string current;
    std::optional<std::size_t> sectionEnd;  // 원하는 섹션의 마지막 내용 줄 다음 위치
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = trim(stripInlineComment(lines[i]));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            current = toLower(trim(line.substr(1, line.size() - 2)));
            if (current == wantedSection) {
                sectionEnd = i + 1;
            }
            continue;
        }
        if (current != wantedSection) {
            continue;
        }
        sectionEnd = i + 1;
        if (keyOf(line) == wantedKey) {
            // 원본 텍스트에서 이 줄만 값을 바꿔 끼움
            const auto offset = static_cast<std::size_t>(lines[i].data() - text.data());
            std::string result(text.substr(0, offset));
            result += replaceLineValue(lines[i], trim(line.substr(line.find('=') + 1)), value);
            result += text.substr(offset + lines[i].size());
            return result;
        }
    }

    const std::string newLine = std::format("{} = {}{}", key, value, eol);
    if (sectionEnd) {
        return insertLine(lines, *sectionEnd, newLine, eol);
    }

    std::string result(text);
    if (!result.empty()) {
        if (!result.ends_with('\n')) {
            result += eol;
        }
        result += eol;  // 섹션 사이 빈 줄
    }
    result += std::format("[{}]{}", section, eol);
    result += newLine;
    return result;
}

bool saveConfigValues(const std::filesystem::path& path, const std::vector<IniValue>& values) {
    std::string content;
    if (std::ifstream in(path, std::ios::binary); in) {
        content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    for (const IniValue& v : values) {
        content = setIniValue(content, v.section, v.key, v.value);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return static_cast<bool>(out);
}

ConfigLoadResult loadConfigFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ConfigLoadResult result;
        result.warnings.push_back(
            std::format("설정 파일을 열 수 없어 기본값을 사용합니다: {}", pathToUtf8(path)));
        return result;
    }

    const std::string content{std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>()};
    return parseConfig(content);
}

}  // namespace deskpet::core
