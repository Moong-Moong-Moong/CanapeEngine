#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Canape
{
    // Names the feature bits that differ between two Jolt version IDs. Empty when the feature bits match.
    std::string DescribeJoltFeatureMismatch(std::uint64_t expectedId, std::uint64_t actualId);

    // Process-wide Jolt state: allocator, Factory, registered types, temp allocator and job system.
    // Only one instance can be live; check IsValid() and read GetError() after construction.
    class JoltRuntime
    {
    public:
        JoltRuntime();
        ~JoltRuntime();

        JoltRuntime(const JoltRuntime&) = delete;
        JoltRuntime& operator=(const JoltRuntime&) = delete;

        bool IsValid() const { return m_Impl != nullptr; }
        const std::string& GetError() const { return m_Error; }

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        std::string m_Error;
    };
}
