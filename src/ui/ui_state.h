// Внутреннее состояние интерфейса, общее для файлов ui/. Снаружи ui/ не включать
#pragma once
#include "ui.h"
#include "../render/render.h"

constexpr u32 UI_MAX_SIZES = 64;
constexpr u32 UI_MAX_CODE = 0x2800;  // коды букв в таблице поиска (кириллица, тире, стрелки, минус, ▶ ▼ ✓)
constexpr u32 UI_MAX_FACES = 4;
constexpr u32 UI_MAX_QUADS_CPU = 16384;
constexpr u32 UI_MAX_BLOCKS = 32;

struct UiFontSize {
    i16 px, asc, desc, cap;
};
struct UiGlyph {
    u16 x, y;  // в атласе
    u8 w, h;
    i8 ox, oy;  // от пера и линии строки
    f32 adv;
};
struct UiRect {
    f32 x0, y0, x1, y1;
};
// Пара кернинга: номера букв a, b и поправка шага в 1/1024 кегля
struct UiKern {
    u32 pair;  // a << 16 | b
    i32 k;
};
struct UiFace {
    UiFontSize sizes[UI_MAX_SIZES];
    UiGlyph *glyphs = nullptr;  // nsizes·nchars
    UiKern *kern = nullptr;     // по возрастанию pair
    u32 nkern = 0;
};

struct UiState {
    bool ready = false;
    u32 nfaces = 0, nsizes = 0, nchars = 0;
    UiFace faces[UI_MAX_FACES];
    u16 char_index[UI_MAX_CODE];
    u32 font[UI_FONTS] = {};    // кегль (номер в sizes) для UiFont в этом кадре
    u32 face[UI_FONTS] = {};    // начертание для UiFont

    f32 w = 0, h = 0, s = 1, hair = 1;
    RenderUiQuad quads[UI_MAX_QUADS_CPU];
    u32 count = 0;
    UiRect blocks[UI_MAX_BLOCKS];
    u32 nblocks = 0;

    // Мышь для виджетов
    f32 mx = 0, my = 0;
    bool down = false, pressed = false, released = false;
    u64 active = 0;  // виджет, который тянут (ползунок, полоса прокрутки)
    f32 wheel = 0;   // щелчки колеса за кадр; область прокрутки под мышью забирает их себе

    // Обрезка (область прокрутки): прямоугольники и буквы за её краем не рисуются, виджеты там не ловят мышь
    bool clip_on = false;
    UiRect clip{};

    // Свёрнутый раздел формы: строки не рисуются и не занимают места (при снятии текста — снимаются)
    bool hidden = false;
    // Снятие текста (ui_capture_begin)
    char *cap_buf = nullptr;
    usize cap_cap = 0, cap_len = 0;
    // Подсказка строки под мышью в этом кадре
    const char *tip = nullptr;
    f64 dt = 0;
};
extern UiState g_ui_state;
