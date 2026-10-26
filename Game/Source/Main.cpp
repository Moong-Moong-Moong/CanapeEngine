#include "GameApp.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <exception>
#include "../../Engine/Source/CNDebug/DebugLog.h"


int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
#if defined(_DEBUG)
    Canape::Debug::Init();
#endif

    try
    {
        GameApp app;
        app.Run();
    }
    catch (const std::exception& e)
    {
        CN_LOG_GAME("Fatal", "치명적 오류: ", e.what());
        MessageBoxA(nullptr, e.what(), "Canape", MB_OK | MB_ICONERROR);
        return -1;
    }

#if defined(_DEBUG)
    Canape::Debug::Shutdown();
#endif
    return 0;
}
