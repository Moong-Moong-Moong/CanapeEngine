#include "Scripts/GarbageCollector.h"
#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<GarbageCollector>("GarbageCollector")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("key", &GarbageCollector::m_Key);
}

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
