#include "Puzzle/PuzzleDirector.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>

#include <process.h>

#include "CNDebug/DebugLog.h"
#include "Scripts/FlyCamera.h"

RTTR_REGISTRATION
{
    rttr::registration::enumeration<PuzzleMode>("PuzzleMode")(
        rttr::value("Solution", PuzzleMode::Solution),
        rttr::value("Control", PuzzleMode::Control));

    rttr::registration::class_<PuzzleDirector>("PuzzleDirector")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("startMode", &PuzzleDirector::startMode);
}

namespace
{
    constexpr char PlaygroundSceneName[] = "PhysicsPlayground";

    template<typename T>
    T* findComponent(Canape::Scene& scene)
    {
        for(auto* object : scene.GetAllGameObjects())
        {
            if(auto* component = object->GetComponent<T>()) return component;
        }
        return nullptr;
    }

    bool physicsFailed(const Canape::PhysicsScene& physics)
    {
        return physics.GetAuthoringIssueCount() > 0 || Canape::StepFailed(physics.GetLastStepError());
    }
}

PuzzleDirector::~PuzzleDirector()
{
    if(snapshotPath.empty() || snapshotHandedOver) return;

    std::error_code error;
    std::filesystem::remove(snapshotPath, error);
}

void PuzzleDirector::Start()
{
    if(snapshotPath.empty())
    {
        saveSnapshot();
    }
    for(const LaneEntry& entry : gatherLanes())
    {
        entry.lane->reset(startMode);
    }
}

void PuzzleDirector::FixedUpdate()
{
    if(restarted) return;

    const bool failed = physicsFailed(GetOwnerObject()->GetScene()->GetPhysics());
    std::size_t contentLanes = 0;
    std::size_t restingLanes = 0;
    for(const LaneEntry& entry : gatherLanes())
    {
        PuzzleLane& lane = *entry.lane;
        if(!Canape::Scene::IsRunnable(&lane)) continue;

        if(failed && !lane.HasVerdict())
        {
            lane.fail();
        }
        if(entry.goals.empty()) continue;

        ++contentLanes;
        if(lane.GetTick() < PuzzleHorizonTicks)
        {
            releaseHinges(entry);
            driveTracks(entry);
            lane.advanceTick();
            continue;
        }
        judge(entry);
        ++restingLanes;
    }

    if(contentLanes > 0 && restingLanes == contentLanes && ++restTicks >= PuzzleRestTicks)
    {
        restart(startMode == PuzzleMode::Solution ? PuzzleMode::Control : PuzzleMode::Solution);
    }
}

void PuzzleDirector::Update()
{
    using enum Canape::Key;

    if(restarted) return;

    if(carriedCamera)
    {
        carryCamera();
    }

    const auto& input = Canape::Application::Get().GetInputManager();
    if(input.IsKeyPressed(R))
    {
        restart(PuzzleMode::Solution);
    }
    else if(input.IsKeyPressed(C))
    {
        restart(PuzzleMode::Control);
    }
    // TODO: [Editor] lane state belongs in a UI panel
    if(input.IsKeyPressed(L))
    {
        logLaneStates();
    }
}

// Every object of the scene with a PuzzleLane, with the tracks, the hinge releases and the runnable goals below it.
PuzzleDirector::LaneEntries PuzzleDirector::gatherLanes() const
{
    LaneEntries entries;
    for(auto* root : GetOwnerObject()->GetScene()->GetAllGameObjects())
    {
        auto* lane = root->IsPendingDestroy() ? nullptr : root->GetComponent<PuzzleLane>();
        if(lane == nullptr) continue;

        LaneEntry& entry = entries.emplace_back(lane);
        collectBelow(*root->GetTransform(), entry.tracks);
        collectBelow(*root->GetTransform(), entry.releases);
        collectBelow(*root->GetTransform(), entry.goals);
        std::erase_if(
            entry.goals,
            [](const PuzzleGoal* goal)
            {
                return !Canape::Scene::IsRunnable(goal);
            }
        );
    }
    return entries;
}

// The scene as it is now, saved once per run so that a restart keeps unsaved edits and untitled scenes.
void PuzzleDirector::saveSnapshot()
{
    static std::atomic<unsigned> counter{ 0 };

    Canape::Scene& scene = *GetOwnerObject()->GetScene();
    std::error_code error;
    const auto path = std::filesystem::temp_directory_path(error)
        / ("CanapePuzzle_" + std::to_string(_getpid()) + "_" + std::to_string(counter++) + "_" + scene.GetName() + ".json");
    if(!error && Canape::Application::Get().GetSceneManager().SaveSceneToFile(scene, path.string()))
    {
        snapshotPath = path.string();
    }
    else
    {
        CN_LOG_GAME("puzzle", "cannot save the start snapshot; restarts reload ", PlaygroundSceneName);
    }
}

void PuzzleDirector::restart(PuzzleMode mode)
{
    auto& sceneManager = Canape::Application::Get().GetSceneManager();
    Canape::Scene* current = GetOwnerObject()->GetScene();
    Canape::Scene* next = snapshotPath.empty() ? nullptr : sceneManager.LoadSceneFromFile(snapshotPath);
    const bool fromSnapshot = next != nullptr;
    if(next == nullptr)
    {
        next = sceneManager.OpenScene(PlaygroundSceneName);
    }
    if(next == nullptr)
    {
        CN_LOG_GAME("puzzle", "restart failed: cannot open the start snapshot or ", PlaygroundSceneName);
        restTicks = 0;
        return;
    }

    if(auto* director = findComponent<PuzzleDirector>(*next))
    {
        director->SetStartMode(mode);
        if(fromSnapshot)
        {
            director->SetSnapshotPath(snapshotPath);
        }
        if(const auto* camera = findComponent<FlyCamera>(*current))
        {
            const auto& transform = *camera->GetTransform();
            director->carriedCamera = Canape::PhysicsPose{ transform.GetLocalPosition(), transform.GetLocalRotation() };
        }
    }
    snapshotHandedOver = fromSnapshot;
    sceneManager.UnloadScene(current);
    restarted = true;
}

// The new camera's Start returns it home, so the carried pose is applied only once the camera has started.
void PuzzleDirector::carryCamera()
{
    auto* camera = findComponent<FlyCamera>(*GetOwnerObject()->GetScene());
    if(camera == nullptr)
    {
        carriedCamera.reset();
        return;
    }
    if(!camera->GetStarted()) return;

    auto& transform = *camera->GetTransform();
    transform.SetLocalPosition(carriedCamera->position);
    transform.SetLocalRotation(carriedCamera->rotation);
    carriedCamera.reset();
}

// Every release due at this lane tick, before the tracks and the step. It fires only while the hinge holds; a hinge
// that holds nothing at that tick is logged and the release stays unapplied.
void PuzzleDirector::releaseHinges(const LaneEntry& entry) const
{
    const PuzzleLane& lane = *entry.lane;
    for(HingeRelease* release : entry.releases)
    {
        const bool pending = Canape::Scene::IsRunnable(release) && release->GetStarted() && !release->IsApplied();
        if(!pending) continue;

        const int tick = release->GetTick(lane.GetMode());
        if(tick < 0 || lane.GetTick() != static_cast<std::uint32_t>(tick)) continue;

        auto* hinge = release->GetComponent<Canape::HingeComponent>();
        if(hinge && hinge->IsHeld())
        {
            release->fire(*hinge);
        }
        else
        {
            CN_LOG_GAME("puzzle", "'", release->GetOwnerObject()->GetName(), "': no held hinge to let go at tick ", tick);
        }
    }
}

// Every track every tick, toward where its key puts the piece after this step.
void PuzzleDirector::driveTracks(const LaneEntry& entry) const
{
    const PuzzleLane& lane = *entry.lane;
    for(const PoseTrack* track : entry.tracks)
    {
        auto* body = track->GetComponent<Canape::RigidBodyComponent>();
        const bool driven = Canape::Scene::IsRunnable(track) && track->GetStarted() && body && track->HasKeys(lane.GetMode());
        if(!driven) continue;

        body->SetKinematicTarget(track->WorldPoseAt(lane.GetMode(), lane.GetTick() + 1));
    }
}

// Once, at the horizon: the lane is solved when every goal below it was met during the run.
void PuzzleDirector::judge(const LaneEntry& entry) const
{
    PuzzleLane& lane = *entry.lane;
    if(lane.HasVerdict()) return;

    lane.judge(std::ranges::all_of(
        entry.goals,
        [](const PuzzleGoal* goal)
        {
            return goal->IsSatisfied();
        }
    ));
#if defined(_DEBUG)
    CN_LOG_GAME("puzzle", lane.GetOwnerObject()->GetName(), " ", enumName(lane.GetMode()), ": ",
        lane.IsSolved() ? "solved" : "unsolved", lane.HasFailed() ? ", failed" : ", ok");
#endif
}

void PuzzleDirector::logLaneStates() const
{
#if defined(_DEBUG)
    for(const LaneEntry& entry : gatherLanes())
    {
        const PuzzleLane& lane = *entry.lane;
        const char* verdict = lane.HasVerdict() ? (lane.IsSolved() ? "solved" : "unsolved") : "pending";
        CN_LOG_GAME("puzzle", lane.GetOwnerObject()->GetName(), " ", enumName(lane.GetMode()), " tick ", lane.GetTick(),
            " ", verdict, lane.HasFailed() ? ", failed" : ", ok");
    }
#endif
}
