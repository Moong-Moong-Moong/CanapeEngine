// Integration check of the physics playground: the real JSON scene, opened through the SceneManager and played by the
// real Application loop, headless: the window is hidden and the loop does not render. The checks run on a fiber that gives
// control back to the loop once per frame, so every frame is a real frame of Application::Run; time scale decides how many
// fixed ticks a frame runs. Lane results are read from the game components' public state; the program changes nothing in
// the game.
#include "Puzzle/GoalCup.h"
#include "Puzzle/GoalZone.h"
#include "Puzzle/HingeRelease.h"
#include "Puzzle/PoseTrack.h"
#include "Puzzle/PuzzleDirector.h"
#include "Puzzle/PuzzleLane.h"
#include "Scripts/FlyCamera.h"
#include "Scripts/PhysicsStatsLog.h"

#include <Engine.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <crtdbg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Every frame runs four fixed ticks (the engine's per-frame cap), however fast a headless frame is.
    constexpr float FourTickScale = 100000.0f;
    // Mean fixed ticks per frame of the mixed-batch runs; the time scale follows the measured frame time to reach it.
    constexpr double SmallBatchTicks = 1.5;
    constexpr double LargeBatchTicks = 2.5;
    constexpr std::uint32_t TicksPerCappedFrame = 4;
    constexpr std::chrono::seconds RunLimit{ 180 };
    constexpr char SceneName[] = "PhysicsPlayground";
    constexpr char RoundTripFile[] = "roundtrip.json";
    constexpr float Pi = 3.14159265358979f;

    int g_Failures = 0;
    int g_Checks = 0;

    void Check(bool condition, const std::string& what)
    {
        ++g_Checks;
        if(!condition)
        {
            ++g_Failures;
            std::printf("FAIL %s\n", what.c_str());
        }
    }

    struct Verdict {
        std::string name;
        bool solved = false;
        bool failed = false;
    };

    // The Application whose per-frame hook hands control to a body running on a fiber.
    class ValidationApp final : public Canape::Application {
    public:
        static constexpr const char* WindowClosedMessage = "the validation window closed before the checks finished";

        using Canape::Application::Application;

        void Execute(std::function<void()> body)
        {
            m_Body = std::move(body);
            m_MainFiber = ConvertThreadToFiber(nullptr);
            m_BodyFiber = CreateFiber(8 * 1024 * 1024, &ValidationApp::BodyEntry, this);
            if(!m_MainFiber || !m_BodyFiber)
            {
                throw std::runtime_error("cannot create the validation fiber");
            }
            RegisterKeyCommand();
            Run();
            if(!m_Finished)
            {
                // Unwinds the body so that the temp files of the running check are removed.
                m_Aborting = true;
                if(m_BodyStarted)
                {
                    SwitchToFiber(m_BodyFiber);
                }
            }
            DeleteFiber(m_BodyFiber);
            ConvertFiberToThread();
            if(m_Aborting && m_Error.empty())
            {
                m_Error = WindowClosedMessage;
            }
            if(!m_Error.empty())
            {
                throw std::runtime_error(m_Error);
            }
        }

        // Called from the body: lets the loop run one frame, returns at the start of the next one.
        void YieldFrame()
        {
            SwitchToFiber(m_MainFiber);
            if(m_Aborting)
            {
                throw std::runtime_error(WindowClosedMessage);
            }
        }

        // The keys go down after the loop's own input update, which drops every key while the window has no focus.
        void PressKeys(const std::vector<Canape::Key>& keys)
        {
            std::string line = "validate.keys";
            for(Canape::Key key : keys)
            {
                line += " " + std::to_string(static_cast<int>(key));
            }
            GetCommands().Submit(line);
        }

        void ReleaseKeys(const std::vector<Canape::Key>& keys)
        {
            for(Canape::Key key : keys)
            {
                SendKey(key, false);
            }
        }

    protected:
        void OnUpdate(float) override
        {
            if(!m_Finished)
            {
                SwitchToFiber(m_BodyFiber);
            }
        }

    private:
        void SendKey(Canape::Key key, bool down)
        {
            MSG message{};
            message.message = down ? WM_KEYDOWN : WM_KEYUP;
            message.wParam = static_cast<WPARAM>(key);
            GetInputManager().ObserveInput(message);
        }

        void RegisterKeyCommand()
        {
            GetCommands().Register("validate.keys", "validate.keys <key>...", "Presses the given keys for one frame.", 1,
                [this](const Canape::CommandArgs& args)
                {
                    for(std::size_t i = 0; args.Has(i); ++i)
                    {
                        SendKey(static_cast<Canape::Key>(args.GetInt(i, 0)), true);
                    }
                });
        }

        static void CALLBACK BodyEntry(void* self) { static_cast<ValidationApp*>(self)->RunBody(); }

        void RunBody()
        {
            m_BodyStarted = true;
            try
            {
                m_Body();
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
            }
            m_Finished = true;
            Quit();
            SwitchToFiber(m_MainFiber);
        }

        std::function<void()> m_Body;
        void* m_MainFiber = nullptr;
        void* m_BodyFiber = nullptr;
        std::string m_Error;
        bool m_Finished = false;
        bool m_Aborting = false;
        bool m_BodyStarted = false;
    };

    struct Harness {
        ValidationApp& app;
        Canape::SceneManager& scenes;
        // The most scenes seen after any frame since the last check.
        std::size_t mostScenes = 0;
        // Frames by the number of fixed ticks they run, since the last scale change.
        std::array<std::size_t, TicksPerCappedFrame + 1> batches{};
        // Mean ticks per frame the time scale aims at while mixed batches are on; 0 keeps the fixed scale.
        double mixedTicks = 0.0;
        float frameSeconds = 0.0f;
        bool measuring = false;

        Canape::Scene& Active() const { return *scenes.GetActiveScene(); }

        void SetScale(float scale)
        {
            mixedTicks = 0.0;
            Canape::Time::SetTimeScale(scale);
            batches = {};
        }

        // Frames run a varying number of ticks: the scale follows a running mean of the real frame time.
        void SetMixedBatches(double ticksPerFrame)
        {
            mixedTicks = ticksPerFrame;
            measuring = false;
            batches = {};
        }

        std::size_t DistinctSmallBatches() const
        {
            return static_cast<std::size_t>(std::count_if(batches.begin() + 1, batches.end() - 1, [](std::size_t n) { return n > 0; }));
        }

        void CheckFourTickBatches(const char* what) const
        {
            Check(batches[TicksPerCappedFrame] > 0, std::string(what) + ": frames of four ticks ran");
        }

        void CheckMixedBatches(const char* what) const
        {
            Check(DistinctSmallBatches() >= 2, std::string(what) + ": frames of different tick counts between one and three ran (zero "
                + std::to_string(batches[0]) + ", one " + std::to_string(batches[1]) + ", two " + std::to_string(batches[2])
                + ", three " + std::to_string(batches[3]) + ", four " + std::to_string(batches[4]) + ")");
        }

        void Frame(const std::vector<Canape::Key>& keys = {})
        {
            if(!keys.empty())
            {
                app.PressKeys(keys);
            }
            app.YieldFrame();
            ++batches[std::min(Canape::Time::FixedStepCount(), TicksPerCappedFrame)];
            FollowFrameTime();
            mostScenes = std::max(mostScenes, scenes.GetSceneCount());
            app.ReleaseKeys(keys);
        }

        // The frame after a scene load carries the load time, so a sample is held to twice the running mean.
        void FollowFrameTime()
        {
            if(mixedTicks <= 0.0)
            {
                return;
            }
            const float seconds = Canape::Time::UnscaledDeltaTime();
            if(!measuring)
            {
                measuring = true;
                frameSeconds = seconds;
            }
            else
            {
                constexpr float Smoothing = 0.25f;
                frameSeconds += Smoothing * (std::min(seconds, 2.0f * frameSeconds) - frameSeconds);
            }
            if(frameSeconds > 0.0f)
            {
                Canape::Time::SetTimeScale(static_cast<float>(mixedTicks) * Canape::Time::FixedDeltaTime() / frameSeconds);
            }
        }

        bool RunUntil(const std::function<bool()>& done)
        {
            const auto deadline = std::chrono::steady_clock::now() + RunLimit;
            while(std::chrono::steady_clock::now() < deadline)
            {
                if(done())
                {
                    return true;
                }
                Frame();
            }
            return done();
        }

        // The lanes of the active scene, in the order the director runs them.
        std::vector<PuzzleLane*> Lanes() const
        {
            std::vector<PuzzleLane*> lanes;
            Canape::Scene& scene = Active();
            for(const char* name : { "KickerJump", "TiltTray", "CarriedScoop", "SwingCut", "LeverCatapult", "BuoyPop" })
            {
                Canape::GameObject* root = scene.GetGameObject(scene.FindGameObjectByName(name));
                auto* lane = root ? root->GetComponent<PuzzleLane>() : nullptr;
                if(lane)
                {
                    lanes.push_back(lane);
                }
            }
            return lanes;
        }

        PuzzleLane* Front() const
        {
            const std::vector<PuzzleLane*> lanes = Lanes();
            return lanes.empty() ? nullptr : lanes.front();
        }

        static const std::string& NameOf(const PuzzleLane& lane) { return lane.GetOwnerObject()->GetName(); }

        static std::vector<PuzzleGoal*> Goals(const PuzzleLane& lane)
        {
            std::vector<PuzzleGoal*> goals;
            collectBelow(*lane.GetOwnerObject()->GetTransform(), goals);
            return goals;
        }

        static PuzzleGoal* Goal(const PuzzleLane& lane)
        {
            const std::vector<PuzzleGoal*> goals = Goals(lane);
            return goals.empty() ? nullptr : goals.front();
        }

        // The object the lane's first goal waits for.
        Canape::GameObject* Payload(const PuzzleLane& lane) const
        {
            const PuzzleGoal* goal = Goal(lane);
            return goal ? Active().GetGameObject(goal->GetPayload()) : nullptr;
        }

        bool AllJudged() const
        {
            const std::vector<PuzzleLane*> lanes = Lanes();
            return !lanes.empty() && std::all_of(lanes.begin(), lanes.end(), [](const PuzzleLane* lane) { return lane->HasVerdict(); });
        }

        bool AllInMode(PuzzleMode mode) const
        {
            const std::vector<PuzzleLane*> lanes = Lanes();
            return !lanes.empty() && std::all_of(lanes.begin(), lanes.end(), [mode](const PuzzleLane* lane) { return lane->GetMode() == mode; });
        }

        bool RunToVerdict(PuzzleMode mode)
        {
            return RunUntil([&] { return AllInMode(mode) && AllJudged(); });
        }

        std::vector<Verdict> Verdicts() const
        {
            std::vector<Verdict> verdicts;
            for(const PuzzleLane* lane : Lanes())
            {
                verdicts.push_back({ NameOf(*lane), lane->IsSolved(), lane->HasFailed() });
            }
            return verdicts;
        }

        PuzzleDirector* Director() const
        {
            for(Canape::GameObject* object : Active().GetAllGameObjects())
            {
                if(auto* director = object->GetComponent<PuzzleDirector>())
                {
                    return director;
                }
            }
            return nullptr;
        }

        FlyCamera* Camera() const
        {
            for(Canape::GameObject* object : Active().GetAllGameObjects())
            {
                if(auto* camera = object->GetComponent<FlyCamera>())
                {
                    return camera;
                }
            }
            return nullptr;
        }

        // A fresh scene from the JSON file replaces the active one; `mode` is its director's start mode.
        // The scene manager applies the change at the end of a frame, so this runs that one frame.
        Canape::Scene* Reload(PuzzleMode mode)
        {
            Canape::Scene* old = scenes.GetActiveScene();
            return Adopt(old, scenes.OpenScene(SceneName), mode);
        }

        Canape::Scene* Adopt(Canape::Scene* old, Canape::Scene* next, PuzzleMode mode)
        {
            if(!next)
            {
                return nullptr;
            }
            for(Canape::GameObject* object : next->GetAllGameObjects())
            {
                if(auto* director = object->GetComponent<PuzzleDirector>())
                {
                    director->SetStartMode(mode);
                }
            }
            if(old)
            {
                for(Canape::GameObject* object : old->GetAllGameObjects())
                {
                    if(auto* director = object->GetComponent<PuzzleDirector>())
                    {
                        director->SetEnabled(false);
                    }
                }
                scenes.UnloadScene(old);
            }
            Frame();
            return next;
        }

        // Steps the physics world of the active scene has taken; the lane tick equals it from the first step on.
        std::uint64_t Steps() const { return Active().GetPhysics().GetStats().stepCount; }
    };

    std::string Tag(const char* run, const std::string& lane)
    {
        return std::string(run) + " " + lane;
    }

    void CheckSolution(const char* run, const std::vector<Verdict>& verdicts)
    {
        Check(!verdicts.empty(), std::string(run) + ": there are lanes with content");
        for(const Verdict& verdict : verdicts)
        {
            Check(verdict.solved, Tag(run, verdict.name) + ": solved at the horizon");
            Check(!verdict.failed, Tag(run, verdict.name) + ": no physics failure");
            std::printf("INFO %s Solution %s\n", verdict.name.c_str(), verdict.solved ? "solved" : "unsolved");
        }
    }

    void CheckControl(const char* run, const std::vector<Verdict>& verdicts)
    {
        Check(!verdicts.empty(), std::string(run) + ": there are lanes with content");
        for(const Verdict& verdict : verdicts)
        {
            Check(!verdict.solved, Tag(run, verdict.name) + ": control stays unsolved");
            Check(!verdict.failed, Tag(run, verdict.name) + ": no physics failure");
        }
    }

    void CheckSame(const char* what, const std::vector<Verdict>& a, const std::vector<Verdict>& b)
    {
        Check(!a.empty() && a.size() == b.size(), std::string(what) + ": same lanes");
        for(std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        {
            Check(a[i].name == b[i].name && a[i].solved == b[i].solved && a[i].failed == b[i].failed,
                Tag(what, a[i].name) + ": same result");
        }
    }

    // Exactly one scene survives every reload, whichever path made it.
    void CheckOneScene(Harness& h, const char* when)
    {
        Check(h.scenes.GetSceneCount() == 1 && h.mostScenes <= 1, std::string(when) + ": one scene is loaded (now " + std::to_string(h.scenes.GetSceneCount())
            + ", most " + std::to_string(h.mostScenes) + ")");
        h.mostScenes = 0;
    }

    void CheckGoalLook(Harness& h, const char* when, bool expectSolved)
    {
        for(const PuzzleLane* lane : h.Lanes())
        {
            const std::vector<PuzzleGoal*> goals = h.Goals(*lane);
            Check(!goals.empty(), Tag(when, h.NameOf(*lane)) + ": the lane has a goal");
            Check(lane->IsSolved() == expectSolved, Tag(when, h.NameOf(*lane)) + ": the lane's verdict is as expected");
            for(const PuzzleGoal* goal : goals)
            {
                Check(goal->IsSatisfied() == expectSolved, Tag(when, h.NameOf(*lane)) + ": the goal was met only when the lane got it");
                if(dynamic_cast<const GoalCup*>(goal))
                {
                    Check(goal->IsShownInside() == expectSolved, Tag(when, h.NameOf(*lane)) + ": the cup shows the payload inside only when the lane got it");
                }
                const std::string& wanted = goal->IsShownInside() ? goal->GetSolvedMaterial() : goal->GetIdleMaterial();
                for(const Canape::GameObject* piece : goal->GetPieces())
                {
                    const auto* renderer = piece->GetComponent<Canape::MeshRendererComponent>();
                    Check(renderer && !wanted.empty() && !renderer->GetMaterialNames().empty() && renderer->GetMaterialNames().front() == wanted, Tag(when, h.NameOf(*lane)) + ": the cup piece shows the right material");
                }
            }
        }
    }

    bool ShowsColor(const Canape::AssetManager& assets, const Canape::MeshRendererComponent& renderer, bool solved)
    {
        const Canape::MaterialAsset* material = assets.GetMaterial(renderer.GetMaterial(0));
        if(!material)
        {
            return false;
        }
        const Canape::Vector4 color = material->GetDesc().baseColor;
        return solved ? color.Y > 0.9f : color.Y < 0.6f;
    }

    void CheckGoalColors(Harness& h, const char* when, bool solved)
    {
        const Canape::AssetManager& assets = h.app.GetAssetManager();
        for(const PuzzleLane* lane : h.Lanes())
        {
            for(const PuzzleGoal* goal : h.Goals(*lane))
            {
                const bool inside = dynamic_cast<const GoalCup*>(goal) ? solved : goal->IsShownInside();
                for(const Canape::GameObject* piece : goal->GetPieces())
                {
                    const auto* renderer = piece->GetComponent<Canape::MeshRendererComponent>();
                    Check(renderer && ShowsColor(assets, *renderer, inside), Tag(when, h.NameOf(*lane)) + ": the goal piece's material resolves to the right color");
                }
            }
        }
    }

    void CheckQueries(Harness& h)
    {
        Canape::PhysicsScene& physics = h.Active().GetPhysics();
        PuzzleLane& lane = *h.Front();
        Canape::GameObject* payload = h.Payload(lane);
        const std::optional<GoalRegion> region = h.Goal(lane) ? h.Goal(lane)->GetRegion() : std::nullopt;
        Check(payload != nullptr && region.has_value(), "the payload object and the goal region exist");
        if(!payload || !region)
        {
            return;
        }

        const Canape::Vector3 tiny{ 0.01f, 0.01f, 0.01f };
        bool foundPayload = false;
        for(const Canape::RigidBodyComponent* body : physics.QueryBox(payload->GetTransform()->GetPosition(), tiny))
        {
            foundPayload = foundPayload || body->GetOwnerObject() == payload;
        }
        Check(foundPayload, "the box query returns the payload");

        const Canape::Vector3 cupFloor = region->center - Canape::Vector3{ 0.0f, region->halfExtent.Y + 0.05f, 0.0f };
        const Canape::RigidBodies atCupFloor = physics.QueryBox(cupFloor, tiny);
        Check(atCupFloor.empty(), "the box query returns nothing where only the static cup floor is");
    }

    bool Near(const Canape::Vector3& a, const Canape::Vector3& b, float tolerance)
    {
        return (a - b).Length() <= tolerance;
    }

    // The regions the shipped cups measure, and that pieces short of a cup measure nothing.
    void CheckCupRegions(Harness& h)
    {
        struct Expected {
            const char* lane;
            Canape::Vector3 min;
            Canape::Vector3 max;
        };
        const Expected expected[] = {
            { "KickerJump", { -4.5f + 0.69f, 0.5f, -0.25f }, { -4.5f + 1.38f, 0.635f, 0.25f } },
            { "TiltTray", { 0.4f, 0.3f, -1.4f }, { 1.0f, 0.375f, -0.8f } },
            { "CarriedScoop", { 4.5f + 0.4f, 0.55f, -0.3f }, { 4.5f + 1.0f, 0.675f, 0.3f } },
            { "SwingCut", { -4.5f + 0.95f, 1.15f, 4.5f - 0.3f }, { -4.5f + 1.4f, 1.3f, 4.5f + 0.3f } },
            { "LeverCatapult", { -0.7f, 1.8f, 4.5f - 0.25f }, { 0.35f, 1.85f, 4.5f + 0.25f } },
        };

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the cup regions");
        Canape::Scene& scene = h.Active();
        for(const Expected& e : expected)
        {
            Canape::GameObject* root = scene.GetGameObject(scene.FindGameObjectByName(e.lane));
            const PuzzleLane* lane = root ? root->GetComponent<PuzzleLane>() : nullptr;
            const auto* cup = lane ? dynamic_cast<const GoalCup*>(h.Goal(*lane)) : nullptr;
            const std::optional<GoalRegion> region = cup ? cup->GetRegion() : std::nullopt;
            Check(region.has_value(), std::string(e.lane) + ": the cup measures a region");
            if(region)
            {
                Check(Near(region->center - region->halfExtent, e.min, 1.0e-3f) && Near(region->center + region->halfExtent, e.max, 1.0e-3f),
                    std::string(e.lane) + ": the region lies between the walls, from the floor top to half the lowest wall");
            }
        }

        Canape::GameObject* halfCup = scene.GetGameObject(scene.AddGameObject("halfCup"));
        halfCup->GetTransform()->SetLocalPosition({ 250.0f, 50.0f, 0.0f });
        auto* cup = halfCup->AddComponent<GoalCup>();
        for(const float x : { -0.5f, 0.5f })
        {
            Canape::GameObject* piece = scene.GetGameObject(scene.AddGameObject("halfCupPiece"));
            piece->GetTransform()->SetParent(halfCup->GetTransform());
            piece->GetTransform()->SetLocalPosition({ x, 0.0f, 0.0f });
            piece->AddComponent<Canape::BoxColliderComponent>();
            piece->AddComponent<Canape::RigidBodyComponent>()->SetMotion(Canape::PhysicsMotion::Static);
        }
        h.Frame();
        Check(!cup->GetRegion().has_value() && !cup->HoldsPayload() && !cup->IsShownInside(), "two pieces are no cup and hold nothing");
    }

    // Moves the payload to a world point from FixedUpdate at a lane tick: off the body for one tick, then on again.
    class TeleportAtTick final : public Canape::ScriptComponent {
    public:
        void Arm(const PuzzleLane* lane, const Handle<Canape::GameObject>& payload, std::uint32_t tick, const Canape::Vector3& target)
        {
            m_Lane = lane;
            m_Payload = payload;
            m_Tick = tick;
            m_Target = target;
        }

        bool IsDone() const { return m_State == 2; }

    protected:
        void FixedUpdate() override
        {
            Canape::GameObject* payload = m_Lane ? GetGameObject(m_Payload) : nullptr;
            auto* body = payload ? payload->GetComponent<Canape::RigidBodyComponent>() : nullptr;
            if(!body)
            {
                return;
            }
            if(m_State == 0 && m_Lane->GetTick() >= m_Tick)
            {
                Canape::TransformComponent* transform = payload->GetTransform();
                const Canape::TransformComponent* parent = transform->GetParent();
                transform->SetLocalPosition(parent ? parent->GetWorldToLocalMatrix().TransformPoint(m_Target) : m_Target);
                body->SetEnabled(false);
                m_State = 1;
            }
            else if(m_State == 1)
            {
                body->SetEnabled(true);
                m_State = 2;
            }
        }

    private:
        const PuzzleLane* m_Lane = nullptr;
        Handle<Canape::GameObject> m_Payload{};
        std::uint32_t m_Tick = 0;
        Canape::Vector3 m_Target{};
        int m_State = 0;
    };

    // A cup is met once the payload stayed inside for a second, and stays met after the payload leaves; the look follows
    // the payload, not the latch.
    void CheckGoalDwell(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the cup dwell");
        PuzzleLane& lane = *h.Front();
        PuzzleGoal* goal = h.Goal(lane);
        Canape::GameObject* payload = h.Payload(lane);
        const std::optional<GoalRegion> region = goal ? goal->GetRegion() : std::nullopt;
        Check(goal && payload && region, "the front lane has a goal, a payload and a region");
        if(!goal || !payload || !region)
        {
            return;
        }
        auto* in = lane.GetOwnerObject()->AddComponent<TeleportAtTick>();
        in->Arm(&lane, payload->GetHandle(), 10, region->center);
        Check(h.RunUntil([&] { return lane.GetTick() >= 12 + CupDwellTicks / 2; }), "the run reaches half a second after the payload was put in");
        Check(goal->HoldsPayload() && goal->IsShownInside() && !goal->IsSatisfied(), "half a second inside shows the payload but does not meet the cup yet");
        Check(h.RunUntil([&] { return lane.GetTick() >= 12 + CupDwellTicks + 8; }), "the run reaches a second after the payload was put in");
        Check(goal->IsSatisfied(), "a second inside meets the cup");

        auto* out = lane.GetOwnerObject()->AddComponent<TeleportAtTick>();
        out->Arm(&lane, payload->GetHandle(), 100, region->center + Canape::Vector3{ 0.0f, 0.0f, -2.0f });
        Check(h.RunUntil([&] { return lane.GetTick() >= 140; }), "the run goes on after the payload was taken out");
        Check(!goal->HoldsPayload() && !goal->IsShownInside() && goal->IsSatisfied(), "the cup stays met after the payload left and shows it outside");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run with the taken-out payload reaches its verdict");
        Check(lane.IsSolved() && !lane.HasFailed(), "a lane whose cup was met is solved at the horizon with the payload gone");
    }

    // A zone is met the moment the payload touches it, even while falling through, and stays met.
    void CheckGoalZone(Harness& h)
    {
        const rttr::type type = rttr::type::get<GoalZone>();
        Check(type.get_constructor().is_valid() && type.get_property("size").is_valid() && type.get_property("payload").is_valid()
            && type.get_properties().size() == 5, "GoalZone registers its size and the goal values (" + std::to_string(type.get_properties().size()) + ")");

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the zone");
        Canape::Scene& scene = h.Active();
        const Canape::Vector3 origin{ 300.0f, 50.0f, 0.0f };
        Canape::GameObject* floor = scene.GetGameObject(scene.AddGameObject("zoneFloor"));
        floor->GetTransform()->SetLocalPosition(origin);
        floor->GetTransform()->SetLocalScale({ 2.0f, 0.2f, 2.0f });
        floor->AddComponent<Canape::BoxColliderComponent>();
        floor->AddComponent<Canape::RigidBodyComponent>()->SetMotion(Canape::PhysicsMotion::Static);
        Canape::GameObject* ball = scene.GetGameObject(scene.AddGameObject("zoneBall"));
        ball->GetTransform()->SetLocalPosition(origin + Canape::Vector3{ 0.0f, 3.0f, 0.0f });
        ball->GetTransform()->SetLocalScale({ 0.2f, 0.2f, 0.2f });
        ball->AddComponent<Canape::SphereColliderComponent>();
        ball->AddComponent<Canape::RigidBodyComponent>()->SetMass(1.0f);
        Canape::GameObject* zoneObject = scene.GetGameObject(scene.AddGameObject("zone"));
        zoneObject->GetTransform()->SetLocalPosition(origin + Canape::Vector3{ 0.0f, 1.0f, 0.0f });
        zoneObject->GetTransform()->SetLocalScale({ 1.0f, 0.2f, 1.0f });
        auto* zone = zoneObject->AddComponent<GoalZone>();
        rttr::type::get<PuzzleGoal>().get_property("payload").set_value(*zone, ball->GetHandle());
        Canape::GameObject* emptyObject = scene.GetGameObject(scene.AddGameObject("emptyZone"));
        emptyObject->GetTransform()->SetLocalPosition(origin + Canape::Vector3{ 0.0f, 1.0f, 0.0f });
        emptyObject->GetTransform()->SetLocalScale({ 0.0f, 1.0f, 1.0f });
        auto* empty = emptyObject->AddComponent<GoalZone>();
        rttr::type::get<PuzzleGoal>().get_property("payload").set_value(*empty, ball->GetHandle());

        h.Frame();
        const std::optional<GoalRegion> region = zone->GetRegion();
        Check(region && Near(region->center, origin + Canape::Vector3{ 0.0f, 1.0f, 0.0f }, 1.0e-5f) && Near(region->halfExtent, { 0.5f, 0.1f, 0.5f }, 1.0e-5f),
            "the zone is its size times the scale, centred on its position");
        Check(!zone->IsSatisfied() && !zone->IsShownInside(), "the zone is not met before the ball reaches it");
        Check(!empty->GetRegion().has_value(), "a zone of zero size measures nothing");
        for(int frame = 0; frame < 30; ++frame)
        {
            h.Frame();
        }
        Check(ball->GetTransform()->GetPosition().Y < origin.Y + 0.5f, "the ball fell through the zone onto the floor");
        Check(zone->IsSatisfied() && !zone->HoldsPayload() && !zone->IsShownInside(), "the zone stays met after the ball fell through and shows it outside");
        Check(!empty->IsSatisfied(), "a zone without a region is never met");
        Check(scene.GetPhysics().GetAuthoringIssueCount() == 0 && !h.Front()->HasFailed(), "zones are no issue");
    }

    // Four ticks per frame while the loop runs at the cap: the steps and the lane ticks advance together.
    void CheckStepAccounting(Harness& h)
    {
        const Canape::PhysicsSceneStats before = h.Active().GetPhysics().GetStats();
        Check(before.bodyCount > 0 && before.activeBodies <= before.bodyCount && before.stepCount > 0, "the physics stats count bodies and steps");
        constexpr int Frames = 3;
        for(int i = 0; i < Frames; ++i)
        {
            h.Frame();
        }
        const Canape::PhysicsSceneStats after = h.Active().GetPhysics().GetStats();
        Check(after.stepCount == before.stepCount + Frames * TicksPerCappedFrame && after.bodyCount == before.bodyCount,
            "three capped frames are twelve steps");
        Check(after.lastStepMilliseconds >= 0.0f && after.lastStepMilliseconds < 1000.0f, "the last step time is plausible");
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    // The checkout decides the line endings of the shipped file and the serializer writes the platform's.
    std::string WithoutCarriageReturns(std::string text)
    {
        std::erase(text, '\r');
        return text;
    }

    void CheckSameText(const std::string& what, const std::string& expected, const std::string& actual)
    {
        const auto [expectedAt, actualAt] = std::mismatch(expected.begin(), expected.end(), actual.begin(), actual.end());
        const bool same = expectedAt == expected.end() && actualAt == actual.end();
        if(!same)
        {
            std::printf("INFO %s: first difference at byte %zu of %zu\n", what.c_str(), static_cast<std::size_t>(expectedAt - expected.begin()), expected.size());
        }
        Check(same, what);
    }

    // Removes the file on every way out of a check.
    struct TempFile {
        std::filesystem::path path;

        ~TempFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };

    void CheckRoundTrip(Harness& h, const std::filesystem::path& sceneFile, const std::vector<Verdict>& solution,
        const std::vector<Verdict>& control)
    {
        const TempFile roundTrip{ RoundTripFile };
        Canape::Scene* fresh = h.Reload(PuzzleMode::Solution);
        Check(fresh != nullptr, "a fresh playground opens for the save");
        if(!fresh)
        {
            return;
        }

        Check(h.scenes.SaveSceneToFile(*fresh, RoundTripFile), "the playground saves");
        CheckSameText("load -> save writes the shipped JSON text, line endings aside", WithoutCarriageReturns(ReadFile(sceneFile)), WithoutCarriageReturns(ReadFile(RoundTripFile)));

        Canape::Scene* reloaded = h.scenes.LoadSceneFromFile(RoundTripFile);
        Check(reloaded != nullptr, "the saved playground loads again");
        if(!reloaded)
        {
            return;
        }
        h.scenes.UnloadScene(fresh);
        h.Frame();
        Check(h.scenes.GetActiveScene() == reloaded, "the reloaded playground is active");

        Check(h.RunToVerdict(PuzzleMode::Solution), "the playground of the second load reaches its verdict");
        CheckSame("load -> save -> load solution", solution, h.Verdicts());

        Check(h.Adopt(h.scenes.GetActiveScene(), h.scenes.LoadSceneFromFile(RoundTripFile), PuzzleMode::Control) != nullptr,
            "the saved playground loads again for the control run");
        Check(h.RunToVerdict(PuzzleMode::Control), "the control run of the saved copy reaches its verdict");
        CheckSame("load -> save -> load control", control, h.Verdicts());
    }

    // A key that does not rise in tick is ignored: the first key of a tick wins.
    void CheckKeyOrder()
    {
        const Canape::Quaternion identity{};
        const PoseKeys keys{ { 10, { Canape::Vector3(1.0f, 0.0f, 0.0f), identity }, Canape::Easing::Linear },
            { 10, { Canape::Vector3(5.0f, 0.0f, 0.0f), identity }, Canape::Easing::Linear },
            { 20, { Canape::Vector3(3.0f, 0.0f, 0.0f), identity }, Canape::Easing::Linear } };
        const Canape::PhysicsPose start{ Canape::Vector3(0.0f, 0.0f, 0.0f), identity };
        Check(EvaluatePoseKeys(keys, start, 10).position.X == 1.0f, "a second key at the same tick is ignored");
        Check(EvaluatePoseKeys(keys, start, 15).position.X == 2.0f, "the pose runs on from the first key of that tick");
    }

    std::vector<Canape::GameObject*> TracksOf(const Canape::GameObject* object)
    {
        std::vector<Canape::GameObject*> found;
        if(object->GetComponent<PoseTrack>())
        {
            found.push_back(const_cast<Canape::GameObject*>(object));
        }
        for(const Canape::TransformComponent* child : object->GetTransform()->GetChildren())
        {
            const std::vector<Canape::GameObject*> below = TracksOf(child->GetOwnerObject());
            found.insert(found.end(), below.begin(), below.end());
        }
        return found;
    }

    // A restart reloads the scene as it was when the run began: an edit made in memory survives R, C and the automatic switch.
    void CheckStartSnapshot(Harness& h, const std::vector<Verdict>& solution)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the edit check");
        PuzzleLane* lane = h.Front();
        Check(lane != nullptr, "the edit check has a lane");
        if(!lane)
        {
            return;
        }
        for(Canape::GameObject* trackObject : TracksOf(lane->GetOwnerObject()))
        {
            rttr::type::get<PoseTrack>().get_property("solutionKeys").set_value(*trackObject->GetComponent<PoseTrack>(), PoseKeys{});
        }

        Check(h.RunToVerdict(PuzzleMode::Solution), "the edited run reaches its verdict");
        const std::vector<Verdict> edited = h.Verdicts();
        Check(!edited.empty() && !edited.front().solved && !solution.empty() && solution.front().solved,
            "the edit changes the result of the lane");

        const std::string snapshot = h.Director()->GetSnapshotPath();
        Check(!snapshot.empty() && std::filesystem::exists(snapshot), "the director saved a start snapshot");

        h.Frame({ Canape::Key::R });
        Check(h.Director()->GetSnapshotPath() == snapshot, "R hands the snapshot on");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run after R of the edited scene reaches its verdict");
        CheckSame("R keeps the edit", edited, h.Verdicts());

        Check(h.RunToVerdict(PuzzleMode::Control), "the automatic switch of the edited scene reaches the control run");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the automatic switch of the edited scene comes back");
        CheckSame("the automatic switch keeps the edit", edited, h.Verdicts());
        Check(h.Director()->GetSnapshotPath() == snapshot, "the automatic switch hands the snapshot on");

        h.Frame({ Canape::Key::C });
        Check(h.RunToVerdict(PuzzleMode::Control), "the run after C of the edited scene reaches its verdict");
        Check(std::filesystem::exists(snapshot), "the snapshot stays while the run goes on");

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens after the edit check");
        Check(!std::filesystem::exists(snapshot), "the snapshot is deleted with the last director");
    }

    bool IsNear(const Canape::Vector3& a, const Canape::Vector3& b)
    {
        return (a - b).Length() < 1.0e-4f;
    }

    bool IsNear(const Canape::Quaternion& a, const Canape::Quaternion& b)
    {
        return std::fabs(a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W) > 0.99999f;
    }

    // The fly camera starts at its home pose over the lanes (the camera Transform stored in the scene file), and F brings
    // a moved camera back to it.
    void CheckCameraHome(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the camera home check");
        h.Frame();
        FlyCamera* camera = h.Camera();
        Check(camera != nullptr, "the playground has a fly camera");
        if(!camera)
        {
            return;
        }

        const Canape::Vector3 homePosition{ 0.0f, 7.3f, -3.9f };
        const Canape::Quaternion homeRotation{ 0.42277023f, 0.0f, 0.0f, 0.9062369f };
        Canape::TransformComponent& transform = *camera->GetTransform();
        Check(IsNear(transform.GetLocalPosition(), homePosition), "the camera starts at the home position over the lanes");
        Check(IsNear(transform.GetLocalRotation(), homeRotation), "the camera starts looking down at the lanes");

        transform.SetLocalPosition({ 4.0f, 1.0f, 2.0f });
        transform.SetLocalRotation(Canape::Rotator(-20.0f, 70.0f, 0.0f).ToQuaternion());
        h.Frame({ Canape::Key::F });
        Check(IsNear(transform.GetLocalPosition(), homePosition), "F returns the camera to the home position");
        Check(IsNear(transform.GetLocalRotation(), homeRotation), "F returns the camera to the home rotation");
    }

    // A reload carries the pose of the fly camera over; the new camera does not snap home.
    void CheckCameraCarry(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the camera check");
        h.Frame();
        FlyCamera* camera = h.Camera();
        Check(camera != nullptr, "the playground has a fly camera");
        if(!camera)
        {
            return;
        }

        const Canape::Vector3 position{ 1.5f, 2.5f, -3.5f };
        const Canape::Quaternion rotation = Canape::Rotator(-12.0f, 33.0f, 0.0f).ToQuaternion();
        camera->GetTransform()->SetLocalPosition(position);
        camera->GetTransform()->SetLocalRotation(rotation);
        h.Frame({ Canape::Key::R });
        h.Frame();
        h.Frame();

        FlyCamera* carried = h.Camera();
        Check(carried != nullptr && carried != camera, "the reloaded scene has its own camera");
        if(carried && carried != camera)
        {
            Check(IsNear(carried->GetTransform()->GetLocalPosition(), position), "the reloaded camera sits at the carried position");
            Check(IsNear(carried->GetTransform()->GetLocalRotation(), rotation), "the reloaded camera keeps the carried rotation");
        }
    }

    // Reads and writes from FixedUpdate at exact ticks, whatever the frame grouping.
    class DisableAtTick final : public Canape::ScriptComponent {
    public:
        void Arm(Canape::RigidBodyComponent* body, std::vector<Canape::GameObject*> watched, std::uint64_t disableTick, std::uint64_t captureTick)
        {
            m_Body = body;
            m_Watched = std::move(watched);
            m_DisableTick = disableTick;
            m_CaptureTick = captureTick;
            m_Armed = true;
        }

        bool IsDone() const { return m_Done; }
        const std::vector<Canape::Vector3>& GetCaptured() const { return m_Captured; }
        bool HasBodyNextTick() const { return m_HasBodyNextTick; }
        const Canape::Vector3& GetPositionAtDisable() const { return m_PositionAtDisable; }
        const Canape::Vector3& GetPositionNextTick() const { return m_PositionNextTick; }

    protected:
        void FixedUpdate() override
        {
            if(!m_Armed)
            {
                return;
            }
            const std::uint64_t tick = GetOwnerObject()->GetScene()->GetPhysics().GetStats().stepCount;
            const Canape::TransformComponent* transform = m_Body->GetTransform();
            if(tick == m_DisableTick)
            {
                m_PositionAtDisable = transform->GetPosition();
                m_Body->SetEnabled(false);
            }
            else if(tick == m_DisableTick + 1)
            {
                m_PositionNextTick = transform->GetPosition();
                m_HasBodyNextTick = m_Body->HasBody();
            }
            if(tick == m_CaptureTick)
            {
                for(const Canape::GameObject* object : m_Watched)
                {
                    m_Captured.push_back(object->GetTransform()->GetPosition());
                }
                m_Done = true;
                m_Armed = false;
            }
        }

    private:
        Canape::RigidBodyComponent* m_Body = nullptr;
        std::vector<Canape::GameObject*> m_Watched;
        std::vector<Canape::Vector3> m_Captured;
        Canape::Vector3 m_PositionAtDisable{};
        Canape::Vector3 m_PositionNextTick{};
        std::uint64_t m_DisableTick = 0;
        std::uint64_t m_CaptureTick = 0;
        bool m_HasBodyNextTick = true;
        bool m_Armed = false;
        bool m_Done = false;
    };

    void CollectBodies(Canape::GameObject* object, std::vector<Canape::GameObject*>& bodies)
    {
        if(const auto* body = object->GetComponent<Canape::RigidBodyComponent>(); body && body->GetMotion() != Canape::PhysicsMotion::Static)
        {
            bodies.push_back(object);
        }
        for(const Canape::TransformComponent* child : object->GetTransform()->GetChildren())
        {
            CollectBodies(child->GetOwnerObject(), bodies);
        }
    }

    bool SamePositions(const std::vector<Canape::Vector3>& a, const std::vector<Canape::Vector3>& b)
    {
        return !a.empty() && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Canape::Vector3)) == 0;
    }

    // A body disabled in FixedUpdate is gone in that same tick, and the poses do not depend on how ticks are grouped into frames.
    void CheckFixedTickSync(Harness& h)
    {
        std::vector<Canape::Vector3> captured[2];
        int index = 0;
        for(const bool mixed : { false, true })
        {
            if(mixed)
            {
                h.SetMixedBatches(SmallBatchTicks);
            }
            else
            {
                h.SetScale(FourTickScale);
            }
            Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the disable probe");
            Canape::Scene& scene = h.Active();
            Canape::GameObject* payload = h.Payload(*h.Front());
            auto* body = payload ? payload->GetComponent<Canape::RigidBodyComponent>() : nullptr;
            Check(body != nullptr, "the payload has a rigid body for the fixed tick sync check");
            if(!body)
            {
                break;
            }

            std::vector<Canape::GameObject*> watched;
            CollectBodies(h.Front()->GetOwnerObject(), watched);
            auto* probe = scene.GetGameObject(scene.AddGameObject("fixedTickProbe"))->AddComponent<DisableAtTick>();
            probe->Arm(body, watched, 125, 160);
            Check(h.RunUntil([&] { return probe->IsDone(); }), "the disable probe reaches its capture tick");
            mixed ? h.CheckMixedBatches("the disable probe") : h.CheckFourTickBatches("the disable probe");
            Check(!probe->HasBodyNextTick(), "a body disabled in FixedUpdate is gone by the next tick");
            Check(std::memcmp(&probe->GetPositionAtDisable(), &probe->GetPositionNextTick(), sizeof(Canape::Vector3)) == 0,
                "a body disabled in FixedUpdate takes no step in that tick");
            Check(!body->HasBody(), "the disabled body stays out of the world");
            captured[index++] = probe->GetCaptured();
        }
        h.SetScale(FourTickScale);

        Check(SamePositions(captured[0], captured[1]), "frames of four ticks and of mixed sizes give bit-identical poses after a body is disabled in FixedUpdate");
    }

    struct ObjectPose {
        Canape::Vector3 position{};
        Canape::Quaternion rotation{};
    };

    ObjectPose PoseOf(const Canape::GameObject& object)
    {
        return { object.GetTransform()->GetPosition(), object.GetTransform()->GetRotation() };
    }

    bool SamePose(const ObjectPose& a, const ObjectPose& b)
    {
        return std::memcmp(&a.position, &b.position, sizeof(Canape::Vector3)) == 0
            && std::memcmp(&a.rotation, &b.rotation, sizeof(Canape::Quaternion)) == 0;
    }

    bool SamePoses(const std::vector<ObjectPose>& a, const std::vector<ObjectPose>& b)
    {
        if(a.empty() || a.size() != b.size())
        {
            return false;
        }
        for(std::size_t i = 0; i < a.size(); ++i)
        {
            if(!SamePose(a[i], b[i]))
            {
                return false;
            }
        }
        return true;
    }

    // Adds a box body from FixedUpdate at a chosen tick and records it and the watched bodies later.
    class AddBodyAtTick final : public Canape::ScriptComponent {
    public:
        void Arm(std::vector<Canape::GameObject*> watched, std::uint64_t addTick, std::uint64_t captureTick)
        {
            m_Watched = std::move(watched);
            m_AddTick = addTick;
            m_CaptureTick = captureTick;
            m_Armed = true;
        }

        bool IsDone() const { return m_Done; }
        const std::vector<ObjectPose>& GetCaptured() const { return m_Captured; }
        bool HasBodyNextTick() const { return m_HasBodyNextTick; }
        bool DroppedNextTick() const { return m_DroppedNextTick; }
        std::size_t GetBodiesGained() const { return m_BodiesNextTick - m_BodiesAtAdd; }

    protected:
        void FixedUpdate() override
        {
            if(!m_Armed)
            {
                return;
            }
            Canape::Scene* scene = GetOwnerObject()->GetScene();
            const Canape::PhysicsSceneStats stats = scene->GetPhysics().GetStats();
            const std::uint64_t tick = stats.stepCount;
            if(tick == m_AddTick)
            {
                m_BodiesAtAdd = stats.bodyCount;
                m_Added = scene->AddGameObject("addedInFixedUpdate");
                Canape::GameObject* object = scene->GetGameObject(m_Added);
                object->GetTransform()->SetLocalPosition(AddedPosition);
                object->AddComponent<Canape::BoxColliderComponent>();
                object->AddComponent<Canape::RigidBodyComponent>()->SetMass(1.0f);
            }
            else if(tick == m_AddTick + 1)
            {
                Canape::GameObject* object = scene->GetGameObject(m_Added);
                const auto* body = object ? object->GetComponent<Canape::RigidBodyComponent>() : nullptr;
                m_BodiesNextTick = stats.bodyCount;
                m_HasBodyNextTick = body && body->HasBody();
                m_DroppedNextTick = body && object->GetTransform()->GetPosition().Y < AddedPosition.Y;
            }
            if(tick == m_CaptureTick)
            {
                if(const Canape::GameObject* object = scene->GetGameObject(m_Added))
                {
                    m_Captured.push_back(PoseOf(*object));
                }
                for(const Canape::GameObject* watched : m_Watched)
                {
                    m_Captured.push_back(PoseOf(*watched));
                }
                m_Done = true;
                m_Armed = false;
            }
        }

    private:
        static inline const Canape::Vector3 AddedPosition{ 200.0f, 80.0f, 20.0f };

        std::vector<Canape::GameObject*> m_Watched;
        std::vector<ObjectPose> m_Captured;
        Handle<Canape::GameObject> m_Added{};
        std::uint64_t m_AddTick = 0;
        std::uint64_t m_CaptureTick = 0;
        std::size_t m_BodiesAtAdd = 0;
        std::size_t m_BodiesNextTick = 0;
        bool m_HasBodyNextTick = false;
        bool m_DroppedNextTick = false;
        bool m_Armed = false;
        bool m_Done = false;
    };

    // A body added in FixedUpdate is in the world and steps in that same tick, whatever the frame grouping.
    void CheckAddedBodySync(Harness& h)
    {
        std::vector<ObjectPose> captured[2];
        int index = 0;
        for(const bool mixed : { false, true })
        {
            if(mixed)
            {
                h.SetMixedBatches(SmallBatchTicks);
            }
            else
            {
                h.SetScale(FourTickScale);
            }
            Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the add probe");
            Canape::Scene& scene = h.Active();
            std::vector<Canape::GameObject*> watched;
            CollectBodies(h.Front()->GetOwnerObject(), watched);
            auto* probe = scene.GetGameObject(scene.AddGameObject("addBodyProbe"))->AddComponent<AddBodyAtTick>();
            probe->Arm(watched, 125, 160);
            Check(h.RunUntil([&] { return probe->IsDone(); }), "the add probe reaches its capture tick");
            mixed ? h.CheckMixedBatches("the add probe") : h.CheckFourTickBatches("the add probe");
            Check(probe->HasBodyNextTick(), "a body added in FixedUpdate exists by the next tick");
            Check(probe->GetBodiesGained() == 1, "the world gains the added body in the step of the tick it was added in");
            Check(probe->DroppedNextTick(), "a body added in FixedUpdate steps in the tick it was added");
            captured[index++] = probe->GetCaptured();
        }
        h.SetScale(FourTickScale);

        Check(SamePoses(captured[0], captured[1]), "frames of four ticks and of mixed sizes give bit-identical poses after a body is added in FixedUpdate");
    }

    // Removes one collider from FixedUpdate at a chosen tick and records the compound body around and after it.
    class RemoveColliderAtTick final : public Canape::ScriptComponent {
    public:
        void Arm(Canape::ColliderComponent* collider, std::uint64_t removeTick, std::uint64_t captureTick)
        {
            m_Collider = collider;
            m_RemoveTick = removeTick;
            m_CaptureTick = captureTick;
            m_Armed = true;
        }

        bool IsDone() const { return m_Done; }
        const ObjectPose& GetAtRemoval() const { return m_AtRemoval; }
        const ObjectPose& GetCaptured() const { return m_Captured; }
        bool HasBodyNextTick() const { return m_HasBodyNextTick; }

    protected:
        void FixedUpdate() override
        {
            if(!m_Armed)
            {
                return;
            }
            const std::uint64_t tick = GetOwnerObject()->GetScene()->GetPhysics().GetStats().stepCount;
            if(tick == m_RemoveTick)
            {
                m_AtRemoval = PoseOf(*GetOwnerObject());
                m_Collider->GetOwnerObject()->RemoveComponent(m_Collider);
            }
            else if(tick == m_RemoveTick + 1)
            {
                m_HasBodyNextTick = GetComponent<Canape::RigidBodyComponent>()->HasBody();
            }
            if(tick == m_CaptureTick)
            {
                m_Captured = PoseOf(*GetOwnerObject());
                m_Done = true;
                m_Armed = false;
            }
        }

    private:
        Canape::ColliderComponent* m_Collider = nullptr;
        ObjectPose m_AtRemoval{};
        ObjectPose m_Captured{};
        std::uint64_t m_RemoveTick = 0;
        std::uint64_t m_CaptureTick = 0;
        bool m_HasBodyNextTick = false;
        bool m_Armed = false;
        bool m_Done = false;
    };

    // A collider removed in FixedUpdate rebuilds its moving body once, in that tick, so the trajectory does not depend on the frame grouping.
    void CheckColliderRemovalSync(Harness& h)
    {
        std::vector<ObjectPose> captured[2];
        int index = 0;
        for(const bool mixed : { false, true })
        {
            if(mixed)
            {
                h.SetMixedBatches(SmallBatchTicks);
            }
            else
            {
                h.SetScale(FourTickScale);
            }
            Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the collider removal probe");
            Canape::Scene& scene = h.Active();
            Canape::GameObject* compound = scene.GetGameObject(scene.AddGameObject("compoundBody"));
            compound->GetTransform()->SetLocalPosition({ 200.0f, 80.0f, 20.0f });
            compound->AddComponent<Canape::BoxColliderComponent>();
            auto* body = compound->AddComponent<Canape::RigidBodyComponent>();
            Canape::GameObject* arm = scene.GetGameObject(scene.AddGameObject("compoundArm"));
            arm->GetTransform()->SetParent(compound->GetTransform());
            arm->GetTransform()->SetLocalPosition({ 1.0f, 0.0f, 0.0f });
            auto* armCollider = arm->AddComponent<Canape::BoxColliderComponent>();
            body->AddImpulse({ 40.0f, 0.0f, 0.0f });

            auto* probe = compound->AddComponent<RemoveColliderAtTick>();
            probe->Arm(armCollider, 125, 160);
            Check(h.RunUntil([&] { return probe->IsDone(); }), "the collider removal probe reaches its capture tick");
            mixed ? h.CheckMixedBatches("the collider removal probe") : h.CheckFourTickBatches("the collider removal probe");
            Check(probe->HasBodyNextTick(), "the body is rebuilt in the tick of the removal");
            Check(probe->GetCaptured().position.Y < probe->GetAtRemoval().position.Y, "the rebuilt body keeps moving");
            Check(scene.GetPhysics().GetAuthoringIssueCount() == 0, "a collider removal is no issue");
            captured[index++] = { probe->GetCaptured() };
        }
        h.SetScale(FourTickScale);

        Check(SamePoses(captured[0], captured[1]), "frames of four ticks and of mixed sizes give bit-identical poses after a collider is removed in FixedUpdate");
    }

    // Moves the payload into the cup from FixedUpdate once the lane has its verdict.
    class GoalPusher final : public Canape::ScriptComponent {
    public:
        void Arm(PuzzleLane* lane, PuzzleGoal* goal)
        {
            m_Lane = lane;
            m_Goal = goal;
        }
        std::uint32_t GetRestTicksAfterPush() const { return m_RestTicksAfterPush; }

    protected:
        void FixedUpdate() override
        {
            if(!m_Lane || !m_Goal)
            {
                return;
            }
            Canape::GameObject* payload = GetGameObject(m_Goal->GetPayload());
            auto* body = payload ? payload->GetComponent<Canape::RigidBodyComponent>() : nullptr;
            const std::optional<GoalRegion> region = m_Goal->GetRegion();
            if(!body || !region)
            {
                return;
            }

            if(m_State == 0 && m_Lane->HasVerdict())
            {
                const Canape::Vector3 goal = region->center;
                Canape::TransformComponent* transform = payload->GetTransform();
                const Canape::TransformComponent* parent = transform->GetParent();
                transform->SetLocalPosition(parent ? parent->GetWorldToLocalMatrix().TransformPoint(goal) : goal);
                body->SetEnabled(false);
                m_State = 1;
            }
            else if(m_State == 1)
            {
                body->SetEnabled(true);
                m_State = 2;
            }
            else if(m_State == 2)
            {
                ++m_RestTicksAfterPush;
            }
        }

    private:
        PuzzleLane* m_Lane = nullptr;
        PuzzleGoal* m_Goal = nullptr;
        std::uint32_t m_State = 0;
        std::uint32_t m_RestTicksAfterPush = 0;
    };

    // The verdict is taken once, at the horizon: a payload that reaches the goal during the rest ticks changes nothing,
    // though the goal shows it.
    void CheckVerdictFinality(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Control) != nullptr, "a control scene opens for the verdict check");
        PuzzleLane& lane = *h.Front();
        PuzzleGoal* goal = h.Goal(lane);
        auto* pusher = lane.GetOwnerObject()->AddComponent<GoalPusher>();
        pusher->Arm(&lane, goal);

        Check(h.RunUntil([&] { return pusher->GetRestTicksAfterPush() >= 3; }), "the payload is pushed during the rest ticks");
        Check(goal->HoldsPayload(), "the pushed payload is inside the goal during the rest");
        Check(lane.HasVerdict() && !lane.IsSolved(), "the verdict at the horizon is unsolved");

        Check(h.RunUntil([&] { return pusher->GetRestTicksAfterPush() >= 20; }), "the rest goes on after the push");
        Check(lane.GetTick() == PuzzleHorizonTicks && lane.HasVerdict(), "the lane stays at its horizon during the rest");
        Check(!lane.IsSolved(), "a goal reached after the verdict does not change it");
        Check(goal->IsShownInside() && !goal->IsSatisfied(), "the goal shows the payload inside all the same, short of its dwell");
        Check(!lane.HasFailed(), "the pushed payload fails nothing");
    }

    int CountMoved(const std::vector<Canape::GameObject*>& objects, const std::vector<ObjectPose>& starts)
    {
        int moved = 0;
        for(std::size_t i = 0; i < objects.size(); ++i)
        {
            const ObjectPose now = PoseOf(*objects[i]);
            moved += (!IsNear(now.position, starts[i].position) || !IsNear(now.rotation, starts[i].rotation)) ? 1 : 0;
        }
        return moved;
    }

    // The director skips a disabled lane and a disabled track like the engine skips a disabled script.
    void CheckDisabledRunnables(Harness& h)
    {
        const auto poseTracks = [&h]
        {
            std::vector<Canape::GameObject*> tracked = TracksOf(h.Front()->GetOwnerObject());
            std::vector<ObjectPose> starts;
            for(const Canape::GameObject* object : tracked)
            {
                starts.push_back(PoseOf(*object));
            }
            return std::make_pair(std::move(tracked), std::move(starts));
        };

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the enabled tracks");
        {
            const auto [tracked, starts] = poseTracks();
            Check(!tracked.empty(), "the playground has pose tracks");
            Check(h.RunUntil([&] { return h.Front()->GetTick() >= 64; }), "a run with every track enabled reaches tick 64");
            Check(CountMoved(tracked, starts) == static_cast<int>(tracked.size()), "enabled tracks move their bodies");
        }

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the disabled tracks");
        {
            const auto [tracked, starts] = poseTracks();
            for(const Canape::GameObject* object : tracked)
            {
                object->GetComponent<PoseTrack>()->SetEnabled(false);
            }
            Check(h.RunUntil([&] { return h.Front()->GetTick() >= 64; }), "a run with every track disabled reaches tick 64");
            Check(CountMoved(tracked, starts) == 0, "a disabled track does not move its body");
        }

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the disabled lane");
        PuzzleLane& lane = *h.Front();
        const std::uint32_t tickBefore = lane.GetTick();
        lane.SetEnabled(false);
        for(int i = 0; i < 20; ++i)
        {
            h.Frame();
        }
        Check(lane.GetTick() == tickBefore, "a disabled lane does not tick");
        lane.SetEnabled(true);
        constexpr std::uint32_t FramesAfterEnable = 5;
        for(std::uint32_t i = 0; i < FramesAfterEnable; ++i)
        {
            h.Frame();
        }
        Check(lane.GetTick() == tickBefore + FramesAfterEnable * TicksPerCappedFrame, "the lane ticks again, four ticks a frame, once it is enabled");
    }

    // A body without a collider fails the physics run; the lanes turn failed before their verdict, and a fresh scene starts clean.
    void CheckPhysicsFailure(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the failure check");
        Canape::Scene& scene = h.Active();
        scene.GetGameObject(scene.AddGameObject("noCollider"))->AddComponent<Canape::RigidBodyComponent>();
        h.Frame();
        h.Frame();
        Check(scene.GetPhysics().GetAuthoringIssueCount() == 1, "a body without a collider is one authoring issue");
        Check(h.Front()->HasFailed() && !h.Front()->HasVerdict(), "the lane is failed before its verdict");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the failed run reaches its verdict");
        Check(h.Front()->HasFailed(), "the verdict keeps the failure");

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens after the failure check");
        Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0 && !h.Front()->HasFailed(), "a fresh scene starts without a failure");
    }

    class ImpulseOnStart final : public Canape::ScriptComponent {
    protected:
        void Start() override { GetComponent<Canape::RigidBodyComponent>()->AddImpulse({ 3.0f, 0.0f, 0.0f }); }
    };

    Canape::GameObject* AddFarBody(Canape::Scene& scene, const char* name, const Canape::Vector3& position, bool withCollider, bool withImpulse)
    {
        Canape::GameObject* object = scene.GetGameObject(scene.AddGameObject(name));
        object->GetTransform()->SetLocalPosition(position);
        if(withCollider)
        {
            object->AddComponent<Canape::BoxColliderComponent>();
        }
        object->AddComponent<Canape::RigidBodyComponent>()->SetMass(1.0f);
        if(withImpulse)
        {
            object->AddComponent<ImpulseOnStart>();
        }
        return object;
    }

    // An impulse added in Start waits for the body and is applied once; a body that never exists drops it.
    void CheckQueuedImpulse(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the impulse check");
        Canape::Scene& scene = h.Active();
        Canape::GameObject* pushed = AddFarBody(scene, "impulsePushed", { 200.0f, 50.0f, 0.0f }, true, true);
        Canape::GameObject* plain = AddFarBody(scene, "impulsePlain", { 200.0f, 50.0f, 5.0f }, true, false);
        Canape::GameObject* failing = AddFarBody(scene, "impulseFailing", { 200.0f, 50.0f, 10.0f }, false, true);
        h.Frame();

        const auto* pushedBody = pushed->GetComponent<Canape::RigidBodyComponent>();
        const auto* plainBody = plain->GetComponent<Canape::RigidBodyComponent>();
        Check(pushedBody->HasBody() && pushed->GetTransform()->GetPosition().X > 200.01f, "an impulse added in Start moves the body in the first frame");
        Check(plainBody->HasBody() && std::fabs(plain->GetTransform()->GetPosition().X - 200.0f) < 1.0e-4f, "a body without the impulse does not move sideways");

        auto* failingBody = failing->GetComponent<Canape::RigidBodyComponent>();
        Check(!failingBody->HasBody() && failingBody->HasFailed(), "a body without a collider has no body");
        failing->AddComponent<Canape::BoxColliderComponent>();
        h.Frame();
        h.Frame();
        Check(failingBody->HasBody() && std::fabs(failing->GetTransform()->GetPosition().X - 200.0f) < 1.0e-4f,
            "an impulse queued on a body that failed to create is dropped");
    }

    // A body added with invalid authoring values is a specific issue; a corrected value creates the body without a toggle.
    void CheckBodyValueValidation(Harness& h)
    {
        using Canape::RigidBodyIssue;
        using Edit = std::function<void(Canape::GameObject&)>;
        struct Case {
            const char* what;
            bool sphere;
            RigidBodyIssue expected;
            Edit spoil;
            Edit fix;
        };
        const float notANumber = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const auto body = [](Canape::GameObject& object) { return object.GetComponent<Canape::RigidBodyComponent>(); };
        const auto box = [](Canape::GameObject& object) { return object.GetComponent<Canape::BoxColliderComponent>(); };
        const auto sphere = [](Canape::GameObject& object) { return object.GetComponent<Canape::SphereColliderComponent>(); };
        const Canape::Vector3 unit = { 1.0f, 1.0f, 1.0f };
        const std::vector<Case> cases = {
            { "a negative mass", false, RigidBodyIssue::InvalidSettings, [&](Canape::GameObject& o) { body(o)->SetMass(-1.0f); }, [&](Canape::GameObject& o) { body(o)->SetMass(1.0f); } },
            { "a NaN mass", false, RigidBodyIssue::InvalidSettings, [&](Canape::GameObject& o) { body(o)->SetMass(notANumber); }, [&](Canape::GameObject& o) { body(o)->SetMass(1.0f); } },
            { "a negative friction", false, RigidBodyIssue::InvalidSettings, [&](Canape::GameObject& o) { body(o)->SetFriction(-0.5f); }, [&](Canape::GameObject& o) { body(o)->SetFriction(0.5f); } },
            { "a negative restitution", false, RigidBodyIssue::InvalidSettings, [&](Canape::GameObject& o) { body(o)->SetRestitution(-0.1f); }, [&](Canape::GameObject& o) { body(o)->SetRestitution(0.1f); } },
            { "an infinite linear damping", false, RigidBodyIssue::InvalidSettings, [&](Canape::GameObject& o) { body(o)->SetLinearDamping(infinity); }, [&](Canape::GameObject& o) { body(o)->SetLinearDamping(0.1f); } },
            { "a negative angular damping", false, RigidBodyIssue::InvalidSettings, [&](Canape::GameObject& o) { body(o)->SetAngularDamping(-0.1f); }, [&](Canape::GameObject& o) { body(o)->SetAngularDamping(0.1f); } },
            { "a zero box size", false, RigidBodyIssue::InvalidExtent, [&](Canape::GameObject& o) { box(o)->SetSize({ 1.0f, 0.0f, 1.0f }); }, [&](Canape::GameObject& o) { box(o)->SetSize(unit); } },
            { "a negative box size", false, RigidBodyIssue::None, [&](Canape::GameObject& o) { box(o)->SetSize({ -1.0f, 1.0f, 1.0f }); }, [&](Canape::GameObject& o) { box(o)->SetSize(unit); } },
            { "a NaN box size", false, RigidBodyIssue::InvalidExtent, [&](Canape::GameObject& o) { box(o)->SetSize({ 1.0f, notANumber, 1.0f }); }, [&](Canape::GameObject& o) { box(o)->SetSize(unit); } },
            { "a negative sphere radius", true, RigidBodyIssue::None, [&](Canape::GameObject& o) { sphere(o)->SetRadius(-0.5f); }, [&](Canape::GameObject& o) { sphere(o)->SetRadius(0.5f); } },
        };

        const auto invalidCases = static_cast<std::size_t>(std::count_if(cases.begin(), cases.end(), [](const Case& c) { return c.expected != RigidBodyIssue::None; }));

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the value validation");
        Canape::Scene& scene = h.Active();
        const Canape::PhysicsScene& physics = scene.GetPhysics();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == 0 && !h.Front()->HasFailed(), "no issue before the invalid values are added");

        std::vector<Canape::GameObject*> objects;
        for(std::size_t i = 0; i < cases.size(); ++i)
        {
            Canape::GameObject* object = scene.GetGameObject(scene.AddGameObject("badValue"));
            object->GetTransform()->SetLocalPosition({ 200.0f + 5.0f * static_cast<float>(i), 50.0f, 40.0f });
            if(cases[i].sphere)
            {
                object->AddComponent<Canape::SphereColliderComponent>();
            }
            else
            {
                object->AddComponent<Canape::BoxColliderComponent>();
            }
            object->AddComponent<Canape::RigidBodyComponent>()->SetMass(1.0f);
            cases[i].spoil(*object);
            objects.push_back(object);
        }
        h.Frame();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == invalidCases, "every invalid value is one authoring issue");
        Check(h.Front()->HasFailed(), "an invalid value fails the lane");
        for(std::size_t i = 0; i < cases.size(); ++i)
        {
            const auto* issued = objects[i]->GetComponent<Canape::RigidBodyComponent>();
            const bool accepted = cases[i].expected == RigidBodyIssue::None;
            Check(issued->HasBody() == accepted && issued->GetIssue() == cases[i].expected,
                std::string(cases[i].what) + (accepted ? " is taken as its absolute value" : " gives its specific issue, not a backend refusal"));
        }

        h.Frame();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == invalidCases, "the issues are counted once, not per frame");

        for(std::size_t i = 0; i < cases.size(); ++i)
        {
            cases[i].fix(*objects[i]);
        }
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == 0, "the corrected values leave no issue");
        for(std::size_t i = 0; i < cases.size(); ++i)
        {
            const auto* fixed = objects[i]->GetComponent<Canape::RigidBodyComponent>();
            Check(fixed->HasBody() && fixed->GetIssue() == RigidBodyIssue::None, std::string("fixing ") + cases[i].what + " creates the body without a toggle");
        }
        Check(h.Front()->HasFailed(), "the lane stays failed after the values are fixed");
    }

    struct HingeRig {
        Canape::GameObject* arm = nullptr;
        Canape::GameObject* tip = nullptr;
        Canape::HingeComponent* hinge = nullptr;
        Canape::RigidBodyComponent* body = nullptr;
        Canape::Vector3 anchor{};
    };

    // An arm body at the anchor, hinged about +z with limits [-pi/4, 0]: a 1 m rod along +x with a sphere at its tip,
    // both on child objects, so gravity swings the arm down to negative angles. The rod gives the body enough inertia
    // about its centre of mass for the solver to keep the pivot tight.
    HingeRig BuildHingeRig(Canape::Scene& scene, const char* name, const Canape::Vector3& anchor, bool colliders = true)
    {
        HingeRig rig;
        rig.anchor = anchor;
        rig.arm = scene.GetGameObject(scene.AddGameObject(std::string(name) + "Arm"));
        rig.arm->GetTransform()->SetLocalPosition(anchor);
        rig.body = rig.arm->AddComponent<Canape::RigidBodyComponent>();
        rig.body->SetMass(1.0f);
        rig.hinge = rig.arm->AddComponent<Canape::HingeComponent>();
        rig.hinge->SetAxis({ 0.0f, 0.0f, 1.0f });
        rig.hinge->SetMinAngle(-Pi / 4.0f);
        rig.hinge->SetMaxAngle(0.0f);
        Canape::GameObject* rod = scene.GetGameObject(scene.AddGameObject(std::string(name) + "Rod"));
        rod->GetTransform()->SetParent(rig.arm->GetTransform());
        rod->GetTransform()->SetLocalPosition({ 0.5f, 0.0f, 0.0f });
        rig.tip = scene.GetGameObject(scene.AddGameObject(std::string(name) + "Tip"));
        rig.tip->GetTransform()->SetParent(rig.arm->GetTransform());
        rig.tip->GetTransform()->SetLocalPosition({ 1.0f, 0.0f, 0.0f });
        if(colliders)
        {
            rod->AddComponent<Canape::BoxColliderComponent>()->SetSize({ 1.0f, 0.1f, 0.1f });
            rig.tip->AddComponent<Canape::SphereColliderComponent>()->SetRadius(0.1f);
        }
        return rig;
    }

    float RadiusOf(const HingeRig& rig)
    {
        return (rig.tip->GetTransform()->GetPosition() - rig.anchor).Length();
    }

    // A hinge fixed to the world holds the arm's tip at its radius and stops at the limit; the anchor point never moves.
    void CheckHingeHolds(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge checks");
        Canape::Scene& scene = h.Active();
        const HingeRig rig = BuildHingeRig(scene, "worldHinge", { 300.0f, 50.0f, 0.0f });
        h.Frame();
        Check(rig.hinge->HasHinge() && rig.hinge->IsHeld() && rig.hinge->GetIssue() == Canape::HingeIssue::None, "the hinge is made and held");
        Check(Near(rig.hinge->GetWorldAnchor(), rig.anchor, 1.0e-5f), "the anchor is the arm's position");
        Check(std::fabs(rig.hinge->GetAngle()) < 0.2f, "the angle starts near 0");
        for(int frame = 0; frame < 3; ++frame)
        {
            h.Frame();
        }
        // The pivot's velocity is the backend's drift correction, a few percent of the tip's.
        const float anchorSpeed = rig.body->GetVelocityAt(rig.anchor).Length();
        const float tipSpeed = rig.body->GetVelocityAt(rig.tip->GetTransform()->GetPosition()).Length();
        Check(tipSpeed > 0.5f && anchorSpeed < 0.1f * tipSpeed, "the anchor point nearly stands still while the tip swings (anchor " + std::to_string(anchorSpeed) + ", tip " + std::to_string(tipSpeed) + ")");

        float worstRadius = 0.0f;
        float lowest = 0.0f;
        float highest = 0.0f;
        for(int frame = 0; frame < 150; ++frame)
        {
            h.Frame();
            worstRadius = std::max(worstRadius, std::fabs(RadiusOf(rig) - 1.0f));
            lowest = std::min(lowest, rig.hinge->GetAngle());
            highest = std::max(highest, rig.hinge->GetAngle());
        }
        Check(worstRadius < 0.03f, "the tip stays at its radius from the anchor (worst error " + std::to_string(worstRadius) + ")");
        Check(lowest > -Pi / 4.0f - 0.2f && highest < 0.2f, "the angle stays within the limits (lowest " + std::to_string(lowest) + ", highest " + std::to_string(highest) + ")");
        Check(std::fabs(rig.hinge->GetAngle() + Pi / 4.0f) < 0.05f, "the arm rests on the lower limit (" + std::to_string(rig.hinge->GetAngle()) + ")");
        Check(rig.hinge->IsHeld(), "the hinge is still held");
        Check(Near(rig.arm->GetTransform()->GetPosition(), rig.anchor, 1.0e-2f), "the arm's origin stays at the anchor (off by " + std::to_string((rig.arm->GetTransform()->GetPosition() - rig.anchor).Length()) + ")");
        Check(rig.body->GetVelocityAt(rig.tip->GetTransform()->GetPosition()).Length() < 0.05f, "a resting arm has no velocity at its tip");
        Check(scene.GetPhysics().GetAuthoringIssueCount() == 0, "a held hinge is no issue");
    }

    // Disabling drops the hinge and enabling makes it again; a release holds until the component is toggled.
    void CheckHingeRelease(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge release checks");
        Canape::Scene& scene = h.Active();
        const HingeRig rig = BuildHingeRig(scene, "releasedHinge", { 300.0f, 50.0f, 0.0f });
        h.Frame();
        Check(rig.hinge->IsHeld(), "the hinge holds before the toggle");
        rig.hinge->SetEnabled(false);
        h.Frame();
        Check(!rig.hinge->HasHinge() && !rig.hinge->IsHeld() && rig.hinge->GetAngle() == 0.0f, "a disabled hinge is dropped");
        rig.hinge->SetEnabled(true);
        h.Frame();
        Check(rig.hinge->HasHinge() && rig.hinge->IsHeld() && std::fabs(rig.hinge->GetAngle()) < 0.3f, "an enabled hinge is made again, at angle 0 from where the arm is");

        const float heightBefore = rig.tip->GetTransform()->GetPosition().Y;
        rig.hinge->Release();
        Check(!rig.hinge->IsHeld() && rig.hinge->HasHinge(), "a released hinge is not held but still exists");
        for(int frame = 0; frame < 8; ++frame)
        {
            h.Frame();
        }
        Check(heightBefore - rig.tip->GetTransform()->GetPosition().Y > 0.5f, "the released arm falls");
        Check(std::isfinite(rig.hinge->GetAngle()), "the angle of a released hinge still reads");
        rig.hinge->Release();
        h.Frame();
        Check(!rig.hinge->IsHeld() && rig.hinge->GetIssue() == Canape::HingeIssue::None && scene.GetPhysics().GetAuthoringIssueCount() == 0,
            "a second release changes nothing");

        rig.body->SetEnabled(false);
        h.Frame();
        rig.body->SetEnabled(true);
        h.Frame();
        Check(rig.body->HasBody() && !rig.hinge->HasHinge() && !rig.hinge->IsHeld() && rig.hinge->GetIssue() == Canape::HingeIssue::None,
            "a rebuilt body does not take a released hinge back");
        rig.hinge->SetEnabled(false);
        h.Frame();
        rig.hinge->SetEnabled(true);
        h.Frame();
        Check(rig.hinge->HasHinge() && rig.hinge->IsHeld(), "toggling the hinge after a release makes it hold again");
        Check(!h.Front()->HasFailed(), "the release checks fail no lane");
    }

    // Removing the arm object ends its hinge; removing the hinge component frees the arm. Neither is an issue.
    void CheckHingeRemoval(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge removal checks");
        Canape::Scene& scene = h.Active();
        const HingeRig gone = BuildHingeRig(scene, "goneArm", { 300.0f, 50.0f, 0.0f });
        const HingeRig freed = BuildHingeRig(scene, "freedArm", { 310.0f, 50.0f, 0.0f });
        h.Frame();
        Check(gone.hinge->IsHeld() && freed.hinge->IsHeld(), "both hinges hold before the removals");

        const float heightBefore = freed.tip->GetTransform()->GetPosition().Y;
        const Handle<Canape::GameObject> goneHandle = gone.arm->GetHandle();
        scene.RemoveGameObject(goneHandle);
        h.Frame();
        Check(scene.GetGameObject(goneHandle) == nullptr, "the removed arm is gone");
        freed.arm->RemoveComponent(freed.hinge);
        h.Frame();
        for(int frame = 0; frame < 8; ++frame)
        {
            h.Frame();
        }
        Check(heightBefore - freed.tip->GetTransform()->GetPosition().Y > 0.5f, "an arm whose hinge component is removed falls free");
        Check(scene.GetPhysics().GetAuthoringIssueCount() == 0 && !Canape::StepFailed(scene.GetPhysics().GetLastStepError()),
            "the removals leave no issue and the world keeps stepping");
        Check(!h.Front()->HasFailed(), "the removals fail no lane");
    }

    // Each authoring mistake is its own issue, counted once; an arm whose body failed is the body's issue, not the hinge's.
    void CheckHingeIssues(Harness& h)
    {
        using Canape::HingeIssue;

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge issue checks");
        Canape::Scene& scene = h.Active();
        const Canape::PhysicsScene& physics = scene.GetPhysics();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == 0, "no issue before the bad hinges");

        Canape::GameObject* bare = scene.GetGameObject(scene.AddGameObject("noBodyArm"));
        bare->GetTransform()->SetLocalPosition({ 300.0f, 50.0f, 0.0f });
        auto* noBody = bare->AddComponent<Canape::HingeComponent>();
        const HingeRig notDynamic = BuildHingeRig(scene, "notDynamic", { 310.0f, 50.0f, 0.0f });
        notDynamic.body->SetMotion(Canape::PhysicsMotion::Static);
        const HingeRig badAxis = BuildHingeRig(scene, "badAxis", { 320.0f, 50.0f, 0.0f });
        badAxis.hinge->SetAxis({ 0.0f, 0.0f, 0.0f });
        const HingeRig bodyFailed = BuildHingeRig(scene, "bodyFailed", { 330.0f, 50.0f, 0.0f }, false);
        h.Frame();
        h.Frame();
        Check(noBody->GetIssue() == HingeIssue::NoBody && !noBody->HasHinge(), "an arm without a RigidBody is NoBody");
        Check(notDynamic.hinge->GetIssue() == HingeIssue::NotDynamic && !notDynamic.hinge->HasHinge(), "a static arm is NotDynamic");
        Check(badAxis.hinge->GetIssue() == HingeIssue::InvalidSettings && !badAxis.hinge->HasHinge(), "a zero axis is InvalidSettings");
        Check(bodyFailed.hinge->GetIssue() == HingeIssue::BodyFailed && bodyFailed.body->GetIssue() == Canape::RigidBodyIssue::NoCollider,
            "an arm without colliders is BodyFailed, and the body keeps its own issue");
        Check(physics.GetAuthoringIssueCount() == 4, "three hinge issues and one body issue are counted (now " + std::to_string(physics.GetAuthoringIssueCount()) + ")");
        Check(h.Front()->HasFailed(), "a hinge issue fails the lane");
        h.Frame();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == 4, "the hinge issues are counted once, not per frame");

        badAxis.hinge->SetAxis({ 0.0f, 0.0f, 1.0f });
        h.Frame();
        Check(badAxis.hinge->HasHinge() && badAxis.hinge->IsHeld() && badAxis.hinge->GetIssue() == HingeIssue::None, "a corrected axis makes the hinge without a toggle");
        Check(physics.GetAuthoringIssueCount() == 3, "the corrected hinge leaves three issues");
    }

    // A disabled child body hands its colliders to the body above it; enabling it takes them back.
    void CheckDisabledChildBody(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the disabled child body");
        Canape::Scene& scene = h.Active();
        Canape::GameObject* holder = scene.GetGameObject(scene.AddGameObject("holder"));
        holder->GetTransform()->SetLocalPosition({ 300.0f, 50.0f, 0.0f });
        holder->AddComponent<Canape::BoxColliderComponent>()->SetSize({ 0.2f, 0.2f, 0.2f });
        auto* holderBody = holder->AddComponent<Canape::RigidBodyComponent>();
        holderBody->SetMotion(Canape::PhysicsMotion::Static);
        Canape::GameObject* rider = scene.GetGameObject(scene.AddGameObject("rider"));
        rider->GetTransform()->SetParent(holder->GetTransform());
        rider->GetTransform()->SetLocalPosition({ 0.0f, 1.0f, 0.0f });
        rider->AddComponent<Canape::SphereColliderComponent>()->SetRadius(0.2f);
        auto* riderBody = rider->AddComponent<Canape::RigidBodyComponent>();
        riderBody->SetEnabled(false);
        h.Frame();
        const Canape::PhysicsScene& physics = scene.GetPhysics();
        Check(holderBody->HasBody() && !riderBody->HasBody() && physics.GetAuthoringIssueCount() == 0, "the holder has a body, the disabled rider none, and nothing is an issue");
        const Canape::Vector3 riderCenter = rider->GetTransform()->GetPosition();
        const Canape::Vector3 tiny{ 0.01f, 0.01f, 0.01f };
        Check(physics.QueryBox(riderCenter, tiny).empty(), "the static holder carries the rider's collider, so a moving-body query finds nothing there");

        rider->GetTransform()->SetParent(nullptr);
        rider->GetTransform()->SetLocalPosition(riderCenter);
        riderBody->SetEnabled(true);
        h.Frame();
        const Canape::RigidBodies atRider = physics.QueryBox(rider->GetTransform()->GetPosition(), tiny);
        Check(riderBody->HasBody() && atRider.size() == 1 && atRider.front() == riderBody, "the enabled rider is its own moving body");
        Check(physics.GetAuthoringIssueCount() == 0, "a rider that left its holder is no issue");
        riderBody->SetEnabled(false);
        rider->GetTransform()->SetParent(holder->GetTransform());
        h.Frame();
        Check(!riderBody->HasBody() && holderBody->HasBody() && physics.GetAuthoringIssueCount() == 0, "a rider disabled under the holder again hands its collider back");
    }

    void CheckHingeReflection()
    {
        const rttr::type type = rttr::type::get<Canape::HingeComponent>();
        Check(type.get_constructor().is_valid(), "HingeComponent has a registered constructor");
        for(const char* name : { "axis", "minAngle", "maxAngle", "maxFrictionTorque" })
        {
            Check(type.get_property(name).is_valid(), std::string("HingeComponent registers ") + name);
        }
        Check(type.get_properties().size() == 5, "HingeComponent registers its authoring values and nothing else (" + std::to_string(type.get_properties().size()) + ")");

        const rttr::type releaseType = rttr::type::get<HingeRelease>();
        Check(releaseType.get_constructor().is_valid() && releaseType.get_property("solutionTick").is_valid() && releaseType.get_property("controlTick").is_valid()
            && releaseType.get_properties().size() == 3, "HingeRelease registers its two ticks and nothing else (" + std::to_string(releaseType.get_properties().size()) + ")");
    }

    // Removes one object or component from inside Update, once.
    class RemoveOnce final : public Canape::ScriptComponent {
    public:
        void Arm(const Handle<Canape::GameObject>& object)
        {
            m_Object = object;
            m_Armed = true;
        }

        void ArmComponent(Canape::Component* component)
        {
            m_Component = component;
            m_Armed = true;
        }

    protected:
        void Update() override
        {
            if(!m_Armed)
            {
                return;
            }
            m_Armed = false;
            if(m_Component)
            {
                m_Component->GetOwnerObject()->RemoveComponent(m_Component);
                m_Component = nullptr;
            }
            else
            {
                GetOwnerObject()->GetScene()->RemoveGameObject(m_Object);
            }
        }

    private:
        Handle<Canape::GameObject> m_Object{};
        Canape::Component* m_Component = nullptr;
        bool m_Armed = false;
    };

    struct WaterRig {
        Canape::GameObject* floor = nullptr;
        Canape::GameObject* buoy = nullptr;
        Canape::GameObject* water = nullptr;
        Canape::WaterVolumeComponent* volume = nullptr;
        Canape::Vector3 origin{};

        // The buoy's height above the rig's origin.
        float Height() const { return buoy->GetTransform()->GetPosition().Y - origin.Y; }
    };

    // The BuoyPop tank without its walls: a floor whose top is 0.2 above the origin, a 10 kg sphere of radius 0.2 and a
    // 1.6 x 1 x 1 water box whose surface is 1.2 above the origin. A 0.01 m^3 cap under water carries the sphere, so it
    // floats with its centre at 1.255.
    WaterRig BuildWaterRig(Canape::Scene& scene, const Canape::Vector3& origin)
    {
        WaterRig rig;
        rig.origin = origin;
        rig.floor = scene.GetGameObject(scene.AddGameObject("waterFloor"));
        rig.floor->GetTransform()->SetLocalPosition(origin + Canape::Vector3{ 0.0f, 0.1f, 0.0f });
        rig.floor->GetTransform()->SetLocalScale({ 1.8f, 0.2f, 1.2f });
        rig.floor->AddComponent<Canape::BoxColliderComponent>();
        rig.floor->AddComponent<Canape::RigidBodyComponent>()->SetMotion(Canape::PhysicsMotion::Static);
        rig.buoy = scene.GetGameObject(scene.AddGameObject("buoy"));
        rig.buoy->GetTransform()->SetLocalPosition(origin + Canape::Vector3{ -0.3f, 1.255f, 0.0f });
        rig.buoy->GetTransform()->SetLocalScale({ 0.4f, 0.4f, 0.4f });
        rig.buoy->AddComponent<Canape::SphereColliderComponent>();
        rig.buoy->AddComponent<Canape::RigidBodyComponent>()->SetMass(10.0f);
        rig.water = scene.GetGameObject(scene.AddGameObject("water"));
        rig.water->GetTransform()->SetLocalPosition(origin + Canape::Vector3{ 0.0f, 0.7f, 0.0f });
        rig.water->GetTransform()->SetLocalScale({ 1.6f, 1.0f, 1.0f });
        rig.volume = rig.water->AddComponent<Canape::WaterVolumeComponent>();
        return rig;
    }

    // A fresh scene with the rig far from the lanes.
    bool OpenWaterRun(Harness& h, WaterRig& rig)
    {
        const bool opened = h.Reload(PuzzleMode::Solution) != nullptr;
        Check(opened, "a scene opens for a water run");
        if(opened)
        {
            rig = BuildWaterRig(h.Active(), { 300.0f, 50.0f, 0.0f });
        }
        return opened;
    }

    bool RunWaterTo(Harness& h, std::uint32_t tick)
    {
        return h.RunUntil([&] { return h.Front()->GetTick() >= tick; });
    }

    constexpr float FloatingHeight = 1.255f;
    constexpr float RestingHeight = 0.4f;
    constexpr std::uint32_t WaterSettleTick = 60;
    constexpr std::uint32_t WaterFallTick = 15;

    bool IsFloating(const WaterRig& rig) { return std::fabs(rig.Height() - FloatingHeight) < 0.05f; }
    bool HasSunk(const WaterRig& rig) { return rig.Height() < RestingHeight + 0.2f; }

    void CheckWaterReflection()
    {
        const rttr::type type = rttr::type::get<Canape::WaterVolumeComponent>();
        std::set<std::string> registered;
        for(const rttr::property& property : type.get_properties())
        {
            registered.insert(property.get_name().to_string());
        }
        const std::set<std::string> expected{ "enabled", "size", "density", "linearDrag", "angularDrag" };
        Check(registered == expected, "WaterVolumeComponent registers exactly its authored properties and no runtime state");
        Check(type.get_constructor().is_valid(), "WaterVolumeComponent can be created by name");
        const Canape::WaterVolumeComponent defaults;
        Check(defaults.GetDensity() == 1000.0f && defaults.GetLinearDrag() == 0.05f && defaults.GetAngularDrag() == 0.01f
            && defaults.GetSize().X == 1.0f && defaults.GetSize().Y == 1.0f && defaults.GetSize().Z == 1.0f,
            "a default water volume is a unit box of 1000 kg/m^3");
    }

    // The water holds the buoy up, and only while it is enabled, valid and under the buoy.
    void CheckWaterBuoyancy(Harness& h)
    {
        WaterRig rig;
        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        Check(RunWaterTo(h, WaterSettleTick), "the water run reaches its settle tick");
        Check(IsFloating(rig), "the buoy floats on the water at " + std::to_string(rig.Height()));
        Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "a floating buoy is no issue");

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetEnabled(false);
        Check(RunWaterTo(h, WaterSettleTick), "the run with disabled water reaches its settle tick");
        Check(HasSunk(rig), "without water the buoy sinks to the floor at " + std::to_string(rig.Height()));
        Check(RunWaterTo(h, 2 * WaterSettleTick), "the run with a sleeping buoy goes on");
        rig.volume->SetEnabled(true);
        Check(RunWaterTo(h, 4 * WaterSettleTick), "the run with water enabled for a sleeping buoy goes on");
        Check(rig.Height() > FloatingHeight - 0.2f, "enabling the water wakes the sleeping buoy and lifts it to " + std::to_string(rig.Height()));

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetEnabled(false);
        Check(RunWaterTo(h, WaterFallTick), "the run with disabled water reaches the fall tick");
        rig.volume->SetEnabled(true);
        Check(RunWaterTo(h, WaterFallTick + 150), "the run with water enabled again goes on");
        Check(rig.Height() > FloatingHeight - 0.2f, "water enabled again lifts the awake buoy at " + std::to_string(rig.Height()));

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.water->SetActive(false);
        Check(RunWaterTo(h, WaterSettleTick), "the run with an inactive water object reaches its settle tick");
        Check(HasSunk(rig), "the water of an inactive object does nothing at " + std::to_string(rig.Height()));
        Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "inactive water is no issue");
    }

    // Authoring values apply from the next step; an invalid volume is ignored and is no run failure.
    void CheckWaterValues(Harness& h)
    {
        struct Case {
            const char* what;
            std::function<void(Canape::WaterVolumeComponent&)> apply;
        };
        const float notANumber = std::numeric_limits<float>::quiet_NaN();
        const std::vector<Case> invalid = {
            { "a zero density", [](Canape::WaterVolumeComponent& water) { water.SetDensity(0.0f); } },
            { "a negative density", [](Canape::WaterVolumeComponent& water) { water.SetDensity(-1000.0f); } },
            { "a NaN density", [&](Canape::WaterVolumeComponent& water) { water.SetDensity(notANumber); } },
            { "a negative linear drag", [](Canape::WaterVolumeComponent& water) { water.SetLinearDrag(-0.1f); } },
            { "a NaN angular drag", [&](Canape::WaterVolumeComponent& water) { water.SetAngularDrag(notANumber); } },
            { "a zero size", [](Canape::WaterVolumeComponent& water) { water.SetSize({ 0.0f, 1.0f, 1.0f }); } },
            { "a NaN size", [&](Canape::WaterVolumeComponent& water) { water.SetSize({ 1.0f, notANumber, 1.0f }); } },
        };
        for(const Case& item : invalid)
        {
            WaterRig rig;
            if(!OpenWaterRun(h, rig))
            {
                return;
            }
            item.apply(*rig.volume);
            Check(RunWaterTo(h, WaterSettleTick), std::string("the run with ") + item.what + " reaches its settle tick");
            Check(HasSunk(rig), std::string("water with ") + item.what + " is ignored");
            Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, std::string("water with ") + item.what + " is no issue");
        }

        // A volume that turns valid again works again, and the invalid phase is not remembered as a failure.
        WaterRig rig;
        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetDensity(0.0f);
        Check(RunWaterTo(h, WaterFallTick), "the run with a zero density falls");
        Check(!HasSunk(rig) && rig.Height() < FloatingHeight - 0.1f, "the buoy falls under invalid water at " + std::to_string(rig.Height()));
        rig.volume->SetDensity(1000.0f);
        Check(RunWaterTo(h, WaterFallTick + 150), "the run with the density fixed goes on");
        Check(rig.Height() > FloatingHeight - 0.2f, "fixing the density lifts the buoy at " + std::to_string(rig.Height()));
        Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "an invalid phase leaves no issue");

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetDensity(100.0f);
        Check(RunWaterTo(h, WaterSettleTick), "the run with a thin water settles");
        Check(HasSunk(rig), "a water lighter than the buoy lets it sink at " + std::to_string(rig.Height()));

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetDensity(2000.0f);
        Check(RunWaterTo(h, 10), "the run with a dense water reaches tick 10");
        Check(rig.Height() > FloatingHeight + 0.02f, "a denser water lifts the buoy at " + std::to_string(rig.Height()));
    }

    // The box is its size times the Transform scale, centered on the Transform position; rotation is ignored.
    void CheckWaterBox(Harness& h)
    {
        WaterRig rig;
        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetSize({ 0.1f, 1.0f, 1.0f });
        Check(RunWaterTo(h, WaterSettleTick), "the run with a narrow water settles");
        Check(HasSunk(rig), "a water box that misses the buoy does nothing at " + std::to_string(rig.Height()));

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.volume->SetSize({ 0.5f, 1.0f, 1.0f });
        Check(RunWaterTo(h, WaterSettleTick), "the run with a half-width water settles");
        Check(IsFloating(rig), "a water box that still holds the buoy carries it at " + std::to_string(rig.Height()));

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        Canape::TransformComponent* transform = rig.water->GetTransform();
        transform->SetLocalPosition(transform->GetLocalPosition() + Canape::Vector3{ 0.0f, 0.3f, 0.0f });
        Check(RunWaterTo(h, WaterSettleTick + 60), "the run with raised water settles");
        Check(rig.Height() > FloatingHeight + 0.15f, "water raised by 0.3 m raises the buoy at " + std::to_string(rig.Height()));

        if(!OpenWaterRun(h, rig))
        {
            return;
        }
        rig.water->GetTransform()->SetLocalRotation(Canape::Rotator(0.0f, 0.0f, 30.0f).ToQuaternion());
        Check(RunWaterTo(h, WaterSettleTick), "the run with rotated water settles");
        Check(IsFloating(rig), "a rotated water volume acts as the axis-aligned box and floats the buoy at " + std::to_string(rig.Height()));
    }

    // Water goes away with its component or its object, inside an Update, without a failure.
    void CheckWaterRemoval(Harness& h)
    {
        for(const bool wholeObject : { false, true })
        {
            WaterRig rig;
            if(!OpenWaterRun(h, rig))
            {
                return;
            }
            auto* remover = rig.floor->AddComponent<RemoveOnce>();
            Check(RunWaterTo(h, 10), "the run reaches the tick of the removal");
            if(wholeObject)
            {
                remover->Arm(rig.water->GetHandle());
            }
            else
            {
                remover->ArmComponent(rig.volume);
            }
            Check(RunWaterTo(h, WaterSettleTick), "the run after the removal reaches its settle tick");
            Check(HasSunk(rig), std::string("the buoy sinks once the water ") + (wholeObject ? "object" : "component") + " is removed at "
                + std::to_string(rig.Height()));
            Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "removing the water is no issue");
        }
    }

    // Changing a water volume wakes the bodies sleeping inside it once; water that stays as it is never does.
    void CheckWaterWake(Harness& h)
    {
        struct Case {
            const char* what;
            bool startsDisabled;
            bool lifts;
            std::function<void(WaterRig&)> apply;
        };
        const std::vector<Case> cases = {
            { "a denser water", false, true, [](WaterRig& rig) { rig.volume->SetDensity(1000.0f); } },
            { "water that becomes enabled", true, true, [](WaterRig& rig) { rig.volume->SetEnabled(true); } },
            { "a stronger linear drag", false, false, [](WaterRig& rig) { rig.volume->SetLinearDrag(2.0f); } },
            { "a stronger angular drag", false, false, [](WaterRig& rig) { rig.volume->SetAngularDrag(2.0f); } },
            { "a wider water box", false, false, [](WaterRig& rig) { rig.volume->SetSize(rig.volume->GetSize() + Canape::Vector3{ 0.2f, 0.0f, 0.0f }); } },
            { "water moved up", false, false, [](WaterRig& rig)
                {
                    Canape::TransformComponent* transform = rig.water->GetTransform();
                    transform->SetLocalPosition(transform->GetLocalPosition() + Canape::Vector3{ 0.0f, 0.1f, 0.0f });
                } },
        };

        for(const Case& item : cases)
        {
            const std::string what = item.what;
            WaterRig rig;
            if(!OpenWaterRun(h, rig))
            {
                return;
            }
            if(item.startsDisabled)
            {
                rig.volume->SetEnabled(false);
            }
            else
            {
                rig.volume->SetDensity(100.0f);
            }
            const auto* body = rig.buoy->GetComponent<Canape::RigidBodyComponent>();
            Check(RunWaterTo(h, 2 * WaterSettleTick), "the run reaches the tick of the water change");
            Check(HasSunk(rig) && !body->IsAwake(), "the buoy sleeps on the floor before " + what);

            Check(RunWaterTo(h, 3 * WaterSettleTick), "the run with unchanged water goes on");
            Check(!body->IsAwake(), "water that does not change leaves the sleeping buoy asleep before " + what);

            item.apply(rig);
            h.Frame();
            Check(body->IsAwake(), what + " wakes the sleeping buoy");

            Check(RunWaterTo(h, 4 * WaterSettleTick + 150), "the run after the water change goes on");
            if(item.lifts)
            {
                Check(rig.Height() > FloatingHeight - 0.2f, what + " lifts the woken buoy at " + std::to_string(rig.Height()));
            }
            else
            {
                Check(HasSunk(rig) && !body->IsAwake(), "the buoy is woken once and sleeps again after " + what);
            }
            Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "a water change is no issue");
        }
    }

    // A body the water held up must not stay asleep in mid-air once the water is gone: it is woken and falls.
    void CheckWaterWithdrawal(Harness& h)
    {
        enum class Withdraw : std::uint8_t {
            Keep,
            Disable,
            MoveAway,
            RemoveComponent,
            RemoveObject,
        };
        struct Case {
            const char* what;
            Withdraw how;
        };
        const Case cases[] = {
            { "water that stays", Withdraw::Keep },
            { "disabled water", Withdraw::Disable },
            { "water moved away", Withdraw::MoveAway },
            { "a removed water component", Withdraw::RemoveComponent },
            { "a removed water object", Withdraw::RemoveObject },
        };

        for(const Case& item : cases)
        {
            using enum Withdraw;
            const std::string what = item.what;
            WaterRig rig;
            if(!OpenWaterRun(h, rig))
            {
                return;
            }
            auto* remover = rig.floor->AddComponent<RemoveOnce>();
            const auto* body = rig.buoy->GetComponent<Canape::RigidBodyComponent>();
            Check(RunWaterTo(h, 2 * WaterSettleTick), "the run reaches the tick of the water withdrawal");
            Check(IsFloating(rig) && !body->IsAwake(), "the buoy sleeps afloat before " + what);

            switch(item.how)
            {
            case Disable:
                rig.volume->SetEnabled(false);
                break;
            case MoveAway:
                rig.water->GetTransform()->SetLocalPosition(rig.water->GetTransform()->GetLocalPosition() + Canape::Vector3{ 40.0f, 0.0f, 0.0f });
                break;
            case RemoveComponent:
                remover->ArmComponent(rig.volume);
                break;
            case RemoveObject:
                remover->Arm(rig.water->GetHandle());
                break;
            default:
                break;
            }
            h.Frame();
            h.Frame();
            if(item.how == Keep)
            {
                Check(!body->IsAwake(), "water that stays leaves the buoy asleep");
            }
            else
            {
                Check(body->IsAwake(), what + " wakes the buoy that it held up");
            }

            Check(RunWaterTo(h, 2 * WaterSettleTick + 150), "the run after the water withdrawal goes on");
            if(item.how == Keep)
            {
                Check(IsFloating(rig), "the buoy stays afloat in water that stays at " + std::to_string(rig.Height()));
            }
            else
            {
                Check(HasSunk(rig), "the buoy falls after " + what + " at " + std::to_string(rig.Height()));
            }
            Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "withdrawing the water is no issue");
        }
    }

    // BuoyPop: the popped buoy touches the zone over the hoop in flight and the lane is solved although the buoy is back
    // in the water at the horizon; a rotated water volume acts as the axis-aligned box and gives the shipped verdicts.
    void CheckBuoyPop(Harness& h, const std::vector<Verdict>& solution)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for BuoyPop");
        Canape::Scene& scene = h.Active();
        Canape::GameObject* root = scene.GetGameObject(scene.FindGameObjectByName("BuoyPop"));
        PuzzleLane* lane = root ? root->GetComponent<PuzzleLane>() : nullptr;
        auto* zone = lane ? dynamic_cast<GoalZone*>(h.Goal(*lane)) : nullptr;
        Canape::GameObject* waterObject = scene.GetGameObject(scene.FindGameObjectByName("water"));
        Check(lane && zone && waterObject && waterObject->GetComponent<Canape::WaterVolumeComponent>(), "BuoyPop has its lane, zone and water");
        if(!lane || !zone || !waterObject)
        {
            return;
        }
        const std::optional<GoalRegion> region = zone->GetRegion();
        Check(region && Near(region->center, root->GetTransform()->GetPosition() + Canape::Vector3{ -0.3f, 2.15f, 0.0f }, 1.0e-4f)
            && Near(region->halfExtent, { 0.25f, 0.05f, 0.25f }, 1.0e-5f), "the zone hangs 0.45 m over the hoop");
        Check(h.RunUntil([&] { return lane->GetTick() >= 100; }), "the run reaches the pop");
        Check(!zone->IsSatisfied(), "the zone is not met before the pop");
        Check(h.RunUntil([&] { return lane->GetTick() >= 160; }), "the run passes the pop");
        Check(zone->IsSatisfied(), "the popped buoy touches the zone");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the BuoyPop run reaches its verdict");
        Check(lane->IsSolved() && !zone->IsShownInside(), "BuoyPop is solved with the buoy back out of the zone");

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for rotated water");
        Canape::Scene& rotatedScene = h.Active();
        Canape::GameObject* rotatedWater = rotatedScene.GetGameObject(rotatedScene.FindGameObjectByName("water"));
        Check(rotatedWater != nullptr, "the rotated-water scene has its water");
        if(!rotatedWater)
        {
            return;
        }
        rotatedWater->GetTransform()->SetLocalRotation(Canape::Rotator(0.0f, 0.0f, 30.0f).ToQuaternion());
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run with rotated water reaches its verdict");
        CheckSame("rotated water", solution, h.Verdicts());
    }

    // I toggles the stats log, L logs the lanes; neither touches the run.
    void CheckStatsLog(Harness& h)
    {
        const rttr::type type = rttr::type::get<PhysicsStatsLog>();
        Check(type.get_property("toggleKey").is_valid() && !type.get_property("logging").is_valid(), "PhysicsStatsLog registers its key and no run state");
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the stats log");
        Canape::Scene& scene = h.Active();
        const Canape::GameObject* object = scene.GetGameObject(scene.FindGameObjectByName("Physics Stats"));
        const auto* log = object ? object->GetComponent<PhysicsStatsLog>() : nullptr;
        Check(log != nullptr, "the playground has a stats log object");
        if(!log)
        {
            return;
        }

        h.Frame();
        Check(!log->IsLogging(), "the stats log starts off");
        h.Frame({ Canape::Key::I });
        Check(log->IsLogging(), "I turns the stats log on");
        for(int i = 0; i < 70; ++i)
        {
            h.Frame();
        }
        h.Frame({ Canape::Key::L });
        Check(&h.Active() == &scene && log->IsLogging(), "L logs the lanes and changes nothing");
        h.Frame({ Canape::Key::I });
        Check(!log->IsLogging(), "I turns the stats log off");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run with the stats log toggled reaches its verdict");
        Check(h.Active().GetPhysics().GetAuthoringIssueCount() == 0, "the stats log is no issue");
    }

    void CheckWater(Harness& h)
    {
        CheckWaterReflection();
        CheckWaterBuoyancy(h);
        CheckWaterValues(h);
        CheckWaterBox(h);
        CheckWaterRemoval(h);
        CheckWaterWake(h);
        CheckWaterWithdrawal(h);
    }

    struct HingeLanes {
        PuzzleLane* swing = nullptr;
        PuzzleLane* lever = nullptr;
        Canape::GameObject* arm = nullptr;
        Canape::RigidBodyComponent* armBody = nullptr;
        Canape::HingeComponent* swingHinge = nullptr;
        Canape::HingeComponent* leverHinge = nullptr;
        HingeRelease* release = nullptr;
        Canape::GameObject* bob = nullptr;
        Canape::RigidBodyComponent* bobBody = nullptr;
        Canape::GameObject* rod = nullptr;
    };

    // The hinged lanes of the active scene, by object name; every part is needed.
    HingeLanes FindHingeLanes(Harness& h)
    {
        Canape::Scene& scene = h.Active();
        const auto object = [&](const char* name) { return scene.GetGameObject(scene.FindGameObjectByName(name)); };
        HingeLanes lanes;
        if(Canape::GameObject* root = object("SwingCut"))
        {
            lanes.swing = root->GetComponent<PuzzleLane>();
        }
        if(Canape::GameObject* root = object("LeverCatapult"))
        {
            lanes.lever = root->GetComponent<PuzzleLane>();
        }
        if((lanes.arm = object("arm")))
        {
            lanes.armBody = lanes.arm->GetComponent<Canape::RigidBodyComponent>();
            lanes.swingHinge = lanes.arm->GetComponent<Canape::HingeComponent>();
            lanes.release = lanes.arm->GetComponent<HingeRelease>();
        }
        if(Canape::GameObject* lever = object("lever"))
        {
            lanes.leverHinge = lever->GetComponent<Canape::HingeComponent>();
        }
        if((lanes.bob = object("bob")))
        {
            lanes.bobBody = lanes.bob->GetComponent<Canape::RigidBodyComponent>();
        }
        lanes.rod = object("rod");
        return lanes;
    }

    bool HasHingeLanes(const HingeLanes& lanes)
    {
        return lanes.swing && lanes.lever && lanes.arm && lanes.armBody && lanes.swingHinge && lanes.leverHinge && lanes.release
            && lanes.bob && lanes.bobBody && lanes.rod;
    }

    bool IsRodShown(const HingeLanes& lanes)
    {
        const auto* renderer = lanes.rod->GetComponent<Canape::MeshRendererComponent>();
        return renderer && renderer->IsEnabled();
    }

    // The bob is a child of the arm with its own body off, so its collider belongs to the arm's body.
    bool RidesOnArm(const HingeLanes& lanes)
    {
        return !lanes.bobBody->IsEnabled() && !lanes.bobBody->HasBody() && lanes.bob->GetTransform()->GetParent() == lanes.arm->GetTransform();
    }

    // The shipped hinges are made on the arms at their pivots; the bob rides in the arm's body and the rod is shown.
    void CheckHingeLanes(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge lanes");
        h.Frame();
        const HingeLanes lanes = FindHingeLanes(h);
        Check(HasHingeLanes(lanes), "SwingCut and LeverCatapult have their lanes, arms, hinges, release, bob and rod");
        if(!HasHingeLanes(lanes))
        {
            return;
        }

        const Canape::PhysicsScene& physics = h.Active().GetPhysics();
        const Canape::Vector3 swingRoot = lanes.swing->GetTransform()->GetPosition();
        const Canape::Vector3 leverRoot = lanes.lever->GetTransform()->GetPosition();
        Check(lanes.swingHinge->IsHeld() && Near(lanes.swingHinge->GetWorldAnchor(), swingRoot + Canape::Vector3{ -0.25f, 3.2f, 0.0f }, 1.0e-4f),
            "the swing arm hangs from the bottom of the crossbar");
        Check(lanes.leverHinge->IsHeld() && Near(lanes.leverHinge->GetWorldAnchor(), leverRoot + Canape::Vector3{ 0.3f, 0.75f, 0.0f }, 1.0e-4f),
            "the lever turns on top of the stand");
        Check(lanes.leverHinge->GetMinAngle() == 0.0f && std::fabs(lanes.leverHinge->GetMaxAngle() - 0.6981317f) < 1.0e-6f, "the lever turns from 0 to 40 degrees");
        Check(RidesOnArm(lanes), "the bob rides in the arm's body with its own body off");
        const Canape::Vector3 bobPosition = lanes.bob->GetTransform()->GetPosition();
        Check(Near(bobPosition, swingRoot + Canape::Vector3{ -1.3224622f, 2.3000972f, 0.0f }, 0.05f), "the bob starts 1.4 m from the pivot, 50 degrees up");
        const Canape::RigidBodies atBob = physics.QueryBox(bobPosition, { 0.01f, 0.01f, 0.01f });
        Check(atBob.size() == 1 && atBob.front() == lanes.armBody, "a query at the bob finds the arm's body");
        Check(!lanes.release->IsApplied() && lanes.release->GetTick(PuzzleMode::Solution) == 50 && lanes.release->GetTick(PuzzleMode::Control) == 37,
            "the release waits for tick 50 in Solution and 37 in Control");
        const Canape::Vector3 scale = lanes.rod->GetTransform()->GetLocalScale();
        Check(IsRodShown(lanes) && scale.X == 0.03f && scale.Y == 1.4f && scale.Z == 0.03f, "the rod is shown at its authored size");
    }

    // Records every fixed tick, against the lane tick, whether the arm is held, whether the release fired and where the bob is.
    class ReleaseProbe final : public Canape::ScriptComponent {
    public:
        struct Sample {
            std::uint32_t laneTick = 0;
            bool held = false;
            bool applied = false;
            bool bobHasBody = false;
            Canape::Vector3 bobPosition{};
        };

        void Arm(const HingeLanes& lanes, std::uint32_t lastTick)
        {
            m_Lanes = lanes;
            m_LastTick = lastTick;
        }

        bool IsDone() const { return m_Done; }

        const Sample* At(std::uint32_t laneTick) const
        {
            const auto found = std::ranges::find(m_Samples, laneTick, &Sample::laneTick);
            return found == m_Samples.end() ? nullptr : &*found;
        }

    protected:
        void FixedUpdate() override
        {
            if(m_Done || !m_Lanes.swing)
            {
                return;
            }
            m_Samples.push_back({ m_Lanes.swing->GetTick(), m_Lanes.swingHinge->IsHeld(), m_Lanes.release->IsApplied(), m_Lanes.bobBody->HasBody(),
                m_Lanes.bob->GetTransform()->GetPosition() });
            m_Done = m_Lanes.swing->GetTick() >= m_LastTick;
        }

    private:
        HingeLanes m_Lanes;
        std::uint32_t m_LastTick = 0;
        std::vector<Sample> m_Samples;
        bool m_Done = false;
    };

    // The release fires at its mode's tick: the bob rides at that lane tick and flies on its own body, at the arm's
    // speed, from the next; the arm is hidden and its body dropped.
    void CheckHingeReleaseTicks(Harness& h, const std::vector<Verdict>& solution, const std::vector<Verdict>& control)
    {
        for(const PuzzleMode mode : { PuzzleMode::Solution, PuzzleMode::Control })
        {
            const bool isSolution = mode == PuzzleMode::Solution;
            const std::uint32_t tick = isSolution ? 50 : 37;
            const std::string what = std::string(isSolution ? "Solution" : "Control") + " release";
            Check(h.Reload(mode) != nullptr, what + ": a scene opens");
            const HingeLanes lanes = FindHingeLanes(h);
            if(!HasHingeLanes(lanes))
            {
                Check(false, what + ": the hinge lanes are missing");
                continue;
            }
            Canape::Scene& scene = h.Active();
            auto* probe = scene.GetGameObject(scene.AddGameObject("releaseProbe"))->AddComponent<ReleaseProbe>();
            probe->Arm(lanes, tick + 3);
            Check(h.RunUntil([&] { return probe->IsDone(); }), what + ": the probe runs past the release");
            // The probe may run after the director in a tick, so the body made at that tick's sync shows a sample later.
            const ReleaseProbe::Sample* before = probe->At(tick);
            const ReleaseProbe::Sample* after = probe->At(tick + 1);
            const ReleaseProbe::Sample* made = probe->At(tick + 2);
            const ReleaseProbe::Sample* later = probe->At(tick + 3);
            Check(before && before->held && !before->applied && !before->bobHasBody, what + ": the bob rides on the held arm at the release tick");
            Check(after && !after->held && after->applied, what + ": the arm is let go and the release applied one tick later");
            Check(made && made->bobHasBody, what + ": the bob has its own body from the release tick's step on");
            Check(after && later && (later->bobPosition - after->bobPosition).Length() > 0.05f,
                what + ": the bob keeps the arm's speed (" + std::to_string(after && later ? (later->bobPosition - after->bobPosition).Length() : 0.0f) + " m in two ticks)");
            h.Frame();
            Check(!IsRodShown(lanes) && !lanes.armBody->IsEnabled() && !lanes.armBody->HasBody(), what + ": the arm is hidden and its body dropped");
            Check(lanes.bob->GetTransform()->GetParent() == lanes.swing->GetTransform() && lanes.bobBody->IsEnabled(), what + ": the bob is handed to the lane");

            Check(h.RunToVerdict(mode), what + ": the run reaches its verdict");
            CheckSame(what.c_str(), isSolution ? solution : control, h.Verdicts());
            Check(lanes.release->IsApplied() && !lanes.swingHinge->IsHeld() && lanes.swingHinge->GetIssue() == Canape::HingeIssue::None,
                what + ": the released hinge stays released to the verdict without an issue");
            Check(lanes.leverHinge->IsHeld() && lanes.leverHinge->GetAngle() >= -1.0e-3f && lanes.leverHinge->GetAngle() <= 0.6981317f + 1.0e-3f,
                what + ": the lever stays on its hinge within its limits");
            Check(scene.GetPhysics().GetAuthoringIssueCount() == 0, what + ": the hinged lanes leave no authoring issue");
        }
    }

    void SetReleaseTicks(HingeRelease& release, int solution, int control)
    {
        const rttr::type type = rttr::type::get<HingeRelease>();
        Check(type.get_property("solutionTick").set_value(release, solution) && type.get_property("controlTick").set_value(release, control),
            "the release ticks are set through reflection");
    }

    // A tick of -1 or a disabled release leaves the arm and the bob alone.
    void CheckHingeReleaseRules(Harness& h)
    {
        for(int variant = 0; variant < 2; ++variant)
        {
            const std::string what = variant == 0 ? "a release of -1" : "a disabled release";
            Check(h.Reload(PuzzleMode::Solution) != nullptr, what + ": a scene opens");
            const HingeLanes lanes = FindHingeLanes(h);
            if(!HasHingeLanes(lanes))
            {
                Check(false, what + ": the hinge lanes are missing");
                continue;
            }
            if(variant == 0)
            {
                SetReleaseTicks(*lanes.release, -1, -1);
            }
            else
            {
                lanes.release->SetEnabled(false);
            }
            Check(h.RunUntil([&] { return lanes.swing->GetTick() >= 120; }), what + ": the run passes the release tick");
            Check(lanes.swingHinge->IsHeld() && !lanes.release->IsApplied() && RidesOnArm(lanes) && IsRodShown(lanes), what + " keeps the bob on the arm past the release tick");
        }
    }

    // A restart in the frame of the release starts the new run with the bob on the arm and gives the same verdicts.
    void CheckRestartOnReleaseTick(Harness& h, const std::vector<Verdict>& solution, const std::vector<Verdict>& control)
    {
        struct Case {
            PuzzleMode from;
            Canape::Key key;
            PuzzleMode to;
            std::uint32_t pressTick;
            const std::vector<Verdict>* expected;
            const char* what;
        };
        const Case cases[] = {
            { PuzzleMode::Solution, Canape::Key::C, PuzzleMode::Control, 48, &control, "C in the release frame" },
            { PuzzleMode::Control, Canape::Key::R, PuzzleMode::Solution, 36, &solution, "R in the release frame" },
        };
        for(const Case& c : cases)
        {
            const std::string what = c.what;
            Check(h.Reload(c.from) != nullptr, what + ": a scene opens");
            Check(h.RunUntil([&] { return h.Front()->GetTick() >= c.pressTick; }), what + ": the run reaches the release frame");
            const Canape::Scene* old = &h.Active();
            h.Frame({ c.key });
            h.Frame();
            Check(&h.Active() != old, what + ": the key reloads the playground");
            const HingeLanes lanes = FindHingeLanes(h);
            if(!HasHingeLanes(lanes))
            {
                Check(false, what + ": the hinge lanes are missing");
                continue;
            }
            Check(lanes.swing->GetMode() == c.to && lanes.swing->GetTick() <= 2 * TicksPerCappedFrame, what + ": the new run starts over in the other mode");
            Check(!lanes.release->IsApplied() && lanes.swingHinge->IsHeld() && RidesOnArm(lanes) && IsRodShown(lanes), what + ": the new run starts with the bob on the arm");
            Check(h.RunToVerdict(c.to), what + ": the new run reaches its verdict");
            CheckSame(c.what, *c.expected, h.Verdicts());
        }
    }

    // The goal materials survive the Delete-key asset collection, in a solved scene and in the one the R key reloads.
    void CheckGoalAfterCollection(Harness& h, const std::vector<Verdict>& solution)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the goal materials");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run for the goal materials reaches its verdict");
        CheckGoalLook(h, "goal before the collection", true);
        CheckGoalColors(h, "goal before the collection", true);
        h.Frame({ Canape::Key::Delete });
        CheckGoalLook(h, "goal after the collection", true);
        CheckGoalColors(h, "goal after the collection", true);

        h.Frame({ Canape::Key::R });
        CheckGoalLook(h, "goal after R", false);
        CheckGoalColors(h, "goal after R", false);
        h.Frame({ Canape::Key::Delete });
        CheckGoalLook(h, "goal after R and the collection", false);
        CheckGoalColors(h, "goal after R and the collection", false);
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run after the collection reaches its verdict");
        CheckSame("after the asset collection", solution, h.Verdicts());
        CheckGoalLook(h, "goal solved again", true);
        CheckGoalColors(h, "goal solved again", true);
        h.Frame({ Canape::Key::Delete });
        CheckGoalColors(h, "goal solved again after the collection", true);
    }

    // P leaves for SampleScene through SceneChanger; the playground opened again afterwards starts a clean run.
    void CheckSceneSwitch(Harness& h, const std::vector<Verdict>& solution, const std::vector<Verdict>& control)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the scene switch");
        for(int i = 0; i < 20; ++i)
        {
            h.Frame();
        }
        h.mostScenes = 0;
        h.Frame({ Canape::Key::P });
        h.Frame();
        Check(h.scenes.GetActiveScene() != nullptr && h.Active().GetName() == "SampleScene", "P in the playground opens SampleScene");
        CheckOneScene(h, "after the switch to SampleScene");
        for(int i = 0; i < 10; ++i)
        {
            h.Frame();
        }

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "the playground opens again after SampleScene");
        CheckOneScene(h, "after the switch back to the playground");
        Check(h.Front() != nullptr && h.Front()->GetMode() == PuzzleMode::Solution && h.Front()->GetTick() <= TicksPerCappedFrame,
            "the playground that comes back starts a new run");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the playground after the round trip reaches its verdict");
        CheckSame("after SampleScene", solution, h.Verdicts());
        CheckGoalLook(h, "after SampleScene", true);

        Check(h.RunToVerdict(PuzzleMode::Control), "the automatic switch works after the round trip");
        CheckSame("control after SampleScene", control, h.Verdicts());
        CheckOneScene(h, "after the round trip and the automatic switch");
    }

    // Results do not depend on how many fixed ticks a frame runs.
    void CheckBatching(Harness& h, const std::vector<Verdict>& solution, const std::vector<Verdict>& control)
    {
        for(const double ticksPerFrame : { SmallBatchTicks, LargeBatchTicks })
        {
            const std::string batching = ticksPerFrame == SmallBatchTicks ? "small batches" : "large batches";
            h.SetMixedBatches(ticksPerFrame);
            Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the batching run");
            Check(h.RunToVerdict(PuzzleMode::Solution), "the batched solution run reaches its verdict");
            CheckSame((batching + " solution").c_str(), solution, h.Verdicts());
            h.Frame({ Canape::Key::C });
            Check(h.RunToVerdict(PuzzleMode::Control), "the batched control run reaches its verdict");
            CheckSame((batching + " control").c_str(), control, h.Verdicts());
            h.CheckMixedBatches(batching.c_str());
        }
        h.SetScale(FourTickScale);
    }

    void RunChecks(ValidationApp& app, const std::filesystem::path& gameDirectory)
    {
        Canape::AssetManager::SetAssetRoot(gameDirectory);
        Canape::SceneManager& scenes = app.GetSceneManager();
        scenes.SetSceneDirectory((gameDirectory / "Content" / "Scenes").string() + "/");
        Harness h{ app, scenes };
        h.SetScale(FourTickScale);

        Canape::Scene* first = scenes.OpenScene(SceneName);
        Check(first != nullptr, "the playground opens from its JSON file");
        if(!first)
        {
            return;
        }
        Check(scenes.GetActiveScene() == first, "the opened playground is the active scene");
        Check(h.Front() != nullptr, "the playground has a lane with content");
        if(!h.Front())
        {
            return;
        }

        // The startup run; the scene stays until the rest ticks end.
        Check(h.RunToVerdict(PuzzleMode::Solution), "the first solution run reaches its verdict");
        Check(&h.Active() == first, "the scene stays loaded until the rest ticks end");
        const auto solutionA = h.Verdicts();
        CheckSolution("solution A", solutionA);
        Check(h.Steps() >= PuzzleHorizonTicks, "the physics world stepped every lane tick");
        CheckQueries(h);
        CheckGoalLook(h, "solution A", true);
        CheckStepAccounting(h);
        CheckOneScene(h, "solution A");

        // The rest ticks end in a reload in the other mode, then in the first mode again.
        Check(h.RunToVerdict(PuzzleMode::Control), "the automatic switch reaches the control run");
        Check(&h.Active() != first, "the automatic switch loaded a new scene");
        CheckOneScene(h, "after the automatic switch");
        const auto controlA = h.Verdicts();
        CheckControl("control A", controlA);
        CheckGoalLook(h, "control A", false);
        Check(h.RunToVerdict(PuzzleMode::Solution), "the automatic switch comes back to the solution run");
        CheckSame("automatic restart", solutionA, h.Verdicts());
        CheckOneScene(h, "after the second automatic switch");

        // R twice in one frame during the rest ticks; one reload, whatever the key count.
        Check(h.AllJudged(), "the lanes are resting");
        const Canape::Scene* resting = &h.Active();
        h.Frame({ Canape::Key::R, Canape::Key::R });
        Check(&h.Active() != resting && h.AllInMode(PuzzleMode::Solution), "R reloads the playground");
        CheckOneScene(h, "after R");
        CheckGoalLook(h, "R after a solved run", false);
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run after R reaches its verdict");
        CheckSame("R", solutionA, h.Verdicts());

        // The rest ticks end in the same frame as an R: the automatic switch wins and the R is dropped.
        Check(h.RunUntil([&] { return h.Director()->GetRestTicks() + TicksPerCappedFrame >= PuzzleRestTicks; }), "the rest ticks run to the last frame");
        Check(h.Director()->GetRestTicks() < PuzzleRestTicks, "the rest is short of the switch");
        const Canape::Scene* nearlyDone = &h.Active();
        h.Frame({ Canape::Key::R });
        Check(&h.Active() != nearlyDone, "the coinciding frame reloads the playground");
        CheckOneScene(h, "after the automatic switch and R in one frame");
        Check(h.Director()->GetStartMode() == PuzzleMode::Control, "the automatic switch wins over the R of the same frame");
        Check(h.RunToVerdict(PuzzleMode::Control), "the run after the coinciding frame reaches its verdict");
        CheckOneScene(h, "the run after the automatic switch and R in one frame");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the automatic switch comes back to the solution run");

        // C during the rest ticks.
        h.Frame({ Canape::Key::C });
        CheckOneScene(h, "after C");
        Check(h.RunToVerdict(PuzzleMode::Control), "the run after C reaches its verdict");
        CheckSame("C", controlA, h.Verdicts());

        // R in the middle of a run restarts it at tick 0; the lane tick follows the steps one to one.
        h.Frame({ Canape::Key::R });
        Check(h.RunUntil([&] { return h.Front()->GetTick() >= 100; }), "a run is under way");
        Check(h.Steps() == h.Front()->GetTick(), "the lane tick equals the steps taken");
        const Canape::Scene* running = &h.Active();
        h.Frame({ Canape::Key::R });
        Check(&h.Active() != running && h.Front()->GetTick() == 0, "R in the middle of a run starts over");
        CheckOneScene(h, "after R in a run");
        Check(h.RunToVerdict(PuzzleMode::Solution), "the run after the mid-run R reaches its verdict");
        CheckSame("R in a run", solutionA, h.Verdicts());

        CheckBatching(h, solutionA, controlA);

        CheckKeyOrder();
        CheckStartSnapshot(h, solutionA);
        CheckCameraHome(h);
        CheckCameraCarry(h);

        // The JSON file survives load -> save -> load.
        CheckRoundTrip(h, gameDirectory / "Content" / "Scenes" / (std::string(SceneName) + ".json"), solutionA, controlA);

        CheckFixedTickSync(h);
        CheckAddedBodySync(h);
        CheckColliderRemovalSync(h);
        CheckVerdictFinality(h);
        CheckDisabledRunnables(h);
        CheckPhysicsFailure(h);
        CheckQueuedImpulse(h);
        CheckBodyValueValidation(h);
        CheckCupRegions(h);
        CheckGoalDwell(h);
        CheckGoalZone(h);
        CheckHingeHolds(h);
        CheckHingeRelease(h);
        CheckHingeRemoval(h);
        CheckHingeIssues(h);
        CheckHingeReflection();
        CheckDisabledChildBody(h);
        CheckWater(h);
        CheckBuoyPop(h, solutionA);
        CheckStatsLog(h);
        CheckHingeLanes(h);
        CheckHingeReleaseTicks(h, solutionA, controlA);
        CheckHingeReleaseRules(h);
        CheckRestartOnReleaseTick(h, solutionA, controlA);
        CheckGoalAfterCollection(h, solutionA);
        CheckSceneSwitch(h, solutionA, controlA);
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    std::signal(SIGABRT, [](int) { std::printf("RESULT FAIL abort() was called\n"); std::_Exit(3); });

    if(argc < 2)
    {
        std::printf("usage: PlaygroundValidation <repoRoot>\n");
        return 2;
    }

    try
    {
        ValidationApp app({ L"PlaygroundValidation", 640, 360, true });
        const std::filesystem::path gameDirectory = std::filesystem::path(argv[1]) / "Game";
        app.Execute([&] { RunChecks(app, gameDirectory); });

        const bool passed = g_Failures == 0;
        std::printf("%s checks=%d failures=%d\n", passed ? "RESULT PASS" : "RESULT FAIL", g_Checks, g_Failures);
        return passed ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("RESULT FAIL exception: %s\n", error.what());
        return 1;
    }
}
