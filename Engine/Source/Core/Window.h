#pragma once

#include <cstdint>
#include <functional>
#include <string>

typedef struct tagMSG MSG;

namespace Canape
{
    struct WindowDesc
    {
        std::wstring Title = L"Canape";
        uint32_t Width = 1280;
        uint32_t Height = 720;
    };

    class Window
    {
    public:
        explicit Window(const WindowDesc& desc);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        bool PumpMessages();
        void SetMessageObserver(std::function<void(const MSG&)> observer) { m_MessageObserver = std::move(observer); }

        bool ShouldClose() const { return m_ShouldClose; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        void* GetNativeHandle() const { return m_Handle; }

    private:
        friend struct WindowProc;

        void* m_Handle = nullptr;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_ShouldClose = false;
        std::function<void(const MSG&)> m_MessageObserver;
    };
}
