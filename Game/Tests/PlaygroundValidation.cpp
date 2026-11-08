// Integration check of the physics playground: the real JSON scene, opened through the SceneManager and played by the
// real Application loop. The checks run on a fiber that gives control back to the loop once per frame, so every frame is
// a real frame of Application::Run; time scale decides how many fixed ticks a frame runs. Lane results are read from the
// game components' public state; the program changes nothing in the game.
#include "Puzzle/GoalCup.h"
#include "Puzzle/PoseTrack.h"
#include "Puzzle/PuzzleDirector.h"
#include "Puzzle/PuzzleLane.h"
#include "Scripts/FlyCamera.h"

#include <Engine.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <crtdbg.h>

#include <algorithm>
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
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Every frame runs four fixed ticks (the engine's per-frame cap); a scale of 1 follows the real clock.
    constexpr float FourTickScale = 1000.0f;
    constexpr float RealTimeScale = 1.0f;
    constexpr float MixedTickScale = 2.5f;
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

        Canape::Scene& Active() const { return *scenes.GetActiveScene(); }

        static void SetScale(float scale) { Canape::Time::SetTimeScale(scale); }

        void Frame(const std::vector<Canape::Key>& keys = {})
        {
            if(!keys.empty())
            {
                app.PressKeys(keys);
            }
            app.YieldFrame();
            mostScenes = std::max(mostScenes, scenes.GetSceneCount());
            app.ReleaseKeys(keys);
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
            for(const char* name : { "KickerJump", "TiltTray", "CarriedScoop" })
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

        static std::vector<GoalCup*> Cups(const PuzzleLane& lane)
        {
            std::vector<GoalCup*> cups;
            collectBelow(*lane.GetOwnerObject()->GetTransform(), cups);
            return cups;
        }

        static GoalCup* Cup(const PuzzleLane& lane)
        {
            const std::vector<GoalCup*> cups = Cups(lane);
            return cups.empty() ? nullptr : cups.front();
        }

        // The object the lane's first cup waits for.
        Canape::GameObject* Payload(const PuzzleLane& lane) const
        {
            const GoalCup* cup = Cup(lane);
            return cup ? Active().GetGameObject(cup->GetPayload()) : nullptr;
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
            const std::vector<GoalCup*> cups = h.Cups(*lane);
            Check(!cups.empty(), Tag(when, h.NameOf(*lane)) + ": the lane has a cup");
            Check(lane->IsSolved() == expectSolved, Tag(when, h.NameOf(*lane)) + ": the lane's verdict is as expected");
            for(const GoalCup* cup : cups)
            {
                Check(cup->IsShownSolved() == expectSolved, Tag(when, h.NameOf(*lane)) + ": the cup shows what the lane got");
                const std::string& wanted = expectSolved ? cup->GetSolvedMaterial() : cup->GetIdleMaterial();
                for(const Canape::GameObject* piece : cup->GetPieces())
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
            for(const GoalCup* cup : h.Cups(*lane))
            {
                for(const Canape::GameObject* piece : cup->GetPieces())
                {
                    const auto* renderer = piece->GetComponent<Canape::MeshRendererComponent>();
                    Check(renderer && ShowsColor(assets, *renderer, solved), Tag(when, h.NameOf(*lane)) + ": the cup piece's material resolves to the right color");
                }
            }
        }
    }

    void CheckQueries(Harness& h)
    {
        Canape::PhysicsScene& physics = h.Active().GetPhysics();
        PuzzleLane& lane = *h.Front();
        Canape::GameObject* payload = h.Payload(lane);
        const std::optional<CupRegion> region = h.Cup(lane) ? h.Cup(lane)->GetRegion() : std::nullopt;
        Check(payload != nullptr && region.has_value(), "the payload object and the cup region exist");
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
        };

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the cup regions");
        Canape::Scene& scene = h.Active();
        for(const Expected& e : expected)
        {
            Canape::GameObject* root = scene.GetGameObject(scene.FindGameObjectByName(e.lane));
            const PuzzleLane* lane = root ? root->GetComponent<PuzzleLane>() : nullptr;
            const GoalCup* cup = lane ? h.Cup(*lane) : nullptr;
            const std::optional<CupRegion> region = cup ? cup->GetRegion() : std::nullopt;
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
        Check(!cup->GetRegion().has_value() && !cup->HoldsPayload() && !cup->IsShownSolved(), "two pieces are no cup and hold nothing");
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
        for(const float scale : { FourTickScale, RealTimeScale })
        {
            h.SetScale(scale);
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
            Check(!probe->HasBodyNextTick(), "a body disabled in FixedUpdate is gone by the next tick");
            Check(std::memcmp(&probe->GetPositionAtDisable(), &probe->GetPositionNextTick(), sizeof(Canape::Vector3)) == 0,
                "a body disabled in FixedUpdate takes no step in that tick");
            Check(!body->HasBody(), "the disabled body stays out of the world");
            captured[index++] = probe->GetCaptured();
        }
        h.SetScale(FourTickScale);

        Check(SamePositions(captured[0], captured[1]), "frames of four ticks and of the real clock give bit-identical poses after a body is disabled in FixedUpdate");
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
        for(const float scale : { FourTickScale, RealTimeScale })
        {
            h.SetScale(scale);
            Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the add probe");
            Canape::Scene& scene = h.Active();
            std::vector<Canape::GameObject*> watched;
            CollectBodies(h.Front()->GetOwnerObject(), watched);
            auto* probe = scene.GetGameObject(scene.AddGameObject("addBodyProbe"))->AddComponent<AddBodyAtTick>();
            probe->Arm(watched, 125, 160);
            Check(h.RunUntil([&] { return probe->IsDone(); }), "the add probe reaches its capture tick");
            Check(probe->HasBodyNextTick(), "a body added in FixedUpdate exists by the next tick");
            Check(probe->GetBodiesGained() == 1, "the world gains the added body in the step of the tick it was added in");
            Check(probe->DroppedNextTick(), "a body added in FixedUpdate steps in the tick it was added");
            captured[index++] = probe->GetCaptured();
        }
        h.SetScale(FourTickScale);

        Check(SamePoses(captured[0], captured[1]), "frames of four ticks and of the real clock give bit-identical poses after a body is added in FixedUpdate");
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
        for(const float scale : { FourTickScale, RealTimeScale })
        {
            h.SetScale(scale);
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
            Check(probe->HasBodyNextTick(), "the body is rebuilt in the tick of the removal");
            Check(probe->GetCaptured().position.Y < probe->GetAtRemoval().position.Y, "the rebuilt body keeps moving");
            Check(scene.GetPhysics().GetAuthoringIssueCount() == 0, "a collider removal is no issue");
            captured[index++] = { probe->GetCaptured() };
        }
        h.SetScale(FourTickScale);

        Check(SamePoses(captured[0], captured[1]), "frames of four ticks and of the real clock give bit-identical poses after a collider is removed in FixedUpdate");
    }

    // Moves the payload into the cup from FixedUpdate once the lane has its verdict.
    class GoalPusher final : public Canape::ScriptComponent {
    public:
        void Arm(PuzzleLane* lane, GoalCup* cup)
        {
            m_Lane = lane;
            m_Cup = cup;
        }
        std::uint32_t GetRestTicksAfterPush() const { return m_RestTicksAfterPush; }

    protected:
        void FixedUpdate() override
        {
            if(!m_Lane || !m_Cup)
            {
                return;
            }
            Canape::GameObject* payload = GetGameObject(m_Cup->GetPayload());
            auto* body = payload ? payload->GetComponent<Canape::RigidBodyComponent>() : nullptr;
            const std::optional<CupRegion> region = m_Cup->GetRegion();
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
        GoalCup* m_Cup = nullptr;
        std::uint32_t m_State = 0;
        std::uint32_t m_RestTicksAfterPush = 0;
    };

    // The verdict is taken once, at the horizon: a payload that reaches the cup during the rest ticks changes nothing,
    // though the cup shows it.
    void CheckVerdictFinality(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Control) != nullptr, "a control scene opens for the verdict check");
        PuzzleLane& lane = *h.Front();
        GoalCup* cup = h.Cup(lane);
        auto* pusher = lane.GetOwnerObject()->AddComponent<GoalPusher>();
        pusher->Arm(&lane, cup);

        Check(h.RunUntil([&] { return pusher->GetRestTicksAfterPush() >= 3; }), "the payload is pushed during the rest ticks");
        Check(cup->HoldsPayload(), "the pushed payload is inside the cup during the rest");
        Check(lane.HasVerdict() && !lane.IsSolved(), "the verdict at the horizon is unsolved");

        Check(h.RunUntil([&] { return pusher->GetRestTicksAfterPush() >= 20; }), "the rest goes on after the push");
        Check(lane.GetTick() == PuzzleHorizonTicks && lane.HasVerdict(), "the lane stays at its horizon during the rest");
        Check(!lane.IsSolved(), "a cup filled after the verdict does not change it");
        Check(cup->IsShownSolved(), "the cup shows the payload inside all the same");
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
        Canape::GameObject* pivot = nullptr;
        Canape::GameObject* held = nullptr;
        Canape::HingeComponent* hinge = nullptr;
        Canape::RigidBodyComponent* body = nullptr;
        Canape::Vector3 anchor{};
    };

    // A sphere 1 m to +x of a pivot, hinged about +z with limits [-pi/4, 0]; gravity swings it down to negative angles.
    // A static mount is a small box body on the pivot object, out of the sphere's way.
    HingeRig BuildHingeRig(Canape::Scene& scene, const char* name, const Canape::Vector3& anchor, bool staticMount, bool collider = true)
    {
        HingeRig rig;
        rig.anchor = anchor;
        rig.pivot = scene.GetGameObject(scene.AddGameObject(std::string(name) + "Pivot"));
        rig.pivot->GetTransform()->SetLocalPosition(anchor);
        if(staticMount)
        {
            rig.pivot->AddComponent<Canape::BoxColliderComponent>()->SetSize({ 0.2f, 0.2f, 0.2f });
            rig.pivot->AddComponent<Canape::RigidBodyComponent>()->SetMotion(Canape::PhysicsMotion::Static);
        }
        rig.held = scene.GetGameObject(scene.AddGameObject(std::string(name) + "Held"));
        rig.held->GetTransform()->SetLocalPosition(anchor + Canape::Vector3{ 1.0f, 0.0f, 0.0f });
        if(collider)
        {
            rig.held->AddComponent<Canape::SphereColliderComponent>()->SetRadius(0.1f);
        }
        rig.body = rig.held->AddComponent<Canape::RigidBodyComponent>();
        rig.body->SetMass(1.0f);
        rig.hinge = rig.pivot->AddComponent<Canape::HingeComponent>();
        rig.hinge->SetBody(rig.held->GetHandle());
        rig.hinge->SetAxis({ 0.0f, 0.0f, 1.0f });
        rig.hinge->SetMinAngle(-Pi / 4.0f);
        rig.hinge->SetMaxAngle(0.0f);
        return rig;
    }

    float RadiusOf(const HingeRig& rig)
    {
        return (rig.held->GetTransform()->GetPosition() - rig.anchor).Length();
    }

    // A hinge fixed to the world and one mounted on a static body both hold the sphere at its radius and stop at the limit.
    void CheckHingeHolds(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge checks");
        Canape::Scene& scene = h.Active();
        const HingeRig rigs[] = {
            BuildHingeRig(scene, "worldHinge", { 300.0f, 50.0f, 0.0f }, false),
            BuildHingeRig(scene, "mountedHinge", { 310.0f, 50.0f, 0.0f }, true),
        };
        h.Frame();
        for(const HingeRig& rig : rigs)
        {
            const std::string& name = rig.pivot->GetName();
            Check(rig.hinge->HasHinge() && rig.hinge->IsHeld() && rig.hinge->GetIssue() == Canape::HingeIssue::None, name + ": the hinge is made and held");
            Check(Near(rig.hinge->GetWorldAnchor(), rig.anchor, 1.0e-5f), name + ": the anchor is the pivot object's position");
            Check(std::fabs(rig.hinge->GetAngle()) < 0.2f, name + ": the angle starts near 0");
        }

        float worstRadius[2] = {};
        float lowest[2] = {};
        float highest[2] = {};
        for(int frame = 0; frame < 150; ++frame)
        {
            h.Frame();
            for(std::size_t i = 0; i < 2; ++i)
            {
                worstRadius[i] = std::max(worstRadius[i], std::fabs(RadiusOf(rigs[i]) - 1.0f));
                lowest[i] = std::min(lowest[i], rigs[i].hinge->GetAngle());
                highest[i] = std::max(highest[i], rigs[i].hinge->GetAngle());
            }
        }
        for(std::size_t i = 0; i < 2; ++i)
        {
            const std::string& name = rigs[i].pivot->GetName();
            Check(worstRadius[i] < 0.03f, name + ": the sphere stays at its radius from the anchor (worst error " + std::to_string(worstRadius[i]) + ")");
            Check(lowest[i] > -Pi / 4.0f - 0.2f && highest[i] < 0.2f, name + ": the angle stays within the limits (lowest " + std::to_string(lowest[i]) + ", highest " + std::to_string(highest[i]) + ")");
            Check(std::fabs(rigs[i].hinge->GetAngle() + Pi / 4.0f) < 0.05f, name + ": the sphere rests on the lower limit (" + std::to_string(rigs[i].hinge->GetAngle()) + ")");
            Check(rigs[i].hinge->IsHeld(), name + ": the hinge is still held");
        }
        Check(scene.GetPhysics().GetAuthoringIssueCount() == 0 && !h.Front()->HasFailed(), "good hinges are no issue");
    }

    // Disabling drops the hinge and enabling makes it again; a release holds until the component is toggled.
    void CheckHingeRelease(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge release checks");
        Canape::Scene& scene = h.Active();
        const HingeRig rig = BuildHingeRig(scene, "releasedHinge", { 300.0f, 50.0f, 0.0f }, false);
        h.Frame();
        Check(rig.hinge->IsHeld(), "the hinge holds before the toggle");
        rig.hinge->SetEnabled(false);
        h.Frame();
        Check(!rig.hinge->HasHinge() && !rig.hinge->IsHeld() && rig.hinge->GetAngle() == 0.0f, "a disabled hinge is dropped");
        rig.hinge->SetEnabled(true);
        h.Frame();
        Check(rig.hinge->HasHinge() && rig.hinge->IsHeld() && std::fabs(rig.hinge->GetAngle()) < 0.3f, "an enabled hinge is made again, at angle 0 from where the body is");

        const float heightBefore = rig.held->GetTransform()->GetPosition().Y;
        rig.hinge->Release();
        Check(!rig.hinge->IsHeld() && rig.hinge->HasHinge(), "a released hinge is not held but still exists");
        for(int frame = 0; frame < 8; ++frame)
        {
            h.Frame();
        }
        Check(heightBefore - rig.held->GetTransform()->GetPosition().Y > 0.5f, "the released body falls");
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

    // Removing the held object ends its hinge like a release; removing the hinge object frees the body. Neither is an issue.
    void CheckHingeRemoval(Harness& h)
    {
        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge removal checks");
        Canape::Scene& scene = h.Active();
        const HingeRig lostHeld = BuildHingeRig(scene, "lostHeld", { 300.0f, 50.0f, 0.0f }, false);
        const HingeRig lostPivot = BuildHingeRig(scene, "lostPivot", { 310.0f, 50.0f, 0.0f }, true);
        h.Frame();
        Check(lostHeld.hinge->IsHeld() && lostPivot.hinge->IsHeld(), "both hinges hold before the removals");

        const float heightBefore = lostPivot.held->GetTransform()->GetPosition().Y;
        scene.RemoveGameObject(lostHeld.held->GetHandle());
        h.Frame();
        scene.RemoveGameObject(lostPivot.pivot->GetHandle());
        h.Frame();
        h.Frame();
        Check(!lostHeld.hinge->HasHinge() && !lostHeld.hinge->IsHeld() && lostHeld.hinge->GetIssue() == Canape::HingeIssue::None,
            "a hinge whose held object is removed ends without an issue");
        for(int frame = 0; frame < 8; ++frame)
        {
            h.Frame();
        }
        Check(heightBefore - lostPivot.held->GetTransform()->GetPosition().Y > 0.5f, "a body whose hinge object is removed falls free");
        Check(scene.GetPhysics().GetAuthoringIssueCount() == 0 && !Canape::StepFailed(scene.GetPhysics().GetLastStepError()),
            "the removals leave no issue and the world keeps stepping");
        Check(!h.Front()->HasFailed(), "the removals fail no lane");
    }

    // Each authoring mistake is its own issue, counted once; a held body that failed is the body's issue, not the hinge's.
    void CheckHingeIssues(Harness& h)
    {
        using Canape::HingeIssue;

        Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the hinge issue checks");
        Canape::Scene& scene = h.Active();
        const Canape::PhysicsScene& physics = scene.GetPhysics();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == 0, "no issue before the bad hinges");

        const HingeRig noBody = BuildHingeRig(scene, "noBody", { 300.0f, 50.0f, 0.0f }, false);
        noBody.hinge->SetBody({});
        const HingeRig notDynamic = BuildHingeRig(scene, "notDynamic", { 310.0f, 50.0f, 0.0f }, false);
        notDynamic.body->SetMotion(Canape::PhysicsMotion::Static);
        const HingeRig badAxis = BuildHingeRig(scene, "badAxis", { 320.0f, 50.0f, 0.0f }, false);
        badAxis.hinge->SetAxis({ 0.0f, 0.0f, 0.0f });
        const HingeRig bodyFailed = BuildHingeRig(scene, "bodyFailed", { 330.0f, 50.0f, 0.0f }, false, false);
        const HingeRig self = BuildHingeRig(scene, "selfHeld", { 340.0f, 50.0f, 0.0f }, true);
        self.hinge->SetBody(self.pivot->GetHandle());
        h.Frame();
        h.Frame();
        Check(noBody.hinge->GetIssue() == HingeIssue::NoBody && !noBody.hinge->HasHinge(), "an empty body handle is NoBody");
        Check(notDynamic.hinge->GetIssue() == HingeIssue::NotDynamic && !notDynamic.hinge->HasHinge(), "a static held body is NotDynamic");
        Check(badAxis.hinge->GetIssue() == HingeIssue::InvalidSettings && !badAxis.hinge->HasHinge(), "a zero axis is InvalidSettings");
        Check(bodyFailed.hinge->GetIssue() == HingeIssue::BodyFailed && bodyFailed.body->GetIssue() == Canape::RigidBodyIssue::NoCollider,
            "a held body without a collider is BodyFailed, and the body keeps its own issue");
        Check(self.hinge->GetIssue() == HingeIssue::InvalidSettings && !self.hinge->HasHinge(), "an object holding itself is InvalidSettings");
        Check(physics.GetAuthoringIssueCount() == 5, "four hinge issues and one body issue are counted (now " + std::to_string(physics.GetAuthoringIssueCount()) + ")");
        Check(h.Front()->HasFailed(), "a hinge issue fails the lane");
        h.Frame();
        h.Frame();
        Check(physics.GetAuthoringIssueCount() == 5, "the hinge issues are counted once, not per frame");

        badAxis.hinge->SetAxis({ 0.0f, 0.0f, 1.0f });
        h.Frame();
        Check(badAxis.hinge->HasHinge() && badAxis.hinge->IsHeld() && badAxis.hinge->GetIssue() == HingeIssue::None, "a corrected axis makes the hinge without a toggle");
        Check(physics.GetAuthoringIssueCount() == 4, "the corrected hinge leaves four issues");
    }

    void CheckHingeReflection()
    {
        const rttr::type type = rttr::type::get<Canape::HingeComponent>();
        Check(type.get_constructor().is_valid(), "HingeComponent has a registered constructor");
        for(const char* name : { "body", "axis", "minAngle", "maxAngle", "maxFrictionTorque" })
        {
            Check(type.get_property(name).is_valid(), std::string("HingeComponent registers ") + name);
        }
        Check(type.get_properties().size() == 6, "HingeComponent registers its authoring values and nothing else (" + std::to_string(type.get_properties().size()) + ")");
        Check(type.get_property("body").get_type() == rttr::type::get<Handle<Canape::GameObject>>(), "the held object is a GameObject handle");
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
        for(const float scale : { RealTimeScale, MixedTickScale })
        {
            h.SetScale(scale);
            Check(h.Reload(PuzzleMode::Solution) != nullptr, "a scene opens for the batching run");
            Check(h.RunToVerdict(PuzzleMode::Solution), "the batched solution run reaches its verdict");
            CheckSame(scale == RealTimeScale ? "real clock solution" : "mixed batches solution", solution, h.Verdicts());
            h.Frame({ Canape::Key::C });
            Check(h.RunToVerdict(PuzzleMode::Control), "the batched control run reaches its verdict");
            CheckSame(scale == RealTimeScale ? "real clock control" : "mixed batches control", control, h.Verdicts());
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
        CheckHingeHolds(h);
        CheckHingeRelease(h);
        CheckHingeRemoval(h);
        CheckHingeIssues(h);
        CheckHingeReflection();
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
        ValidationApp app({ L"PlaygroundValidation", 640, 360 });
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
