#include "core/LogFile.h"

#include <system_error>

namespace deskpet::core {

bool LogFile::open(const std::filesystem::path& path, std::uintmax_t maxBytes) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::path previous = path;
        previous += ".1";
        std::filesystem::rename(path, previous, ec);  // 기존 .1은 덮어씀. 실패해도 계속 진행
    }

    file_.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    written_ = 0;
    maxBytes_ = maxBytes;
    truncated_ = false;
    return file_.is_open();
}

void LogFile::write(std::string_view line) {
    if (!file_.is_open() || truncated_) {
        return;
    }
    if (written_ + line.size() + 1 > maxBytes_) {
        file_ << kTruncatedNotice << '\n';
        file_.flush();
        truncated_ = true;
        return;
    }
    file_ << line << '\n';
    file_.flush();
    written_ += line.size() + 1;
}

}  // namespace deskpet::core
