#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

//***** UI 샘플이 함께 쓰는 코드 생성 텍스처
namespace UIDemoTextures
{
    constexpr uint32_t kRoundedSize = 32;
    constexpr float kRoundedRadius = 12.0f;

    // 흰색 둥근 사각형(가장자리 1픽셀 안티앨리어싱), RGBA8. 9-slice로 늘려 쓴다.
    inline std::vector<uint8_t> MakeRounded()
    {
        std::vector<uint8_t> pixels(size_t(kRoundedSize) * kRoundedSize * 4, 255);
        for (uint32_t y = 0; y < kRoundedSize; ++y)
        {
            for (uint32_t x = 0; x < kRoundedSize; ++x)
            {
                const float px = x + 0.5f, py = y + 0.5f;
                const float cx = std::clamp(px, kRoundedRadius, kRoundedSize - kRoundedRadius);
                const float cy = std::clamp(py, kRoundedRadius, kRoundedSize - kRoundedRadius);
                const float distance = std::hypot(px - cx, py - cy);
                const float alpha = std::clamp(kRoundedRadius - distance + 0.5f, 0.0f, 1.0f);
                pixels[(size_t(y) * kRoundedSize + x) * 4 + 3] = static_cast<uint8_t>(std::lround(alpha * 255.0f));
            }
        }
        return pixels;
    }
}
