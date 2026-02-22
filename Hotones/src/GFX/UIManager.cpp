#include <GFX/UIManager.hpp>
#include <raylib.h>
#include <memory>
#include "GFX/LayoutEngine.hpp"

namespace Hotones::GFX {

UIManager& UIManager::Get() {
    static UIManager instance;
    return instance;
}

// --- Layout integration -----------------------------------------------------
Hotones::GFX::UIElement* UIManager::CreateLabel(const char* text, int fs, Color col) {
    if (fs == 0) fs = theme.fontSizeLabel;
    if (col.a == 0 && col.r == 0 && col.g == 0 && col.b == 0) col = theme.textDim;
    auto lbl = std::make_unique<Hotones::GFX::LabelElement>(std::string(text), fs, col);
    Hotones::GFX::UIElement* ptr = lbl.get();
    ownedElements.push_back(std::move(lbl));
    return ptr;
}

Hotones::GFX::LayoutBox* UIManager::CreateLayout(Hotones::GFX::LayoutBox::Direction dir, int spacing, int padding) {
    auto box = std::make_unique<Hotones::GFX::LayoutBox>(dir, spacing, padding);
    Hotones::GFX::LayoutBox* ptr = box.get();
    ownedElements.push_back(std::move(box));
    return ptr;
}

Hotones::GFX::SpacerElement* UIManager::CreateSpacer(int height) {
    auto sp = std::make_unique<Hotones::GFX::SpacerElement>(height);
    Hotones::GFX::SpacerElement* ptr = static_cast<Hotones::GFX::SpacerElement*>(sp.get());
    ownedElements.push_back(std::move(sp));
    return ptr;
}

void UIManager::SetRoot(Hotones::GFX::UIElement* root) {
    rootElement = root;
}

void UIManager::RenderLayout(int x, int y, int w, int h) const {
    if (!rootElement) return;
    Hotones::GFX::UISize s = rootElement->Measure(w, h);
    rootElement->Layout(x, y, w, h);
    rootElement->Draw();
}

// ── Button ────────────────────────────────────────────────────────────────────
bool UIManager::Button(const char* text, Rectangle rect, Color bg, Color fg) const {
    Vector2 mp  = GetMousePosition();
    bool over   = CheckCollisionPointRec(mp, rect);
    bool press  = over && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool click  = over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    Color col  = press ? theme.btnPress : (over ? theme.btnHover : bg);
    Color bord = over  ? theme.accent   : theme.btnBorder;

    DrawRectangleRec(rect, col);
    DrawRectangleLinesEx(rect, 2.f, bord);

    int fs = theme.fontSizeButton;
    int tw = MeasureText(text, fs);
    DrawText(text,
             (int)(rect.x + (rect.width  - tw) * 0.5f),
             (int)(rect.y + (rect.height - fs) * 0.5f),
             fs, over ? WHITE : fg);
    return click;
}

bool UIManager::Button(const char* text, Rectangle rect) const {
    return Button(text, rect, theme.btnNormal, theme.textBright);
}

// ── Label ─────────────────────────────────────────────────────────────────────
void UIManager::Label(const char* text, int x, int y, int fs, Color col) const {
    if (fs  == 0)                     fs  = theme.fontSizeLabel;
    if (col.a == 0 && col.r == 0 &&
        col.g == 0 && col.b == 0)     col = theme.textDim;
    DrawText(text, x, y, fs, col);
}

// ── Panel ─────────────────────────────────────────────────────────────────────
void UIManager::Panel(Rectangle rect, Color fill, Color border, float borderThick) const {
    if (fill.a == 0   && fill.r == 0   && fill.g == 0   && fill.b == 0)
        fill   = theme.bgPanel;
    if (border.a == 0 && border.r == 0 && border.g == 0 && border.b == 0)
        border = theme.accent;
    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, borderThick, border);
}

// ── GridBackground ────────────────────────────────────────────────────────────
void UIManager::GridBackground(int sw, int sh, int spacing) const {
    ClearBackground(theme.bgDark);
    for (int x = 0; x < sw; x += spacing) DrawLine(x, 0, x, sh, theme.gridLine);
    for (int y = 0; y < sh; y += spacing) DrawLine(0, y, sw, y, theme.gridLine);
}

// ── Title ─────────────────────────────────────────────────────────────────────
void UIManager::Title(const char* text, int sw, int y, int fs, Color col) const {
    if (fs  == 0)                     fs  = theme.fontSizeTitle;
    if (col.a == 0 && col.r == 0 &&
        col.g == 0 && col.b == 0)     col = theme.accent;
    DrawText(text, (sw - MeasureText(text, fs)) / 2, y, fs, col);
}

} // namespace Hotones::GFX
