#pragma once

#include <raylib.h>
#include <string>
#include <vector>

namespace Hotones::GFX {

// Simple measured size
struct UISize { int w = 0; int h = 0; };

// UIElement: minimal interface for measure/layout/draw
class UIElement {
public:
    virtual ~UIElement() = default;
    virtual UISize Measure(int availW, int availH) = 0;
    virtual void Layout(int x, int y, int w, int h) = 0;
    virtual void Draw() = 0;
};

// LabelElement: draws simple text and reports measured size
class LabelElement : public UIElement {
public:
    LabelElement(std::string txt, int fontSize = 16, Color col = {220,210,235,255});
    UISize Measure(int availW, int availH) override;
    void Layout(int x, int y, int w, int h) override;
    void Draw() override;

    // Expose assigned rectangle for external use
    Rectangle GetRect() const { return rect; }

private:
    std::string text;
    int fs;
    Color color;
    Rectangle rect{0,0,0,0};
};

// SpacerElement: reserves space but doesn't draw (useful for button targets)
class SpacerElement : public UIElement {
public:
    explicit SpacerElement(int height = 16);
    UISize Measure(int availW, int availH) override;
    void Layout(int x, int y, int w, int h) override;
    void Draw() override;
    Rectangle GetRect() const { return rect; }

private:
    int desiredH;
    Rectangle rect{0,0,0,0};
};

// LayoutBox: vertical / horizontal stacking container
class LayoutBox : public UIElement {
public:
    enum class Direction { Vertical, Horizontal };

    LayoutBox(Direction d = Direction::Vertical, int spacing = 4, int padding = 4);
    ~LayoutBox();

    void AddChild(UIElement* child);

    UISize Measure(int availW, int availH) override;
    void Layout(int x, int y, int w, int h) override;
    void Draw() override;

private:
    Direction dir;
    int spacing;
    int padding;
    std::vector<UIElement*> children;
    Rectangle rect{0,0,0,0};
    std::vector<UISize> measured;
};

} // namespace Hotones::GFX
