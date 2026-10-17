#include <beatforge/audio/LevelMeter.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using beatforge::audio::LevelMeter;

TEST_CASE ("Level meter keeps the maximum peak across blocks until taken", "[audio][meter]")
{
    LevelMeter meter;
    std::vector<float> a { 0.1f, -0.5f, 0.2f }, b { 0.3f, 0.0f, -0.25f };

    const float* blockA[] { a.data(), b.data() };
    meter.process (blockA, 2, 3);

    std::vector<float> quiet { 0.01f, 0.01f, 0.01f };
    const float* blockB[] { quiet.data(), quiet.data() };
    meter.process (blockB, 2, 3);

    REQUIRE (meter.takePeak (0) == 0.5f);
    REQUIRE (meter.takePeak (1) == 0.3f);
    REQUIRE (meter.takePeak (0) == 0.0f);
}

TEST_CASE ("Level meter ignores out-of-range channels", "[audio][meter]")
{
    LevelMeter meter;
    REQUIRE (meter.takePeak (-1) == 0.0f);
    REQUIRE (meter.takePeak (LevelMeter::maxChannels) == 0.0f);
}

TEST_CASE ("Decibel conversion", "[audio][meter]")
{
    REQUIRE (LevelMeter::toDecibels (1.0f) == Catch::Approx (0.0f));
    REQUIRE (LevelMeter::toDecibels (0.5f) == Catch::Approx (-6.0206f).margin (1e-3));
    REQUIRE (LevelMeter::toDecibels (0.0f) == -100.0f);
    REQUIRE (LevelMeter::toDecibels (1e-9f, -60.0f) == -60.0f);
}
