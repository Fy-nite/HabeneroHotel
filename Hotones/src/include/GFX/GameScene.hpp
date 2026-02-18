#pragma once

#include <GFX/Scene.hpp>
#include <GFX/Player.hpp>
#include <memory>
#include <raylib.h>
#include <GFX/CollidableModel.hpp>

namespace Hotones {

class GameScene : public Scene {
public:
    GameScene();
    ~GameScene() override;

    void Init() override;
    void Update() override;
    void Draw() override;
    void Unload() override;

    // Expose player access for debug UI
    Player* GetPlayer() { return &player; }
    void SetWorldDebug(bool enabled) { worldDebug = enabled; if (worldModel) worldModel->SetDebug(worldDebug); }
    bool IsWorldDebug() const { return worldDebug; }

private:
    Hotones::Player player;
    Camera camera;
    // Main world model
    std::shared_ptr<CollidableModel> worldModel;
    bool worldDebug = false;

    void DrawLevel();
};

} // namespace Hotones
