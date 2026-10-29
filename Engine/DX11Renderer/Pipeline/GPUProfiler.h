#pragma once

#if CANAPE_RENDER_DEBUG

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Canape
{
    struct GpuTimingResult
    {
        uint64_t frameId = 0;
        bool valid = false;
        double frameMs = 0.0;
        double drawSceneMs = 0.0;
        double finalOutputMs = 0.0;
    };

    class GPUProfiler
    {
    public:
        bool Initialize(ID3D11Device* device, std::string& error);
        void Poll(ID3D11DeviceContext* context);
        bool BeginFrame(ID3D11DeviceContext* context, uint64_t frameId);
        void BeginDrawScene(ID3D11DeviceContext* context);
        void EndDrawScene(ID3D11DeviceContext* context);
        void BeginFinalOutput(ID3D11DeviceContext* context);
        void EndFinalOutput(ID3D11DeviceContext* context);
        void EndFrame(ID3D11DeviceContext* context);

        const GpuTimingResult& LatestResult() const { return m_latest; }

    private:
        static constexpr size_t RingSize = 4;
        enum class SlotState { Free, Recording, Pending, Failed };

        struct TimestampPair
        {
            Microsoft::WRL::ComPtr<ID3D11Query> start;
            Microsoft::WRL::ComPtr<ID3D11Query> end;
        };

        struct FrameSlot
        {
            SlotState state = SlotState::Free;
            uint64_t frameId = 0;
            bool valid = true;
            bool drawOpen = false;
            bool finalOpen = false;
            bool finalComplete = false;
            size_t drawCount = 0;
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            TimestampPair frame;
            TimestampPair finalOutput;
            std::vector<TimestampPair> drawScenes;
        };

        bool CreateTimestampPair(TimestampPair& pair);
        void Publish(const GpuTimingResult& result);

        Microsoft::WRL::ComPtr<ID3D11Device> m_device;
        std::array<FrameSlot, RingSize> m_slots{};
        size_t m_nextSlot = 0;
        int m_activeSlot = -1;
        GpuTimingResult m_latest{};
    };
}

#endif
