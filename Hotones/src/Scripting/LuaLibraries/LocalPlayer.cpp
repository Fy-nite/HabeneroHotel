#include <lua.hpp>
#include <GFX/Player.hpp>
#include "../../include/Scripting/LuaLoader/LocalPlayer.hpp"

namespace Hotones::Scripting::LuaLoader {

namespace {
    static Hotones::Player* g_localPlayer = nullptr;
} // anonymous namespace

void setLocalPlayer(Player* player)
{
    g_localPlayer = player;
}

// ── player.getPos() → x, y, z ───────────────────────────────────────────────
// Returns the eye-space world position: body.position + BOTTOM_HEIGHT + headLerp.
static int l_getPos(lua_State* L)
{
    if (!g_localPlayer) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 3;
    }
    const auto& p = g_localPlayer->body.position;
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y + Player::BOTTOM_HEIGHT + g_localPlayer->headLerp);
    lua_pushnumber(L, p.z);
    return 3;
}

static int l_setpos(lua_State* L)
{
    if (!g_localPlayer) return 0;
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    g_localPlayer->body.position = {x, y - Player::BOTTOM_HEIGHT - g_localPlayer->headLerp, z};
    return 0;
}



// ── player.getLook() → yaw_rad, pitch_rad ───────────────────────────────────
// C++ convention: lookRotation.x decreases on mouse-right, lookRotation.y
// increases when looking down.  We negate both here so the returned values
// follow the intuitive convention used by fireDir():
//   yaw_rad   = 0  → facing -Z;  increases clockwise (right = +X at yaw = π/2)
//   pitch_rad = 0  → horizontal; positive = looking up
static int l_getLook(lua_State* L)
{
    if (!g_localPlayer) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 2;
    }
    lua_pushnumber(L, -g_localPlayer->lookRotation.x);  // negate: right = positive yaw
    lua_pushnumber(L, -g_localPlayer->lookRotation.y);  // negate: up   = positive pitch
    return 2;
}

// ── Registration ─────────────────────────────────────────────────────────────
void registerLocalPlayer(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"getPos",  l_getPos},
        {"getLook", l_getLook},
        {"setPos",  l_setpos},
        {nullptr, nullptr}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "player");
}

} // namespace Hotones::Scripting::LuaLoader
