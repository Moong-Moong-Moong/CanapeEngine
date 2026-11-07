// rttr declares the element wrapper of a sequence container it never defines.
#pragma warning(push)
#pragma warning(disable : 4506)

#include "Puzzle/PuzzleLane.h"

#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::enumeration<PuzzleKind>("PuzzleKind")(
        rttr::value("KickerJump", PuzzleKind::KickerJump),
        rttr::value("TiltTray", PuzzleKind::TiltTray),
        rttr::value("CarriedScoop", PuzzleKind::CarriedScoop));

    rttr::registration::class_<PuzzleLane>("PuzzleLane")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("kind", &PuzzleLane::kind)
        .property("payload", &PuzzleLane::payload)
        .property("goalCenter", &PuzzleLane::goalCenter)
        .property("goalHalfExtent", &PuzzleLane::goalHalfExtent)
        .property("goalPieces", &PuzzleLane::goalPieces)
        .property("goalIdleMaterial", &PuzzleLane::goalIdleMaterial)
        .property("goalSolvedMaterial", &PuzzleLane::goalSolvedMaterial);
}

void PuzzleLane::reset(PuzzleMode startMode)
{
    mode = startMode;
    tick = 0;
    solved = false;
    solvedAt = 0;
    failed = false;
    hasVerdict = false;
    showGoal(false);
}

void PuzzleLane::solve()
{
    solved = true;
    solvedAt = tick;
    showGoal(true);
}

void PuzzleLane::showGoal(bool shown)
{
    if(shown == goalShownSolved) return;

    const std::string& name = shown ? goalSolvedMaterial : goalIdleMaterial;
    bool applied = true;
    for(const auto& handle : goalPieces)
    {
        const auto* piece = GetGameObject(handle);
        auto* renderer = piece ?
            piece->GetComponent<Canape::MeshRendererComponent>() :
            nullptr;
        if(renderer && !name.empty() && !renderer->SetMaterial(0, name))
        {
            if(applied)
            {
                CN_LOG_GAME("puzzle", "goal material '", name, "' could not be set; the goal keeps its old look");
            }
            applied = false;
        }
    }

    if(applied)
    {
        goalShownSolved = shown;
    }
}

#pragma warning(pop)
