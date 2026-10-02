#include "renderer/SampleCount.h"

#include <gtest/gtest.h>

#include <vector>

using deskpet::renderer::chooseSampleCount;

TEST(SampleCount, SupportedRequest_IsKept) {
    EXPECT_EQ(chooseSampleCount(4, [](int) { return true; }), 4);
}

TEST(SampleCount, UnsupportedRequest_HalvesUntilSupported) {
    std::vector<int> tried;
    const int chosen = chooseSampleCount(8, [&](int n) {
        tried.push_back(n);
        return n <= 2;
    });
    EXPECT_EQ(chosen, 2);
    EXPECT_EQ(tried, (std::vector<int>{8, 4, 2}));
}

TEST(SampleCount, NothingSupported_FallsBackToOne) {
    EXPECT_EQ(chooseSampleCount(8, [](int) { return false; }), 1);
}

TEST(SampleCount, OneOrLess_MeansOffWithoutQuerying) {
    bool queried = false;
    const auto supported = [&](int) {
        queried = true;
        return true;
    };
    EXPECT_EQ(chooseSampleCount(1, supported), 1);
    EXPECT_EQ(chooseSampleCount(0, supported), 1);
    EXPECT_FALSE(queried);
}
