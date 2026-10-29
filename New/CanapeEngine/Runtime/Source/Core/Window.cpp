#include "Core/Window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <stdexcept>

namespace Canape
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"CanapeWindowClass";
    }

    struct WindowProc
    {
        static LRESULT CALLBACK Dispatch(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (msg == WM_NCCREATE)
            {
                const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            }

            auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (window)
            {
                switch (msg)
                {
                case WM_SIZE:
                    window->m_Width = LOWORD(lParam);
                    window->m_Height = HIWORD(lParam);
                    return 0;
                case WM_CLOSE:
                    window->m_ShouldClose = true;
                    return 0;
                }
            }

            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    };

    Window::Window(const WindowDesc& desc)
        : m_Width(desc.Width)
        , m_Height(desc.Height)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &WindowProc::Dispatch;
        windowClass.hInstance = instance;
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kWindowClassName;

        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            throw std::runtime_error("Failed to register window class");
        }

        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rect{ 0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };
        AdjustWindowRect(&rect, style, FALSE);

        const int windowWidth = rect.right - rect.left;
        const int windowHeight = rect.bottom - rect.top;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

        HWND hwnd = CreateWindowExW(
            0, kWindowClassName, desc.Title.c_str(), style,
            x, y, windowWidth, windowHeight,
            nullptr, nullptr, instance, this);

        if (!hwnd)
        {
            throw std::runtime_error("Failed to create window");
        }

        m_Handle = hwnd;
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    }

    Window::~Window()
    {
        if (m_Handle)
        {
            HWND hwnd = static_cast<HWND>(m_Handle);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            DestroyWindow(hwnd);
            m_Handle = nullptr;
        }
    }

    bool Window::PumpMessages()
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_ShouldClose = true;
            }

            if (m_MessageObserver)
            {
                m_MessageObserver(msg);
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return !m_ShouldClose;
    }
}
