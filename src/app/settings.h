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
    f32 fov_deg = 60.0f;
    // Графика (docs/light.md, «Ступени»): RENDER_LIGHT_* — низко, средне, высоко, ультра
    u32 light_quality = 2;
    // Отладка
    bool debug = true;        // отладочные клавиши и панель
    bool debug_panel = false; // панель отладки (F3)
    bool lines = true;        // отладочные отрезки и точки (L)
    bool pixel = true;        // правило ячейки (P)
};

// Путь файла настроек: рядом с исполняемым файлом
const char *settings_path();
// Нет файла — значения по умолчанию, незнакомые ключи пропускаются, числа зажимаются в допустимые пределы
void settings_load(Settings &s, const char *path);
bool settings_save(const Settings &s, const char *path);
