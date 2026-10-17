#include <beatforge/BuildInfo.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Version string matches the CMake project version", "[buildinfo]")
{
    REQUIRE (beatforge::versionString() == BEATFORGE_EXPECTED_VERSION);
}

TEST_CASE ("JUCE version is pinned to the expected release", "[buildinfo]")
{
    REQUIRE (beatforge::juceVersionString() == "9.0.4");
}
