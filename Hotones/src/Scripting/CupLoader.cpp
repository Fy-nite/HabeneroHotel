// Minimal Lua runtime integration for the project.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <raylib.h>
#include "../include/Scripting/CupLoader.hpp"
#include "../include/Scripting/CupPackage.hpp"
#include "../include/Scripting/LuaLoader/Rendering.hpp"
#include "../include/Scripting/LuaLoader/Audio.hpp"
#include "../include/Scripting/LuaLoader/Input.hpp"
#include "../include/Scripting/LuaLoader/ServerLib.hpp"
#include "../include/Scripting/LuaLoader/MeshGen.hpp"
#include "../include/Scripting/LuaLoader/Lighting.hpp"
#include "../include/Scripting/LuaLoader/Players.hpp"
#include "../include/Scripting/LuaLoader/Physics.hpp"
#include "../include/Scripting/LuaLoader/LocalPlayer.hpp"
#include "../include/Scripting/LuaLoader/ECS.hpp"
#include <server/NetworkManager.hpp>

#include <lua.hpp>

// ── Timing globals (work in both headless and windowed Lua contexts) ──────────
namespace {
    static std::chrono::steady_clock::time_point g_luaLastFrame;
    static std::chrono::steady_clock::time_point g_luaStartTime;
    static bool g_luaTimingInit = false;

    // GetFrameTime() — seconds elapsed since the last call (or 0 on first call)
    static int l_GetFrameTime(lua_State* L) {
        auto now = std::chrono::steady_clock::now();
        float dt = 0.0f;
        if (g_luaTimingInit) {
            dt = std::chrono::duration<float>(now - g_luaLastFrame).count();
        } else {
            g_luaStartTime  = now;
            g_luaTimingInit = true;
        }
        g_luaLastFrame = now;
        lua_pushnumber(L, (lua_Number)dt);
        return 1;
    }

    // GetTime() — seconds since the Lua state was initialised
    static int l_GetTime(lua_State* L) {
        if (!g_luaTimingInit) {
            lua_pushnumber(L, 0.0);
            return 1;
        }
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - g_luaStartTime).count();
        lua_pushnumber(L, (lua_Number)secs);
        return 1;
    }
} // anonymous namespace

// Internal reload request flag (global for the single CupLoader instance).
static std::atomic<bool> g_reloadRequested { false };

// Lua binding: reload the currently-loaded pack. Upvalue 1 = CupLoader* (lightuserdata)
static int l_reload(lua_State* L)
{
    void* p = lua_touserdata(L, lua_upvalueindex(1));
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Hotones::Scripting::CupLoader* loader = static_cast<Hotones::Scripting::CupLoader*>(p);
    // Defer actual reload to avoid closing the active Lua state while this
    // C function is still executing.  Mark a request and return true.
    g_reloadRequested.store(true);
    lua_pushboolean(L, 1);
    return 1;
}

namespace Hotones::Scripting {

CupLoader::CupLoader()
    : L(nullptr), m_classRef(LUA_NOREF)
{
}

void CupLoader::requestReload()
{
    g_reloadRequested.store(true);
}

void CupLoader::setNetworkManager(Net::NetworkManager* nm)
{
    m_netMgr = nm;
    // Update the Players library immediately so Lua can query live player data
    // even when init() was already called before the connection was established.
    Hotones::Scripting::LuaLoader::setPlayersNetworkManager(nm);
}

void CupLoader::setLocalPlayer(Hotones::Player* player)
{
    m_localPlayer = player;
    // Update the LocalPlayer library immediately; the binding reads through a
    // static pointer so existing Lua states pick up the new player at once.
    Hotones::Scripting::LuaLoader::setLocalPlayer(player);
    // Keep the ECS library's player pointer in sync as well.
    Hotones::Scripting::LuaLoader::setECSLocalPlayer(player);
}

CupLoader::~CupLoader()
{
    if (L) {
        if (m_classRef != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, m_classRef);
        lua_close(L);
    }
}

bool CupLoader::init()
{
    L = luaL_newstate();
    if (!L) return false;
    luaL_openlibs(L);

    Hotones::Scripting::LuaLoader::registerRendering(L);
    Hotones::Scripting::LuaLoader::registerAudio(L);
    Hotones::Scripting::LuaLoader::registerInput(L);
    Hotones::Scripting::LuaLoader::registerServer(L);
    Hotones::Scripting::LuaLoader::registerMeshGen(L);
    Hotones::Scripting::LuaLoader::registerLighting(L);
    Hotones::Scripting::LuaLoader::registerPlayers(L, m_netMgr);
    Hotones::Scripting::LuaLoader::registerPhysics(L);
    Hotones::Scripting::LuaLoader::registerLocalPlayer(L);
    Hotones::Scripting::LuaLoader::registerECS(L);

    // Register timing globals so Lua scripts work in both headless and windowed modes
    g_luaTimingInit = false; // reset on each new Lua state
    lua_pushcfunction(L, l_GetFrameTime);
    lua_setglobal(L, "GetFrameTime");
    lua_pushcfunction(L, l_GetTime);
    lua_setglobal(L, "GetTime");

    // Expose reloadPack() to Lua so scripts can request reloading the current pack.
    // The closure carries a lightuserdata upvalue pointing to this CupLoader instance.
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, l_reload, 1);
    lua_setglobal(L, "reloadPack");

    return true;
}

bool CupLoader::loadScript(const std::string& path)
{
    if (!L) return false;
    if (!std::filesystem::exists(path)) {
        TraceLog(LOG_ERROR, "[CupLoader] Script not found: %s", path.c_str());
        return false;
    }
    int status = luaL_loadfile(L, path.c_str());
    if (status != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        std::string err = msg ? msg : "<unknown>";
        TraceLog(LOG_ERROR, "[CupLoader] Load error: %s", err.c_str());
        m_lastLuaError = err;
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool CupLoader::run()
{
    if (!L) return false;
    int status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        std::string err = msg ? msg : "<unknown>";
        TraceLog(LOG_ERROR, "[CupLoader] Runtime error: %s", err.c_str());
        m_lastLuaError = err;
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool CupLoader::loadPak(CupPackage& pkg)
{
    if (!L) {
        TraceLog(LOG_ERROR, "[CupLoader] Call init() before loadPak().");
        return false;
    }
    if (!pkg.isOpen()) {
        TraceLog(LOG_ERROR, "[CupLoader] CupPackage is not open.");
        return false;
    }

    const std::string initPath = pkg.initScript();
    if (!std::filesystem::exists(initPath)) {
        TraceLog(LOG_ERROR, "[CupLoader] Pack is missing init.lua: %s", initPath.c_str());
        return false;
    }
    if (!loadScript(initPath) || !run())
        return false;

    // Remember package root and init path so we can reload later
    m_initPath = initPath;
    m_packageRoot = pkg.rootPath();

    lua_getglobal(L, "Init");
    if (!lua_istable(L, -1)) {
        TraceLog(LOG_ERROR, "[CupLoader] init.lua must declare a global 'Init' table.");
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, "MainScene");
    if (lua_isstring(L, -1)) {
        const char* rel = lua_tostring(L, -1);
        m_mainScene = (std::filesystem::path(pkg.rootPath()) / rel).string();
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "Debug");
    if (lua_toboolean(L, -1))
        TraceLog(LOG_INFO, "[CupLoader] Pack debug mode enabled.");
    lua_pop(L, 1);

    lua_getfield(L, -1, "MainClass");
    if (lua_istable(L, -1)) {
        m_classRef = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
        lua_pop(L, 1);
        TraceLog(LOG_WARNING, "[CupLoader] Warning: Init.MainClass is not a table.");
    }
    lua_pop(L, 1);

    callMethod("Init");
    return true;
}

bool CupLoader::reload()
{
    if (m_initPath.empty()) {
        TraceLog(LOG_ERROR, "[CupLoader] reload(): no package loaded previously");
        return false;
    }

    // Create a fresh Lua state and fully initialise it.  We build the new
    // state first so that failures leave the existing state untouched.
    lua_State* newL = luaL_newstate();
    if (!newL) {
        TraceLog(LOG_ERROR, "[CupLoader] reload(): failed to create new Lua state");
        return false;
    }
    luaL_openlibs(newL);

    // Register engine libraries into the new state
    Hotones::Scripting::LuaLoader::registerRendering(newL);
    Hotones::Scripting::LuaLoader::registerAudio(newL);
    Hotones::Scripting::LuaLoader::registerInput(newL);
    Hotones::Scripting::LuaLoader::registerServer(newL);
    Hotones::Scripting::LuaLoader::registerMeshGen(newL);
    Hotones::Scripting::LuaLoader::registerLighting(newL);
    // NOTE: registerPlayers, registerPhysics, and registerLocalPlayer must be
    // included here so they remain available after a reloadPack() call.
    Hotones::Scripting::LuaLoader::registerPlayers(newL, m_netMgr);
    Hotones::Scripting::LuaLoader::registerPhysics(newL);
    Hotones::Scripting::LuaLoader::registerLocalPlayer(newL);

    // Timing globals
    g_luaTimingInit = false;
    lua_pushcfunction(newL, l_GetFrameTime);
    lua_setglobal(newL, "GetFrameTime");
    lua_pushcfunction(newL, l_GetTime);
    lua_setglobal(newL, "GetTime");

    // reloadPack closure in the new state (upvalue = this)
    lua_pushlightuserdata(newL, this);
    lua_pushcclosure(newL, l_reload, 1);
    lua_setglobal(newL, "reloadPack");

    // Load and run init.lua inside the new state
    int status = luaL_loadfile(newL, m_initPath.c_str());
    if (status != LUA_OK) {
        const char* msg = lua_tostring(newL, -1);
        std::string err = msg ? msg : "<unknown>";
        TraceLog(LOG_ERROR, "[CupLoader] reload(): load error: %s", err.c_str());
        m_lastLuaError = err;
        lua_pop(newL, 1);
        lua_close(newL);
        return false;
    }
    status = lua_pcall(newL, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char* msg = lua_tostring(newL, -1);
        std::string err = msg ? msg : "<unknown>";
        TraceLog(LOG_ERROR, "[CupLoader] reload(): runtime error: %s", err.c_str());
        m_lastLuaError = err;
        lua_pop(newL, 1);
        lua_close(newL);
        return false;
    }

    // Validate and read Init table
    lua_getglobal(newL, "Init");
    if (!lua_istable(newL, -1)) {
        TraceLog(LOG_ERROR, "[CupLoader] reload(): init.lua did not set Init table");
        lua_pop(newL, 1);
        lua_close(newL);
        return false;
    }

    std::string newMainScene;
    lua_getfield(newL, -1, "MainScene");
    if (lua_isstring(newL, -1)) {
        const char* rel = lua_tostring(newL, -1);
        newMainScene = (std::filesystem::path(m_packageRoot) / rel).string();
    }
    lua_pop(newL, 1);

    int newClassRef = LUA_NOREF;
    lua_getfield(newL, -1, "MainClass");
    if (lua_istable(newL, -1)) {
        newClassRef = luaL_ref(newL, LUA_REGISTRYINDEX);
    } else {
        lua_pop(newL, 1);
        TraceLog(LOG_WARNING, "[CupLoader] reload(): Init.MainClass is not a table.");
    }
    lua_pop(newL, 1); // pop Init table

    // Swap in the new state (close old state after extracting anything we need)
    if (L) {
        if (m_classRef != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, m_classRef);
        lua_close(L);
    }
    L = newL;
    m_classRef = newClassRef;
    if (!newMainScene.empty()) m_mainScene = newMainScene;

    // Call Init() on the new MainClass (if one existed)
    if (m_classRef != LUA_NOREF) callMethod("Init");

    TraceLog(LOG_INFO, "[CupLoader] reload(): successfully reloaded %s", m_initPath.c_str());
    return true;
}

void CupLoader::update()
{
    // Call the scripted Update() first.  If the script requested a reload
    // via reloadPack(), perform the reload AFTER the call returns to avoid
    // closing the active Lua state while a C function is on the stack.
    callMethod("Update");
    if (g_reloadRequested.exchange(false)) {
        // perform the actual reload now
        reload();
    }
}

void CupLoader::draw3D()  { callMethod("draw3D");  }
void CupLoader::draw()    { callMethod("Draw");    }

void CupLoader::firePlayerJoined(uint8_t id, const char* name)
{
    if (!L || m_classRef == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_classRef);
    lua_getfield(L, -1, "onPlayerJoined");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_pushvalue(L, -2);
    lua_pushinteger(L, id);
    lua_pushstring(L, name ? name : "");
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        TraceLog(LOG_ERROR, "[CupLoader] onPlayerJoined() error: %s", (err ? err : "<unknown>"));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void CupLoader::firePlayerLeft(uint8_t id)
{
    if (!L || m_classRef == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_classRef);
    lua_getfield(L, -1, "onPlayerLeft");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return; }
    lua_pushvalue(L, -2);
    lua_pushinteger(L, id);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        TraceLog(LOG_ERROR, "[CupLoader] onPlayerLeft() error: %s", (err ? err : "<unknown>"));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

bool CupLoader::callMethod(const char* method, int /*nargs*/)
{
    if (!L || m_classRef == LUA_NOREF) return false;

    lua_rawgeti(L, LUA_REGISTRYINDEX, m_classRef);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }

    lua_getfield(L, -1, method);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return false; }

    lua_pushvalue(L, -2);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string serr = err ? err : "<unknown>";
        TraceLog(LOG_ERROR, "[CupLoader] %s() error: %s", method, serr.c_str());
        m_lastLuaError = serr;
        lua_pop(L, 2);
        return false;
    }

    lua_pop(L, 1);
    return true;
}

lua_State* CupLoader::state()
{
    return L;
}

} // namespace Hotones::Scripting
