#include <raylib.h>
#include <lua.hpp>
#include "../../include/Scripting/LuaLoader/Input.hpp"
#include <Input/InputHandler.hpp>

namespace Hotones::Scripting::LuaLoader {

// ── Keyboard ─────────────────────────────────────────────────────────────────

static int l_isKeyDown(lua_State* L)
{
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Hotones::Input::InputHandler::Get().IsKeyDown(key) ? 1 : 0);
    return 1;
}

static int l_isKeyPressed(lua_State* L)
{
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Hotones::Input::InputHandler::Get().IsKeyPressed(key) ? 1 : 0);
    return 1;
}

static int l_isKeyReleased(lua_State* L)
{
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Hotones::Input::InputHandler::Get().IsKeyReleased(key) ? 1 : 0);
    return 1;
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

static int l_isMouseDown(lua_State* L)
{
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Hotones::Input::InputHandler::Get().IsMouseDown(btn) ? 1 : 0);
    return 1;
}

static int l_isMousePressed(lua_State* L)
{
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Hotones::Input::InputHandler::Get().IsMousePressed(btn) ? 1 : 0);
    return 1;
}

static int l_getMousePos(lua_State* L)
{
    Vector2 p = Hotones::Input::InputHandler::Get().GetMousePos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}

static int l_getMouseDelta(lua_State* L)
{
    Vector2 d = Hotones::Input::InputHandler::Get().GetMouseDelta();
    lua_pushnumber(L, d.x);
    lua_pushnumber(L, d.y);
    return 2;
}

static int l_getMouseWheel(lua_State* L)
{
    lua_pushnumber(L, (lua_Number)Hotones::Input::InputHandler::Get().GetMouseWheel());
    return 1;
}

static int l_isKeyPressedRepeat(lua_State* L)
{
    int key = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Hotones::Input::InputHandler::Get().IsKeyPressedRepeat(key) ? 1 : 0);
    return 1;
}

static int l_getChar(lua_State* L)
{
    int c = Hotones::Input::InputHandler::Get().GetCharPressed();
    lua_pushinteger(L, c);
    return 1;
}

// ── Registration ─────────────────────────────────────────────────────────────

void registerInput(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"isKeyDown",      l_isKeyDown},
        {"isKeyPressed",   l_isKeyPressed},
        {"isKeyReleased",  l_isKeyReleased},
        {"isKeyPressedRepeat", l_isKeyPressedRepeat},
        {"isMouseDown",    l_isMouseDown},
        {"isMousePressed", l_isMousePressed},
        {"getMousePos",    l_getMousePos},
        {"getMouseDelta",  l_getMouseDelta},
        {"getMouseWheel",  l_getMouseWheel},
        {"getChar",        l_getChar},
        {nullptr, nullptr}
    };

    luaL_newlib(L, funcs);

    // ── Key constants ─────────────────────────────────────────────────────────
    // Letters
    const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 26; ++i) {
        char keyName[8] = "KEY_";
        keyName[4] = letters[i]; keyName[5] = '\0';
        lua_pushinteger(L, KEY_A + i);   // raylib keys A-Z are contiguous
        lua_setfield(L, -2, keyName);
    }
    // Digits
    for (int i = 0; i <= 9; ++i) {
        char keyName[8];
        snprintf(keyName, sizeof(keyName), "KEY_%d", i);
        lua_pushinteger(L, KEY_ZERO + i);
        lua_setfield(L, -2, keyName);
    }
    // Function keys
    for (int i = 1; i <= 12; ++i) {
        char keyName[8];
        snprintf(keyName, sizeof(keyName), "KEY_F%d", i);
        lua_pushinteger(L, KEY_F1 + (i - 1));
        lua_setfield(L, -2, keyName);
    }
    // Named keys
    struct { const char* name; int code; } named[] = {
        {"KEY_SPACE",     KEY_SPACE},
        {"KEY_ENTER",     KEY_ENTER},
        {"KEY_ESCAPE",    KEY_ESCAPE},
        {"KEY_TAB",       KEY_TAB},
        {"KEY_BACKSPACE", KEY_BACKSPACE},
        {"KEY_DELETE",    KEY_DELETE},
        {"KEY_UP",        KEY_UP},
        {"KEY_DOWN",      KEY_DOWN},
        {"KEY_LEFT",      KEY_LEFT},
        {"KEY_RIGHT",     KEY_RIGHT},
        {"KEY_LSHIFT",    KEY_LEFT_SHIFT},
        {"KEY_RSHIFT",    KEY_RIGHT_SHIFT},
        {"KEY_LCTRL",     KEY_LEFT_CONTROL},
        {"KEY_RCTRL",     KEY_RIGHT_CONTROL},
        {"KEY_LALT",      KEY_LEFT_ALT},
        {"KEY_RALT",      KEY_RIGHT_ALT},
        // Mouse button constants (also on `input` table for convenience)
        {"MOUSE_LEFT",    MOUSE_BUTTON_LEFT},
        {"MOUSE_RIGHT",   MOUSE_BUTTON_RIGHT},
        {"MOUSE_MIDDLE",  MOUSE_BUTTON_MIDDLE},
        {nullptr, 0}
    };
    for (int i = 0; named[i].name; ++i) {
        lua_pushinteger(L, named[i].code);
        lua_setfield(L, -2, named[i].name);
    }

    lua_setglobal(L, "input");
}

} // namespace Hotones::Scripting::LuaLoader
