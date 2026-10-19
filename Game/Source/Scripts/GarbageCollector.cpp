#include "Scripts/GarbageCollector.h"
#include "Debug/DebugLog.h"

void GarbageCollector::Update()
{
    Canape::Application& application = Canape::Application::Get();
    if (!application.GetInputManager().IsKeyPressed(m_Key))
    {
        return;
    }

    const size_t released = application.GetAssetManager().ReleaseUnusedAssets();
    CN_LOG_GAME("GC", "미사용 에셋 해제: ", released, "개");
}
