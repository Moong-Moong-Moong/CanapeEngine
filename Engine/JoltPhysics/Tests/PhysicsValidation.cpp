// Standalone backend validation; intentionally not part of JoltPhysics.vcxproj.
// It includes the private conversion header (and so Jolt) to prove the Canape and Jolt rotation conventions agree.
#include "JoltPhysics/JoltRuntime.h"
#include "JoltPhysics/JoltWorld.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h>

#include "JoltPhysics/JoltConvert.h"

using namespace Canape;

namespace
{
    int g_failures = 0;
    int g_cases = 0;

    void Check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    void BeginCase(const char* name)
    {
        ++g_cases;
        std::cout << "case: " << name << '\n';
    }

    void RuntimeConstructsAndTearsDown()
    {
        BeginCase("runtime constructs with a matching version id and tears down");
        {
            JoltRuntime runtime;
            Check(runtime.IsValid(), "runtime is valid");
            Check(runtime.GetError().empty(), "runtime has no error");
        }
        JoltRuntime again;
        Check(again.IsValid(), "runtime can be created after the first one is gone");
    }

    void SecondRuntimeIsRefused()
    {
        BeginCase("a second live runtime is refused and does not disturb the first");
        JoltRuntime first;
        Check(first.IsValid(), "first runtime is valid");
        {
            JoltRuntime second;
            Check(!second.IsValid(), "second runtime is invalid");
            Check(!second.GetError().empty(), "second runtime reports an error");
        }
        Check(first.IsValid(), "first runtime stays valid after a refused second one is destroyed");
        JoltRuntime third;
        Check(!third.IsValid(), "the refused destructor did not release the process-wide slot");
    }

    void ConstructDestroyTwiceInARow()
    {
        BeginCase("construct and destroy twice in a row");
        for (int i = 0; i < 2; ++i)
        {
            JoltRuntime runtime;
            Check(runtime.IsValid(), "runtime is valid in iteration");
        }
    }

    void FeatureMismatchNamesTheDifferingBits()
    {
        BeginCase("feature mismatch names the differing bits");
        constexpr std::uint64_t shift = 24;
        constexpr std::uint64_t version = 0x050600;
        const auto same = DescribeJoltFeatureMismatch(version | (0x5ull << shift), version | (0x5ull << shift));
        Check(same.empty(), "identical ids produce no description");

        const auto text = DescribeJoltFeatureMismatch(version | (0x4ull << shift), version | (0x1ull << shift));
        Check(text.find("JPH_DOUBLE_PRECISION") != std::string::npos, "bit 1 is named");
        Check(text.find("JPH_FLOATING_POINT_EXCEPTIONS_ENABLED") != std::string::npos, "bit 3 is named");
        Check(text.find("JPH_CROSS_PLATFORM_DETERMINISTIC") == std::string::npos, "bit 2 is not named");
    }

    constexpr float Dt = 1.0f / 60.0f;
    constexpr float Pi = 3.14159265358979f;

    void Check(bool value, const std::string& message)
    {
        Check(value, message.c_str());
    }

    bool Near(float a, float b, float tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }

    std::uint32_t Bits(float value)
    {
        return std::bit_cast<std::uint32_t>(value);
    }

    Quaternion AxisAngle(const Vector3& axis, float angle)
    {
        const float s = std::sin(angle * 0.5f);
        return Quaternion(axis.X * s, axis.Y * s, axis.Z * s, std::cos(angle * 0.5f));
    }

    PhysicsShapePart BoxPart(const Vector3& halfExtent, const Vector3& localPosition = {}, std::optional<float> friction = std::nullopt,
        const Quaternion& localRotation = {})
    {
        PhysicsShapePart part;
        part.type = PhysicsShapeType::Box;
        part.halfExtent = halfExtent;
        part.localPose = PhysicsPose{ localPosition, localRotation };
        part.friction = friction;
        return part;
    }

    PhysicsShapePart SpherePart(float radius)
    {
        PhysicsShapePart part;
        part.type = PhysicsShapeType::Sphere;
        part.radius = radius;
        return part;
    }

    PhysicsBodyDesc BodyOf(std::vector<PhysicsShapePart> parts, const Vector3& position, PhysicsMotion motion = PhysicsMotion::Dynamic,
        const Quaternion& rotation = {})
    {
        PhysicsBodyDesc desc;
        desc.parts = std::move(parts);
        desc.pose = PhysicsPose{ position, rotation };
        desc.settings.motion = motion;
        return desc;
    }

    PhysicsBodyDesc BoxBody(const Vector3& position, const Vector3& halfExtent = { 0.5f, 0.5f, 0.5f },
        PhysicsMotion motion = PhysicsMotion::Dynamic, const Quaternion& rotation = {})
    {
        return BodyOf({ BoxPart(halfExtent) }, position, motion, rotation);
    }

    class StateHash
    {
    public:
        void Add(std::uint32_t word)
        {
            for (int i = 0; i < 4; ++i)
            {
                m_Value = (m_Value ^ ((word >> (8 * i)) & 0xffu)) * 1099511628211ull;
            }
        }

        void Add(float value) { Add(Bits(value)); }
        void Add(const Vector3& v) { Add(v.X); Add(v.Y); Add(v.Z); }
        void Add(const Quaternion& q) { Add(q.X); Add(q.Y); Add(q.Z); Add(q.W); }
        void Add(std::uint64_t value) { Add(static_cast<std::uint32_t>(value)); Add(static_cast<std::uint32_t>(value >> 32)); }

        std::uint64_t Value() const { return m_Value; }

    private:
        std::uint64_t m_Value = 14695981039346656037ull;
    };

    std::uint64_t HashBodies(const JoltWorld& world, const PhysicsBodyIds& ids)
    {
        StateHash hash;
        for (const PhysicsBodyId id : ids)
        {
            const PhysicsPose pose = world.GetPose(id);
            hash.Add(pose.position);
            hash.Add(pose.rotation);
            hash.Add(world.GetLinearVelocity(id));
            hash.Add(world.GetAngularVelocity(id));
        }
        return hash.Value();
    }

    // A floor, a 5x5x2 grid of boxes, rolling spheres, a mixed-friction compound and a heavy box; scripted impulses and a destroy/create.
    class Simulation
    {
    public:
        static constexpr int GridFirst = 1;
        static constexpr int SphereFirst = 51;
        static constexpr int ReplacedIndex = 7;

        Simulation(JoltRuntime& runtime, float impulseScale = 1.0f)
            : m_World(runtime)
            , m_ImpulseScale(impulseScale)
        {
            m_Ids.push_back(m_World.CreateBody(BoxBody({ 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, PhysicsMotion::Static)));
            for (int layer = 0; layer < 2; ++layer)
            {
                for (int row = 0; row < 5; ++row)
                {
                    for (int column = 0; column < 5; ++column)
                    {
                        const Vector3 position(-2.4f + 1.2f * static_cast<float>(column), 0.5f + 1.1f * static_cast<float>(layer),
                            -2.4f + 1.2f * static_cast<float>(row));
                        m_Ids.push_back(m_World.CreateBody(BoxBody(position)));
                    }
                }
            }
            for (int i = 0; i < 5; ++i)
            {
                m_Ids.push_back(m_World.CreateBody(BodyOf({ SpherePart(0.3f) }, { -2.0f + static_cast<float>(i), 0.3f, -5.0f })));
            }
            m_Ids.push_back(m_World.CreateBody(BodyOf(
                { BoxPart({ 1.0f, 0.1f, 0.3f }, {}, 0.0f), BoxPart({ 0.3f, 0.4f, 0.3f }, { 0.5f, 0.5f, 0.0f }, std::nullopt, AxisAngle({ 0, 0, 1 }, 0.5f)) },
                { 3.0f, 2.0f, 3.0f }, PhysicsMotion::Dynamic, AxisAngle({ 0, 1, 0 }, 0.7f))));
            PhysicsBodyDesc heavy = BoxBody({ -3.0f, 1.0f, 3.0f });
            heavy.settings.mass = 3.0f;
            m_Ids.push_back(m_World.CreateBody(heavy));
        }

        void Tick()
        {
            if (m_Tick == 0)
            {
                for (int i = 0; i < 5; ++i)
                {
                    m_World.AddImpulse(m_Ids[SphereFirst + i], { 0.0f, 0.0f, 400.0f * m_ImpulseScale });
                }
            }
            if (m_Tick == 100)
            {
                const PhysicsBodyId target = m_Ids[GridFirst + 9];
                m_World.AddImpulseAt(target, { 30.0f, 0.0f, 0.0f }, m_World.GetPose(target).position + Vector3(0.0f, 0.4f, 0.0f));
            }
            if (m_Tick == 200)
            {
                m_World.DestroyBody(m_Ids[ReplacedIndex]);
                m_Ids[ReplacedIndex] = PhysicsBodyId{};
            }
            if (m_Tick == 201)
            {
                m_Ids[ReplacedIndex] = m_World.CreateBody(BoxBody({ 0.0f, 6.0f, 0.0f }));
            }
            m_Errors |= m_World.Step(Dt);
            ++m_Tick;
            StateHash hash;
            hash.Add(m_Digest);
            hash.Add(HashBodies(m_World, m_Ids));
            m_Digest = hash.Value();
        }

        void Run(int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                Tick();
            }
        }

        const JoltWorld& World() const { return m_World; }
        const PhysicsBodyIds& Ids() const { return m_Ids; }
        std::uint64_t Digest() const { return m_Digest; }
        std::uint64_t FinalState() const { return HashBodies(m_World, m_Ids); }
        PhysicsStepError Errors() const { return m_Errors; }

        std::vector<std::uint32_t> FinalBits() const
        {
            std::vector<std::uint32_t> bits;
            for (const PhysicsBodyId id : m_Ids)
            {
                const PhysicsPose pose = m_World.GetPose(id);
                const Vector3 linear = m_World.GetLinearVelocity(id);
                const Vector3 angular = m_World.GetAngularVelocity(id);
                for (const float value : { pose.position.X, pose.position.Y, pose.position.Z, pose.rotation.X, pose.rotation.Y, pose.rotation.Z,
                         pose.rotation.W, linear.X, linear.Y, linear.Z, angular.X, angular.Y, angular.Z })
                {
                    bits.push_back(Bits(value));
                }
            }
            return bits;
        }

    private:
        JoltWorld m_World;
        PhysicsBodyIds m_Ids;
        float m_ImpulseScale = 1.0f;
        int m_Tick = 0;
        std::uint64_t m_Digest = 0;
        PhysicsStepError m_Errors = PhysicsStepError::None;
    };

    std::string Hex(std::uint64_t value)
    {
        return std::format("{:016x}", value);
    }

    void QuaternionRotationMatchesJolt()
    {
        BeginCase("Canape Quaternion::Rotate and Jolt rotate a vector the same way");
        const Quaternion rotations[] = {
            AxisAngle({ 0, 1, 0 }, Pi * 0.5f),
            AxisAngle({ 1, 0, 0 }, 0.7f),
            AxisAngle({ 0, 0, 1 }, -2.1f),
            AxisAngle(Vector3(1, 2, 3).Normalized(), 1.3f),
            AxisAngle(Vector3(-2, 0.5f, 1).Normalized(), -0.4f),
        };
        const Vector3 vectors[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1.5f, -2.0f, 0.25f } };
        bool same = true;
        bool nontrivial = false;
        for (const Quaternion& q : rotations)
        {
            for (const Vector3& v : vectors)
            {
                const Vector3 canape = q.Rotate(v);
                const Vector3 jolt = ToCanape(ToJolt(q) * ToJolt(v));
                same = same && Near(canape.X, jolt.X, 1.0e-5f) && Near(canape.Y, jolt.Y, 1.0e-5f) && Near(canape.Z, jolt.Z, 1.0e-5f);
                nontrivial = nontrivial || !Near(canape.X, v.X, 0.1f) || !Near(canape.Y, v.Y, 0.1f) || !Near(canape.Z, v.Z, 0.1f);
            }
        }
        Check(same, "Canape and Jolt rotations agree");
        Check(nontrivial, "the rotations actually move vectors");
        const Quaternion q = rotations[3];
        const Quaternion roundTrip = ToCanape(ToJolt(q));
        Check(Bits(roundTrip.X) == Bits(q.X) && Bits(roundTrip.Y) == Bits(q.Y) && Bits(roundTrip.Z) == Bits(q.Z) && Bits(roundTrip.W) == Bits(q.W),
            "quaternion conversion round trips bit for bit");
    }

    void CompoundPartFollowsBodyRotation(JoltRuntime& runtime)
    {
        BeginCase("a compound part lands where Canape Rotate says after the body rotates");
        JoltWorld world(runtime);
        const Quaternion rotation = AxisAngle({ 0, 1, 0 }, Pi * 0.5f);
        const Vector3 origin(10.0f, 5.0f, -3.0f);
        const Vector3 local(2.0f, 0.0f, 0.0f);
        PhysicsBodyDesc desc = BodyOf({ BoxPart({ 0.5f, 0.5f, 0.5f }, local) }, origin, PhysicsMotion::Dynamic, rotation);
        desc.parts.push_back(BoxPart({ 0.1f, 0.1f, 0.1f }));
        const PhysicsBodyId id = world.CreateBody(desc);
        Check(id.IsValid(), "compound created");
        const Vector3 expected = origin + rotation.Rotate(local);
        const Vector3 mirror = origin - rotation.Rotate(local);
        Check(world.QueryBox(expected, { 0.1f, 0.1f, 0.1f }).size() == 1, "the part is where Canape Rotate predicts");
        Check(world.QueryBox(mirror, { 0.1f, 0.1f, 0.1f }).empty(), "nothing is at the mirrored position");
        const PhysicsPose pose = world.GetPose(id);
        Check(Bits(pose.position.X) == Bits(origin.X) && Bits(pose.position.Y) == Bits(origin.Y) && Bits(pose.position.Z) == Bits(origin.Z),
            "the pose reads the body origin (pivot), not the centre of mass");
    }

    void TwoWorldsStayBitIdentical(JoltRuntime& runtime)
    {
        BeginCase("two worlds fed the same input for 600 ticks stay bit-identical every tick");
        std::uint64_t digest = 0;
        std::uint64_t finalState = 0;
        std::vector<std::uint32_t> finalBits;
        std::vector<std::uint32_t> startBits;
        {
            Simulation a(runtime);
            Simulation b(runtime);
            startBits = a.FinalBits();
            bool lockstep = true;
            for (int tick = 0; tick < 600; ++tick)
            {
                a.Tick();
                b.Tick();
                lockstep = lockstep && a.Digest() == b.Digest();
            }
            Check(lockstep, "the running digests match after every tick");
            Check(a.FinalBits() == b.FinalBits(), "final poses and velocities match as raw bits");
            Check(!StepFailed(a.Errors()) && !StepFailed(b.Errors()), "no step reported an error");
            Check(a.World().GetBodyCount() == 58, "the scene holds 58 bodies");
            Check(a.FinalBits() != startBits, "the scene moved");
            digest = a.Digest();
            finalState = a.FinalState();
            finalBits = a.FinalBits();
        }
        std::cout << "DIGEST scene ticks=600 bodies=58 running=" << Hex(digest) << " final=" << Hex(finalState) << '\n';

        BeginCase("destroying the world and building it again in the same order gives the same bits");
        Simulation again(runtime);
        again.Run(600);
        Check(again.Digest() == digest, "the rebuilt world has the same running digest");
        Check(again.FinalBits() == finalBits, "the rebuilt world has the same final raw bits");

        BeginCase("the digest reacts to the input");
        Simulation different(runtime, 1.01f);
        different.Run(600);
        Check(different.Digest() != digest, "a different impulse changes the digest");
    }

    float SlideAfter(JoltRuntime& runtime, float bodyFriction, std::optional<float> partFriction, int ticks = 30)
    {
        constexpr float Sin = 0.34202014f;
        constexpr float Cos = 0.93969262f;
        const Quaternion slope(0.0f, 0.0f, 0.17364818f, 0.98480775f);
        JoltWorld world(runtime);
        PhysicsBodyDesc slopeDesc = BodyOf({ BoxPart({ 1.0f, 0.05f, 0.5f }, {}, partFriction) }, { 0.0f, 1.0f, 0.0f }, PhysicsMotion::Static, slope);
        slopeDesc.settings.friction = bodyFriction;
        world.CreateBody(slopeDesc);
        PhysicsBodyDesc blockDesc = BoxBody({ -0.15f * Sin, 1.0f + 0.15f * Cos, 0.0f }, { 0.1f, 0.1f, 0.1f }, PhysicsMotion::Dynamic, slope);
        blockDesc.settings.friction = 1.0f;
        const PhysicsBodyId block = world.CreateBody(blockDesc);
        const Vector3 start = world.GetPose(block).position;
        for (int i = 0; i < ticks; ++i)
        {
            world.Step(Dt);
        }
        return (start - world.GetPose(block).position).Length();
    }

    void FrictionOverrideReachesTheCombine(JoltRuntime& runtime)
    {
        BeginCase("a part friction override decides whether a block slides down a 20 degree slope");
        const float overrideZero = SlideAfter(runtime, 1.0f, 0.0f);
        const float overrideHalf = SlideAfter(runtime, 0.0f, 0.5f);
        const float plainLow = SlideAfter(runtime, 0.0f, std::nullopt);
        const float plainHigh = SlideAfter(runtime, 1.0f, std::nullopt);
        Check(overrideZero > 0.3f, std::format("override 0 beats body friction 1 and slides (moved {})", overrideZero));
        Check(overrideHalf < 0.01f, std::format("override 0.5 beats body friction 0 and holds (moved {})", overrideHalf));
        Check(plainLow > 0.3f, std::format("control: body friction 0 slides (moved {})", plainLow));
        Check(plainHigh < 0.01f, std::format("control: body friction 1 holds (moved {})", plainHigh));
    }

    void MixedOverridesInOneCompound(JoltRuntime& runtime)
    {
        BeginCase("a compound with mixed overrides gives each part its own friction");
        constexpr float Sin = 0.34202014f;
        constexpr float Cos = 0.93969262f;
        const Quaternion slope(0.0f, 0.0f, 0.17364818f, 0.98480775f);
        JoltWorld world(runtime);
        PhysicsBodyDesc slopeDesc = BodyOf({ BoxPart({ 1.0f, 0.05f, 0.25f }, { 0.0f, 0.0f, -0.3f }),
                                               BoxPart({ 1.0f, 0.05f, 0.25f }, { 0.0f, 0.0f, 0.3f }, 0.0f) },
            { 0.0f, 1.0f, 0.0f }, PhysicsMotion::Static, slope);
        slopeDesc.settings.friction = 1.0f;
        world.CreateBody(slopeDesc);
        const auto blockAt = [&](float z)
        {
            PhysicsBodyDesc desc = BoxBody({ -0.15f * Sin, 1.0f + 0.15f * Cos, z }, { 0.1f, 0.1f, 0.1f }, PhysicsMotion::Dynamic, slope);
            desc.settings.friction = 1.0f;
            return world.CreateBody(desc);
        };
        const PhysicsBodyId held = blockAt(-0.3f);
        const PhysicsBodyId slid = blockAt(0.3f);
        const Vector3 heldStart = world.GetPose(held).position;
        const Vector3 slidStart = world.GetPose(slid).position;
        for (int i = 0; i < 30; ++i)
        {
            world.Step(Dt);
        }
        Check((world.GetPose(held).position - heldStart).Length() < 0.01f, "the block on the plain part holds");
        const Vector3 slide = slidStart - world.GetPose(slid).position;
        Check(slide.X > 0.3f && slide.Y > 0.1f, "the block on the zero-friction part slides down the slope");
    }

    void QueryExcludesStaticsAndIncludesSleepers(JoltRuntime& runtime)
    {
        BeginCase("box query skips static bodies and includes a sleeping dynamic body");
        JoltWorld world(runtime);
        const PhysicsBodyId floor = world.CreateBody(BoxBody({ 0.0f, -0.5f, 0.0f }, { 5.0f, 0.5f, 5.0f }, PhysicsMotion::Static));
        const PhysicsBodyId box = world.CreateBody(BoxBody({ 0.0f, 0.5f, 0.0f }));
        Check(floor.IsValid() && box.IsValid(), "bodies created");
        for (int i = 0; i < 900 && world.IsAwake(box); ++i)
        {
            world.Step(Dt);
        }
        Check(!world.IsAwake(box), "the resting box fell asleep");
        Check(world.GetStats().activeBodies == 0, "no body is active");
        const PhysicsBodyIds found = world.QueryBox({ 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f });
        Check(found.size() == 1 && found[0] == box, "only the sleeping dynamic box is found");
    }

    void QueryReturnsCreationOrder(JoltRuntime& runtime)
    {
        BeginCase("box query returns creation order, not slot or spatial order");
        JoltWorld world(runtime);
        const Vector3 spots[] = { { 3, 0, 0 }, { -3, 0, 1 }, { 0, 0, 0 }, { 2, 1, 2 }, { -2, -1, -2 }, { 1, 2, -1 }, { 0, -2, 2 }, { -1, 0, -3 } };
        PhysicsBodyIds created;
        for (const Vector3& spot : spots)
        {
            created.push_back(world.CreateBody(BoxBody(spot, { 0.4f, 0.4f, 0.4f })));
        }
        Check(world.QueryBox({ 0, 0, 0 }, { 6, 6, 6 }) == created, "scattered bodies come back in creation order");

        world.DestroyBody(created[1]);
        world.DestroyBody(created[4]);
        const PhysicsBodyId reusedFirst = world.CreateBody(BoxBody({ 0.0f, 3.0f, 0.0f }, { 0.4f, 0.4f, 0.4f }));
        const PhysicsBodyId reusedSecond = world.CreateBody(BoxBody({ 0.0f, -3.0f, 0.0f }, { 0.4f, 0.4f, 0.4f }));
        Check(reusedFirst.index == created[4].index && reusedSecond.index == created[1].index, "slots are reused last destroyed first");
        const PhysicsBodyIds expected = { created[0], created[2], created[3], created[5], created[6], created[7], reusedFirst, reusedSecond };
        Check(world.QueryBox({ 0, 0, 0 }, { 6, 6, 6 }) == expected, "reused slots sort by creation serial");
    }

    void QueryHasNoDuplicatesForCompounds(JoltRuntime& runtime)
    {
        BeginCase("box query reports a compound with overlapping parts once");
        JoltWorld world(runtime);
        const PhysicsBodyId compound = world.CreateBody(
            BodyOf({ BoxPart({ 0.5f, 0.5f, 0.5f }), BoxPart({ 0.5f, 0.5f, 0.5f }, { 0.3f, 0.0f, 0.0f }), BoxPart({ 0.5f, 0.5f, 0.5f }, { 0.6f, 0.1f, 0.0f }),
                      BoxPart({ 0.5f, 0.5f, 0.5f }, { 0.2f, 0.4f, 0.0f }) },
                { 0, 0, 0 }));
        const PhysicsBodyId plain = world.CreateBody(BoxBody({ 0.0f, 0.0f, 0.4f }, { 0.3f, 0.3f, 0.3f }));
        const PhysicsBodyIds found = world.QueryBox({ 0.3f, 0.0f, 0.0f }, { 3.0f, 3.0f, 3.0f });
        Check(found.size() == 2 && found[0] == compound && found[1] == plain, "one entry per body, creation order");
    }

    void QueryIsExact(JoltRuntime& runtime)
    {
        BeginCase("box query is exact: an overlapping AABB without an overlapping shape is excluded");
        JoltWorld world(runtime);
        const Quaternion diamond = AxisAngle({ 0, 0, 1 }, Pi * 0.25f);
        const PhysicsBodyId body = world.CreateBody(BoxBody({ 0, 0, 0 }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Dynamic, diamond));
        Check(world.QueryBox({ 0.55f, 0.55f, 0.0f }, { 0.05f, 0.05f, 0.5f }).empty(), "a query inside the corner of the AABB misses the shape");
        const PhysicsBodyIds hit = world.QueryBox({ 0.3f, 0.3f, 0.0f }, { 0.05f, 0.05f, 0.5f });
        Check(hit.size() == 1 && hit[0] == body, "a query inside the shape hits it");
        Check(world.QueryBox({ 0, 0, 0 }, { 0.0f, 1.0f, 1.0f }).empty(), "a flat query box is refused");
    }

    void MassFollowsTheDesc(JoltRuntime& runtime)
    {
        BeginCase("mass 0 means density 1000 and a given mass is honoured");
        JoltWorld world(runtime);
        const PhysicsBodyId unit = world.CreateBody(BoxBody({ 0, 1, 0 }));
        Check(Near(world.GetMass(unit), 1000.0f, 0.5f), std::format("1x1x1 m box weighs 1000 kg (got {})", world.GetMass(unit)));

        PhysicsBodyDesc light = BoxBody({ 3, 1, 0 });
        light.settings.mass = 5.0f;
        Check(Near(world.GetMass(world.CreateBody(light)), 5.0f, 1.0e-3f), "a given mass of 5 kg is kept");

        PhysicsBodyDesc compound = BodyOf({ BoxPart({ 0.5f, 0.5f, 0.5f }), BoxPart({ 0.5f, 0.5f, 0.5f }, { 2.0f, 0.0f, 0.0f }) }, { 0, 1, 5 });
        const PhysicsBodyId two = world.CreateBody(compound);
        Check(Near(world.GetMass(two), 2000.0f, 1.0f), "a compound of two 1 m cubes weighs 2000 kg");
        compound.settings.mass = 7.0f;
        Check(Near(world.GetMass(world.CreateBody(compound)), 7.0f, 1.0e-3f), "a compound honours a given mass");

        const PhysicsBodyId sphere = world.CreateBody(BodyOf({ SpherePart(0.5f) }, { 0, 1, 9 }));
        Check(Near(world.GetMass(sphere), 4.0f / 3.0f * Pi * 0.125f * 1000.0f, 1.0f), "a sphere weighs density times volume");

        const PhysicsBodyId fixed = world.CreateBody(BoxBody({ 0, -1, 0 }, { 5, 0.5f, 5 }, PhysicsMotion::Static));
        Check(world.GetMass(fixed) == 0.0f, "a static body reports no mass");
    }

    void ThinBoxesAreAccepted(JoltRuntime& runtime)
    {
        BeginCase("boxes thinner than the convex radius are created, weigh their full volume and rest on a floor");
        JoltWorld world(runtime);
        const PhysicsBodyId floor = world.CreateBody(BoxBody({ 0, -0.5f, 0 }, { 10, 0.5f, 10 }, PhysicsMotion::Static));
        const PhysicsBodyId slab = world.CreateBody(BoxBody({ 0, 0.5f, 0 }, { 0.5f, 0.04f, 0.5f }));
        const PhysicsBodyId plate = world.CreateBody(BodyOf({ BoxPart({ 0.5f, 0.5f, 0.5f }), BoxPart({ 0.5f, 0.03f, 0.5f }, { 2.0f, 0.0f, 0.0f }) }, { 4, 1, 0 }));
        Check(world.CreateBody(BoxBody({ 0, 5, 0 }, { 0.5f, 0.01f, 0.5f })).IsValid(), "a 0.01 thick box is created");
        Check(floor.IsValid() && slab.IsValid() && plate.IsValid(), "a 0.04 thick box and a compound with a 0.03 thick part are created");
        Check(Near(world.GetMass(slab), 1.0f * 0.08f * 1.0f * 1000.0f, 0.5f), std::format("thin box mass is full volume x 1000 (got {})", world.GetMass(slab)));
        Check(Near(world.GetMass(plate), (1.0f + 0.06f) * 1000.0f, 0.5f), std::format("compound mass counts the thin part (got {})", world.GetMass(plate)));

        bool clean = true;
        for (int i = 0; i < 180; ++i)
        {
            clean = clean && !StepFailed(world.Step(Dt));
        }
        Check(clean, "stepping reports no error");
        const float restY = world.GetPose(slab).position.Y;
        Check(Near(restY, 0.04f, 0.02f), std::format("the thin box rests on the floor (y {})", restY));
        Check(world.GetPose(plate).position.Y > 0.0f, "the compound with a thin part does not fall through");
    }

    void InvalidRuntimeWorldIsInert()
    {
        BeginCase("a world on an invalid runtime creates nothing, reports NoRuntime and answers queries with defaults");
        JoltRuntime first;
        JoltRuntime second;
        Check(first.IsValid() && !second.IsValid(), "one runtime is valid, the second is not");
        JoltWorld world(second);
        Check(!world.CreateBody(BoxBody({ 0, 1, 0 })).IsValid(), "no body can be created");
        Check(HasStepError(world.Step(Dt), PhysicsStepError::NoRuntime), "stepping reports NoRuntime");
        const PhysicsStepError badStep = world.Step(-1.0f);
        Check(HasStepError(badStep, PhysicsStepError::NoRuntime) && HasStepError(badStep, PhysicsStepError::InvalidTimeStep),
            "a bad time step on an invalid runtime reports both errors");
        Check(world.GetBodyCount() == 0 && world.GetStats().bodyCount == 0 && world.GetStats().activeBodies == 0, "stats are empty");
        Check(world.QueryBox({ 0, 0, 0 }, { 1, 1, 1 }).empty(), "queries find nothing");
        Check(!world.IsValid(PhysicsBodyId{}), "the default id is not valid");
    }

    void FullWorldReturnsInvalidAndKeepsStepping(JoltRuntime& runtime)
    {
        BeginCase("a full world returns an invalid id for the next body and keeps stepping");
        PhysicsWorldDesc desc;
        desc.maxBodies = 4;
        JoltWorld world(runtime, desc);
        const PhysicsBodyId floor = world.CreateBody(BoxBody({ 0, -0.5f, 0 }, { 10, 0.5f, 10 }, PhysicsMotion::Static));
        PhysicsBodyIds bodies;
        for (int i = 0; i < 3; ++i)
        {
            bodies.push_back(world.CreateBody(BoxBody({ 2.0f * static_cast<float>(i), 5.0f, 0.0f })));
        }
        Check(floor.IsValid() && bodies[0].IsValid() && bodies[1].IsValid() && bodies[2].IsValid(), "four bodies fit");
        const PhysicsBodyId extra = world.CreateBody(BoxBody({ 0, 9, 0 }));
        Check(!extra.IsValid(), "the fifth body is refused");
        Check(world.GetBodyCount() == 4, "the body count stays at four");

        const float startY = world.GetPose(bodies[0]).position.Y;
        bool clean = true;
        for (int i = 0; i < 30; ++i)
        {
            clean = clean && !StepFailed(world.Step(Dt));
        }
        Check(clean, "stepping a full world reports no error");
        Check(world.GetPose(bodies[0]).position.Y < startY, "the bodies still fall");

        world.DestroyBody(bodies[2]);
        Check(world.CreateBody(BoxBody({ 0, 9, 0 })).IsValid(), "a freed place can be used again");
        Check(!world.CreateBody(BoxBody({ 0, 9, 3 })).IsValid(), "and the world is full again");
    }

    void InvalidInputReturnsInvalidIds(JoltRuntime& runtime)
    {
        BeginCase("invalid descriptions return an invalid id and never throw");
        JoltWorld world(runtime);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(!world.CreateBody(PhysicsBodyDesc{}).IsValid(), "no parts");
        Check(!world.CreateBody(BoxBody({ 0, 0, 0 }, { 0.5f, 0.0f, 0.5f })).IsValid(), "zero half extent");
        Check(!world.CreateBody(BoxBody({ 0, 0, 0 }, { 0.5f, -1.0f, 0.5f })).IsValid(), "negative half extent");
        Check(!world.CreateBody(BoxBody({ 0, 0, 0 }, { 0.5f, nan, 0.5f })).IsValid(), "NaN half extent");
        Check(!world.CreateBody(BoxBody({ 0, 0, 0 }, { 0.5f, std::numeric_limits<float>::infinity(), 0.5f })).IsValid(), "infinite half extent");
        Check(!world.CreateBody(BodyOf({ SpherePart(0.0f) }, { 0, 0, 0 })).IsValid(), "zero radius");
        Check(!world.CreateBody(BoxBody({ nan, 0, 0 })).IsValid(), "NaN position");
        Check(!world.CreateBody(BoxBody({ 0, 0, 0 }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Dynamic, Quaternion(0, 0, 0, 2.0f))).IsValid(),
            "non-unit rotation");
        Check(!world.CreateBody(BodyOf({ BoxPart({ 0.5f, 0.5f, 0.5f }, {}, -1.0f) }, { 0, 0, 0 })).IsValid(), "negative friction override");
        PhysicsBodyDesc negativeMass = BoxBody({ 0, 0, 0 });
        negativeMass.settings.mass = -1.0f;
        Check(!world.CreateBody(negativeMass).IsValid(), "negative mass");
        Check(world.GetBodyCount() == 0, "no failed creation left a body behind");
    }

    void StaleAndForeignIdsAreHarmless(JoltRuntime& runtime)
    {
        BeginCase("stale, default and foreign ids are harmless");
        JoltWorld world(runtime);
        JoltWorld other(runtime);
        const PhysicsBodyId first = world.CreateBody(BoxBody({ 1, 2, 3 }));
        const PhysicsBodyId foreign = other.CreateBody(BoxBody({ 1, 2, 3 }));
        Check(first.index == foreign.index, "the two ids name the same slot of different worlds");
        Check(!world.IsValid(foreign) && !other.IsValid(first), "a foreign id is invalid");
        Check(world.GetMass(foreign) == 0.0f && world.GetPose(foreign).position.X == 0.0f, "queries on a foreign id return defaults");

        world.DestroyBody(first);
        world.DestroyBody(first);
        world.DestroyBody(PhysicsBodyId{});
        world.DestroyBody(foreign);
        Check(other.IsValid(foreign), "a foreign destroy touched nothing");
        Check(world.GetBodyCount() == 0, "destroying twice removes once");

        const PhysicsBodyId second = world.CreateBody(BoxBody({ 4, 5, 6 }));
        Check(second.index == first.index && second.generation != first.generation, "the slot is reused with a new generation");
        Check(!world.IsValid(first) && world.IsValid(second), "the old id stays dead after the slot is reused");

        world.AddImpulse(first, { 1, 1, 1 });
        world.AddImpulseAt(first, { 1, 1, 1 }, { 0, 0, 0 });
        world.AddImpulse(PhysicsBodyId{}, { 1, 1, 1 });
        Check(world.GetLinearVelocity(second).Length() == 0.0f, "an impulse on a stale id did not reach the new body");
        Check(world.GetLinearVelocity(first).Length() == 0.0f && world.GetAngularVelocity(first).Length() == 0.0f, "stale velocity queries return zero");
        Check(!world.IsAwake(first) && world.GetMotion(first) == PhysicsMotion::Static && world.QueryBox({ 4, 5, 6 }, { 1, 1, 1 }).size() == 1,
            "stale awake and motion queries return defaults and the query sees the live body");
        Check(Bits(world.GetPose(PhysicsBodyId{}).rotation.W) == Bits(1.0f), "the default pose is the identity");
    }

    void ImpulsesMoveOnlyDynamicBodies(JoltRuntime& runtime)
    {
        BeginCase("impulses wake sleepers, add spin at a point and ignore non-dynamic bodies");
        PhysicsWorldDesc desc;
        desc.gravity = { 0.0f, 0.0f, 0.0f };
        JoltWorld world(runtime, desc);
        const PhysicsBodyId box = world.CreateBody(BoxBody({ 0, 0, 0 }));
        const PhysicsBodyId fixed = world.CreateBody(BoxBody({ 5, 0, 0 }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Static));
        for (int i = 0; i < 120 && world.IsAwake(box); ++i)
        {
            world.Step(Dt);
        }
        Check(!world.IsAwake(box), "the body at rest sleeps");
        world.AddImpulse(box, { 1000.0f, 0.0f, 0.0f });
        Check(world.IsAwake(box), "an impulse wakes the sleeper");
        Check(Near(world.GetLinearVelocity(box).X, 1.0f, 1.0e-3f), "the impulse changes the velocity by impulse over mass");
        world.AddImpulse(fixed, { 1000.0f, 0.0f, 0.0f });
        Check(world.GetLinearVelocity(fixed).Length() == 0.0f, "a static body ignores impulses");

        const PhysicsBodyId spun = world.CreateBody(BoxBody({ 0, 10, 0 }));
        world.AddImpulseAt(spun, { 0.0f, 0.0f, 1000.0f }, { 0.5f, 10.0f, 0.0f });
        Check(world.GetAngularVelocity(spun).Length() > 0.1f, "an impulse off the centre adds spin");
    }

    void PointVelocityFollowsTheSpin(JoltRuntime& runtime)
    {
        BeginCase("a point's velocity is the linear velocity plus the spin across the offset from the centre");
        PhysicsWorldDesc desc;
        desc.gravity = { 0.0f, 0.0f, 0.0f };
        JoltWorld world(runtime, desc);
        const PhysicsBodyId box = world.CreateBody(BoxBody({ 0, 0, 0 }));
        world.AddImpulse(box, { 300.0f, 0.0f, 0.0f });
        world.AddImpulseAt(box, { 0.0f, 0.0f, 500.0f }, { 0.5f, 0.0f, 0.0f });
        const Vector3 linear = world.GetLinearVelocity(box);
        const Vector3 angular = world.GetAngularVelocity(box);
        const Vector3 point{ 0.0f, 0.5f, 0.0f };
        const Vector3 expected = linear + Vector3::Cross(angular, point - world.GetPose(box).position);
        Check(angular.Length() > 0.1f, "the body spins");
        Check((world.GetPointVelocity(box, point) - expected).Length() < 1.0e-4f, "the point velocity matches v + w x r");
        Check((world.GetPointVelocity(box, world.GetPose(box).position) - linear).Length() < 1.0e-4f, "the centre moves at the linear velocity");
        Check(world.GetPointVelocity(PhysicsBodyId{}, point).Length() == 0.0f, "a missing body has no point velocity");
    }

    void StatsCountBodies(JoltRuntime& runtime)
    {
        BeginCase("stats report live and active bodies");
        JoltWorld world(runtime);
        world.CreateBody(BoxBody({ 0, -0.5f, 0 }, { 5, 0.5f, 5 }, PhysicsMotion::Static));
        world.CreateBody(BoxBody({ 0, 3, 0 }));
        world.CreateBody(BoxBody({ 2, 3, 0 }));
        PhysicsWorldStats stats = world.GetStats();
        Check(stats.bodyCount == 3 && world.GetBodyCount() == 3, "three bodies");
        Check(stats.activeBodies == 2, "the two dynamic bodies are active");
        Check(HasStepError(PhysicsStepError::BodyPairCacheFull | PhysicsStepError::NoRuntime, PhysicsStepError::NoRuntime), "step error flags combine");
        Check(StepFailed(world.Step(-1.0f)) && StepFailed(world.Step(std::numeric_limits<float>::infinity())), "a bad time step is reported");
    }

    Vector3 Rotate(const Quaternion& rotation, const Vector3& v)
    {
        return ToCanape(ToJolt(rotation) * ToJolt(v));
    }

    PhysicsBodyId MakeFloor(JoltWorld& world)
    {
        return world.CreateBody(BoxBody({ 0.0f, -0.5f, 0.0f }, { 40.0f, 0.5f, 20.0f }, PhysicsMotion::Static));
    }

    float SphereMassAtDensity(float radius, float density)
    {
        return density * 4.0f / 3.0f * Pi * radius * radius * radius;
    }

    PhysicsBodyDesc SphereBody(const Vector3& position, float radius, float density)
    {
        PhysicsBodyDesc desc = BodyOf({ SpherePart(radius) }, position);
        desc.settings.mass = SphereMassAtDensity(radius, density);
        return desc;
    }

    // A lever of half length 1 whose pivot is its end at -x, hinged about +z; falling swings it to negative angles.
    PhysicsHingeDesc LeverHinge(PhysicsBodyId body, const Vector3& pivot, float minAngle, float maxAngle, PhysicsBodyId mount = {})
    {
        PhysicsHingeDesc desc;
        desc.body = body;
        desc.mount = mount;
        desc.pivot = pivot;
        desc.axis = { 0.0f, 0.0f, 1.0f };
        desc.normal = { 1.0f, 0.0f, 0.0f };
        desc.minAngle = minAngle;
        desc.maxAngle = maxAngle;
        return desc;
    }

    PhysicsBodyDesc LeverBody(const Vector3& pivot)
    {
        return BoxBody(pivot + Vector3(1.0f, 0.0f, 0.0f), { 1.0f, 0.1f, 0.1f });
    }

    void KinematicHeldTargetLeavesZeroVelocity(JoltRuntime& runtime)
    {
        BeginCase("a kinematic target held every step leaves the velocity exactly zero");
        JoltWorld world(runtime);
        const Quaternion rotation = AxisAngle({ 0.0f, 1.0f, 0.0f }, 0.9f);
        const PhysicsBodyId body = world.CreateBody(BoxBody({ 1.3f, 7.7f, -2.1f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic, rotation));
        const PhysicsPose start = world.GetPose(body);
        bool exactlyZero = true;
        for (int i = 0; i < 120; ++i)
        {
            world.MoveKinematic(body, start, Dt);
            world.Step(Dt);
            exactlyZero = exactlyZero && Bits(world.GetLinearVelocity(body).Length()) == Bits(0.0f) && Bits(world.GetAngularVelocity(body).Length()) == Bits(0.0f);
        }
        Check(exactlyZero, "the velocity read after every step is exactly zero");
        const PhysicsPose end = world.GetPose(body);
        Check(Near(end.position.X, start.position.X, 1.0e-5f) && Near(end.position.Y, start.position.Y, 1.0e-5f) && Near(end.position.Z, start.position.Z, 1.0e-5f),
            "gravity did not move the kinematic body");
    }

    void KinematicMovingTargetGivesDeltaOverDt(JoltRuntime& runtime)
    {
        BeginCase("a moving kinematic target gives velocity delta over dt and arrives; holding it then stops the body");
        JoltWorld world(runtime);
        const PhysicsBodyId body = world.CreateBody(BoxBody({ 0.0f, 2.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic));
        const Vector3 stepDelta(0.05f, 0.0f, -0.02f);
        bool velocityMatches = true;
        for (int i = 1; i <= 100; ++i)
        {
            const Vector3 target = Vector3(0.0f, 2.0f, 0.0f) + stepDelta * static_cast<float>(i);
            world.MoveKinematic(body, PhysicsPose{ target, Quaternion{} }, Dt);
            world.Step(Dt);
            const Vector3 velocity = world.GetLinearVelocity(body);
            velocityMatches = velocityMatches && Near(velocity.X, stepDelta.X / Dt, 2.0e-3f) && Near(velocity.Z, stepDelta.Z / Dt, 2.0e-3f) && Near(velocity.Y, 0.0f, 1.0e-3f);
        }
        Check(velocityMatches, "every step reads (delta / dt)");
        const Vector3 goal = Vector3(0.0f, 2.0f, 0.0f) + stepDelta * 100.0f;
        const PhysicsPose arrived = world.GetPose(body);
        Check(Near(arrived.position.X, goal.X, 1.0e-3f) && Near(arrived.position.Y, goal.Y, 1.0e-3f) && Near(arrived.position.Z, goal.Z, 1.0e-3f),
            "the pose arrived at the target");

        for (int i = 0; i < 5; ++i)
        {
            world.MoveKinematic(body, PhysicsPose{ goal, Quaternion{} }, Dt);
            world.Step(Dt);
        }
        Check(Bits(world.GetLinearVelocity(body).Length()) == Bits(0.0f), "the velocity drops to exactly zero once the target is held");
    }

    void KinematicRotationArrives(JoltRuntime& runtime)
    {
        BeginCase("a kinematic rotation target arrives and a non-normalised quaternion is normalised");
        JoltWorld world(runtime);
        const PhysicsBodyId body = world.CreateBody(BoxBody({ 0.0f, 2.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic));
        const Quaternion unit = AxisAngle({ 0.0f, 0.0f, 1.0f }, 0.6f);
        const float scale = 1.0004f;
        const Quaternion loose(unit.X * scale, unit.Y * scale, unit.Z * scale, unit.W * scale);
        for (int i = 0; i < 90; ++i)
        {
            world.MoveKinematic(body, PhysicsPose{ { 0.0f, 2.0f, 0.0f }, loose }, Dt);
            world.Step(Dt);
        }
        const Quaternion rotation = world.GetPose(body).rotation;
        Check(Near(rotation.X, unit.X, 1.0e-4f) && Near(rotation.Y, unit.Y, 1.0e-4f) && Near(rotation.Z, unit.Z, 1.0e-4f) && Near(rotation.W, unit.W, 1.0e-4f),
            "the body reached the normalised target rotation");
        Check(world.GetAngularVelocity(body).Length() == 0.0f, "the held rotation leaves no spin");

        const Quaternion wild(unit.X * 3.0f, unit.Y * 3.0f, unit.Z * 3.0f, unit.W * 3.0f);
        world.MoveKinematic(body, PhysicsPose{ { 5.0f, 2.0f, 0.0f }, wild }, Dt);
        world.Step(Dt);
        Check(Near(world.GetPose(body).position.X, 0.0f, 1.0e-5f), "a target far from unit length is ignored");
    }

    void KinematicMisuseIsHarmless(JoltRuntime& runtime)
    {
        BeginCase("moving a non-kinematic, stale or invalid target is a safe no-op");
        JoltWorld world(runtime);
        const PhysicsBodyId dynamic = world.CreateBody(BoxBody({ 0.0f, 2.0f, 0.0f }));
        const PhysicsBodyId fixed = world.CreateBody(BoxBody({ 5.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Static));
        const PhysicsBodyId kinematic = world.CreateBody(BoxBody({ 0.0f, 9.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic));
        const PhysicsBodyId gone = world.CreateBody(BoxBody({ 0.0f, 20.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic));
        world.DestroyBody(gone);

        const PhysicsPose far{ { 50.0f, 50.0f, 50.0f }, Quaternion{} };
        world.MoveKinematic(dynamic, far, Dt);
        world.MoveKinematic(fixed, far, Dt);
        world.MoveKinematic(gone, far, Dt);
        world.MoveKinematic(PhysicsBodyId{}, far, Dt);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        world.MoveKinematic(kinematic, PhysicsPose{ { nan, 0.0f, 0.0f }, Quaternion{} }, Dt);
        world.MoveKinematic(kinematic, PhysicsPose{ { 0.0f, 0.0f, 0.0f }, Quaternion{ nan, 0.0f, 0.0f, 1.0f } }, Dt);
        world.MoveKinematic(kinematic, far, 0.0f);
        world.MoveKinematic(kinematic, far, -1.0f);
        world.MoveKinematic(kinematic, far, nan);
        world.Step(Dt);
        Check(world.GetPose(dynamic).position.X == 0.0f && world.GetPose(fixed).position.X == 5.0f, "dynamic and static bodies did not move");
        Check(world.GetLinearVelocity(kinematic).Length() == 0.0f && world.GetPose(kinematic).position.Y == 9.0f, "bad targets and time steps changed nothing");
    }

    void DefaultHingeDescCreatesAFullRangeHinge(JoltRuntime& runtime)
    {
        BeginCase("a default hinge desc with only the body, pivot and axis set creates a hinge");
        JoltWorld world(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        PhysicsHingeDesc desc;
        desc.body = lever;
        desc.pivot = pivot;
        desc.axis = { 0.0f, 0.0f, 1.0f };
        const PhysicsHingeId hinge = world.CreateHinge(desc);
        Check(hinge.IsValid() && world.IsValid(hinge) && world.IsHingeHeld(hinge), "the default limits are accepted");
    }

    void HingeHoldsThePivotAndStopsAtTheLimit(JoltRuntime& runtime)
    {
        BeginCase("a held hinge keeps the pivot fixed and its limit stops a falling lever");
        JoltWorld world(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        const PhysicsHingeId hinge = world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f));
        Check(hinge.IsValid() && world.IsValid(hinge) && world.IsHingeHeld(hinge), "the hinge is created and held");
        Check(Near(world.GetHingeAngle(hinge), 0.0f, 1.0e-6f), "the angle reads 0 at creation");

        float worstPivotError = 0.0f;
        float lowestAngle = 0.0f;
        float highestAngle = 0.0f;
        for (int i = 0; i < 300; ++i)
        {
            world.Step(Dt);
            const PhysicsPose pose = world.GetPose(lever);
            const Vector3 pivotPoint = pose.position + Rotate(pose.rotation, { -1.0f, 0.0f, 0.0f });
            worstPivotError = std::max(worstPivotError, (pivotPoint - pivot).Length());
            lowestAngle = std::min(lowestAngle, world.GetHingeAngle(hinge));
            highestAngle = std::max(highestAngle, world.GetHingeAngle(hinge));
        }
        Check(worstPivotError < 0.02f, std::format("the pivot point stayed fixed, even at the impact on the limit (worst error {})", worstPivotError));
        const PhysicsPose rest = world.GetPose(lever);
        Check(((rest.position + Rotate(rest.rotation, { -1.0f, 0.0f, 0.0f })) - pivot).Length() < 2.0e-3f, "the pivot point is exact once the lever rests");
        Check(highestAngle < 0.02f, std::format("the angle never passed the upper limit ({})", highestAngle));
        Check(lowestAngle > -Pi / 4.0f - 0.05f, std::format("the angle never passed the lower limit ({})", lowestAngle));
        Check(Near(world.GetHingeAngle(hinge), -Pi / 4.0f, 0.05f), std::format("the lever rests on the lower limit ({})", world.GetHingeAngle(hinge)));
        Check(world.IsHingeHeld(hinge), "the hinge is still held");
    }

    void HingeReleaseFreesAndWakesTheBody(JoltRuntime& runtime)
    {
        BeginCase("releasing a hinge wakes the body and lets it fall; ids survive and a second release is a no-op");
        JoltWorld world(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        const PhysicsHingeId hinge = world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f));
        for (int i = 0; i < 900 && world.IsAwake(lever); ++i)
        {
            world.Step(Dt);
        }
        Check(!world.IsAwake(lever), "the held lever came to rest and sleeps");

        world.ReleaseHinge(hinge);
        Check(world.IsValid(hinge) && !world.IsHingeHeld(hinge), "the id stays valid and the hinge is no longer held");
        Check(world.IsAwake(lever), "the release woke the body");
        const float heightBefore = world.GetPose(lever).position.Y;
        for (int i = 0; i < 30; ++i)
        {
            world.Step(Dt);
        }
        Check(heightBefore - world.GetPose(lever).position.Y > 0.5f, "the released lever falls freely");

        world.ReleaseHinge(hinge);
        world.ReleaseHinge(hinge);
        Check(world.IsValid(hinge) && !world.IsHingeHeld(hinge), "releasing again changes nothing");
        world.ReleaseHinge(PhysicsHingeId{});
        Check(!world.IsValid(PhysicsHingeId{}) && !world.IsHingeHeld(PhysicsHingeId{}) && world.GetHingeAngle(PhysicsHingeId{}) == 0.0f,
            "an invalid hinge id reads defaults");
    }

    void HingeSecondReleaseDoesNotWakeASleeper(JoltRuntime& runtime)
    {
        BeginCase("a second release does not wake a body that fell asleep after the first");
        PhysicsWorldDesc desc;
        desc.gravity = { 0.0f, 0.0f, 0.0f };
        JoltWorld world(runtime, desc);
        const PhysicsBodyId body = world.CreateBody(LeverBody({ 0.0f, 5.0f, 0.0f }));
        const PhysicsHingeId hinge = world.CreateHinge(LeverHinge(body, { 0.0f, 5.0f, 0.0f }, -Pi / 4.0f, 0.0f));
        world.ReleaseHinge(hinge);
        for (int i = 0; i < 300 && world.IsAwake(body); ++i)
        {
            world.Step(Dt);
        }
        Check(!world.IsAwake(body), "the released body in zero gravity sleeps");
        world.ReleaseHinge(hinge);
        Check(!world.IsAwake(body), "the second release left it asleep");
    }

    void HingeFailuresReturnInvalidIds(JoltRuntime& runtime)
    {
        BeginCase("invalid hinge input returns an invalid id and creates nothing");
        JoltWorld world(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        const PhysicsBodyId fixed = world.CreateBody(BoxBody({ 5.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Static));
        const PhysicsBodyId moving = world.CreateBody(BoxBody({ 8.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic));
        const PhysicsBodyId gone = world.CreateBody(BoxBody({ 12.0f, 0.0f, 0.0f }));
        world.DestroyBody(gone);

        const PhysicsHingeDesc good = LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto rejects = [&world](const PhysicsHingeDesc& desc) { return !world.CreateHinge(desc).IsValid(); };
        const auto with = [&good](auto change)
        {
            PhysicsHingeDesc desc = good;
            change(desc);
            return desc;
        };

        Check(rejects(LeverHinge(fixed, pivot, -Pi / 4.0f, 0.0f)), "a static body is refused");
        Check(rejects(LeverHinge(moving, pivot, -Pi / 4.0f, 0.0f)), "a kinematic body is refused");
        Check(rejects(LeverHinge(gone, pivot, -Pi / 4.0f, 0.0f)), "a destroyed body is refused");
        Check(rejects(LeverHinge(PhysicsBodyId{}, pivot, -Pi / 4.0f, 0.0f)), "the default id is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.axis = { 0.0f, 0.0f, 0.0f }; })), "a zero axis is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.axis = { 0.0f, 0.0f, 2.0f }; })), "a long axis is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.normal = { 0.0f, 0.0f, 1.0f }; })), "a normal parallel to the axis is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.normal = { 0.7071f, 0.0f, 0.7071f }; })), "a normal that is not perpendicular is refused");
        Check(rejects(with([nan](PhysicsHingeDesc& d) { d.axis = { nan, 0.0f, 1.0f }; })), "a NaN axis is refused");
        Check(rejects(with([nan](PhysicsHingeDesc& d) { d.pivot = { nan, 0.0f, 0.0f }; })), "a NaN pivot is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.minAngle = 0.1f; })), "a positive minimum is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.minAngle = -Pi - 0.01f; })), "a minimum below -pi is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.maxAngle = -0.1f; })), "a negative maximum is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.maxAngle = Pi + 0.01f; })), "a maximum above pi is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.minAngle = 0.0f; d.maxAngle = 0.0f; })), "empty limits are refused");
        Check(rejects(with([nan](PhysicsHingeDesc& d) { d.maxAngle = nan; })), "a NaN limit is refused");
        Check(rejects(with([](PhysicsHingeDesc& d) { d.maxFrictionTorque = -1.0f; })), "a negative friction torque is refused");
        Check(rejects(with([lever](PhysicsHingeDesc& d) { d.mount = lever; })), "the held body as its own mount is refused");
        Check(rejects(with([gone](PhysicsHingeDesc& d) { d.mount = gone; })), "a destroyed mount is refused");

        const PhysicsHingeId first = world.CreateHinge(good);
        Check(first.IsValid(), "the unmodified description is accepted");
        Check(world.CreateHinge(LeverHinge(lever, pivot, -Pi, Pi)).IsValid(), "full-range limits are accepted");
        Check(world.CreateHinge(LeverHinge(lever, pivot, -Pi / 2.0f, Pi / 2.0f)).IsValid(), "symmetric limits are accepted");
        Check(world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f, fixed)).IsValid(), "a static mount is accepted");
        Check(world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f, moving)).IsValid(), "a kinematic mount is accepted");
    }

    void HingeDiesWithEitherBody(JoltRuntime& runtime)
    {
        BeginCase("destroying the held body or the mount of a hinge retires the hinge and the world keeps stepping");
        JoltWorld world(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        const PhysicsHingeId hinge = world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f));
        world.Step(Dt);
        world.DestroyBody(lever);
        Check(!world.IsValid(hinge) && !world.IsHingeHeld(hinge) && world.GetHingeAngle(hinge) == 0.0f, "the hinge is invalid once its body is gone");
        world.ReleaseHinge(hinge);
        world.DestroyHinge(hinge);
        Check(!StepFailed(world.Step(Dt)), "stepping after the body is gone is fine");

        const PhysicsBodyId reused = world.CreateBody(LeverBody(pivot));
        Check(reused.index == lever.index && !world.IsValid(hinge), "the stale hinge does not attach to the body that reuses the slot");
        const PhysicsHingeId again = world.CreateHinge(LeverHinge(reused, pivot, -Pi / 4.0f, 0.0f));
        Check(again.IsValid() && again != hinge, "a new hinge gets a fresh id");
        world.Step(Dt);
        Check(world.IsHingeHeld(again), "the new hinge is held");

        const Vector3 mountedPivot(10.0f, 5.0f, 0.0f);
        const PhysicsBodyId mount = world.CreateBody(BoxBody(mountedPivot + Vector3(0.0f, 0.0f, 1.0f), { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Static));
        const PhysicsBodyId mountedLever = world.CreateBody(LeverBody(mountedPivot));
        const PhysicsHingeId mounted = world.CreateHinge(LeverHinge(mountedLever, mountedPivot, -Pi / 4.0f, 0.0f, mount));
        world.Step(Dt);
        Check(world.IsHingeHeld(mounted), "the mounted hinge is held");
        world.DestroyBody(mount);
        Check(!world.IsValid(mounted) && !world.IsHingeHeld(mounted), "the hinge is invalid once its mount is gone");
        const float heightBefore = world.GetPose(mountedLever).position.Y;
        for (int i = 0; i < 30; ++i)
        {
            world.Step(Dt);
        }
        Check(heightBefore - world.GetPose(mountedLever).position.Y > 0.5f, "the lever falls freely without its mount");
        Check(!StepFailed(world.Step(Dt)) && world.IsValid(mountedLever), "stepping after the mount is gone is fine");
    }

    void HingeWorldTeardownWithLiveHinges(JoltRuntime& runtime)
    {
        BeginCase("a world with live, released, mounted and destroyed hinges tears down cleanly");
        {
            JoltWorld world(runtime);
            const PhysicsBodyId a = world.CreateBody(LeverBody({ 0.0f, 5.0f, 0.0f }));
            const PhysicsBodyId b = world.CreateBody(LeverBody({ 0.0f, 8.0f, 0.0f }));
            const PhysicsBodyId c = world.CreateBody(LeverBody({ 0.0f, 11.0f, 0.0f }));
            const PhysicsBodyId mount = world.CreateBody(BoxBody({ 0.0f, 10.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Static));
            world.CreateHinge(LeverHinge(a, { 0.0f, 5.0f, 0.0f }, -Pi / 4.0f, 0.0f));
            world.ReleaseHinge(world.CreateHinge(LeverHinge(b, { 0.0f, 8.0f, 0.0f }, -Pi / 4.0f, 0.0f)));
            world.CreateHinge(LeverHinge(c, { 0.0f, 11.0f, 0.0f }, -Pi / 4.0f, 0.0f, mount));
            world.DestroyHinge(world.CreateHinge(LeverHinge(c, { 0.0f, 11.0f, 0.0f }, -Pi, Pi)));
            world.Step(Dt);
        }
        JoltWorld again(runtime);
        Check(again.GetBodyCount() == 0, "a new world is fine after the teardown");
    }

    void HingeOnAStaticMountHoldsLikeTheWorld(JoltRuntime& runtime)
    {
        BeginCase("a hinge mounted on a static body swings exactly like one fixed to the world");
        JoltWorld fixedWorld(runtime);
        JoltWorld mountedWorld(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        // The mount sits behind the pivot along the axis, clear of the lever's sweep.
        const PhysicsBodyId mount = mountedWorld.CreateBody(BoxBody(pivot + Vector3(0.0f, 0.0f, 1.0f), { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Static));
        const PhysicsBodyId fixedLever = fixedWorld.CreateBody(LeverBody(pivot));
        const PhysicsBodyId mountedLever = mountedWorld.CreateBody(LeverBody(pivot));
        const PhysicsHingeId fixedHinge = fixedWorld.CreateHinge(LeverHinge(fixedLever, pivot, -Pi / 4.0f, 0.0f));
        const PhysicsHingeId mountedHinge = mountedWorld.CreateHinge(LeverHinge(mountedLever, pivot, -Pi / 4.0f, 0.0f, mount));
        Check(mountedHinge.IsValid() && mountedWorld.IsHingeHeld(mountedHinge), "the mounted hinge is created and held");

        float worstDifference = 0.0f;
        for (int i = 0; i < 300; ++i)
        {
            fixedWorld.Step(Dt);
            mountedWorld.Step(Dt);
            const PhysicsPose fixedPose = fixedWorld.GetPose(fixedLever);
            const PhysicsPose mountedPose = mountedWorld.GetPose(mountedLever);
            worstDifference = std::max(worstDifference, (fixedPose.position - mountedPose.position).Length());
            worstDifference = std::max(worstDifference, std::abs(fixedWorld.GetHingeAngle(fixedHinge) - mountedWorld.GetHingeAngle(mountedHinge)));
        }
        Check(worstDifference < 1.0e-3f, std::format("the mounted lever follows the fixed one (worst difference {})", worstDifference));
        Check(Near(mountedWorld.GetHingeAngle(mountedHinge), -Pi / 4.0f, 0.05f), "the mounted lever rests on the lower limit");
        Check(mountedWorld.GetPose(mount).position.Z == pivot.Z + 1.0f, "the static mount did not move");
    }

    void HingeOnADynamicMountMovesBothBodies(JoltRuntime& runtime)
    {
        BeginCase("a hinge between two dynamic bodies keeps them joined while they drift");
        PhysicsWorldDesc desc;
        desc.gravity = { 0.0f, 0.0f, 0.0f };
        JoltWorld world(runtime, desc);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId mount = world.CreateBody(BoxBody(pivot + Vector3(0.0f, 0.0f, 1.0f), { 0.5f, 0.5f, 0.5f }));
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        const PhysicsHingeId hinge = world.CreateHinge(LeverHinge(lever, pivot, -Pi, Pi, mount));
        Check(hinge.IsValid() && world.IsHingeHeld(hinge), "the hinge between two dynamic bodies is created and held");

        world.AddImpulse(lever, { 0.0f, 0.0f, 200.0f });
        float worstGap = 0.0f;
        for (int i = 0; i < 120; ++i)
        {
            world.Step(Dt);
            const PhysicsPose leverPose = world.GetPose(lever);
            const PhysicsPose mountPose = world.GetPose(mount);
            const Vector3 pivotPoint = leverPose.position + Rotate(leverPose.rotation, { -1.0f, 0.0f, 0.0f });
            const Vector3 mountPoint = mountPose.position + Rotate(mountPose.rotation, { 0.0f, 0.0f, -1.0f });
            worstGap = std::max(worstGap, (pivotPoint - mountPoint).Length());
        }
        Check(worstGap < 0.02f, std::format("the pivot stays on the mount (worst gap {})", worstGap));
        Check(world.GetPose(mount).position.Z > 0.1f, "the mount was carried along by the lever");
        Check(world.IsAwake(mount) && world.IsAwake(lever), "both bodies are awake");

        world.ReleaseHinge(hinge);
        Check(world.IsAwake(mount) && world.IsAwake(lever) && !world.IsHingeHeld(hinge), "the release leaves both bodies awake and the hinge released");
    }

    void DestroyHingeFreesTheBodyAndRetiresTheId(JoltRuntime& runtime)
    {
        BeginCase("destroying a hinge frees the body at once and retires the id");
        JoltWorld world(runtime);
        const Vector3 pivot(0.0f, 5.0f, 0.0f);
        const PhysicsBodyId lever = world.CreateBody(LeverBody(pivot));
        const PhysicsHingeId hinge = world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f));
        world.Step(Dt);
        world.DestroyHinge(hinge);
        Check(!world.IsValid(hinge) && !world.IsHingeHeld(hinge) && world.GetHingeAngle(hinge) == 0.0f, "the destroyed hinge reads as invalid");
        const float heightBefore = world.GetPose(lever).position.Y;
        for (int i = 0; i < 30; ++i)
        {
            world.Step(Dt);
        }
        Check(heightBefore - world.GetPose(lever).position.Y > 0.5f, "the lever falls freely once the hinge is destroyed");

        world.DestroyHinge(hinge);
        world.DestroyHinge(PhysicsHingeId{});
        world.ReleaseHinge(hinge);
        Check(!StepFailed(world.Step(Dt)), "destroying again, an invalid id or releasing a destroyed hinge is harmless");

        const PhysicsHingeId released = world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f));
        world.ReleaseHinge(released);
        world.DestroyHinge(released);
        Check(!world.IsValid(released), "a released hinge can be destroyed");
        const PhysicsHingeId again = world.CreateHinge(LeverHinge(lever, pivot, -Pi / 4.0f, 0.0f));
        Check(again.IsValid() && again != hinge && again != released && world.IsHingeHeld(again), "a new hinge on the same body gets a fresh id");
    }

    PhysicsWaterDesc Pool()
    {
        PhysicsWaterDesc water;
        water.center = { 0.0f, 2.0f, 0.0f };
        water.halfExtent = { 5.0f, 2.0f, 5.0f };
        return water;
    }

    PhysicsWaterDesc FarPool()
    {
        PhysicsWaterDesc water;
        water.center = { 100.0f, 2.0f, 0.0f };
        water.halfExtent = { 1.0f, 1.0f, 1.0f };
        return water;
    }

    void RunWater(JoltWorld& world, const PhysicsWaterDesc& water, int ticks)
    {
        for (int i = 0; i < ticks; ++i)
        {
            world.ApplyWater(water, Dt);
            world.Step(Dt);
        }
    }

    void WaterFloatsLightAndSinksHeavy(JoltRuntime& runtime)
    {
        BeginCase("a light sphere rises to the surface and a heavy one sinks to the floor");
        JoltWorld world(runtime);
        MakeFloor(world);
        const PhysicsBodyId light = world.CreateBody(SphereBody({ -2.0f, 1.0f, 0.0f }, 0.5f, 500.0f));
        const PhysicsBodyId heavy = world.CreateBody(SphereBody({ 2.0f, 3.0f, 0.0f }, 0.5f, 2000.0f));
        Check(Near(world.GetMass(light), SphereMassAtDensity(0.5f, 500.0f), 1.0e-4f), "the light sphere has the requested mass");
        RunWater(world, Pool(), 600);
        const float lightY = world.GetPose(light).position.Y;
        const float heavyY = world.GetPose(heavy).position.Y;
        Check(lightY > 3.4f && lightY < 4.6f, std::format("the light sphere floats at the surface (y = {})", lightY));
        Check(heavyY > 0.45f && heavyY < 0.6f, std::format("the heavy sphere rests on the floor (y = {})", heavyY));
        Check(Near(world.GetPose(light).position.X, -2.0f, 0.05f) && Near(world.GetPose(heavy).position.X, 2.0f, 0.05f), "neither sphere drifted sideways");
    }

    void WaterLeavesSleepersAlone(JoltRuntime& runtime)
    {
        BeginCase("a sleeping body inside the water stays asleep and unmoved");
        JoltWorld world(runtime);
        MakeFloor(world);
        PhysicsBodyDesc desc = BoxBody({ 0.0f, 0.5f, 0.0f });
        desc.settings.mass = 500.0f;
        const PhysicsBodyId sleeper = world.CreateBody(desc);
        const PhysicsBodyId bystander = world.CreateBody(BoxBody({ 3.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f }, PhysicsMotion::Kinematic));
        for (int i = 0; i < 600 && world.IsAwake(sleeper); ++i)
        {
            world.Step(Dt);
        }
        Check(!world.IsAwake(sleeper), "the body at rest sleeps");
        const PhysicsPose before = world.GetPose(sleeper);
        RunWater(world, Pool(), 120);
        const PhysicsPose after = world.GetPose(sleeper);
        Check(!world.IsAwake(sleeper), "the water did not wake it");
        Check(Bits(before.position.X) == Bits(after.position.X) && Bits(before.position.Y) == Bits(after.position.Y) && Bits(before.position.Z) == Bits(after.position.Z)
                && Bits(before.rotation.W) == Bits(after.rotation.W),
            "the sleeper did not move a bit");
        const Vector3 linear = world.GetLinearVelocity(sleeper);
        const Vector3 angular = world.GetAngularVelocity(sleeper);
        Check(Bits(linear.X) == 0u && Bits(linear.Y) == 0u && Bits(linear.Z) == 0u && Bits(angular.X) == 0u && Bits(angular.Y) == 0u && Bits(angular.Z) == 0u,
            "the sleeper's velocities stayed exactly zero");
        Check(world.GetLinearVelocity(bystander).Length() == 0.0f && world.GetPose(bystander).position.Y == 0.5f, "a kinematic body in the water is untouched");
    }

    std::uint64_t HashAfterWater(JoltRuntime& runtime, const PhysicsWaterDesc& water, const Vector3& spherePosition, float waterDt)
    {
        JoltWorld world(runtime);
        MakeFloor(world);
        const PhysicsBodyId sphere = world.CreateBody(SphereBody(spherePosition, 0.5f, 500.0f));
        for (int i = 0; i < 120; ++i)
        {
            world.ApplyWater(water, waterDt);
            world.Step(Dt);
        }
        return HashBodies(world, { sphere });
    }

    void WaterOutsideTheBoxAndBadDescsChangeNothing(JoltRuntime& runtime)
    {
        BeginCase("water does nothing to a body outside the box, and a degenerate water does nothing at all");
        const Vector3 inside(0.0f, 1.0f, 0.0f);
        const Vector3 outside(12.0f, 1.0f, 0.0f);
        const PhysicsWaterDesc pool = Pool();
        const std::uint64_t dryOutside = HashAfterWater(runtime, FarPool(), outside, Dt);
        const std::uint64_t dryInside = HashAfterWater(runtime, FarPool(), inside, Dt);
        Check(HashAfterWater(runtime, pool, outside, Dt) == dryOutside, "a body outside the box is unaffected");
        Check(HashAfterWater(runtime, pool, inside, Dt) != dryInside, "the same body inside the box is affected");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto degenerate = [&](auto change)
        {
            PhysicsWaterDesc water = pool;
            change(water);
            return HashAfterWater(runtime, water, inside, Dt) == dryInside;
        };
        Check(degenerate([](PhysicsWaterDesc& w) { w.halfExtent.Y = 0.0f; }), "a flat box (zero volume) does nothing");
        Check(degenerate([](PhysicsWaterDesc& w) { w.halfExtent.X = -1.0f; }), "a negative extent does nothing");
        Check(degenerate([](PhysicsWaterDesc& w) { w.density = 0.0f; }), "zero density does nothing");
        Check(degenerate([nan](PhysicsWaterDesc& w) { w.density = nan; }), "NaN density does nothing");
        Check(degenerate([](PhysicsWaterDesc& w) { w.linearDrag = -1.0f; }), "negative drag does nothing");
        Check(degenerate([nan](PhysicsWaterDesc& w) { w.center.X = nan; }), "a NaN centre does nothing");
        Check(HashAfterWater(runtime, pool, inside, 0.0f) == dryInside && HashAfterWater(runtime, pool, inside, nan) == dryInside, "a bad dt does nothing");
    }

    void WaterBodyLeavingTheBoxFallsBack(JoltRuntime& runtime)
    {
        BeginCase("a body thrown out of the water falls back in and is handled each tick");
        JoltWorld world(runtime);
        MakeFloor(world);
        const PhysicsBodyId sphere = world.CreateBody(SphereBody({ 0.0f, 1.0f, 0.0f }, 0.5f, 500.0f));
        const PhysicsWaterDesc pool = Pool();
        world.AddImpulse(sphere, { 0.0f, world.GetMass(sphere) * 14.0f, 0.0f });
        float highest = 0.0f;
        bool finite = true;
        for (int i = 0; i < 400; ++i)
        {
            world.ApplyWater(pool, Dt);
            world.Step(Dt);
            const Vector3 position = world.GetPose(sphere).position;
            highest = std::max(highest, position.Y);
            finite = finite && std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z);
        }
        Check(highest > 4.6f, std::format("the sphere left the box through the top (highest {})", highest));
        Check(finite, "the pose stayed finite");
        const float y = world.GetPose(sphere).position.Y;
        Check(y > 3.2f && y < 4.8f, std::format("it settled back at the surface (y = {})", y));
    }

    // Kinematic track, a held-then-released hinge lever and a pool, all stepped through ApplyWater and MoveKinematic like a game tick.
    class BackendScene
    {
    public:
        static constexpr int ReleaseTick = 200;
        static constexpr int TrackTicks = 300;

        explicit BackendScene(JoltRuntime& runtime, int releaseTick = ReleaseTick)
            : m_World(runtime)
            , m_ReleaseTick(releaseTick)
        {
            MakeFloor(m_World);
            m_Track = m_World.CreateBody(BoxBody({ -10.0f, 0.5f, 0.0f }, { 1.5f, 0.1f, 1.0f }, PhysicsMotion::Kinematic));
            m_Ids.push_back(m_Track);
            m_Ids.push_back(m_World.CreateBody(BoxBody({ -10.0f, 1.2f, 0.0f }, { 0.4f, 0.4f, 0.4f })));

            const Vector3 pivot(0.0f, 3.0f, 5.0f);
            PhysicsHingeDesc hinge = LeverHinge(m_World.CreateBody(LeverBody(pivot)), pivot, -Pi / 3.0f, 0.0f);
            hinge.maxFrictionTorque = 0.5f;
            m_Ids.push_back(hinge.body);
            m_Hinge = m_World.CreateHinge(hinge);

            m_Ids.push_back(m_World.CreateBody(SphereBody({ 9.0f, 1.0f, 0.0f }, 0.5f, 500.0f)));
            m_Ids.push_back(m_World.CreateBody(SphereBody({ 11.0f, 3.0f, 0.0f }, 0.5f, 2000.0f)));
            PhysicsBodyDesc neutral = BoxBody({ 10.0f, 2.0f, 2.0f }, { 0.3f, 0.3f, 0.3f });
            neutral.settings.mass = 216.0f;
            m_Ids.push_back(m_World.CreateBody(neutral));
            m_Water.center = { 10.0f, 2.0f, 0.0f };
            m_Water.halfExtent = { 3.0f, 2.0f, 3.0f };
        }

        void Tick()
        {
            if (m_Tick == m_ReleaseTick)
            {
                m_World.ReleaseHinge(m_Hinge);
            }
            m_World.ApplyWater(m_Water, Dt);
            const float progress = static_cast<float>(std::min(m_Tick + 1, TrackTicks)) / static_cast<float>(TrackTicks);
            m_World.MoveKinematic(m_Track, PhysicsPose{ { -10.0f + 4.0f * progress, 0.5f, 0.0f }, Quaternion{} }, Dt);
            m_Errors |= m_World.Step(Dt);
            ++m_Tick;

            StateHash hash;
            hash.Add(m_Digest);
            hash.Add(HashBodies(m_World, m_Ids));
            hash.Add(m_World.GetHingeAngle(m_Hinge));
            hash.Add(static_cast<std::uint32_t>(m_World.IsHingeHeld(m_Hinge)));
            m_Digest = hash.Value();
        }

        void Run(int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                Tick();
            }
        }

        const JoltWorld& World() const { return m_World; }
        const PhysicsBodyIds& Ids() const { return m_Ids; }
        PhysicsHingeId Hinge() const { return m_Hinge; }
        std::uint64_t Digest() const { return m_Digest; }
        std::uint64_t FinalState() const { return HashBodies(m_World, m_Ids); }
        PhysicsStepError Errors() const { return m_Errors; }

    private:
        JoltWorld m_World;
        PhysicsWaterDesc m_Water;
        PhysicsBodyIds m_Ids;
        PhysicsBodyId m_Track;
        PhysicsHingeId m_Hinge;
        int m_ReleaseTick = ReleaseTick;
        int m_Tick = 0;
        std::uint64_t m_Digest = 0;
        PhysicsStepError m_Errors = PhysicsStepError::None;
    };

    void BackendSceneRebuildsBitIdentical(JoltRuntime& runtime)
    {
        BeginCase("a kinematic track, a hinge release and water give identical bits in fresh worlds");
        std::uint64_t digest = 0;
        std::uint64_t finalState = 0;
        {
            BackendScene a(runtime);
            BackendScene b(runtime);
            bool lockstep = true;
            for (int tick = 0; tick < 600; ++tick)
            {
                a.Tick();
                b.Tick();
                lockstep = lockstep && a.Digest() == b.Digest();
            }
            Check(lockstep, "the running digests match after every tick");
            Check(a.FinalState() == b.FinalState(), "the final states match");
            Check(!StepFailed(a.Errors()) && !StepFailed(b.Errors()), "no step reported an error");
            Check(!a.World().IsHingeHeld(a.Hinge()), "the hinge was released at its tick");
            const Vector3 track = a.World().GetPose(a.Ids()[0]).position;
            Check(Near(track.X, -6.0f, 1.0e-3f) && a.World().GetLinearVelocity(a.Ids()[0]).Length() == 0.0f, "the track arrived and stopped");
            const float sphereY = a.World().GetPose(a.Ids()[3]).position.Y;
            const float heavyY = a.World().GetPose(a.Ids()[4]).position.Y;
            Check(sphereY > 3.2f && heavyY < 0.6f, "the light sphere floats and the heavy one sank");
            digest = a.Digest();
            finalState = a.FinalState();
        }
        std::cout << "DIGEST backend ticks=600 bodies=7 running=" << Hex(digest) << " final=" << Hex(finalState) << '\n';

        BeginCase("destroying the scene and building it again gives the same bits");
        BackendScene again(runtime);
        again.Run(600);
        Check(again.Digest() == digest && again.FinalState() == finalState, "the rebuilt scene has the same digest and final state");

        BeginCase("the backend digest reacts to the release tick");
        BackendScene later(runtime, BackendScene::ReleaseTick + 1);
        later.Run(600);
        Check(later.Digest() != digest, "releasing a tick later changes the digest");
    }
}

int main()
{
    RuntimeConstructsAndTearsDown();
    SecondRuntimeIsRefused();
    ConstructDestroyTwiceInARow();
    InvalidRuntimeWorldIsInert();
    FeatureMismatchNamesTheDifferingBits();

    JoltRuntime runtime;
    if (!runtime.IsValid())
    {
        std::cout << "RESULT FAIL runtime: " << runtime.GetError() << '\n';
        return EXIT_FAILURE;
    }
    QuaternionRotationMatchesJolt();
    CompoundPartFollowsBodyRotation(runtime);
    TwoWorldsStayBitIdentical(runtime);
    FrictionOverrideReachesTheCombine(runtime);
    MixedOverridesInOneCompound(runtime);
    QueryExcludesStaticsAndIncludesSleepers(runtime);
    QueryReturnsCreationOrder(runtime);
    QueryHasNoDuplicatesForCompounds(runtime);
    QueryIsExact(runtime);
    MassFollowsTheDesc(runtime);
    FullWorldReturnsInvalidAndKeepsStepping(runtime);
    InvalidInputReturnsInvalidIds(runtime);
    StaleAndForeignIdsAreHarmless(runtime);
    ImpulsesMoveOnlyDynamicBodies(runtime);
    PointVelocityFollowsTheSpin(runtime);
    StatsCountBodies(runtime);
    ThinBoxesAreAccepted(runtime);
    KinematicHeldTargetLeavesZeroVelocity(runtime);
    KinematicMovingTargetGivesDeltaOverDt(runtime);
    KinematicRotationArrives(runtime);
    KinematicMisuseIsHarmless(runtime);
    HingeHoldsThePivotAndStopsAtTheLimit(runtime);
    DefaultHingeDescCreatesAFullRangeHinge(runtime);
    HingeReleaseFreesAndWakesTheBody(runtime);
    HingeSecondReleaseDoesNotWakeASleeper(runtime);
    HingeFailuresReturnInvalidIds(runtime);
    HingeDiesWithEitherBody(runtime);
    HingeWorldTeardownWithLiveHinges(runtime);
    HingeOnAStaticMountHoldsLikeTheWorld(runtime);
    HingeOnADynamicMountMovesBothBodies(runtime);
    DestroyHingeFreesTheBodyAndRetiresTheId(runtime);
    WaterFloatsLightAndSinksHeavy(runtime);
    WaterLeavesSleepersAlone(runtime);
    WaterOutsideTheBoxAndBadDescsChangeNothing(runtime);
    WaterBodyLeavingTheBoxFallsBack(runtime);
    BackendSceneRebuildsBitIdentical(runtime);

    if (g_failures != 0)
    {
        std::cout << "RESULT FAIL cases=" << g_cases << " failures=" << g_failures << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "RESULT PASS cases=" << g_cases << '\n';
    return EXIT_SUCCESS;
}
