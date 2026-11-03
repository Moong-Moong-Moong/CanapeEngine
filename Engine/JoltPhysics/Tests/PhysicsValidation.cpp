// Standalone backend validation; intentionally not part of JoltPhysics.vcxproj.
#include "JoltPhysics/JoltRuntime.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace Canape;

namespace
{
    int g_failures = 0;
    int g_cases = 0;

    void Check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    void BeginCase(const char* name)
    {
        ++g_cases;
        std::cout << "case: " << name << '\n';
    }

    void RuntimeConstructsAndTearsDown()
    {
        BeginCase("runtime constructs with a matching version id and tears down");
        {
            JoltRuntime runtime;
            Check(runtime.IsValid(), "runtime is valid");
            Check(runtime.GetError().empty(), "runtime has no error");
        }
        JoltRuntime again;
        Check(again.IsValid(), "runtime can be created after the first one is gone");
    }

    void SecondRuntimeIsRefused()
    {
        BeginCase("a second live runtime is refused and does not disturb the first");
        JoltRuntime first;
        Check(first.IsValid(), "first runtime is valid");
        {
            JoltRuntime second;
            Check(!second.IsValid(), "second runtime is invalid");
            Check(!second.GetError().empty(), "second runtime reports an error");
        }
        Check(first.IsValid(), "first runtime stays valid after a refused second one is destroyed");
        JoltRuntime third;
        Check(!third.IsValid(), "the refused destructor did not release the process-wide slot");
    }

    void ConstructDestroyTwiceInARow()
    {
        BeginCase("construct and destroy twice in a row");
        for (int i = 0; i < 2; ++i)
        {
            JoltRuntime runtime;
            Check(runtime.IsValid(), "runtime is valid in iteration");
        }
    }

    void FeatureMismatchNamesTheDifferingBits()
    {
        BeginCase("feature mismatch names the differing bits");
        constexpr std::uint64_t shift = 24;
        constexpr std::uint64_t version = 0x050600;
        const auto same = DescribeJoltFeatureMismatch(version | (0x5ull << shift), version | (0x5ull << shift));
        Check(same.empty(), "identical ids produce no description");

        const auto text = DescribeJoltFeatureMismatch(version | (0x4ull << shift), version | (0x1ull << shift));
        Check(text.find("JPH_DOUBLE_PRECISION") != std::string::npos, "bit 1 is named");
        Check(text.find("JPH_FLOATING_POINT_EXCEPTIONS_ENABLED") != std::string::npos, "bit 3 is named");
        Check(text.find("JPH_CROSS_PLATFORM_DETERMINISTIC") == std::string::npos, "bit 2 is not named");
    }
}

int main()
{
    RuntimeConstructsAndTearsDown();
    SecondRuntimeIsRefused();
    ConstructDestroyTwiceInARow();
    FeatureMismatchNamesTheDifferingBits();

    if (g_failures != 0)
    {
        std::cout << "RESULT FAIL cases=" << g_cases << " failures=" << g_failures << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "RESULT PASS cases=" << g_cases << '\n';
    return EXIT_SUCCESS;
}
