#include <GFX/GameScene.hpp>
#include <raymath.h>
#include <GFX/CollidableModel.hpp>
#include <server/NetworkManager.hpp>

namespace Hotones {

GameScene::GameScene()
    : worldDebug(false) // Added worldDebug flag
{
    camera = { 0 };
}

GameScene::~GameScene()
{
    Unload();
}

void GameScene::Init()
{
    DisableCursor(); // lock cursor for FPS gameplay; menu called EnableCursor()

    // Ensure player starts at origin when the game scene loads
    player.body.position = (Vector3){0.0f, 0.0f, 0.0f};
    
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.position = (Vector3){
        player.body.position.x,
        player.body.position.y + (Player::BOTTOM_HEIGHT + player.headLerp),
        player.body.position.z,
    };
    player.AttachCamera(&camera);


    // Load the main world model (replace path as needed)
    worldModel = std::make_shared<CollidableModel>("assets/hsome.obj", (Vector3){0,0,0}); // TODO: Change this after testing pak
    // Let player use the world for collisions (shared ownership)
    player.AttachWorld(worldModel);
}

void GameScene::Update()
{
    player.Update();

    // Toggle world debug visuals (moved to F2 to reserve F1 for ImGui)
    if (IsKeyPressed(KEY_F2)) {
        worldDebug = !worldDebug;
        if (worldModel) worldModel->SetDebug(worldDebug);
    }

    // if (worldModel && worldModel->CheckCollision(player.body.position)) {
    //     TraceLog(LOG_INFO, "Player colliding with world!");
    // }
}

void GameScene::Draw()
{
    ClearBackground(RAYWHITE);
    BeginMode3D(camera);
    DrawLevel();

    // Draw other connected players as coloured ghost shapes
    if (m_netMgr) {
        for (auto& [id, rp] : m_netMgr->GetRemotePlayers()) {
            if (!rp.active) continue;
            // Body (tall box)
            DrawCube({ rp.posX, rp.posY + 1.0f, rp.posZ }, 0.6f, 2.0f, 0.6f,
                     { 255, 80, 80, 200 });
            // Head (smaller box)
            DrawCube({ rp.posX, rp.posY + 2.3f, rp.posZ }, 0.5f, 0.5f, 0.5f,
                     { 255, 140, 60, 220 });
            DrawCubeWires({ rp.posX, rp.posY + 1.0f, rp.posZ }, 0.6f, 2.0f, 0.6f, DARKGRAY);
        }
    }

    EndMode3D();

    // HUD
    DrawRectangle(5, 5, 330, 75, Fade(SKYBLUE, 0.5f));
    DrawRectangleLines(5, 5, 330, 75, BLUE);
    DrawText("Camera controls:", 15, 15, 10, BLACK);
    DrawText("- Move keys: W, A, S, D, Space, Left-Ctrl", 15, 30, 10, BLACK);
    DrawText("- Look around: arrow keys or mouse", 15, 45, 10, BLACK);
    DrawText(TextFormat("- Velocity Len: (%06.3f)", Vector2Length((Vector2){ player.body.velocity.x, player.body.velocity.z })), 15, 60, 10, BLACK);

    // world drawing moved to DrawLevel()
}

void GameScene::DrawLevel()
{
    // Draw the main world model and its bounding box
    if (worldModel) {
        worldModel->Draw();
        // Draw per-mesh boxes in red so they align exactly with the F1 debug boxes
        worldModel->DrawMeshBoundingBoxes(RED);
        if (worldDebug) worldModel->DrawDebug();
    }
}

void GameScene::Unload()
{
    // No testModel cleanup needed
    if (worldModel) {
        worldModel.reset();
    }
}

} // namespace Hotones
