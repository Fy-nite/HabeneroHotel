#pragma once

// ── Hotones::Input — C++ convenience API ─────────────────────────────────────
//
// Mirrors the Lua `input.*` table exactly, so the same logic can be written
// identically in C++ or Lua.
//
// Usage example (C++):
//   #include <Input/Input.hpp>
//   using namespace Hotones::Input;
//
//   if (IsKeyPressed(KEY_SPACE)) { /* jump */ }
//   if (IsKeyDown(KEY_W))        { /* move forward */ }
//   Vector2 delta = GetMouseDelta();
//   int ch = GetChar();   // next typed unicode char, 0 when none
//
// The underlying InputHandler::Update() is called once per frame from main.cpp
// before any scene / script update, so all reads within a frame are consistent.

#include <Input/InputHandler.hpp>   // Hotones::Input::InputHandler

namespace Hotones::Input {

// ── Keyboard ──────────────────────────────────────────────────────────────────

inline bool IsKeyDown(int key)           { return InputHandler::Get().IsKeyDown(key); }
inline bool IsKeyPressed(int key)        { return InputHandler::Get().IsKeyPressed(key); }
inline bool IsKeyReleased(int key)       { return InputHandler::Get().IsKeyReleased(key); }
/// True every frame the key is held (first press + OS repeat), same semantics
/// as Lua input.isKeyPressedRepeat().
inline bool IsKeyPressedRepeat(int key)  { return InputHandler::Get().IsKeyPressedRepeat(key); }

// ── Mouse ─────────────────────────────────────────────────────────────────────

inline bool    IsMouseDown(int btn)     { return InputHandler::Get().IsMouseDown(btn); }
inline bool    IsMousePressed(int btn)  { return InputHandler::Get().IsMousePressed(btn); }
inline Vector2 GetMousePos()            { return InputHandler::Get().GetMousePos(); }
inline Vector2 GetMouseDelta()          { return InputHandler::Get().GetMouseDelta(); }
inline float   GetMouseWheel()          { return InputHandler::Get().GetMouseWheel(); }

// ── Typed text ────────────────────────────────────────────────────────────────

/// Dequeue one typed Unicode codepoint this frame.
/// Returns 0 when the queue is empty.  Call in a loop to drain all chars:
///   int ch;
///   while ((ch = Hotones::Input::GetChar()) != 0) { ... }
inline int GetChar()                    { return InputHandler::Get().GetCharPressed(); }

} // namespace Hotones::Input
