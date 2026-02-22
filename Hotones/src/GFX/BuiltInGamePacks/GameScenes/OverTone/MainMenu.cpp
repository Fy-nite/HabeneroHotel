// MainMenu for OverTone built-in pack
#include <ECS/ECS.hpp>
#include <ECS/Entity.hpp>
#include <ECS/ComponentPool.hpp>
#include <ECS/Registry.hpp>
#include <ECS/System.hpp>
#include <ECS/Components.hpp>
#include <GFX/UIManager.hpp>
#include <PakRegistry.hpp>
#include <GFX/Renderer.hpp>
#include <Scripting/ScriptBehavior.hpp>
#include <vector>
#include <string>
/*
    OverTone is a 2.5D built in pack based on similar designs to Habo hotel, but with a more Modern focus.
    primarily designed to be the default pack for the game, and to be a template for future pack creators to build on.
*/
using namespace Hotones::ECS;
// Main menu game scene
class OverTone : public Hotones::Scripting::ScriptBehavior {
public:
    void Init()   override { /* load assets */
        // Build main menu UI
        auto& UI = Hotones::GFX::UIManager::Get();
        // Root vertical layout centered later during render
        root = UI.CreateLayout(Hotones::GFX::LayoutBox::Direction::Vertical, 12, 16);

        // Title
        title = UI.CreateLabel("OverTone", UI.theme.fontSizeTitle, UI.theme.accent);
        root->AddChild(title);

        // Spacer before buttons
        root->AddChild(UI.CreateSpacer(24));

        // Buttons: create spacers as targets and track labels
        const char* labels[] = { "Start", "Options", "Quit" };
        for (auto &l : labels) {
            auto *sp = UI.CreateSpacer(48);
            root->AddChild(sp);
            buttonSpacers.push_back(sp);
            buttonLabels.push_back(std::string(l));
            // small spacer between buttons
            root->AddChild(UI.CreateSpacer(8));
        }
    }
    void Update() override { /* game logic  */ }
    void Draw3D() override { /* mesh draws  */
    
    }
    void Draw()   override {
        ClearBackground(BLACK);
        auto& UI = Hotones::GFX::UIManager::Get();

        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        int w = 420;
        int h = sh - 200;
        int x = (sw - w) / 2;
        int y = 100;

        // Render layout into rectangle
        UI.SetRoot(root);
        UI.RenderLayout(x, y, w, h);

        // Draw interactive buttons using the spacer rects
        for (size_t i = 0; i < buttonSpacers.size(); ++i) {
            Rectangle r = buttonSpacers[i]->GetRect();
            // pad horizontal a bit
            Rectangle br = { r.x + 8, r.y, r.width - 16, r.height };
            if (UI.Button(buttonLabels[i].c_str(), br)) {
                // Handle clicks
                if (buttonLabels[i] == "Quit");
                // other actions can be added here
            }
        }
    }

private:
    Hotones::GFX::LayoutBox* root = nullptr;
    Hotones::GFX::UIElement* title = nullptr;
    std::vector<Hotones::GFX::SpacerElement*> buttonSpacers;
    std::vector<std::string> buttonLabels;
};

// Adapter scene that owns an OverTone instance and forwards Scene calls.
class OverTone_SceneAdapter : public Hotones::Scene {
public:
    OverTone_SceneAdapter() = default;
    void Init() override { m_impl.Init(); }
    void Update() override { m_impl.Update(); }
    void Draw3D() override { m_impl.Draw3D(); }
    void Draw() override { m_impl.Draw(); }
    void Unload() override {}

private:
    OverTone m_impl;
};

// Static registration so this built-in registers itself with PakRegistry
namespace {
    struct OverToneRegister {
        OverToneRegister() {
            Hotones::PakRegistry::Get().RegisterBuiltIn("overtone", [](){
                return std::make_unique<OverTone_SceneAdapter>();
            });
        }
    } s_overToneRegister;
}