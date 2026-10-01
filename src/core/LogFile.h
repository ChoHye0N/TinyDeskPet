#pragma once

// 로그 파일: 실행할 때마다 이전 로그를 .1로 보존하고, 크기 한도를 넘으면 기록을 멈춥니다.
// 명세: docs/03-detailed-design/core.md §3.5

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace deskpet::core {

class LogFile {
public:
    static constexpr std::string_view kTruncatedNotice =
        "[로그 크기 한도에 도달해 이후 기록을 생략합니다]";

    // 기존 파일을 "<이름>.1"로 옮긴 뒤 새로 엽니다. 실패하면 false (이후 write는 무시).
    [[nodiscard]] bool open(const std::filesystem::path& path, std::uintmax_t maxBytes);

    // 한 줄을 쓰고 바로 flush합니다 (비정상 종료 시에도 남도록).
    void write(std::string_view line);

    [[nodiscard]] bool isOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
    std::uintmax_t written_ = 0;
    std::uintmax_t maxBytes_ = 0;
    bool truncated_ = false;
};

}  // namespace deskpet::core
