#include <raylib.h>
#include <raymath.h>
#include <GFX/Player.hpp>
#include <GFX/SceneManager.hpp>
#include <GFX/LoadingScene.hpp>
#include <GFX/SimpleScene.hpp>
#include <GFX/GameScene.hpp>
#include <SFX/AudioSystem.hpp>
#include <Assets/AssetLoader.hpp>
#include <filesystem>
#if defined(_WIN32)
#include <crtdbg.h>
#endif

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
static void DrawLevel(void);

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------


int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 1280;
    const int screenHeight = 768;

    InitWindow(screenWidth, screenHeight, "Habanero Hotel - Hotones");

    // Enable CRT debug heap checks on Windows to catch heap corruption early
#if defined(_WIN32) && defined(_DEBUG)
    int dbgFlags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    dbgFlags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(dbgFlags);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
    // Initialize audio subsystem so game systems can play sounds (footsteps etc.)
    Ho_tones::InitAudioSystem();

    // Try to find and load a footstep asset using the AssetLoader
    {       
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic1.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic2.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic3.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic4.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic5.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic6.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic7.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic8.wav");
        Ho_tones::GetSoundBus().LoadSoundFile("footstep", "assets/hardboot_generic9.wav");
    }

    // Initialize player and camera
    Hotones::Player player;
    Camera camera = { 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.position = (Vector3){
        player.body.position.x,
        player.body.position.y + (Hotones::Player::BOTTOM_HEIGHT + player.headLerp),
        player.body.position.z,
    };

    player.AttachCamera(&camera);
    // Scene manager + scenes
    Hotones::SceneManager sceneMgr;
    sceneMgr.Add("loading", [](){ return std::make_unique<Hotones::LoadingScene>(); });
    sceneMgr.Add("game", [](){ return std::make_unique<Hotones::GameScene>(); });
    sceneMgr.SwitchTo("loading");

    DisableCursor();        // Limit cursor to relative movement inside the window

    SetTargetFPS(60);       // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------
    
    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        player.Update();
        sceneMgr.Update();

        // If loading finished, switch to game
        if (sceneMgr.GetCurrentName() == "loading" && sceneMgr.GetCurrent() && sceneMgr.GetCurrent()->IsFinished()) {
            // use an animated transition when switching to the game scene
            sceneMgr.SwitchWithTransition("game", 1.0f);
        }
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            // Let scene manager draw current scene (GameScene handles its own 3D camera)
            sceneMgr.Draw();

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    // Shutdown audio before closing the window
    Ho_tones::ShutdownAudioSystem();

    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
// Draw game level
static void DrawLevel(void)
{
    const int floorExtent = 25;
    const float tileSize = 5.0f;
    const Color tileColor1 = (Color){ 150, 200, 200, 255 };

    // Floor tiles
    for (int y = -floorExtent; y < floorExtent; y++)
    {
        for (int x = -floorExtent; x < floorExtent; x++)
        {
            if ((y & 1) && (x & 1))
            {
                DrawPlane((Vector3){ x*tileSize, 0.0f, y*tileSize}, (Vector2){ tileSize, tileSize }, tileColor1);
            }
            else if (!(y & 1) && !(x & 1))
            {
                DrawPlane((Vector3){ x*tileSize, 0.0f, y*tileSize}, (Vector2){ tileSize, tileSize }, LIGHTGRAY);
            }
        }
    }

    const Vector3 towerSize = (Vector3){ 16.0f, 32.0f, 16.0f };
    const Color towerColor = (Color){ 150, 200, 200, 255 };

    Vector3 towerPos = (Vector3){ 16.0f, 16.0f, 16.0f };
    DrawCubeV(towerPos, towerSize, towerColor);
    DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

    towerPos.x *= -1;
    DrawCubeV(towerPos, towerSize, towerColor);
    DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

    towerPos.z *= -1;
    DrawCubeV(towerPos, towerSize, towerColor);
    DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

    towerPos.x *= -1;
    DrawCubeV(towerPos, towerSize, towerColor);
    DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

    // Red sun
    DrawSphere((Vector3){ 300.0f, 300.0f, 0.0f }, 100.0f, (Color){ 255, 0, 0, 255 });
}

// touch: force rebuild after TransitionScene changes
