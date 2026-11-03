#include "JoltPhysics/JoltRuntime.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace Canape
{
    namespace
    {
        constexpr std::size_t TempAllocatorBytes = 10u * 1024u * 1024u;
        constexpr std::size_t FeatureBitCount = 11;
        constexpr std::uint64_t FeatureShift = 24;

        // Same order as JPH_VERSION_FEATURE_BIT_1..11 in Jolt/Core/Core.h.
        constexpr std::array<const char*, FeatureBitCount> FeatureNames = {
            "JPH_DOUBLE_PRECISION",
            "JPH_CROSS_PLATFORM_DETERMINISTIC",
            "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
            "JPH_PROFILE_ENABLED",
            "JPH_EXTERNAL_PROFILE",
            "JPH_DEBUG_RENDERER",
            "JPH_DISABLE_TEMP_ALLOCATOR",
            "JPH_DISABLE_CUSTOM_ALLOCATOR",
            "JPH_OBJECT_LAYER_BITS=32",
            "JPH_ENABLE_ASSERTS",
            "JPH_OBJECT_STREAM",
        };

        std::atomic<bool> s_Alive = false;

        void TraceToDebugger(const char* format, ...)
        {
            std::array<char, 1024> buffer;
            std::va_list args;
            va_start(args, format);
            std::vsnprintf(buffer.data(), buffer.size() - 1, format, args);
            va_end(args);
            buffer.back() = '\0';
            OutputDebugStringA(buffer.data());
            OutputDebugStringA("\n");
        }

#ifdef JPH_ENABLE_ASSERTS
        bool AssertToDebugger(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            TraceToDebugger("%s(%u): Jolt assert failed: %s%s%s", file, line, expression, message ? " : " : "",
                message ? message : "");
            return IsDebuggerPresent() != FALSE;
        }
#endif

        // Finds the feature bits of the linked library by probing VerifyJoltVersionIDInternal.
        std::string DescribeLinkedLibraryMismatch()
        {
            using JPH::uint64;
            const std::uint64_t expected = JPH_VERSION_ID;
            const std::uint64_t versionBits = expected & ((std::uint64_t{1} << FeatureShift) - 1);
            for (std::uint64_t features = 0; features < (std::uint64_t{1} << FeatureBitCount); ++features)
            {
                const std::uint64_t candidate = (features << FeatureShift) | versionBits;
                if (JPH::VerifyJoltVersionIDInternal(candidate))
                {
                    return DescribeJoltFeatureMismatch(expected, candidate);
                }
            }
            return "Jolt version differs (major/minor/patch) from the headers used to build the engine";
        }
    }

    std::string DescribeJoltFeatureMismatch(std::uint64_t expectedId, std::uint64_t actualId)
    {
        const std::uint64_t diff = (expectedId ^ actualId) >> FeatureShift;
        std::string text;
        for (std::size_t i = 0; i < FeatureBitCount; ++i)
        {
            if (((diff >> i) & 1u) == 0)
            {
                continue;
            }
            const bool expectedOn = ((expectedId >> (FeatureShift + i)) & 1u) != 0;
            if (!text.empty())
            {
                text += ", ";
            }
            text += FeatureNames[i];
            text += expectedOn ? " (engine: on, Jolt.lib: off)" : " (engine: off, Jolt.lib: on)";
        }
        return text;
    }

    struct JoltRuntime::Impl
    {
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem;

        Impl()
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = TraceToDebugger;
#ifdef JPH_ENABLE_ASSERTS
            JPH::AssertFailed = AssertToDebugger;
#endif
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
            tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(TempAllocatorBytes);
            jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        }

        ~Impl()
        {
            jobSystem.reset();
            tempAllocator.reset();
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    };

    JoltRuntime::JoltRuntime()
    {
        if (s_Alive.exchange(true))
        {
            m_Error = "A JoltRuntime already exists in this process";
            return;
        }
        if (!JPH::VerifyJoltVersionID())
        {
            m_Error = "Jolt.lib was built with different options than the engine: " + DescribeLinkedLibraryMismatch();
            s_Alive = false;
            return;
        }
        try
        {
            m_Impl = std::make_unique<Impl>();
        }
        catch (...)
        {
            s_Alive = false;
            throw;
        }
    }

    JoltRuntime::~JoltRuntime()
    {
        if (m_Impl)
        {
            m_Impl.reset();
            s_Alive = false;
        }
    }
}
