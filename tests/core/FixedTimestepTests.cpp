#include "core/FixedTimestep.h"

#include <gtest/gtest.h>

#include <limits>

using deskpet::core::FixedTimestep;

namespace {
constexpr double kStep = 1.0 / 60.0;
}

TEST(FixedTimestep, ExactlyOneStep_ReturnsOne) {
    FixedTimestep timestep(kStep);
    EXPECT_EQ(timestep.advance(kStep), 1);
    EXPECT_NEAR(timestep.alpha(), 0.0, 1e-6);
}

TEST(FixedTimestep, LessThanStep_AccumulatesUntilEnough) {
    FixedTimestep timestep(kStep);
    EXPECT_EQ(timestep.advance(kStep / 2.0), 0);
    EXPECT_NEAR(timestep.alpha(), 0.5, 1e-6);
    EXPECT_EQ(timestep.advance(kStep / 2.0), 1);
}

TEST(FixedTimestep, SeveralSteps_ReturnsCountAndKeepsRemainder) {
    FixedTimestep timestep(kStep);
    EXPECT_EQ(timestep.advance(kStep * 2.5), 2);
    EXPECT_NEAR(timestep.alpha(), 0.5, 1e-6);
}

TEST(FixedTimestep, LongStall_ClampsToMaxStepsAndDropsBacklog) {
    FixedTimestep timestep(kStep, 5);
    EXPECT_EQ(timestep.advance(1.0), 5);  // 1초 멈춤 → 60스텝이 아니라 5스텝
    EXPECT_EQ(timestep.advance(0.0), 0);  // 밀린 시간은 버려짐
}

TEST(FixedTimestep, InvalidInput_TreatedAsZero) {
    FixedTimestep timestep(kStep);
    EXPECT_EQ(timestep.advance(-1.0), 0);
    EXPECT_EQ(timestep.advance(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ(timestep.advance(std::numeric_limits<double>::infinity()), 0);
    EXPECT_NEAR(timestep.alpha(), 0.0, 1e-9);
}

TEST(FixedTimestep, Reset_ClearsAccumulator) {
    FixedTimestep timestep(kStep);
    (void)timestep.advance(kStep * 0.9);
    timestep.reset();
    EXPECT_EQ(timestep.advance(kStep * 0.5), 0);
}

TEST(FixedTimestep, ManyFrames_TotalStepsMatchElapsedTime) {
    // 144Hz로 1초 동안 진행하면 60스텝이어야 함 (결정성)
    FixedTimestep timestep(kStep, 5);
    int total = 0;
    for (int i = 0; i < 144; ++i) {
        total += timestep.advance(1.0 / 144.0);
    }
    EXPECT_EQ(total, 60);
}
