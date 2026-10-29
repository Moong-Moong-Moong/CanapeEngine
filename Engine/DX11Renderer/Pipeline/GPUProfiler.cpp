#include "DX11Renderer/Pipeline/GPUProfiler.h"

#if CANAPE_RENDER_DEBUG

#include <utility>

namespace Canape
{
    bool GPUProfiler::CreateTimestampPair(TimestampPair& pair)
    {
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_TIMESTAMP;
        return SUCCEEDED(m_device->CreateQuery(&desc, pair.start.GetAddressOf()))
            && SUCCEEDED(m_device->CreateQuery(&desc, pair.end.GetAddressOf()));
    }

    bool GPUProfiler::Initialize(ID3D11Device* device, std::string& error)
    {
        m_device = device;
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        for (FrameSlot& slot : m_slots)
        {
            if (FAILED(device->CreateQuery(&desc, slot.disjoint.GetAddressOf()))
                || !CreateTimestampPair(slot.frame)
                || !CreateTimestampPair(slot.finalOutput))
            {
                error = "GPUProfiler could not create timestamp queries";
                return false;
            }
        }
        return true;
    }

    void GPUProfiler::Publish(const GpuTimingResult& result)
    {
        if (result.frameId > m_latest.frameId) m_latest = result;
    }

    void GPUProfiler::Poll(ID3D11DeviceContext* context)
    {
        for (FrameSlot& slot : m_slots)
        {
            if (slot.state != SlotState::Pending) continue;

            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            const HRESULT disjointResult = context->GetData(slot.disjoint.Get(), &disjoint,
                sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (disjointResult == S_FALSE) continue;

            GpuTimingResult result{};
            result.frameId = slot.frameId;
            if (FAILED(disjointResult))
            {
                Publish(result);
                slot.state = SlotState::Failed;
                continue;
            }
            if (!slot.valid || disjoint.Disjoint || disjoint.Frequency == 0)
            {
                Publish(result);
                slot.state = SlotState::Free;
                continue;
            }

            bool ready = true;
            bool valid = true;
            bool queryFailed = false;
            auto readTick = [&](ID3D11Query* query, UINT64& tick)
            {
                const HRESULT status = context->GetData(query, &tick, sizeof(tick),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (status == S_FALSE) ready = false;
                else if (FAILED(status)) { valid = false; queryFailed = true; }
            };
            UINT64 frameStart = 0, frameEnd = 0, finalStart = 0, finalEnd = 0;
            readTick(slot.frame.start.Get(), frameStart);
            readTick(slot.frame.end.Get(), frameEnd);
            readTick(slot.finalOutput.start.Get(), finalStart);
            readTick(slot.finalOutput.end.Get(), finalEnd);

            double drawTicks = 0.0;
            for (size_t i = 0; i < slot.drawCount; ++i)
            {
                UINT64 start = 0, end = 0;
                readTick(slot.drawScenes[i].start.Get(), start);
                readTick(slot.drawScenes[i].end.Get(), end);
                if (ready && end < start) valid = false;
                else drawTicks += static_cast<double>(end - start);
            }
            if (!ready && valid) continue;
            if (frameEnd < frameStart || finalEnd < finalStart) valid = false;
            if (valid)
            {
                const double msPerTick = 1000.0 / static_cast<double>(disjoint.Frequency);
                result.valid = true;
                result.frameMs = static_cast<double>(frameEnd - frameStart) * msPerTick;
                result.drawSceneMs = drawTicks * msPerTick;
                result.finalOutputMs = static_cast<double>(finalEnd - finalStart) * msPerTick;
            }
            Publish(result);
            slot.state = queryFailed ? SlotState::Failed : SlotState::Free;
        }
    }

    bool GPUProfiler::BeginFrame(ID3D11DeviceContext* context, uint64_t frameId)
    {
        if (m_activeSlot >= 0 || !m_device) return false;
        for (size_t i = 0; i < RingSize; ++i)
        {
            const size_t index = (m_nextSlot + i) % RingSize;
            FrameSlot& slot = m_slots[index];
            if (slot.state != SlotState::Free) continue;

            slot.state = SlotState::Recording;
            slot.frameId = frameId;
            slot.valid = true;
            slot.drawOpen = false;
            slot.finalOpen = false;
            slot.finalComplete = false;
            slot.drawCount = 0;
            m_activeSlot = static_cast<int>(index);
            m_nextSlot = (index + 1) % RingSize;
            context->Begin(slot.disjoint.Get());
            context->End(slot.frame.start.Get());
            return true;
        }
        // All slots still await the GPU. Skip this frame rather than overwriting a query.
        return false;
    }

    void GPUProfiler::BeginDrawScene(ID3D11DeviceContext* context)
    {
        if (m_activeSlot < 0) return;
        FrameSlot& slot = m_slots[m_activeSlot];
        if (!slot.valid || slot.drawOpen) return;
        if (slot.drawCount == slot.drawScenes.size())
        {
            TimestampPair pair;
            if (!CreateTimestampPair(pair))
            {
                slot.valid = false;
                return;
            }
            slot.drawScenes.push_back(std::move(pair));
        }
        context->End(slot.drawScenes[slot.drawCount].start.Get());
        slot.drawOpen = true;
    }

    void GPUProfiler::EndDrawScene(ID3D11DeviceContext* context)
    {
        if (m_activeSlot < 0) return;
        FrameSlot& slot = m_slots[m_activeSlot];
        if (!slot.drawOpen) return;
        context->End(slot.drawScenes[slot.drawCount].end.Get());
        ++slot.drawCount;
        slot.drawOpen = false;
    }

    void GPUProfiler::BeginFinalOutput(ID3D11DeviceContext* context)
    {
        if (m_activeSlot < 0) return;
        FrameSlot& slot = m_slots[m_activeSlot];
        context->End(slot.finalOutput.start.Get());
        slot.finalOpen = true;
    }

    void GPUProfiler::EndFinalOutput(ID3D11DeviceContext* context)
    {
        if (m_activeSlot < 0) return;
        FrameSlot& slot = m_slots[m_activeSlot];
        if (!slot.finalOpen) return;
        context->End(slot.finalOutput.end.Get());
        slot.finalOpen = false;
        slot.finalComplete = true;
    }

    void GPUProfiler::EndFrame(ID3D11DeviceContext* context)
    {
        if (m_activeSlot < 0) return;
        FrameSlot& slot = m_slots[m_activeSlot];
        slot.valid = slot.valid && !slot.drawOpen && slot.finalComplete;
        context->End(slot.frame.end.Get());
        context->End(slot.disjoint.Get());
        slot.state = SlotState::Pending;
        m_activeSlot = -1;
    }
}

#endif
