#pragma once

#include <lua.hpp>

namespace Hotones { class Player; }

namespace Hotones::Scripting::LuaLoader {

// Update the local player pointer used by the `player.*` Lua library.
// Safe to call before or after registerLocalPlayer(); the binding reads
// through a static pointer so an update here is visible immediately.
void setLocalPlayer(Player* player);

// Register the `player` Lua table on the given Lua state.
//
// Exposed functions:
//   player.getPos()  -> x, y, z       (eye-space world position)
//   player.getLook() -> yaw_rad, pitch_rad
//       yaw_rad:   0 = facing -Z; positive rotates toward +X (right)
//       pitch_rad: positive = looking UP
//
// Both functions return 0.0 for all values when no player is attached
// (e.g. headless-server mode).
void registerLocalPlayer(lua_State* L);

} // namespace Hotones::Scripting::LuaLoader
