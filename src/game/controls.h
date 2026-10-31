// Управление: меню НАСТРОЙКИ слева вверху (бывшие F1–F10) и приказы отряду мышью и клавишами
#ifndef ANA_CONTROLS_H
#define ANA_CONTROLS_H

#include "../render/render.h"
#include "bench.h"
#include "camera.h"
#include "hud.h"
#include "world.h"

// Этапы сцены для разбора производительности (docs/performance-stages.md). ПУСТО — только местность:
// рельеф, постройки, вода и мегаструктуры при неподвижном утреннем солнце. Каждый следующий этап
// возвращает одну систему поверх прежних, ПОЛНАЯ — игра как есть
typedef enum SceneStage {
    STAGE_EMPTY,    // местность, прежний простой свет без теней
    STAGE_SHADOWS,  // тени солнца
    STAGE_SKY,      // небо, туман, дымка
    STAGE_RAYS,     // объёмные лучи солнца
    STAGE_AO,       // затенение углов
    STAGE_PIXEL,    // пиксельность по глубине и шейдинг блоками
    STAGE_POST,     // фильтр кадра
    STAGE_FLORA,    // деревья, кусты, трава, камни
    STAGE_LIGHT,    // набор СВЕТ: материалы, кривая, экспозиция, свечение, отражённый свет (после растений: сетка неба с ними)
    STAGE_WORLD,    // шаг мира: люди, отряд, арена, поле боя, отряды вдали (без отрисовки людей)
    STAGE_PEOPLE,   // люди: позы и отрисовка, метки
    STAGE_DAY,      // смена суток, факелы и фонари
    STAGE_FULL,     // звук, интерфейс и портреты: полная сцена
    STAGE_COUNT
} SceneStage;
extern const char *const g_stage_name[STAGE_COUNT];

typedef struct Settings {
    RenderSettings rs;
    bool vsync, fullscreen;
    bool follow;             // камера следует за своим персонажем
    f32  ui_user, ui_user_text;  // ползунки размера интерфейса
    bool sound_ok, sound_on, music_on;
    f32  sound_vol, music_vol;
    // Просьбы меню на этот кадр
    bool travel, humans, bench, bands_test, bench_stages, save, load;
    u32  stage;              // SceneStage
    bool stand, stand_moved;  // стенд фигур: показан; показать ряд stand_row или убрать
    u32  stand_row;
} Settings;

// Наборы группы СВЕТ (docs/lighting.md): от прежнего света к самому полному
typedef enum LightPreset { LIGHT_PRESET_OLD, LIGHT_PRESET_FAST, LIGHT_PRESET_REAL, LIGHT_PRESET_COUNT } LightPreset;
extern const char *const g_light_preset_name[LIGHT_PRESET_COUNT];
void light_preset_apply(RenderSettings *rs, LightPreset p);
// Какой набор совпадает с настройками; LIGHT_PRESET_COUNT — свой
LightPreset light_preset_of(const RenderSettings *rs);

// Настройки рендера с учётом этапа сцены: проходы выше этапа выключены
RenderSettings stage_render(const Settings *s);

// Меню НАСТРОЙКИ. *in — ввод для мира и интерфейса: если мышь занята меню, клики из него убраны
void menu_input(Hud *h, Input *in, Settings *s, World *w, RtsCamera *cam, bool humans_on, const FrameStats *st);
// ПКМ по чужому: выбранные атакуют его. ПКМ по земле: идут туда, бросив бой и погоню.
// Z шаг, X трусца, C бег — выбранным; G — держать строй; H — стоять: бросить приказ, но отбиваться.
// in — сырой ввод (курсор), hin — после меню
void orders_input(const Hud *h, const Input *in, const Input *hin, const HudWorld *hw, bool ui_busy, V3 eye, i32 ww, i32 wh,
                  World *w);

#endif
