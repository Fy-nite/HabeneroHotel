#pragma once

struct lua_State;

namespace Hotones::ECS { class Registry; }
namespace Hotones       { class Player;   }

namespace Hotones::Scripting::LuaLoader {

// ── Lifetime setters ─────────────────────────────────────────────────────────
// Call these whenever the active scene / player changes.
// Safe to call before or after registerECS().

/// Set the ECS Registry the `ecs.*` Lua functions operate on.
/// Pass nullptr to disable ECS calls (e.g. during scene transitions).
void setECSRegistry(ECS::Registry* reg);

/// Set the local Player pointer so ecs.addPlayer() can link an entity to the
/// engine player controller.  Mirrors the LocalPlayer library's pointer.
void setECSLocalPlayer(Player* player);

// ── Registration ─────────────────────────────────────────────────────────────
/// Register the `ecs` global table into the given Lua state.
///
/// Entity management
/// -----------------
///   ecs.create()                    → id          -- spawn a blank entity
///   ecs.destroy(id)                               -- destroy + strip all components
///   ecs.isAlive(id)                 → bool
///
/// Transform  (auto-created on first setPos / setScale / setVelocity)
/// ---------
///   ecs.setPos(id, x, y, z)
///   ecs.getPos(id)                  → x, y, z
///   ecs.setScale(id, sx, sy, sz)
///   ecs.setVelocity(id, vx, vy, vz)
///   ecs.getVelocity(id)             → vx, vy, vz
///
/// Tag
/// ---
///   ecs.setTag(id, name)
///   ecs.getTag(id)                  → string (or "")
///
/// Health
/// ------
///   ecs.addHealth(id, maxHp)        -- adds HealthComponent; current = max
///   ecs.getHealth(id)               → current, max  (0, 0 if absent)
///   ecs.damage(id, amount)
///   ecs.heal(id, amount)
///   ecs.isDead(id)                  → bool
///
/// Lifetime
/// --------
///   ecs.setLifetime(id, seconds)    -- add/replace LifetimeComponent
///   ecs.getLifetime(id)             → remaining  (0 if absent)
///
/// Player controller  (NOT added by default — must be called explicitly)
/// -----------------
///   ecs.addPlayer(id)               -- link entity to the engine Player
///   ecs.hasPlayer(id)               → bool
///   ecs.removePlayer(id)
///   ecs.setPlayerBhop(id, enabled)  -- toggle Source-style bhop
void registerECS(lua_State* L);

} // namespace Hotones::Scripting::LuaLoader
