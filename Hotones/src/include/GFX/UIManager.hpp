#pragma once

#include <raylib.h>
#include <string>
#include <memory>
#include <vector>
#include "GFX/LayoutEngine.hpp"

namespace Hotones::GFX {

// ─── UITheme ──────────────────────────────────────────────────────────────────
// Customise the look of every UIManager widget by modifying the active theme.
struct UITheme {
    // Background / panel
    Color bgDark    = {  15,  12,  20, 255 };
    Color bgPanel   = {  25,  22,  35, 230 };

    // Button states
    Color btnNormal = {  55,  35,  85, 255 };
    Color btnHover  = {  85,  55, 125, 255 };
    Color btnPress  = {  35,  15,  55, 255 };
    Color btnBorder = {  75,  55, 105, 255 };

    // Accent / highlight
    Color accent    = { 220,  75, 110, 255 };

    // Text
    Color textDim    = { 155, 145, 175, 255 };
    Color textBright = { 220, 210, 235, 255 };

    // List / table rows
    Color selBg  = {  60,  40, 100, 255 };
    Color rowAlt = {  22,  19,  32, 255 };

    // Grid line colour (for DrawGridBackground)
    Color gridLine = { 28, 24, 40, 255 };

    // Default font sizes
    int fontSizeButton = 18;
    int fontSizeLabel  = 16;
    int fontSizeTitle  = 64;
};

// ─── UIManager ────────────────────────────────────────────────────────────────
// Singleton UI helper that exposes a small widget library.
// Call UIManager::Get() to access it, then draw with Button(), Label(), etc.
// Swap the active theme to restyle everything at once.
class UIManager {
public:
    static UIManager& Get();

    // ── Theme ──────────────────────────────────────────────────────────────────
    UITheme theme;

    // ── Widgets ───────────────────────────────────────────────────────────────
    // Button: returns true on the frame the left mouse button is released over it.
    bool Button(const char* text, Rectangle rect,
                Color bg, Color fg) const;
    // Overload using theme defaults
    bool Button(const char* text, Rectangle rect) const;

    // Label: plain text draw helper
    void Label(const char* text, int x, int y,
               int fs = 0, Color col = {0,0,0,0}) const;

    // Panel: draws a filled + outlined rectangle
    void Panel(Rectangle rect,
               Color fill = {0,0,0,0},
               Color border = {0,0,0,0},
               float borderThick = 2.f) const;

    // GridBackground: draws the tiled grid seen behind the main menu
    void GridBackground(int screenW, int screenH, int spacing = 60) const;

    // Title: centred title text at the top of the screen
    void Title(const char* text, int screenW, int y = 50,
               int fs = 0, Color col = {0,0,0,0}) const;

    // --- Layout integration -------------------------------------------------
    // CreateLabel: owned by UIManager, returns pointer for composition
    Hotones::GFX::UIElement* CreateLabel(const char* text, int fs = 0, Color col = {0,0,0,0});
    // Create a spacer element with given height (reserved area)
    Hotones::GFX::SpacerElement* CreateSpacer(int height = 16);
    // CreateLayout: create a LayoutBox container (owned)
    Hotones::GFX::LayoutBox* CreateLayout(Hotones::GFX::LayoutBox::Direction dir = Hotones::GFX::LayoutBox::Direction::Vertical,
                                          int spacing = 4, int padding = 4);
    // Set the root element to render
    void SetRoot(Hotones::GFX::UIElement* root);
    // Render the root element within the given rectangle (measure, layout, draw)
    void RenderLayout(int x, int y, int w, int h) const;

private:
    UIManager() = default;
    // Owned UIElements
    std::vector<std::unique_ptr<Hotones::GFX::UIElement>> ownedElements;
    Hotones::GFX::UIElement* rootElement = nullptr;
};

} // namespace Hotones::GFX
