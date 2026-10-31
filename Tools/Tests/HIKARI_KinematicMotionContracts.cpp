#include <cmath>
#include <iostream>
#include <stdexcept>

#include "HIKARI/Project/FrameContext.h"
#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackend.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsSystem.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace {
using namespace HIKARI;
using namespace HIKARI::PHYSICS;

void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct MotionFixture {
    World world;
    PhysicsWorldService physics;
    KinematicMotionService motion;
    PhysicsRuntimeStatusService status;
    PhysicsSystem system;
    bool playing = true;
    RuntimePlayStateService play{&playing};
    GameObject* actor = nullptr;
    FrameContext frame{};
    KinematicMotionRequest request{};

    MotionFixture() {
        Require(physics.InstallBackend(CreateJoltPhysicsBackend()), "install Jolt backend");
        world.Services().Register(physics);
        world.Services().Register(motion);
        world.Services().Register(status);
        world.Services().Register(play);
        actor = world.CreateObject("TraversalActor");
        Require(actor->SetLocalPosition({0, .8f, 0}), "set actor pose");
        actor->AddComponent<PhysicsBodyComponent>()->SetMotionType(PhysicsMotionType::Kinematic);
        actor->AddComponent<ColliderComponent>()->Deserialize(
            {{"shape", "Capsule"}, {"radius", .3f}, {"height", 1.6f}});
        system.OnWorldAttached(world);
        frame.fixedDt = 1.f / 60.f;
    }

    ~MotionFixture() { system.OnWorldDetached(world); }

    void Box(MATH::Vec3 position, MATH::Vec3 halfExtents, MATH::Quat rotation = MATH::Quat::Identity()) {
        PhysicsBodyCreateInfo body;
        body.body.object = world.CreateObject("SupportGeometry")->GetRuntimeHandle();
        body.body.motionType = PhysicsMotionType::Static;
        body.initialPose.position = position;
        body.initialPose.rotation = rotation;
        body.shapes.push_back({});
        body.shapes.back().halfExtents = halfExtents;
        Require(physics.CreateBody(body).Succeeded(), "create support geometry");
    }

    KinematicMotionState Tick(float speed, bool submit = true) {
        ++frame.frameIndex;
        ++frame.fixedTickIndex;
        request.horizontalVelocity = {speed, 0, 0};
        if (submit)
            motion.SubmitMove(actor->GetRuntimeHandle(), 1, 0, request, frame.fixedTickIndex);
        system.PreFixedUpdate(world, frame);
        system.FixedUpdate(world, frame);
        system.PostFixedUpdate(world, frame);
        const auto* state = motion.FindState(actor->GetRuntimeHandle());
        if (state == nullptr) {
            if (const auto* error = motion.FindStatus(actor->GetRuntimeHandle()))
                throw std::runtime_error("motion state: " + error->message);
            if (const auto* error = status.FindBodyStatus(actor->GetRuntimeHandle()))
                throw std::runtime_error("physics body: " + error->message);
            throw std::runtime_error("motion solver publishes no state or body status");
        }
        return *state;
    }
};

enum class SupportCase { LowCorner, TallSide, Cliff, DeepDrop, SmallDrop, SteepSlope };

void CheckGravityOverride() {
    MotionFixture fixture;
    auto* body = fixture.actor->GetComponent<PhysicsBodyComponent>();
    nlohmann::json authored;
    body->Serialize(authored);
    authored["gravityScale"] = 2.f;
    body->Deserialize(authored);
    fixture.request.overrideVerticalGravity = true;
    fixture.request.verticalGravityAcceleration = -12.f;
    auto state = fixture.Tick(0);
    Require(std::abs(state.velocity.y + 12.f * fixture.frame.fixedDt) < 1e-4f,
            "override applies actual downward acceleration once, without authored gravityScale");
    float previousVelocity = state.velocity.y;
    fixture.request.verticalGravityAcceleration = 0.f;
    state = fixture.Tick(0);
    Require(std::abs(state.velocity.y - previousVelocity) < 1e-4f,
            "enabled zero gravity preserves in-flight velocity rather than falling back to authored gravity");
    fixture.request.verticalGravityAcceleration = 18.f;
    state = fixture.Tick(0);
    Require(std::abs(state.velocity.y - previousVelocity - 18.f * fixture.frame.fixedDt) < 1e-4f,
            "positive override accelerates upward");
    previousVelocity = state.velocity.y;
    fixture.request.overrideVerticalGravity = false;
    state = fixture.Tick(0);
    Require(std::abs(state.velocity.y - previousVelocity -
                     PhysicsWorldSettings{}.gravity.y * 2.f * fixture.frame.fixedDt) < 1e-4f,
            "disabled override restores authored world gravity times body scale");
    const auto beforeMissingMove = state;
    state = fixture.Tick(0, false);
    Require(std::abs(state.velocity.y - beforeMissingMove.velocity.y) < 1e-4f,
            "missing move does not replay the previous gravity request");
    nlohmann::json after;
    body->Serialize(after);
    Require(after == authored, "runtime gravity never changes authored PhysicsBody properties");
}

void CheckSupportCase(SupportCase test) {
    MotionFixture fixture;
    const bool ledge = test == SupportCase::Cliff || test == SupportCase::DeepDrop || test == SupportCase::SmallDrop;
    fixture.Box(ledge ? MATH::Vec3{-5, -.5f, 0} : MATH::Vec3{0, -.5f, 0},
                ledge ? MATH::Vec3{5.3f, .5f, 5} : MATH::Vec3{5, .5f, 5});
    if (test == SupportCase::LowCorner)
        fixture.Box({1.11f, .08f, 0}, {.5f, .08f, 1});
    if (test == SupportCase::TallSide)
        fixture.Box({1.11f, 1, 0}, {.5f, 1, 1});
    if (test == SupportCase::DeepDrop || test == SupportCase::SmallDrop) {
        const float drop = test == SupportCase::DeepDrop ? 1.f : .2f;
        fixture.Box({0, -.5f - drop, 0}, {5, .5f, 5});
    }
    if (test == SupportCase::SteepSlope)
        fixture.Box({.45f, .2f, 0}, {.25f, .025f, 1}, {0, 0, .5f, .8660254f});

    KinematicMotionState start;
    for (int tick = 0; tick < 6; ++tick)
        start = fixture.Tick(0);
    Require(start.IsGrounded(), "fixture starts on walkable ground");

    // Establish the actual Jolt query geometry before exercising the production motion system.
    const auto& settings = fixture.request.controller;
    const float reach =
        std::max(settings.stepUpHeight, std::tan(settings.maximumSlopeAngleRadians) * settings.edgeProbeDistance) +
        settings.characterPadding;
    PhysicsRaycastQuery ray;
    ray.origin = start.groundPosition + MATH::Vec3{settings.edgeProbeDistance, reach, 0};
    ray.direction = {0, -1, 0};
    ray.maxDistance = reach + settings.maximumGroundedDrop + settings.characterPadding;
    ray.filter.ignoredObject = fixture.actor->GetRuntimeHandle();
    PhysicsShapeCastQuery sweep;
    sweep.shape.type = PhysicsShapeType::Sphere;
    sweep.shape.radius = settings.edgeProbeRadius;
    sweep.startPose.position = ray.origin + MATH::Vec3{0, sweep.shape.radius, 0};
    sweep.direction = ray.direction;
    sweep.maxDistance = ray.maxDistance + sweep.shape.radius * 2;
    sweep.filter = ray.filter;
    PhysicsHit sphereHit{}, rayHit{};
    const bool sphereFound = fixture.physics.ShapeCast(sweep, sphereHit);
    const bool rayFound = fixture.physics.Raycast(ray, rayHit);
    if (test == SupportCase::LowCorner || test == SupportCase::TallSide) {
        Require(sphereFound && sphereHit.normal.y < std::cos(settings.maximumSlopeAngleRadians),
                "sphere first touches an unwalkable side/corner");
        Require(rayFound && rayHit.normal.y > .99f && std::abs(rayHit.position.y) < .01f,
                "center ray finds the unobstructed floor");
    }
    if (test == SupportCase::Cliff || test == SupportCase::DeepDrop)
        Require(!rayFound, "no support within permitted ledge drop");
    if (test == SupportCase::SteepSlope)
        Require(rayFound && rayHit.normal.y < .6f, "nearest ray support remains a steep slope");

    const bool shouldAdvance =
        test == SupportCase::LowCorner || test == SupportCase::TallSide || test == SupportCase::SmallDrop;
    auto result = fixture.Tick(3);
    if (shouldAdvance)
        Require(result.pose.position.x > start.pose.position.x + .025f,
                "valid center support must not prematurely zero horizontal movement");
    else
        Require(std::abs(result.pose.position.x - start.pose.position.x) < .005f && result.hitWall,
                "true cliff, excessive drop and steep support remain guarded");
    if (test == SupportCase::TallSide) {
        for (int tick = 0; tick < 30; ++tick)
            result = fixture.Tick(3);
        Require(result.pose.position.x < .34f, "secondary support check must not bypass real wall collision");
    }
}
} // namespace

bool RunKinematicMotionContracts() {
    bool passed = true;
    try {
        CheckGravityOverride();
        std::cout
            << "Kinematic gravity: negative/zero/positive override, authored fallback and no persistence passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: kinematic gravity override: " << error.what() << '\n';
        passed = false;
    }
    for (const auto test : {SupportCase::LowCorner, SupportCase::TallSide, SupportCase::Cliff, SupportCase::DeepDrop,
                            SupportCase::SmallDrop, SupportCase::SteepSlope}) {
        try {
            CheckSupportCase(test);
        } catch (const std::exception& error) {
            std::cerr << "FAIL: kinematic support case " << static_cast<int>(test) << ": " << error.what() << '\n';
            passed = false;
        }
    }
    return passed;
}
