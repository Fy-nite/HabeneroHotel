#pragma once

namespace Hotones::ECS {

class Registry;

// ---------------------------------------------------------------------------
// System — base class for all ECS systems.
//
// A System encapsulates logic that runs on a set of entities each frame
// (or tick). Derive from this class and implement Update to process
// components from the Registry.
//
// Usage
// -----
//   class MovementSystem : public System {
//   public:
//       void Update(Registry& reg, float dt) override {
//           reg.View<TransformComponent, VelocityComponent>(
//               [dt](EntityId, TransformComponent& t, VelocityComponent& v) {
//                   t.position = Vector3Add(t.position,
//                       Vector3Scale(v.linear, dt));
//               });
//       }
//   };
//
// Recommended ownership
// ---------------------
//   Store systems in a SystemManager or std::vector<std::unique_ptr<System>>
//   and call Update on each one per frame from the scene's Update() method.
// ---------------------------------------------------------------------------

class System {
public:
    virtual ~System() = default;

    // Called once per frame / tick before Draw.
    // dt — delta time in seconds.
    virtual void Update(Registry& reg, float dt) = 0;

    // Optional: called once during scene Init to perform one-time setup.
    virtual void Init(Registry& /*reg*/) {}

    // Optional: called on scene Unload to release GPU / physics resources.
    virtual void Shutdown(Registry& /*reg*/) {}

    // Systems can be individually paused without removing them.
    void  SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

private:
    bool m_enabled = true;
};

} // namespace Hotones::ECS
