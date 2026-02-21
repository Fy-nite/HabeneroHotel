#include <Input/InputHandler.hpp>
#include <raylib.h>

namespace Hotones::Input {

InputHandler& InputHandler::Get()
{
    static InputHandler instance;
    return instance;
}

void InputHandler::Update()
{
    // Collect typed characters into our queue
    int ch;
    while ((ch = ::GetCharPressed()) != 0) {
        chars_.push_back(ch);
    }

    mouseDelta_ = ::GetMouseDelta();
    mousePos_ = ::GetMousePosition();
    mouseWheel_ = ::GetMouseWheelMove();
}

bool InputHandler::IsKeyDown(int key) const { return ::IsKeyDown(key); }
bool InputHandler::IsKeyPressed(int key) const { return ::IsKeyPressed(key); }
bool InputHandler::IsKeyReleased(int key) const { return ::IsKeyReleased(key); }
bool InputHandler::IsKeyPressedRepeat(int key) const { return ::IsKeyPressedRepeat(key); }

bool InputHandler::IsMouseDown(int btn) const { return ::IsMouseButtonDown(btn); }
bool InputHandler::IsMousePressed(int btn) const { return ::IsMouseButtonPressed(btn); }

Vector2 InputHandler::GetMousePos() const { return mousePos_; }
Vector2 InputHandler::GetMouseDelta() const { return mouseDelta_; }
float   InputHandler::GetMouseWheel() const { return mouseWheel_; }

int InputHandler::GetCharPressed()
{
    if (chars_.empty()) return 0;
    int c = chars_.front();
    chars_.pop_front();
    return c;
}

} // namespace Hotones::Input
