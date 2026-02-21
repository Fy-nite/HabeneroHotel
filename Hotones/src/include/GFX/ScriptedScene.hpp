#pragma once

#include <GFX/Scene.hpp>
#include <GFX/Player.hpp>
#include <ECS/Registry.hpp>
#include <memory>
#include <raylib.h>

// Forward declarations
namespace Hotones          { class CollidableModel; }
namespace Hotones::Net     { class NetworkManager;  }
namespace Hotones::Scripting { class CupLoader;     }

namespace Hotones {

/// A blank FPS scene driven entirely by a CupLoader (Lua pack).
///
/// Draw pipeline each frame:
///   1. ClearBackground
///   2. BeginMode3D
///       – world model (if Init.MainScene was set)
///       – script.draw3D()   ← Lua calls mesh.* here (immediate 3D raylib calls)
///       – remote player ghosts
///   3. EndMode3D
///   4. script.draw()        ← Lua calls render.* here (2D HUD overlay)
///
/// If Init.MainScene is empty (or not set), a procedural flat ground plane is
/// drawn so the player has something to stand on.
class ScriptedScene : public Scene {
public:
    /// `script` must outlive this scene (owned by main.cpp).
    explicit ScriptedScene(Scripting::CupLoader* script);
    ~ScriptedScene() override;

    void Init()   override;
    void Update() override;
    void Draw()   override;
    void Unload() override;

    Player* GetPlayer() { return &m_player; }

    void SetNetworkManager(Net::NetworkManager* nm);

private:
    Scripting::CupLoader*            m_script   = nullptr;
    Player                           m_player;
    Camera                           m_camera   = {};
    std::shared_ptr<CollidableModel> m_world;
    Net::NetworkManager*             m_netMgr   = nullptr;
    ECS::Registry                    m_registry;   ///< ECS world for this scene

    void DrawFallbackGround() const;
};

} // namespace Hotones
