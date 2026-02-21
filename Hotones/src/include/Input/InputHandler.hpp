#pragma once

#include <raylib.h>
#include <deque>

namespace Hotones::Input {

class InputHandler {
public:
    static InputHandler& Get();

    // Called once per frame to sample/collect input
    void Update();

    bool IsKeyDown(int key) const;
    bool IsKeyPressed(int key) const;
    bool IsKeyReleased(int key) const;
    bool IsKeyPressedRepeat(int key) const;

    bool IsMouseDown(int btn) const;
    bool IsMousePressed(int btn) const;

    Vector2 GetMousePos() const;
    Vector2 GetMouseDelta() const;
    float   GetMouseWheel() const;

    // Typed character queue (returns 0 when empty)
    int GetCharPressed();

private:
    InputHandler() = default;

    std::deque<int> chars_;
    Vector2 mousePos_{0,0};
    Vector2 mouseDelta_{0,0};
    float   mouseWheel_{0.0f};
};

} // namespace Hotones::Input
