#pragma once

namespace Canape
{
    // Shared CPU settings; no graphics API types or GPU packing requirements.
    struct PostProcessSettings
    {
        bool enabled = true;
        bool toneMappingEnabled = true;
    };
}
