#include "EditorApp.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <exception>
#include "CNDebug/DebugLog.h"

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    try
    {
        EditorApp app;
        app.Run();
    }
    catch (const std::exception& e)
    {
        CN_LOG_GAME("Fatal", "Editor error: ", e.what());
        MessageBoxA(nullptr, e.what(), "Canape Editor", MB_OK | MB_ICONERROR);
        return -1;
    }
    return 0;
}
