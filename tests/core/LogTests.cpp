#include "core/Log.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace logging = deskpet::core::logging;

namespace {

// 테스트마다 싱크와 레벨을 원래대로 되돌립니다 (전역 상태이므로).
class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::setSink([this](logging::Level level, std::string_view line) {
            captured_.push_back({level, std::string(line)});
        });
    }

    void TearDown() override {
        logging::setSink(nullptr);
        logging::setLevel(logging::Level::Info);
    }

    struct Entry {
        logging::Level level;
        std::string line;
    };
    std::vector<Entry> captured_;
};

}  // namespace

TEST_F(LogTest, MessagesBelowLevel_AreFiltered) {
    logging::setLevel(logging::Level::Warn);
    logging::info("숨겨짐 {}", 1);
    logging::warn("보임 {}", 2);
    logging::error("보임 {}", 3);

    ASSERT_EQ(captured_.size(), 2U);
    EXPECT_EQ(captured_[0].level, logging::Level::Warn);
    EXPECT_NE(captured_[0].line.find("보임 2"), std::string::npos);
    EXPECT_EQ(captured_[1].level, logging::Level::Error);
}

TEST_F(LogTest, OffLevel_DisablesEverything) {
    logging::setLevel(logging::Level::Off);
    logging::error("숨겨짐");
    EXPECT_TRUE(captured_.empty());
    EXPECT_FALSE(logging::isEnabled(logging::Level::Error));
}

TEST_F(LogTest, FormatLine_ContainsLevelAndMessage) {
    const std::string line = logging::formatLine(logging::Level::Warn, "hello");
    EXPECT_NE(line.find("[WARN ]"), std::string::npos);
    EXPECT_NE(line.find("hello"), std::string::npos);
}

TEST(LogLevel, ParseLevel_AcceptsNamesCaseInsensitively) {
    EXPECT_EQ(logging::parseLevel("trace"), logging::Level::Trace);
    EXPECT_EQ(logging::parseLevel("DEBUG"), logging::Level::Debug);
    EXPECT_EQ(logging::parseLevel("Info"), logging::Level::Info);
    EXPECT_EQ(logging::parseLevel("warning"), logging::Level::Warn);
    EXPECT_EQ(logging::parseLevel("error"), logging::Level::Error);
    EXPECT_EQ(logging::parseLevel("off"), logging::Level::Off);
    EXPECT_FALSE(logging::parseLevel("verbose").has_value());
}

TEST(LogLevel, ToString_RoundTrips) {
    for (const auto level : {logging::Level::Trace, logging::Level::Debug, logging::Level::Info,
                             logging::Level::Warn, logging::Level::Error, logging::Level::Off}) {
        EXPECT_EQ(logging::parseLevel(logging::toString(level)), level);
    }
}
