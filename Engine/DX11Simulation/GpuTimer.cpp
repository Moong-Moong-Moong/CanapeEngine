#include "DX11Simulation/GpuTimer.h"

#include <cassert>

namespace
{
    Microsoft::WRL::ComPtr<ID3D11Query> createQuery(
        ID3D11Device& device,
        const D3D11_QUERY_DESC& desc)
    {
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        ThrowIfFailed(
            device.CreateQuery(
                &desc,
                &query
            ),
            "ID3D11Device::CreateQuery"
        );

        return query;
    }

    Microsoft::WRL::ComPtr<ID3D11Query> createDisjointQuery(ID3D11Device& device)
    {
        const D3D11_QUERY_DESC desc{
            .Query = D3D11_QUERY_TIMESTAMP_DISJOINT
        };
        return createQuery(device, desc);
    }

    constexpr D3D11_QUERY_DESC TimestampQueryDesc{
        .Query = D3D11_QUERY_TIMESTAMP
    };
}

namespace Canape
{
    void GpuTimer::QueryPool::Reserve(std::size_t count)
    {
        while(freeQueries.size() < count)
        {
            QueryRef query;
            ThrowIfFailed(
                device.CreateQuery(
                    &desc,
                    &query
                ),
                "ID3D11Device::CreateQuery"
            );
            queries.push_back(std::move(query));
            freeQueries.push_back(queries.back().Get());
        }
    }

    ID3D11Query* GpuTimer::QueryPool::Acquire()
    {
        assert(!freeQueries.empty());

        auto* const query = freeQueries.back();
        freeQueries.pop_back();

        return query;
    }

    GpuTimer::Frame::Frame(ID3D11Device& device)
    {
        disjoint = createDisjointQuery(device);
        ranges.reserve(Frame::ReservedRanges);
    }

    bool GpuTimer::Frame::TryRead(
        ID3D11DeviceContext& context,
        GpuTiming& outLatest) const
    {
        constexpr UINT NoFlush = D3D11_ASYNC_GETDATA_DONOTFLUSH;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT queryData{};
        const auto disjointResult = context.GetData(
            disjoint.Get(),
            &queryData,
            sizeof(queryData),
            NoFlush
        );
        if(disjointResult == S_FALSE) return false;

        bool measurable = SUCCEEDED(disjointResult)
            && !queryData.Disjoint
            && queryData.Frequency != 0;
        const auto millisecondsPerTick = measurable ?
            1000.0 / static_cast<double>(queryData.Frequency) :
            0.0;
        GpuTiming timing { .frame = number };
        // Every query is read, measurable or not: issuing one again with its result left behind is a warning.
        for(const auto& range : ranges)
        {
            UINT64 start = 0;
            UINT64 end = 0;
            const auto startResult = context.GetData(
                range.start,
                &start,
                sizeof(start),
                NoFlush
            );
            const auto endResult = context.GetData(
                range.end,
                &end,
                sizeof(end),
                NoFlush
            );
            if(startResult == S_FALSE || endResult == S_FALSE) return false;

            if(FAILED(startResult) || FAILED(endResult) || end < start)
            {
                measurable = false;
                continue;
            }

            const auto elapsedMs = millisecondsPerTick * (end - start);
            switch(range.kind)
            {
            case GpuPass::Compute:
                timing.computeMs += elapsedMs;
                break;
            case GpuPass::Render:
                timing.renderMs += elapsedMs;
                break;
            // default:
            //     std::unreachable();
            }
        }
        if(measurable)
        {
            outLatest = timing;
        }

        return true;
    }

    GpuTimer::GpuTimer(ID3D11Device& device, ID3D11DeviceContext& context)
        : context(context), timestamps(device, TimestampQueryDesc), frames(createFrames(device)) {}

    std::optional<GpuTiming> GpuTimer::Collect()
    {
        assert(!recording);

        GpuTiming latest;
        // The GPU finishes the frames in the order they were recorded.
        while(!frames.Empty())
        {
            auto& frame = frames.Front();
            if(!frame.TryRead(context, latest)) break;

            frame.Reset();

            frames.Pop();
        }

        if(latest.frame == NoFrame) [[unlikely]] return std::nullopt;

        return latest;
    }

    void GpuTimer::BeginFrame(std::uint64_t number)
    {
        assert(!recording);
        assert(!frames.Full());

        if(!enabled) return;

        auto& frame = frames.Push();
        assert(frame.ranges.empty());
        frame.number = number;

        context.Begin(frame.disjoint.Get());
        recording = true;
    }

    void GpuTimer::EndFrame()
    {
        assert(recording);
        assert(!computeOpen && !renderOpen);

        auto& frame = frames.Back();

        context.End(frame.disjoint.Get());
        recording = false;
    }

    void GpuTimer::BeginCompute()
    {
        assert(recording);
        assert(!computeOpen && !renderOpen);

        auto& frame = frames.Back();
        auto& range = frame.NewRange(timestamps, GpuPass::Compute);

        context.End(range.start);
    #ifndef NDEBUG
        computeOpen = true;
    #endif
    }

    void GpuTimer::EndCompute()
    {
        assert(recording);
        assert(computeOpen && !renderOpen);

        auto& frame = frames.Back();
        auto& range = frame.BackRange();

        context.End(range.end);
    #ifndef NDEBUG
        computeOpen = false;
    #endif
    }

    void GpuTimer::BeginRender()
    {
        assert(recording);
        assert(!computeOpen && !renderOpen);

        auto& frame = frames.Back();
        auto& range = frame.NewRange(timestamps, GpuPass::Render);

        context.End(range.start);
    #ifndef NDEBUG
        renderOpen = true;
    #endif
    }

    void GpuTimer::EndRender()
    {
        assert(recording);
        assert(!computeOpen && renderOpen);

        auto& frame = frames.Back();
        auto& range = frame.BackRange();

        context.End(range.end);
    #ifndef NDEBUG
        renderOpen = false;
    #endif
    }

    std::array<GpuTimer::Frame, GpuTimer::RingSize> GpuTimer::createFrames(
        ID3D11Device& device)
    {
        return {
            Frame(device),
            Frame(device),
            Frame(device),
            Frame(device)
        };
    }
}
