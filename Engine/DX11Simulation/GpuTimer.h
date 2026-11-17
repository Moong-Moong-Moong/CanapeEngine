#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "DX11Simulation/D3D.h"
#include "DX11Simulation/FrameStats.h"
#include "DX11Simulation/RingBuffer.h"

namespace Canape
{
    // Times ranges of GPU commands with timestamp queries.
    class GpuTimer
    {
    private:
        using QueryRef = RefPtr<ID3D11Query>;

        class QueryPool
        {
        private:
            ID3D11Device& device;
            D3D11_QUERY_DESC desc;

            std::vector<QueryRef> queries;
            std::vector<ID3D11Query*> freeQueries;

        public:
            QueryPool(ID3D11Device& device, const D3D11_QUERY_DESC& desc)
                : device(device), desc(desc) {}

            // Makes queries until count of them are free.
            void Reserve(std::size_t count);

            [[nodiscard]] ID3D11Query* Acquire();
            void Release(ID3D11Query* query)
            {
                freeQueries.push_back(query);
            }
        };

        enum class GpuPass : std::uint8_t
        {
            Compute,
            Render
        };

        // The queries are the pool's, lent from BeginRange until the frame is collected.
        struct Range
        {
            QueryPool& pool;
            ID3D11Query* start = nullptr;
            ID3D11Query* end = nullptr;
            GpuPass kind = GpuPass::Compute;

            Range(QueryPool& pool, GpuPass kind)
                : pool(pool), kind(kind)
            {
                // Both queries or neither.
                pool.Reserve(2);

                start = pool.Acquire();
                end = pool.Acquire();
            }
            ~Range()
            {
                if(start != nullptr)
                {
                    pool.Release(start);
                    start = nullptr;
                }

                if(end != nullptr)
                {
                    pool.Release(end);
                    end = nullptr;
                }
            }

            Range(const Range&) = delete;
            Range& operator=(const Range&) = delete;
            Range(Range&& other) noexcept
                : pool(other.pool),
                  start(std::exchange(other.start, nullptr)),
                  end(std::exchange(other.end, nullptr)),
                  kind(other.kind) {}
            Range& operator=(Range&&) = delete;
        };

        struct Frame
        {
            static constexpr std::size_t ReservedRanges = 8;

            QueryRef disjoint = nullptr;
            std::vector<Range> ranges;
            std::uint64_t number = NoFrame;

            Frame(ID3D11Device&);

            Range& NewRange(QueryPool& pool, GpuPass kind)
            {
                ranges.emplace_back(pool, kind);
                return ranges.back();
            }

            void Reset()
            {
                ranges.clear();
            }

            Range& BackRange()
            {
                return ranges.back();
            } 

            // False while the GPU has not finished the frame. Otherwise the frame is done with, and latest
            // has become its measurement if that can be trusted.
            [[nodiscard]] bool TryRead(ID3D11DeviceContext&, GpuTiming& latest) const;
        };

        // Present lets the GPU fall three frames behind at most,
        // DXGI's default frame latency, and one more frame is being recorded.
        static constexpr std::size_t RingSize = 4;

    private:
        ID3D11DeviceContext& context;

        QueryPool timestamps;
        // Frames waiting for the GPU, oldest in front; while recording, the back one is being recorded.
        RingBuffer<Frame, RingSize> frames;

        bool enabled = false;
        bool recording = false;
    #ifndef NDEBUG
        bool renderOpen = false;
        bool computeOpen = false;
    #endif

    public:
        GpuTimer(ID3D11Device&, ID3D11DeviceContext&);

        // Takes effect from the next BeginFrame.
        void SetEnabled(bool value) noexcept
        {
            enabled = value;
        }

        // Reads the frames the GPU has finished, giving the measurement of the last that could be trusted.
        // Not while a frame is being recorded.
        std::optional<GpuTiming> Collect();
        // Every frame of the ring waits for the GPU. Frames paced by Present leave room once collected;
        // only a CPU running ahead without that pacing fills the ring.
        bool IsFull() const
        {
            return frames.Full();
        }

        // Not while full. A frame that began is recorded to its end, and its measurement carries number.
        void BeginFrame(std::uint64_t number);
        void EndFrame();
        // Inside a frame, one range at a time.
        void BeginCompute();
        void EndCompute();
        void BeginRender();
        void EndRender();

        bool IsRecording() const noexcept
        {
            return recording;
        }

    private:
        static std::array<Frame, RingSize> createFrames(ID3D11Device&);
    };

    class RenderTimeScope
    {
    private:
        GpuTimer& timer;

    public:
        RenderTimeScope(GpuTimer& timer)
            : timer(timer)
        {
            timer.BeginRender();
        }
        ~RenderTimeScope()
        {
            timer.EndRender();
        }

        RenderTimeScope(const RenderTimeScope&) = delete;
        RenderTimeScope& operator=(const RenderTimeScope&) = delete;
        RenderTimeScope(RenderTimeScope&&) = delete;
        RenderTimeScope& operator=(RenderTimeScope&&) = delete;
    };

    class ComputeTimeScope
    {
    private:
        GpuTimer& timer;

    public:
        ComputeTimeScope(GpuTimer& timer)
            : timer(timer)
        {
            timer.BeginCompute();
        }
        ~ComputeTimeScope()
        {
            timer.EndCompute();
        }

        ComputeTimeScope(const ComputeTimeScope&) = delete;
        ComputeTimeScope& operator=(const ComputeTimeScope&) = delete;
        ComputeTimeScope(ComputeTimeScope&&) = delete;
        ComputeTimeScope& operator=(ComputeTimeScope&&) = delete;
    };
}
