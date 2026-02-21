#pragma once

#include <raylib.h>
#include <raymath.h>
#include <string>
#include <cstdint>

// Forward-declare the heavy Player class so this header stays light.
namespace Hotones { class Player; }

// ---------------------------------------------------------------------------
// Components.hpp — built-in ECS components for the Hotones engine.
//
// All structs are plain aggregates (no virtual, no heap ownership by default)
// so they can live directly in the dense component arrays without indirection.
//
// Add new game-specific components freely in your own headers; you do NOT
// need to register them anywhere — the Registry discovers them at first use
// via std::type_index.
// ---------------------------------------------------------------------------

namespace Hotones::ECS {

// ---- Spatial / physics ----------------------------------------------------

/// World-space position, orientation, and non-uniform scale.
/// Use raymath helpers (QuaternionFromEuler, etc.) to manipulate rotation.
struct TransformComponent {
    Vector3    position = { 0.0f, 0.0f, 0.0f };
    Quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // identity
    Vector3    scale    = { 1.0f, 1.0f, 1.0f };

    /// Convenience: return a Matrix suitable for DrawModelEx / shader uniforms.
    [[nodiscard]] Matrix ToMatrix() const {
        return MatrixMultiply(
            MatrixMultiply(
                MatrixScale(scale.x, scale.y, scale.z),
                QuaternionToMatrix(rotation)),
            MatrixTranslate(position.x, position.y, position.z));
    }
};

/// Linear and angular velocity (units per second).
struct VelocityComponent {
    Vector3 linear  = { 0.0f, 0.0f, 0.0f };
    Vector3 angular = { 0.0f, 0.0f, 0.0f }; // Euler rates, radians/s
};

/// Sphere collider — wraps a handle to the PhysicsSystem static mesh.
/// Attach a TransformComponent on the same entity; the physics system reads
/// and writes back TransformComponent::position after collision resolution.
struct ColliderSphereComponent {
    float   radius        = 0.5f;
    int     physicsHandle = -1;   // handle returned by PhysicsSystem::RegisterStaticMeshFromModel
    bool    isTrigger     = false; // if true, report overlaps but don't resolve
    bool    isStatic      = false; // if true, the physics system won't move it
};

// ---- Rendering ------------------------------------------------------------

/// Holds a loaded raylib Model handle and render parameters.
///
/// Ownership
/// ---------
///   If ownsModel is true the entity "owns" the GPU resources; you are
///   responsible for calling UnloadModel on the model when the entity is
///   destroyed (e.g. in a LifetimeSystem or explicit cleanup).
///   If ownsModel is false the model is shared / managed elsewhere.
struct RenderModelComponent {
    Model model    = {};
    Color tint     = WHITE;
    bool  visible  = true;
    bool  ownsModel = false; // set true if this component loaded the model
};

/// Billboarded sprite drawn with DrawBillboard (for particles, icons, etc.).
struct BillboardComponent {
    Texture2D texture  = {};
    Color     tint     = WHITE;
    float     width    = 1.0f;
    bool      visible  = true;
};

// ---- Identity / tagging ---------------------------------------------------

/// Human-readable name for the entity (useful for debug UIs / Lua lookups).
struct TagComponent {
    std::string name;
};

/// Attach a compile-time integer tag (group / layer / team) to an entity.
/// Use different integer constants to represent logical groups without
/// paying the cost of a string comparison.
struct GroupComponent {
    uint32_t groupId = 0;
};

// ---- Gameplay -------------------------------------------------------------

/// Simple health model. Systems should check isDead() after applying damage.
struct HealthComponent {
    float current = 100.0f;
    float max     = 100.0f;

    [[nodiscard]] bool  isDead()    const noexcept { return current <= 0.0f; }
    [[nodiscard]] float normalised() const noexcept { return max > 0.0f ? current / max : 0.0f; }

    void ApplyDamage(float dmg)  noexcept { current -= dmg;  if (current < 0.0f)  current = 0.0f; }
    void Heal       (float hp)   noexcept { current += hp;   if (current > max)    current = max;  }
};

/// Countdown lifetime — the entity is marked for destruction when remaining
/// reaches zero. A LifetimeSystem should decrement this each frame.
struct LifetimeComponent {
    float remaining = 1.0f; // seconds
};

// ---- Networking -----------------------------------------------------------

/// Marks an entity as a network-replicated peer (player or object).
struct NetworkComponent {
    uint8_t peerId  = 0;
    bool    isLocal = false; // true for the locally controlled entity
};

// ---- Audio ----------------------------------------------------------------

/// Point-source audio emitter attached to an entity's world position.
/// An AudioSystem should read TransformComponent::position each frame to
/// update the 3-D source position via AudioSystem / SoundBus.
struct AudioEmitterComponent {
    std::string soundKey;             // key registered with SoundBus::LoadSoundFile
    float       volume    = 1.0f;
    float       pitch     = 1.0f;
    float       maxDist   = 50.0f;
    bool        loop      = false;
    bool        playing   = false;
    bool        autoPlay  = false;    // start playing as soon as the component is added
};

// ---- Scripting ------------------------------------------------------------

/// Lua-driven behaviour — stores the name of the Lua class that drives this
/// entity and an integer reference into the Lua registry (LUA_NOREF = -1).
///
/// The CupLoader / script system is responsible for calling the Lua methods
/// on this component each frame.
struct ScriptComponent {
    std::string className;      // e.g. "Enemies.Grunt"
    int         luaRef  = -1;   // lua_ref into the Lua registry (LUA_NOREF = -1)
    bool        active  = true;
};

// ---- Player controller ----------------------------------------------------

/// Marks an entity as the locally-controlled player.
///
/// This component is NEVER added automatically — you must explicitly call
/// ecs.addPlayer(id) from Lua or C++ to make an entity player-controlled.
///
/// The `player` pointer is a non-owning reference to the engine Player object
/// set by the ECS Lua library via setECSLocalPlayer().  It is safe to leave as
/// nullptr in headless / server mode — all player-related ECS calls become
/// no-ops in that case.
struct PlayerComponent {
    Hotones::Player* player = nullptr; ///< non-owning ptr to the engine Player
    bool  enableSourceBhop  = false;   ///< mirrors Player::enableSourceBhop
    float speedMultiplier   = 1.0f;    ///< scales MAX_SPEED (1.0 = default)
    float jumpMultiplier    = 1.0f;    ///< scales JUMP_FORCE (1.0 = default)
};

} // namespace Hotones::ECS
