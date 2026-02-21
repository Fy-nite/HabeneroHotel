// Single compilation unit that compiles the stb_textedit implementation
// against our StbTextBuf string type and provides the StbTextField class.

#include <GFX/StbTextField.hpp>
#include <Input/Input.hpp>
#include <cstring>
#include <raylib.h>

// ─── stb_textedit backend helpers ─────────────────────────────────────────────
// These must be defined as plain C functions before STB_TEXTEDIT_IMPLEMENTATION.

static bool StbTF_Insert(StbTextBuf* o, int pos, const char* c, int n) {
    if (o->len + n >= StbTextBuf::MAX) return false;
    memmove(o->chars + pos + n, o->chars + pos, o->len - pos);
    memcpy(o->chars + pos, c, n);
    o->len += n;
    o->chars[o->len] = '\0';
    return true;
}

static void StbTF_LayoutRow(StbTexteditRow* r, StbTextBuf* /*o*/, int /*n*/) {
    // Single-line widget: report one infinitely wide row.
    r->x0               = 0.f;
    r->x1               = 100000.f;
    r->baseline_y_delta = 1.f;
    r->ymin             = 0.f;
    r->ymax             = 1.f;
    r->num_chars        = 32767;
}

// ─── stb_textedit macro configuration ────────────────────────────────────────
#define STB_TEXTEDIT_STRING                   StbTextBuf
#define STB_TEXTEDIT_STRINGLEN(o)             ((o)->len)
#define STB_TEXTEDIT_GETCHAR(o, i)            ((o)->chars[i])
#define STB_TEXTEDIT_NEWLINE                  '\n'
#define STB_TEXTEDIT_DELETECHARS(o, pos, n)   \
    do { memmove((o)->chars+(pos), (o)->chars+(pos)+(n), (o)->len-(pos)-(n)); \
         (o)->len -= (n); (o)->chars[(o)->len] = '\0'; } while(0)
#define STB_TEXTEDIT_INSERTCHARS(o, pos, c, n) StbTF_Insert((o),(pos),(c),(n))
#define STB_TEXTEDIT_GETWIDTH(o, n, i)         8.0f
#define STB_TEXTEDIT_LAYOUTROW(r, o, n)        StbTF_LayoutRow((r),(o),(n))
#define STB_TEXTEDIT_KEYTOTEXT(k)              ((k) >= 32 && (k) < 127 ? (k) : -1)

// Key codes: high bits avoid collision with printable ASCII.
#define STB_TEXTEDIT_K_SHIFT      0x10000
#define STB_TEXTEDIT_K_LEFT       0x20000
#define STB_TEXTEDIT_K_RIGHT      0x20001
#define STB_TEXTEDIT_K_UP         0x20002
#define STB_TEXTEDIT_K_DOWN       0x20003
#define STB_TEXTEDIT_K_LINESTART  0x20004
#define STB_TEXTEDIT_K_LINEEND    0x20005
#define STB_TEXTEDIT_K_TEXTSTART  0x20006
#define STB_TEXTEDIT_K_TEXTEND    0x20007
#define STB_TEXTEDIT_K_DELETE     0x20008
#define STB_TEXTEDIT_K_BACKSPACE  0x20009
#define STB_TEXTEDIT_K_UNDO       0x2000A
#define STB_TEXTEDIT_K_REDO       0x2000B
#define STB_TEXTEDIT_K_PGUP       0x2000C
#define STB_TEXTEDIT_K_PGDOWN     0x2000D

// Compile the implementation in this TU only.
// INCLUDE_STB_TEXTEDIT_H is already set by the header include above, so the
// struct declarations are skipped and only the #ifdef STB_TEXTEDIT_IMPLEMENTATION
// block is compiled.
#define STB_TEXTEDIT_IMPLEMENTATION
#include <stb_textedit.h>

// ─── StbTextField implementation ─────────────────────────────────────────────

StbTextField::StbTextField(const char* initial) {
    stb_textedit_initialize_state(&state, 1 /*single-line*/);
    if (initial && *initial) buf.set(initial);
}

void StbTextField::setText(const char* s) {
    buf.set(s ? s : "");
    stb_textedit_initialize_state(&state, 1);
}

void StbTextField::UpdateInput() {
    bool ctrl  = Hotones::Input::IsKeyDown(KEY_LEFT_CONTROL) || Hotones::Input::IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = Hotones::Input::IsKeyDown(KEY_LEFT_SHIFT)   || Hotones::Input::IsKeyDown(KEY_RIGHT_SHIFT);

    auto feedKey = [&](int rlKey, int stbKey) {
        if (Hotones::Input::IsKeyPressed(rlKey) || Hotones::Input::IsKeyPressedRepeat(rlKey)) {
            int k = stbKey | (shift ? STB_TEXTEDIT_K_SHIFT : 0);
            stb_textedit_key(&buf, &state, k);
        }
    };

    feedKey(KEY_LEFT,      STB_TEXTEDIT_K_LEFT);
    feedKey(KEY_RIGHT,     STB_TEXTEDIT_K_RIGHT);
    feedKey(KEY_HOME,      STB_TEXTEDIT_K_LINESTART);
    feedKey(KEY_END,       STB_TEXTEDIT_K_LINEEND);
    feedKey(KEY_DELETE,    STB_TEXTEDIT_K_DELETE);
    feedKey(KEY_BACKSPACE, STB_TEXTEDIT_K_BACKSPACE);

    if (ctrl) {
        if (Hotones::Input::IsKeyPressed(KEY_A)) {
            // Select all
            state.select_start = 0;
            state.select_end   = buf.len;
            state.cursor       = buf.len;
        }
        if (Hotones::Input::IsKeyPressed(KEY_Z)) stb_textedit_key(&buf, &state, STB_TEXTEDIT_K_UNDO);
        if (Hotones::Input::IsKeyPressed(KEY_Y)) stb_textedit_key(&buf, &state, STB_TEXTEDIT_K_REDO);
    }

    // Typed characters
    int ch;
    while ((ch = Hotones::Input::GetChar()) != 0)
        if (ch >= 32 && ch < 127)
            stb_textedit_key(&buf, &state, ch);
}

bool StbTextField::Draw(Rectangle r, bool focused,
                         int fontSize, Color textCol,
                         Color bg, Color border, Color focusBorder) {
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 2.f, focused ? focusBorder : border);

    // Character width approximation for cursor/selection placement
    float cw = (float)fontSize * 0.60f;

    // Selection highlight
    if (focused && state.select_start != state.select_end) {
        int a = state.select_start < state.select_end ? state.select_start : state.select_end;
        int b = state.select_start < state.select_end ? state.select_end   : state.select_start;
        Rectangle sr = { r.x + 8.f + a * cw, r.y + 4.f, (b - a) * cw, r.height - 8.f };
        DrawRectangleRec(sr, {100, 80, 160, 200});
    }

    // Text
    DrawText(buf.chars,
             (int)(r.x + 8),
             (int)(r.y + (r.height - fontSize) * 0.5f),
             fontSize, textCol);

    // Blinking cursor (only when focused and no selection)
    if (focused && state.select_start == state.select_end
        && (int)(GetTime() * 2) % 2 == 0) {
        int cx = (int)(r.x + 8.f + state.cursor * cw);
        DrawLine(cx, (int)(r.y + 5), cx, (int)(r.y + r.height - 5), focusBorder);
    }

    // Click: place cursor
    bool clicked = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mp = GetMousePosition();
        if (CheckCollisionPointRec(mp, r)) {
            clicked = true;
            float relX = mp.x - r.x - 8.f;
            stb_textedit_click(&buf, &state, relX / cw, 0.5f);
        }
    }

    // Drag: extend selection
    if (focused && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 mp = GetMousePosition();
        float relX = mp.x - r.x - 8.f;
        stb_textedit_drag(&buf, &state, relX / cw, 0.5f);
    }

    return clicked;
}
