#pragma once

#include "JoltPhysics/JoltRuntime.h"

#include <memory>

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>

namespace Canape
{
    struct JoltRuntime::Impl
    {
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem;

        Impl();
        ~Impl();
    };
}
