// rttr declares the element wrapper of a sequence container it never defines.
#pragma warning(push)
#pragma warning(disable : 4506)

#include "Puzzle/PuzzleDirector.h"

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
        .property("startMode", &PuzzleDirector::startMode)
        .property("lanes", &PuzzleDirector::lanes);
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

    void collectTracks(const Canape::TransformComponent& node, std::vector<PoseTrack*>& tracks)
    {
        if(auto* track = node.GetOwnerObject()->GetComponent<PoseTrack>()) tracks.push_back(track);

        for(const auto* child : node.GetChildren())
        {
            collectTracks(*child, tracks);
        }
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
        if(!lane.HasContent()) continue;

        ++contentLanes;
        latchGoal(lane);
        if(lane.GetTick() < PuzzleHorizonTicks)
        {
            driveTracks(entry);
            lane.advanceTick();
            continue;
        }
        logVerdict(lane);
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

PuzzleDirector::LaneEntries PuzzleDirector::gatherLanes() const
{
    LaneEntries entries;
    for(const auto& handle : lanes)
    {
        auto* root = GetGameObject(handle);
        auto* lane = root ? root->GetComponent<PuzzleLane>() : nullptr;
        if(lane == nullptr) continue;

        entries.push_back({ lane, {} });
        collectTracks(*root->GetTransform(), entries.back().tracks);
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

// Latches the goal once, on the previous tick's post-step state, when the payload overlaps the goal zone.
// A lane's verdict is final: nothing is judged after it.
void PuzzleDirector::latchGoal(PuzzleLane& lane) const
{
    if(lane.HasVerdict() || lane.IsSolved() || lane.GetTick() == 0) return;

    const auto* payload = GetGameObject(lane.GetPayload());
    if(payload == nullptr) return;

    const auto* laneRoot = lane.GetOwnerObject();
    const auto center = laneRoot->GetTransform()->GetLocalToWorldMatrix().TransformPoint(lane.GetGoalCenter());
    for(const auto* body : laneRoot->GetScene()->GetPhysics().QueryBox(center, lane.GetGoalHalfExtent()))
    {
        if(body->GetOwnerObject() == payload)
        {
            lane.solve();
            return;
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

void PuzzleDirector::logVerdict(PuzzleLane& lane) const
{
    if(lane.HasVerdict()) return;

    lane.judge();
#if defined(_DEBUG)
    const char* health = lane.HasFailed() ? ", failed" : ", ok";
    if(lane.IsSolved())
    {
        CN_LOG_GAME("puzzle", enumName(lane.GetKind()), " ", enumName(lane.GetMode()), ": solved at ", lane.GetSolvedAt(), health);
    }
    else
    {
        CN_LOG_GAME("puzzle", enumName(lane.GetKind()), " ", enumName(lane.GetMode()), ": unsolved", health);
    }
#endif
}

void PuzzleDirector::logLaneStates() const
{
#if defined(_DEBUG)
    for(const LaneEntry& entry : gatherLanes())
    {
        const PuzzleLane& lane = *entry.lane;
        const std::string solvedAt = lane.IsSolved() ? std::to_string(lane.GetSolvedAt()) : "-";
        CN_LOG_GAME("puzzle", enumName(lane.GetKind()), " ", enumName(lane.GetMode()), " tick ", lane.GetTick(),
            " solvedAt ", solvedAt, lane.HasFailed() ? ", failed" : ", ok");
    }
#endif
}

#pragma warning(pop)
