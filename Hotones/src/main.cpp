#include <raylib.h>
#include <raymath.h>
#include <Input/Input.hpp>
#include <GFX/Player.hpp>
#include <GFX/SceneManager.hpp>
#include <GFX/LoadingScene.hpp>
#include <GFX/SimpleScene.hpp>
#include <GFX/GameScene.hpp>
#include <GFX/ScriptedScene.hpp>
#include <GFX/MainMenuScene.hpp>
#include <imgui/imgui.h>
#include <imgui/rlImGui.h>
#include <SFX/AudioSystem.hpp>
#include <Assets/AssetLoader.hpp>
#include <server/NetworkManager.hpp>
#include <server/Server.hpp>
#include <Scripting/CupLoader.hpp>
#include <Scripting/CupPackage.hpp>
#include <Physics/PhysicsSystem.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
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


int main(int argc, char** argv)
{
    // ── Command-line argument parsing ───────────────────────────────────────
    bool        isServer    = false;
    uint16_t    serverPort  = Hotones::Net::DEFAULT_PORT;
    std::string connectHost;
    uint16_t    connectPort = Hotones::Net::DEFAULT_PORT;
    std::string playerName  = "Player";
    std::string pakPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            isServer = true;
        } else if (arg == "--port" && i + 1 < argc) {
            serverPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--connect" && i + 1 < argc) {
            connectHost = argv[++i];
        } else if (arg == "--cport" && i + 1 < argc) {
            connectPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--name" && i + 1 < argc) {
            playerName = argv[++i];
        } else if (arg == "--pak" && i + 1 < argc) {
            pakPath = argv[++i];
        }
    }
    TraceLog(LOG_DEBUG, "CLI args: isServer=%d serverPort=%d connectHost=%s connectPort=%d playerName=%s pak=%s",
             isServer ? 1 : 0, (int)serverPort, connectHost.c_str(), (int)connectPort, playerName.c_str(), pakPath.c_str());
    SetTraceLogLevel(LOG_WARNING); // Reduce raylib log spam (can be set to LOG_INFO for more details)

    // Temporary startup tracing to a file to diagnose early exit/crash locations
    std::ofstream __startup_log("hotones_startup.log", std::ios::app);
    if (__startup_log) __startup_log << "args parsed\n";
    // ── Headless server mode (no window needed) ─────────────────────────────
    if (isServer) {
        Hotones::RunHeadlessServer(serverPort, pakPath);
        return 0;
    }
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 1280;
    const int screenHeight = 768;

    InitWindow(screenWidth, screenHeight, "Habanero");
    TraceLog(LOG_INFO, "Window initialized %dx%d", screenWidth, screenHeight);
    if (__startup_log) __startup_log << "after InitWindow\n";

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
    bool audioOk = Ho_tones::InitAudioSystem();
    TraceLog(audioOk ? LOG_INFO : LOG_WARNING, "Audio system %s", audioOk ? "initialized" : "failed to initialize");
    if (__startup_log) __startup_log << "audioOk=" << (audioOk ? "1" : "0") << "\n";

    TraceLog(LOG_INFO, "Initializing physics subsystem");
    Hotones::Physics::InitPhysics();
    TraceLog(LOG_INFO, "Physics subsystem initialized");

    // ── Cup pack state variables ─────────────────────────────────────────────
    // setupPack() (defined below near the scene manager) handles initialisation.
    std::unique_ptr<Hotones::Scripting::CupPackage> g_pak;
    std::unique_ptr<Hotones::Scripting::CupLoader>  g_script;
    std::thread              g_packThread;
    std::atomic<bool>        g_packLoaded{false};
    std::atomic<bool>        g_packFailed{false};
    std::atomic<bool>        g_pakExtracted{false};
    std::mutex               g_packErrMutex;
    std::string              g_packError;

    // Initialize player and camera
    Hotones::Player player;
    player.RegisterSounds();
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
    TraceLog(LOG_DEBUG, "Player and camera initialized; camera pos=(%.2f,%.2f,%.2f)",
             camera.position.x, camera.position.y, camera.position.z);

    // ── Network manager (declared BEFORE scene manager so menu can use it) ──
    Hotones::Net::NetworkManager netMgr;
    if (!connectHost.empty()) {
        netMgr.Connect(connectHost, connectPort, playerName);
    }
    // Rate-limit player-update sends to ~20 Hz
    float netSendTimer = 0.f;
    static constexpr float NET_SEND_INTERVAL = 1.f / 20.f;

    // ── setupPack — initialise async pack loading (callable at any point) ───
    // Can be called either at startup (--pak) or after the menu selects a pack.
    auto setupPack = [&](const std::string& path) {
        if (path.empty()) return;
        // Join any previous pack thread first
        if (g_packThread.joinable()) {
            TraceLog(LOG_INFO, "Joining previous pack thread before re-init");
            g_packThread.join();
        }
        g_pak.reset();
        g_script.reset();
        g_pakExtracted.store(false);
        g_packLoaded.store(false);
        g_packFailed.store(false);
        {
            std::lock_guard<std::mutex> lk(g_packErrMutex);
            g_packError.clear();
        }
        g_pak    = std::make_unique<Hotones::Scripting::CupPackage>();
        g_script = std::make_unique<Hotones::Scripting::CupLoader>();

        Hotones::Scripting::CupPackage* pakPtr    = g_pak.get();
        std::string                      pathCopy  = path;
        TraceLog(LOG_INFO, "Opening pack (synchronous): %s", pathCopy.c_str());
        // Synchronous open to avoid threading/allocation issues during startup.
        if (!pakPtr->open(pathCopy)) {
            std::lock_guard<std::mutex> lk(g_packErrMutex);
            g_packError = "Failed to open pack: " + pathCopy;
            g_packFailed.store(true);
        } else {
            g_pakExtracted.store(true);
            TraceLog(LOG_INFO, "Pack extracted: %s", pathCopy.c_str());
        }
    };

    // Scene manager + scenes
    Hotones::SceneManager sceneMgr;
    sceneMgr.Add("menu",    [&netMgr](){ return std::make_unique<Hotones::MainMenuScene>(&netMgr); });
    // Loading scene receives callbacks to display pack extraction / init progress
    sceneMgr.Add("loading", [&g_pakExtracted, &g_packLoaded, &g_packFailed, &g_packErrMutex, &g_packError, &g_script]() {
        auto progressCb = [&g_pakExtracted, &g_packLoaded, &g_packFailed]() -> float {
            if (g_packFailed.load()) return 0.0f;
            if (!g_pakExtracted.load()) return 0.05f;
            if (!g_packLoaded.load()) return 0.6f;
            return 1.0f;
        };
        auto errorCb = [&g_packErrMutex, &g_packError, &g_script]() -> std::string {
            {
                std::lock_guard<std::mutex> lk(g_packErrMutex);
                if (!g_packError.empty()) return g_packError;
            }
            if (g_script) {
                std::string lerr = g_script->GetLastError();
                if (!lerr.empty()) return lerr;
            }
            return std::string();
        };
        return std::make_unique<Hotones::LoadingScene>(3.0f, progressCb, errorCb);
    });
    sceneMgr.Add("game",    [](){ return std::make_unique<Hotones::GameScene>(); });

    // When a pack was given on the command line, set it up now and register the
    // scripted scene.  If the pack comes from the menu, setupPack + scene
    // registration happen inside the menu→loading transition below.
    if (!pakPath.empty()) {
        TraceLog(LOG_INFO, "Pak requested from CLI: %s", pakPath.c_str());
        setupPack(pakPath);
    }
    if (g_script) {
        Hotones::Scripting::CupLoader* rawScript = g_script.get();
        sceneMgr.Add("scripted", [rawScript](){
            return std::make_unique<Hotones::ScriptedScene>(rawScript);
        });
        TraceLog(LOG_INFO, "Registered scripted scene; switching to loading screen");
        sceneMgr.SwitchTo("loading");
    } else {
        TraceLog(LOG_INFO, "No pack provided; switching to main menu");
        sceneMgr.SwitchTo("menu");
    }

    // Cursor starts enabled (menu). GameScene::Init() calls DisableCursor().

    SetTargetFPS(60);       // Set our game to run at 60 frames-per-second
    TraceLog(LOG_DEBUG, "Target FPS set to 60");
    // Initialize rlImGui (optional system-installed integration)
    rlImGuiSetup(true);
    //--------------------------------------------------------------------------------------
    bool showDebugUI = false;
    
    TraceLog(LOG_INFO, "Entering main loop");
    if (__startup_log) __startup_log << "entering main loop\n";
    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // if (__startup_log) __startup_log << "main loop iter\n";
        TraceLog(LOG_DEBUG, "Main loop iteration start — frameTime=%.6f scene=%s", GetFrameTime(), sceneMgr.GetCurrentName().c_str());
        // Update
        //----------------------------------------------------------------------------------
        if (IsKeyPressed(KEY_F1)) {
            showDebugUI = !showDebugUI;
            TraceLog(LOG_DEBUG, "F1 pressed — debug UI=%d", showDebugUI ? 1 : 0);
            // When opening the debug UI ensure the mouse is visible; when
            // closing restore cursor capture for gameplay scenes.
            if (showDebugUI) {
                EnableCursor();
            } else {
                // If we're in gameplay or scripted scenes, re-capture the cursor.
                std::string cur = sceneMgr.GetCurrentName();
                if (cur == "game" || cur == "scripted") DisableCursor();
                else EnableCursor();
            }
        }

        // Only tick the standalone player while actually playing
        if (sceneMgr.GetCurrentName() == "game") {
            TraceLog(LOG_TRACE, "Player.Update() about to run");
            player.Update();
            TraceLog(LOG_TRACE, "Player.Update() finished");
        }
        TraceLog(LOG_TRACE, "SceneManager.Update() about to run (current=%s)", sceneMgr.GetCurrentName().c_str());
        // Refresh input state before scenes/scripts run so Lua can query it
        Hotones::Input::InputHandler::Get().Update();
        sceneMgr.Update();
        TraceLog(LOG_TRACE, "SceneManager.Update() finished (current=%s)", sceneMgr.GetCurrentName().c_str());

        // ── Scene transitions ────────────────────────────────────────────────
        // Menu finished → start networking then fade to loading screen
        if (sceneMgr.GetCurrentName() == "menu" && sceneMgr.GetCurrent() && sceneMgr.GetCurrent()->IsFinished()) {
            auto* menu = dynamic_cast<Hotones::MainMenuScene*>(sceneMgr.GetCurrent());
        if (menu) {
                if (menu->GetAction() == Hotones::MainMenuScene::Action::Quit) {
                    break; // exit game loop cleanly
                }
                playerName = menu->GetPlayerName();
                std::string menuPakPath = menu->GetPakPath();

                if (menu->GetAction() == Hotones::MainMenuScene::Action::Host) {
                    serverPort = menu->GetConnectPort();
                    // If a pack was selected, load it and advertise it
                    if (!menuPakPath.empty()) {
                        pakPath = menuPakPath;
                        setupPack(pakPath);
                        if (g_script) {
                            Hotones::Scripting::CupLoader* rs = g_script.get();
                            sceneMgr.Add("scripted", [rs](){
                                return std::make_unique<Hotones::ScriptedScene>(rs);
                            });
                        }
                        // Tell the server what pack it's running so the browser shows it
                        std::filesystem::path pp(pakPath);
                        netMgr.SetHostedPakName(pp.stem().string().c_str());
                    }
                    TraceLog(LOG_INFO, "Starting server on port %d", serverPort);
                    netMgr.StartServer(serverPort);
                } else if (menu->GetAction() == Hotones::MainMenuScene::Action::Join) {
                    connectHost = menu->GetConnectHost();
                    connectPort = menu->GetConnectPort();
                    // If the server advertised a pack we have locally, load it
                    if (!menuPakPath.empty()) {
                        pakPath = menuPakPath;
                        setupPack(pakPath);
                        if (g_script) {
                            Hotones::Scripting::CupLoader* rs = g_script.get();
                            sceneMgr.Add("scripted", [rs](){
                                return std::make_unique<Hotones::ScriptedScene>(rs);
                            });
                        }
                    }
                    TraceLog(LOG_INFO, "Joining server %s:%d as %s", connectHost.c_str(), connectPort, playerName.c_str());
                    netMgr.Connect(connectHost, connectPort, playerName);
                }
                TraceLog(LOG_INFO, "Switching to loading scene");
                sceneMgr.SwitchWithTransition("loading", 1.0f);
            }
        }

        // Loading finished → enter game (or scripted scene if a pack is loaded)
        if (sceneMgr.GetCurrentName() == "loading" && sceneMgr.GetCurrent() && sceneMgr.GetCurrent()->IsFinished()) {
            // Only enter the scripted scene if the pack finished loading successfully.
            if (g_script) {
                if (g_packFailed.load()) {
                    std::lock_guard<std::mutex> lk(g_packErrMutex);
                    TraceLog(LOG_ERROR, "%s", g_packError.c_str());
                    break; // exit game loop
                }
                // If the package extraction finished but Lua hasn't been
                // initialised/loaded yet, do it now on the main thread.
                if (g_pakExtracted.load() && !g_packLoaded.load()) {
                    // Perform Lua init/load on main thread
                    if (!g_script->init() || !g_script->loadPak(*g_pak)) {
                        std::lock_guard<std::mutex> lk(g_packErrMutex);
                        g_packError = "Failed to initialise pack (main thread).";
                        g_packFailed.store(true);
                        TraceLog(LOG_ERROR, "%s", g_packError.c_str());
                        break;
                    }
                    g_packLoaded.store(true);
                    TraceLog(LOG_INFO, "Pack initialised on main thread and marked loaded");
                }
                    if (!g_packLoaded.load()) {
                        // still extracting/initialising; keep showing loading screen
                        TraceLog(LOG_DEBUG, "Pack still loading/extracting");
                    } else {
                        TraceLog(LOG_INFO, "Switching to scripted scene");
                        sceneMgr.SwitchWithTransition("scripted", 1.0f);
                    }
            } else {
                    TraceLog(LOG_INFO, "Switching to game scene");
                    sceneMgr.SwitchWithTransition("game", 1.0f);
            }
        }

        // ── Network tick ────────────────────────────────────────────────────
        TraceLog(LOG_TRACE, "Network.Update() about to run");
        netMgr.Update();
        TraceLog(LOG_TRACE, "Network.Update() finished");
        netSendTimer += GetFrameTime();
        bool netActive = netMgr.IsConnected()
                       || netMgr.GetMode() == Hotones::Net::NetworkManager::Mode::Server;
        if (netActive && netSendTimer >= NET_SEND_INTERVAL) {
            netSendTimer = 0.f;
            Hotones::Player* p = nullptr;
            if (auto* gs = dynamic_cast<Hotones::GameScene*>(sceneMgr.GetCurrent()))
                p = gs->GetPlayer();
            else if (auto* ss = dynamic_cast<Hotones::ScriptedScene*>(sceneMgr.GetCurrent()))
                p = ss->GetPlayer();
            if (p) {
                netMgr.SendPlayerUpdate(
                    p->body.position.x, p->body.position.y, p->body.position.z,
                    p->lookRotation.x,  p->lookRotation.y
                );
            }
        }
        // Pass NetworkManager to GameScene / ScriptedScene every frame
        {
            Hotones::GameScene* gs = dynamic_cast<Hotones::GameScene*>(sceneMgr.GetCurrent());
            if (gs) gs->SetNetworkManager(&netMgr);
            Hotones::ScriptedScene* ss = dynamic_cast<Hotones::ScriptedScene*>(sceneMgr.GetCurrent());
            if (ss) ss->SetNetworkManager(&netMgr);
        }
        //----------------------------------------------------------------------------------
        // Draw
        //----------------------------------------------------------------------------------
        TraceLog(LOG_TRACE, "BeginDrawing() about to run");
        BeginDrawing();

            // Let scene manager draw current scene (GameScene handles its own 3D camera)
            sceneMgr.Draw();

            // ImGui debug overlay
            if (showDebugUI) {
                rlImGuiBegin();
                ImGui::SetNextWindowSize({520, 340}, ImGuiCond_FirstUseEver);
                ImGui::Begin("Debug (F1 to toggle)");

                if (ImGui::BeginTabBar("##debugtabs")) {

                    // ── General ──────────────────────────────────────────────
                    if (ImGui::BeginTabItem("General")) {
                        ImGui::Text("Scene: %s", sceneMgr.GetCurrentName().c_str());
                        ImGui::Text("FPS: %d  (%.2f ms/frame)", GetFPS(), GetFrameTime() * 1000.0f);

                        ImGui::SeparatorText("Quick-switch scene");
                        if (ImGui::Button("Menu"))    sceneMgr.SwitchTo("menu");
                        ImGui::SameLine();
                        if (ImGui::Button("Game"))    sceneMgr.SwitchWithTransition("game", 0.5f);
                        ImGui::SameLine();
                        if (ImGui::Button("Loading")) sceneMgr.SwitchTo("loading");
                        if (g_script) {
                            ImGui::SameLine();
                            if (ImGui::Button("Scripted")) sceneMgr.SwitchWithTransition("scripted", 0.5f);
                        }
                        ImGui::EndTabItem();
                    }

                    // ── Player ───────────────────────────────────────────────
                    if (ImGui::BeginTabItem("Player")) {
                        Hotones::Player* p = nullptr;
                        if (auto* gs = dynamic_cast<Hotones::GameScene*>(sceneMgr.GetCurrent()))
                            p = gs->GetPlayer();
                        else if (auto* ss = dynamic_cast<Hotones::ScriptedScene*>(sceneMgr.GetCurrent()))
                            p = ss->GetPlayer();

                        if (p) {
                            Vector3 pos = p->body.position;
                            Vector3 vel = p->body.velocity;
                            float   spd = Vector3Length(vel);

                            ImGui::SeparatorText("Transform");
                            ImGui::Text("Pos:   %.3f, %.3f, %.3f", pos.x, pos.y, pos.z);
                            ImGui::Text("Vel:   %.3f, %.3f, %.3f", vel.x, vel.y, vel.z);
                            ImGui::Text("Speed: %.3f u/s", spd);
                            ImGui::Text("Look:  yaw=%.1f  pitch=%.1f", p->lookRotation.x, p->lookRotation.y);
                            ImGui::Text("Grounded: %s", p->body.isGrounded ? "yes" : "no");

                            ImGui::SeparatorText("Tweaks");
                            bool bhop = p->IsSourceBhopEnabled();
                            if (ImGui::Checkbox("Source Bhop", &bhop))
                                p->SetSourceBhopEnabled(bhop);
                            ImGui::SetItemTooltip("Allows Source-style bunny-hop acceleration exploit");

                            if (auto* gs = dynamic_cast<Hotones::GameScene*>(sceneMgr.GetCurrent())) {
                                bool worldDbg = gs->IsWorldDebug();
                                if (ImGui::Checkbox("World Debug (F2)", &worldDbg))
                                    gs->SetWorldDebug(worldDbg);
                            }
                        } else {
                            ImGui::TextDisabled("No player in the current scene.");
                        }
                        ImGui::EndTabItem();
                    }

                    // ── Network ──────────────────────────────────────────────
                    if (ImGui::BeginTabItem("Network")) {
                        auto mode = netMgr.GetMode();
                        const char* modeStr =
                            mode == Hotones::Net::NetworkManager::Mode::Server ? "Server" :
                            mode == Hotones::Net::NetworkManager::Mode::Client ? "Client" : "None";
                        ImGui::Text("Mode: %s", modeStr);

                        if (mode == Hotones::Net::NetworkManager::Mode::Client) {
                            if (netMgr.IsConnected()) {
                                ImGui::TextColored({0,1,0,1}, "Connected  (local ID %d)", netMgr.GetLocalId());
                            } else {
                                ImGui::TextColored({1,1,0,1}, "Connecting to %s:%d ...",
                                                  connectHost.c_str(), (int)connectPort);
                            }
                        } else if (mode == Hotones::Net::NetworkManager::Mode::Server) {
                            ImGui::TextColored({0,1,0.5f,1}, "Hosting on port %d", (int)serverPort);
                        } else {
                            ImGui::TextDisabled("Offline  (launch with --connect <ip> or --server)");
                        }

                        const auto& remotes = netMgr.GetRemotePlayers();
                        if (!remotes.empty()) {
                            ImGui::SeparatorText("Remote Players");
                            ImGui::BeginChild("##remoteplayers", {0, 120}, ImGuiChildFlags_Borders);
                            for (const auto& [id, rp] : remotes) {
                                if (rp.active)
                                    ImGui::Text("[%2d] %-16s  (%.1f, %.1f, %.1f)",
                                                (int)id, rp.name, rp.posX, rp.posY, rp.posZ);
                            }
                            ImGui::EndChild();
                        }
                        ImGui::EndTabItem();
                    }

                    // ── Audio ────────────────────────────────────────────────
                    if (ImGui::BeginTabItem("Audio")) {
                        ImGui::Text("Sample rate: %d Hz   Channels: %d",
                                    Ho_tones::GetAudioSampleRate(),
                                    Ho_tones::GetAudioChannels());
                        ImGui::Text("Audio device ready: %s", IsAudioDeviceReady() ? "yes" : "no");

                        ImGui::Separator();
                        int vol = Ho_tones::GetSoundBus().GetVolume();
                        if (ImGui::SliderInt("Master Volume", &vol, 0, 100))
                            Ho_tones::GetSoundBus().SetVolume(vol);
                        if (ImGui::Button("Stop All Sounds"))
                            Ho_tones::GetSoundBus().StopAll();
                        ImGui::EndTabItem();
                    }

                    // ── Lua / Pack ───────────────────────────────────────────
                    if (g_script) {
                        if (ImGui::BeginTabItem("Lua")) {
                            const std::string& lerr = g_script->GetLastError();
                            if (lerr.empty()) {
                                ImGui::TextColored({0.4f,1,0.4f,1}, "No errors.");
                            } else {
                                ImGui::TextColored({1,0.4f,0.4f,1}, "Error:");
                                ImGui::Separator();
                                ImGui::BeginChild("##luaerr", {0, 120}, ImGuiChildFlags_Borders);
                                ImGui::TextWrapped("%s", lerr.c_str());
                                ImGui::EndChild();
                                if (ImGui::Button("Copy##luacopy"))  SetClipboardText(lerr.c_str());
                                ImGui::SameLine();
                                if (ImGui::Button("Clear##luaclr"))  g_script->ClearLastError();
                            }
                            ImGui::EndTabItem();
                        }
                    }

                    ImGui::EndTabBar();
                }

                ImGui::End();
                rlImGuiEnd();
            }

        EndDrawing();
        TraceLog(LOG_TRACE, "EndDrawing() finished");
        TraceLog(LOG_DEBUG, "Main loop iteration end — scene=%s", sceneMgr.GetCurrentName().c_str());
        //----------------------------------------------------------------------------------
    }
    // De-Initialization
    //--------------------------------------------------------------------------------------
    // Ensure background pack loader finished before tearing down subsystems
    if (g_packThread.joinable()) {
        TraceLog(LOG_INFO, "Waiting for pack extraction thread to finish");
        g_packThread.join();
        TraceLog(LOG_INFO, "Pack extraction thread joined");
    }
    TraceLog(LOG_INFO, "Shutting down physics subsystem");
    Hotones::Physics::ShutdownPhysics();
    TraceLog(LOG_INFO, "Physics shutdown complete");
    // Shutdown audio before closing the window
    TraceLog(LOG_INFO, "Shutting down audio system");
    Ho_tones::ShutdownAudioSystem();
    TraceLog(LOG_INFO, "Audio shutdown complete");
    if (__startup_log) __startup_log << "shutdown\n";
    rlImGuiShutdown();

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
