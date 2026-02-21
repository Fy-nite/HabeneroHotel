#include <GFX/ScriptedScene.hpp>
#include <GFX/CollidableModel.hpp>
#include <GFX/LightingSystem.hpp>
#include <GFX/Player.hpp>
#include <ECS/Components.hpp>
#include <Scripting/CupLoader.hpp>
#include <Scripting/LuaLoader/ECS.hpp>
#include <server/NetworkManager.hpp>
#include <raylib.h>
#include <raymath.h>

namespace Hotones {

ScriptedScene::ScriptedScene(Scripting::CupLoader* script)
    : m_script(script)
{
}

ScriptedScene::~ScriptedScene()
{
    Unload();
}

void ScriptedScene::Init()
{
    DisableCursor();

    m_player.body.position = { 0.f, 0.f, 0.f };

    m_camera.fovy       = 60.0f;
    m_camera.projection = CAMERA_PERSPECTIVE;
    m_camera.up         = { 0.f, 1.f, 0.f };
    m_camera.position   = {
        m_player.body.position.x,
        m_player.body.position.y + (Player::BOTTOM_HEIGHT + m_player.headLerp),
        m_player.body.position.z
    };
    m_player.AttachCamera(&m_camera);

    // Expose the local player to the Lua `player.*` API so scripts can query
    // position and look direction without shadow-tracking.
    if (m_script) m_script->setLocalPlayer(&m_player);

    // Expose the ECS registry and local player to the `ecs.*` Lua library.
    Hotones::Scripting::LuaLoader::setECSRegistry(&m_registry);
    Hotones::Scripting::LuaLoader::setECSLocalPlayer(&m_player);

    // Initialise lighting (idempotent; safe if already done).
    auto& ls = GFX::LightingSystem::Get();
    if (!ls.IsReady()) ls.Init();

    // Load the model the pack declared in Init.MainScene, if any.
    if (m_script && !m_script->mainScenePath().empty()) {
        m_world = std::make_shared<CollidableModel>(
            m_script->mainScenePath(), Vector3{0.f, 0.f, 0.f});
        m_player.AttachWorld(m_world);
        // Patch every material in the world model to use the lighting shader.
        if (ls.IsReady()) m_world->SetShader(ls.GetShader());
    }
    // If no world model, the player will fall through.  The fallback ground
    // plane drawn in DrawFallbackGround() is purely visual — pack authors who
    // want solid ground should either provide a MainScene or add collision via
    // scripted physics (future).
}

void ScriptedScene::Update()
{
    m_player.Update();

    // ── ECS tick ──────────────────────────────────────────────────────────────
    const float dt = GetFrameTime();

    // Keep TransformComponent in sync with the engine player's live position
    // so Lua can read ecs.getPos(playerEntityId) and get an up-to-date value.
    m_registry.Each<ECS::PlayerComponent>(
        [&](ECS::EntityId id, ECS::PlayerComponent& pc) {
            if (!pc.player) return;
            if (m_registry.HasComponent<ECS::TransformComponent>(id))
                m_registry.GetComponent<ECS::TransformComponent>(id).position =
                    pc.player->body.position;
        });

    // Tick lifetime components and collect expired entities.
    std::vector<ECS::EntityId> toDestroy;
    m_registry.Each<ECS::LifetimeComponent>(
        [&](ECS::EntityId id, ECS::LifetimeComponent& lt) {
            lt.remaining -= dt;
            if (lt.remaining <= 0.0f) toDestroy.push_back(id);
        });
    for (auto id : toDestroy) m_registry.DestroyEntity(id);

    if (m_script) m_script->update();
}

void ScriptedScene::Draw()
{
    ClearBackground(BLACK);

    // Upload light uniforms so the world model's shader has fresh data this frame.
    {
        auto& ls = GFX::LightingSystem::Get();
        if (ls.IsReady()) ls.UploadUniforms(m_camera);
    }

    BeginMode3D(m_camera);

        // World model (loaded from Init.MainScene)
        if (m_world) {
            m_world->Draw();
        }

        // ── Lua 3D pass ───────────────────────────────────────────────────────
        // draw3D() is called HERE, inside BeginMode3D. mesh.* calls go directly
        // to raylib 3D primitives so they render into the 3D scene correctly.
        if (m_script) m_script->draw3D();

        //TODO: make devs handle ghosts instead.
        // // Remote player ghosts
        // if (m_netMgr) {
        //     for (auto& [id, rp] : m_netMgr->GetRemotePlayers()) {
        //         if (!rp.active) continue;
        //         DrawCube({ rp.posX, rp.posY + 1.0f, rp.posZ },
        //                  0.6f, 2.0f, 0.6f, { 255, 80, 80, 200 });
        //         DrawCube({ rp.posX, rp.posY + 2.3f, rp.posZ },
        //                  0.5f, 0.5f, 0.5f, { 255, 140, 60, 220 });
        //         DrawCubeWires({ rp.posX, rp.posY + 1.0f, rp.posZ },
        //                       0.6f, 2.0f, 0.6f, DARKGRAY);
        //     }
        // }

    EndMode3D();

    // ── Lua 2D / HUD pass ─────────────────────────────────────────────────────
    // draw() is called AFTER EndMode3D. render.* calls work correctly here and
    // will appear on top of the 3D scene.
    if (m_script) m_script->draw();
}

void ScriptedScene::Unload()
{
    if (m_world) m_world.reset();
    m_registry.Clear();
    // Null out the static pointer so stale Lua calls after scene teardown
    // are silently ignored rather than crashing.
    Hotones::Scripting::LuaLoader::setECSRegistry(nullptr);
    Hotones::Scripting::LuaLoader::setECSLocalPlayer(nullptr);
}

void ScriptedScene::SetNetworkManager(Net::NetworkManager* nm)
{
    m_netMgr = nm;
    if (m_script) m_script->setNetworkManager(nm);
}

// A simple tiled floor so packs without a MainScene have visible ground.
void ScriptedScene::DrawFallbackGround() const
{
    // Large flat plane at y = 0
    DrawPlane({ 0.f, 0.f, 0.f }, { 200.f, 200.f },
              { 45, 45, 50, 255 });  // near-black grey

    // Subtle grid on top
    DrawGrid(40, 5.0f);
}

} // namespace Hotones
