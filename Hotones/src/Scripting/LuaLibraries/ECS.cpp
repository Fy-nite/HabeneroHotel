#include <lua.hpp>
#include <ECS/ECS.hpp>
#include <GFX/Player.hpp>
#include "../../include/Scripting/LuaLoader/ECS.hpp"

// ── Module-level state ────────────────────────────────────────────────────────
// These pointers are set by the scene before registering / every time the
// active world changes.  All Lua bindings below check for nullptr.

namespace Hotones::Scripting::LuaLoader {

namespace {
    static ECS::Registry* g_registry    = nullptr;
    static Hotones::Player* g_ecsPlayer = nullptr;
} // anonymous namespace

void setECSRegistry(ECS::Registry* reg)      { g_registry  = reg; }
void setECSLocalPlayer(Hotones::Player* p)   { g_ecsPlayer = p;   }

// ── Helpers ───────────────────────────────────────────────────────────────────

static inline bool registryReady(lua_State* L)
{
    if (g_registry) return true;
    TraceLog(LOG_WARNING, "[ecs] Registry not set — call ignored");
    return false;
}

static inline ECS::EntityId toEntityId(lua_State* L, int idx)
{
    return static_cast<ECS::EntityId>(luaL_checkinteger(L, idx));
}

// Push three zeros — used for missing-component fallbacks.
static inline int push3zeros(lua_State* L)
{
    lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0); lua_pushnumber(L, 0.0);
    return 3;
}

// ── Entity management ─────────────────────────────────────────────────────────

// ecs.create() → id
static int l_create(lua_State* L)
{
    if (!registryReady(L)) { lua_pushinteger(L, ECS::INVALID_ENTITY); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(g_registry->CreateEntity()));
    return 1;
}

// ecs.destroy(id)
static int l_destroy(lua_State* L)
{
    if (!registryReady(L)) return 0;
    g_registry->DestroyEntity(toEntityId(L, 1));
    return 0;
}

// ecs.isAlive(id) → bool
static int l_isAlive(lua_State* L)
{
    if (!g_registry) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, g_registry->IsAlive(toEntityId(L, 1)) ? 1 : 0);
    return 1;
}

// ── Transform ────────────────────────────────────────────────────────────────

// ecs.setPos(id, x, y, z)
static int l_setPos(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id = toEntityId(L, 1);
    float x  = static_cast<float>(luaL_checknumber(L, 2));
    float y  = static_cast<float>(luaL_checknumber(L, 3));
    float z  = static_cast<float>(luaL_checknumber(L, 4));
    if (!g_registry->IsAlive(id)) return 0;

    // If this is a player entity, teleport the engine player directly.
    if (g_registry->HasComponent<ECS::PlayerComponent>(id)) {
        auto& pc = g_registry->GetComponent<ECS::PlayerComponent>(id);
        if (pc.player) pc.player->body.position = {x, y, z};
    }

    g_registry->GetOrAdd<ECS::TransformComponent>(id).position = {x, y, z};
    return 0;
}

// ecs.getPos(id) → x, y, z
static int l_getPos(lua_State* L)
{
    if (!g_registry) return push3zeros(L);
    auto id = toEntityId(L, 1);
    if (!g_registry->IsAlive(id)) return push3zeros(L);

    // Player entity: read live position from the engine Player.
    if (g_registry->HasComponent<ECS::PlayerComponent>(id)) {
        auto& pc = g_registry->GetComponent<ECS::PlayerComponent>(id);
        if (pc.player) {
            auto& p = pc.player->body.position;
            lua_pushnumber(L, p.x);
            lua_pushnumber(L, p.y);
            lua_pushnumber(L, p.z);
            return 3;
        }
    }

    if (g_registry->HasComponent<ECS::TransformComponent>(id)) {
        auto& t = g_registry->GetComponent<ECS::TransformComponent>(id);
        lua_pushnumber(L, t.position.x);
        lua_pushnumber(L, t.position.y);
        lua_pushnumber(L, t.position.z);
        return 3;
    }
    return push3zeros(L);
}

// ecs.setScale(id, sx, sy, sz)
static int l_setScale(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id = toEntityId(L, 1);
    float sx = static_cast<float>(luaL_checknumber(L, 2));
    float sy = static_cast<float>(luaL_checknumber(L, 3));
    float sz = static_cast<float>(luaL_checknumber(L, 4));
    if (!g_registry->IsAlive(id)) return 0;
    g_registry->GetOrAdd<ECS::TransformComponent>(id).scale = {sx, sy, sz};
    return 0;
}

// ecs.setVelocity(id, vx, vy, vz)
static int l_setVelocity(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id = toEntityId(L, 1);
    float vx = static_cast<float>(luaL_checknumber(L, 2));
    float vy = static_cast<float>(luaL_checknumber(L, 3));
    float vz = static_cast<float>(luaL_checknumber(L, 4));
    if (!g_registry->IsAlive(id)) return 0;
    g_registry->GetOrAdd<ECS::VelocityComponent>(id).linear = {vx, vy, vz};
    return 0;
}

// ecs.getVelocity(id) → vx, vy, vz
static int l_getVelocity(lua_State* L)
{
    if (!g_registry) return push3zeros(L);
    auto id = toEntityId(L, 1);
    if (g_registry->IsAlive(id) && g_registry->HasComponent<ECS::VelocityComponent>(id)) {
        auto& v = g_registry->GetComponent<ECS::VelocityComponent>(id).linear;
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        lua_pushnumber(L, v.z);
        return 3;
    }
    return push3zeros(L);
}

// ── Tag ───────────────────────────────────────────────────────────────────────

// ecs.setTag(id, name)
static int l_setTag(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto        id   = toEntityId(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!g_registry->IsAlive(id)) return 0;
    g_registry->GetOrAdd<ECS::TagComponent>(id).name = name;
    return 0;
}

// ecs.getTag(id) → string  (empty string if no tag)
static int l_getTag(lua_State* L)
{
    if (!g_registry) { lua_pushstring(L, ""); return 1; }
    auto id = toEntityId(L, 1);
    if (g_registry->IsAlive(id) && g_registry->HasComponent<ECS::TagComponent>(id))
        lua_pushstring(L, g_registry->GetComponent<ECS::TagComponent>(id).name.c_str());
    else
        lua_pushstring(L, "");
    return 1;
}

// ── Health ────────────────────────────────────────────────────────────────────

// ecs.addHealth(id, maxHp)  — creates HealthComponent; current = max
static int l_addHealth(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id    = toEntityId(L, 1);
    float maxHp = static_cast<float>(luaL_checknumber(L, 2));
    if (!g_registry->IsAlive(id)) return 0;
    if (!g_registry->HasComponent<ECS::HealthComponent>(id)) {
        auto& h = g_registry->AddComponent<ECS::HealthComponent>(id);
        h.max = h.current = maxHp;
    } else {
        auto& h  = g_registry->GetComponent<ECS::HealthComponent>(id);
        h.max    = maxHp;
        h.current = maxHp;
    }
    return 0;
}

// ecs.getHealth(id) → current, max  (0, 0 if absent)
static int l_getHealth(lua_State* L)
{
    if (!g_registry) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    auto id = toEntityId(L, 1);
    if (g_registry->IsAlive(id) && g_registry->HasComponent<ECS::HealthComponent>(id)) {
        const auto& h = g_registry->GetComponent<ECS::HealthComponent>(id);
        lua_pushnumber(L, h.current);
        lua_pushnumber(L, h.max);
    } else {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
    }
    return 2;
}

// ecs.damage(id, amount)
static int l_damage(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id  = toEntityId(L, 1);
    float amt = static_cast<float>(luaL_checknumber(L, 2));
    if (g_registry->IsAlive(id) && g_registry->HasComponent<ECS::HealthComponent>(id))
        g_registry->GetComponent<ECS::HealthComponent>(id).ApplyDamage(amt);
    return 0;
}

// ecs.heal(id, amount)
static int l_heal(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id  = toEntityId(L, 1);
    float amt = static_cast<float>(luaL_checknumber(L, 2));
    if (g_registry->IsAlive(id) && g_registry->HasComponent<ECS::HealthComponent>(id))
        g_registry->GetComponent<ECS::HealthComponent>(id).Heal(amt);
    return 0;
}

// ecs.isDead(id) → bool
static int l_isDead(lua_State* L)
{
    if (!g_registry) { lua_pushboolean(L, 0); return 1; }
    auto id = toEntityId(L, 1);
    bool dead = g_registry->IsAlive(id)
             && g_registry->HasComponent<ECS::HealthComponent>(id)
             && g_registry->GetComponent<ECS::HealthComponent>(id).isDead();
    lua_pushboolean(L, dead ? 1 : 0);
    return 1;
}

// ── Lifetime ──────────────────────────────────────────────────────────────────

// ecs.setLifetime(id, seconds)
static int l_setLifetime(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto  id  = toEntityId(L, 1);
    float sec = static_cast<float>(luaL_checknumber(L, 2));
    if (!g_registry->IsAlive(id)) return 0;
    g_registry->GetOrAdd<ECS::LifetimeComponent>(id).remaining = sec;
    return 0;
}

// ecs.getLifetime(id) → remaining seconds  (0 if absent)
static int l_getLifetime(lua_State* L)
{
    if (!g_registry) { lua_pushnumber(L, 0); return 1; }
    auto id = toEntityId(L, 1);
    if (g_registry->IsAlive(id) && g_registry->HasComponent<ECS::LifetimeComponent>(id))
        lua_pushnumber(L, g_registry->GetComponent<ECS::LifetimeComponent>(id).remaining);
    else
        lua_pushnumber(L, 0);
    return 1;
}

// ── Player controller ─────────────────────────────────────────────────────────

// ecs.addPlayer(id)  — link the entity to the engine Player controller.
// Does nothing if the entity already owns a PlayerComponent.
static int l_addPlayer(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto id = toEntityId(L, 1);
    if (!g_registry->IsAlive(id)) return 0;

    if (!g_registry->HasComponent<ECS::PlayerComponent>(id)) {
        auto& pc    = g_registry->AddComponent<ECS::PlayerComponent>(id);
        pc.player   = g_ecsPlayer;
        // Mirror current engine bhop setting if a player is attached.
        if (pc.player) pc.enableSourceBhop = pc.player->enableSourceBhop;
    }
    // Ensure the entity also has a TransformComponent so getPos works.
    g_registry->GetOrAdd<ECS::TransformComponent>(id);
    return 0;
}

// ecs.hasPlayer(id) → bool
static int l_hasPlayer(lua_State* L)
{
    if (!g_registry) { lua_pushboolean(L, 0); return 1; }
    auto id = toEntityId(L, 1);
    lua_pushboolean(L,
        g_registry->IsAlive(id) && g_registry->HasComponent<ECS::PlayerComponent>(id) ? 1 : 0);
    return 1;
}

// ecs.removePlayer(id)
static int l_removePlayer(lua_State* L)
{
    if (!registryReady(L)) return 0;
    g_registry->RemoveComponent<ECS::PlayerComponent>(toEntityId(L, 1));
    return 0;
}

// ecs.setPlayerBhop(id, enabled)
static int l_setPlayerBhop(lua_State* L)
{
    if (!registryReady(L)) return 0;
    auto id      = toEntityId(L, 1);
    bool enabled = lua_toboolean(L, 2) != 0;
    if (!g_registry->IsAlive(id)) return 0;
    if (!g_registry->HasComponent<ECS::PlayerComponent>(id)) return 0;
    auto& pc = g_registry->GetComponent<ECS::PlayerComponent>(id);
    pc.enableSourceBhop = enabled;
    if (pc.player) pc.player->SetSourceBhopEnabled(enabled);
    return 0;
}

// ── Registration ─────────────────────────────────────────────────────────────

void registerECS(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        // Entity lifecycle
        {"create",          l_create},
        {"destroy",         l_destroy},
        {"isAlive",         l_isAlive},
        // Transform
        {"setPos",          l_setPos},
        {"getPos",          l_getPos},
        {"setScale",        l_setScale},
        {"setVelocity",     l_setVelocity},
        {"getVelocity",     l_getVelocity},
        // Tag
        {"setTag",          l_setTag},
        {"getTag",          l_getTag},
        // Health
        {"addHealth",       l_addHealth},
        {"getHealth",       l_getHealth},
        {"damage",          l_damage},
        {"heal",            l_heal},
        {"isDead",          l_isDead},
        // Lifetime
        {"setLifetime",     l_setLifetime},
        {"getLifetime",     l_getLifetime},
        // Player controller (opt-in)
        {"addPlayer",       l_addPlayer},
        {"hasPlayer",       l_hasPlayer},
        {"removePlayer",    l_removePlayer},
        {"setPlayerBhop",   l_setPlayerBhop},
        {nullptr, nullptr}
    };

    luaL_newlib(L, funcs);
    lua_setglobal(L, "ecs");
}

} // namespace Hotones::Scripting::LuaLoader
