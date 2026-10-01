#include "core/LogFile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using deskpet::core::LogFile;
namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class LogFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("deskpet_logfile_" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
};

}  // namespace

TEST_F(LogFileTest, Open_KeepsPreviousRunAsDotOne) {
    const fs::path path = dir_ / "deskpet.log";
    {
        LogFile first;
        ASSERT_TRUE(first.open(path, 1024));
        first.write("first run");
    }
    {
        LogFile second;
        ASSERT_TRUE(second.open(path, 1024));
        second.write("second run");
    }
    EXPECT_EQ(readAll(path), "second run\n");
    EXPECT_EQ(readAll(dir_ / "deskpet.log.1"), "first run\n");
}

TEST_F(LogFileTest, Write_StopsAtSizeLimitWithNotice) {
    const fs::path path = dir_ / "deskpet.log";
    LogFile log;
    ASSERT_TRUE(log.open(path, 30));
    for (int i = 0; i < 10; ++i) {
        log.write("0123456789");  // 11바이트씩
    }
    const std::string content = readAll(path);
    EXPECT_EQ(content.find("0123456789\n0123456789\n"), 0U);
    EXPECT_NE(content.find(LogFile::kTruncatedNotice), std::string::npos);
    EXPECT_LT(content.size(), 30U + LogFile::kTruncatedNotice.size() + 2U);
}

TEST_F(LogFileTest, OpenFailure_ReturnsFalse) {
    LogFile log;
    EXPECT_FALSE(log.open(dir_ / "no_such_dir" / "deskpet.log", 1024));
    log.write("ignored");  // 열리지 않은 상태에서도 안전
}
