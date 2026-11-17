// Настройки игрока (docs/ui.md): текстовый файл settings.txt рядом с игрой, «ключ значение» в строке.
// Не часть мира: в хеш и запись повтора не входят
#pragma once
#include "../core/types.h"

enum FpsMode : u32 { FPS_OFF, FPS_SHORT, FPS_FULL, FPS_MODES };

struct Settings {
    // Экран
    bool fullscreen = false;
    bool vsync = true;
    // Интерфейс
    f32 ui_scale = 1.0f;
    f32 text_scale = 1.0f;
    u32 fps = FPS_SHORT;
    // Управление (камера — CameraSettings в src/view/view.h, копируется каждый кадр)
    f32 mouse_sens = 1.0f;
    bool invert_y = false;
    bool invert_x = false;   // мышь вправо — взгляд влево
    f32 cam_smooth = 1.0f;   // плавность камеры: 0 — без сглаживания
    f32 fov_deg = 60.0f;
    // Графика (docs/light.md, «Ступени»): RENDER_LIGHT_* — низко, средне, высоко, ультра
    u32 light_quality = 2;
    u32 aa = 2;               // сглаживание (docs/aa.md): RENDER_AA_* — выкл, FXAA, TAA
    // Отладка
    bool debug = true;        // отладочные клавиши и панель
    bool debug_panel = false; // окна отладки (F3): отладка слева, показатели справа
    bool debug_metrics = true; // показатели справа от отладки (кнопка в окне отладки)
    bool lines = true;        // отладочные отрезки и точки (L)
};

// Путь файла настроек: рядом с исполняемым файлом
const char *settings_path();
// Нет файла — значения по умолчанию, незнакомые ключи пропускаются, числа зажимаются в допустимые пределы
void settings_load(Settings &s, const char *path);
bool settings_save(const Settings &s, const char *path);
