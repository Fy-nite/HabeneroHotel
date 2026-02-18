#pragma once


#include "../include/Scene.hpp"
#include "../include/Player.hpp"
#include "GFX/CollidableModel.hpp"
#include <raylib.h>

namespace Hotones {

class GameScene : public Scene {
public:
    GameScene();
    ~GameScene() override = default;

    void Init() override;
    void Update() override;
    void Draw() override;
    void Unload() override;

    // Expose player access for debug UI
    Hotones::Player* GetPlayer() { return &player; }
    void SetWorldDebug(bool enabled) { worldDebug = enabled; if (worldModel) worldModel->SetDebug(worldDebug); }
    bool IsWorldDebug() const { return worldDebug; }

private:
    Hotones::Player player;
    Camera camera;
    // Collidable model for demo
        // Main world model
        CollidableModel* worldModel = nullptr;
        bool worldDebug = false;

    void DrawLevel();
};

} // namespace Hotones
