#include "GFX/LayoutEngine.hpp"

#include <algorithm>

using namespace Hotones::GFX;

// ---------------- LabelElement ----------------
LabelElement::LabelElement(std::string txt, int fontSize, Color col)
    : text(std::move(txt)), fs(fontSize), color(col) {}

UISize LabelElement::Measure(int availW, int /*availH") */) {
    int w = MeasureText(text.c_str(), fs);
    int h = fs + 4;
    return UISize{w, h};
}

void LabelElement::Layout(int x, int y, int w, int h) {
    rect.x = (float)x;
    rect.y = (float)y;
    rect.width = (float)w;
    rect.height = (float)h;
}

void LabelElement::Draw() {
    // Draw at top-left of assigned rect with given font size and color
    DrawText(text.c_str(), (int)rect.x, (int)rect.y, fs, color);
}

// ---------------- SpacerElement ----------------
SpacerElement::SpacerElement(int height)
    : desiredH(height) {}

UISize SpacerElement::Measure(int availW, int /*availH*/) {
    return UISize{availW, desiredH};
}

void SpacerElement::Layout(int x, int y, int w, int h) {
    rect.x = (float)x;
    rect.y = (float)y;
    rect.width = (float)w;
    rect.height = (float)h;
}

void SpacerElement::Draw() {
    // intentionally empty: spacer only reserves space / provides rect
}

// ---------------- LayoutBox ----------------
LayoutBox::LayoutBox(Direction d, int spacing_, int padding_)
    : dir(d), spacing(spacing_), padding(padding_) {}

LayoutBox::~LayoutBox() = default;

void LayoutBox::AddChild(UIElement* child) {
    children.push_back(child);
}

UISize LayoutBox::Measure(int availW, int availH) {
    measured.clear();
    if (children.empty()) return UISize{0,0};

    if (dir == Direction::Vertical) {
        int totalH = padding * 2;
        int maxW = 0;
        int childAvailW = std::max(0, availW - padding * 2);
        for (auto *c : children) {
            UISize s = c->Measure(childAvailW, availH);
            measured.push_back(s);
            totalH += s.h;
            maxW = std::max(maxW, s.w);
        }
        totalH += spacing * (int)children.size() - spacing;
        return UISize{maxW + padding*2, totalH};
    } else {
        int totalW = padding * 2;
        int maxH = 0;
        int childAvailH = std::max(0, availH - padding * 2);
        for (auto *c : children) {
            UISize s = c->Measure(availW, childAvailH);
            measured.push_back(s);
            totalW += s.w;
            maxH = std::max(maxH, s.h);
        }
        totalW += spacing * (int)children.size() - spacing;
        return UISize{totalW, maxH + padding*2};
    }
}

void LayoutBox::Layout(int x, int y, int w, int h) {
    rect.x = (float)x;
    rect.y = (float)y;
    rect.width = (float)w;
    rect.height = (float)h;

    if (children.empty()) return;

    if (dir == Direction::Vertical) {
        int childX = x + padding;
        int childW = std::max(0, w - padding*2);
        int curY = y + padding;
        for (size_t i = 0; i < children.size(); ++i) {
            int ch = measured.size() > i ? measured[i].h : 0;
            children[i]->Layout(childX, curY, childW, ch);
            curY += ch + spacing;
        }
    } else {
        int childY = y + padding;
        int childH = std::max(0, h - padding*2);
        int curX = x + padding;
        for (size_t i = 0; i < children.size(); ++i) {
            int cw = measured.size() > i ? measured[i].w : 0;
            children[i]->Layout(curX, childY, cw, childH);
            curX += cw + spacing;
        }
    }
}

void LayoutBox::Draw() {
    for (auto *c : children) c->Draw();
}
