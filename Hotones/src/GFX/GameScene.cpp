#include <GFX/GameScene.hpp>
#include <raymath.h>
#include <GFX/CollidableModel.hpp>

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
    worldModel = std::make_shared<CollidableModel>("assets/home.obj", (Vector3){0,0,0});
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

    // Example: check collision with player position
    if (worldModel && worldModel->CheckCollision(player.body.position)) {
        // TraceLog(LOG_INFO, "Player colliding with world!");
    }
}

void GameScene::Draw()
{
    ClearBackground(RAYWHITE);
    BeginMode3D(camera);
    DrawLevel();
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
