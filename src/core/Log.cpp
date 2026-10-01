#include "core/Log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace deskpet::core::logging {
namespace {

std::atomic<Level> gLevel{Level::Info};
std::mutex gMutex;
Sink gSink;  // 비어 있으면 기본 싱크 사용

const auto gStartTime = std::chrono::steady_clock::now();

void defaultSink(Level /*level*/, std::string_view line) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(line.size()), line.data());
}

}  // namespace

void setLevel(Level level) {
    gLevel.store(level, std::memory_order_relaxed);
}

Level level() {
    return gLevel.load(std::memory_order_relaxed);
}

bool isEnabled(Level level) {
    const Level current = gLevel.load(std::memory_order_relaxed);
    return level != Level::Off && current != Level::Off && level >= current;
}

void setSink(Sink sink) {
    const std::scoped_lock lock(gMutex);
    gSink = std::move(sink);
}

void write(Level level, std::string_view message) {
    if (!isEnabled(level)) {
        return;
    }
    const std::string line = formatLine(level, message);
    const std::scoped_lock lock(gMutex);
    if (gSink) {
        gSink(level, line);
    } else {
        defaultSink(level, line);
    }
}

std::string_view toString(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Off: return "OFF";
    }
    return "?";
}

std::optional<Level> parseLevel(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "trace") {
        return Level::Trace;
    }
    if (lower == "debug") {
        return Level::Debug;
    }
    if (lower == "info") {
        return Level::Info;
    }
    if (lower == "warn" || lower == "warning") {
        return Level::Warn;
    }
    if (lower == "error") {
        return Level::Error;
    }
    if (lower == "off" || lower == "none") {
        return Level::Off;
    }
    return std::nullopt;
}

std::string formatLine(Level level, std::string_view message) {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - gStartTime;
    return std::format("[{:8.3f}] [{:<5}] {}", elapsed.count(), toString(level), message);
}

}  // namespace deskpet::core::logging
