// Звук: процедурный банк (синтез при старте, без файлов), микшер в своём потоке,
// затухание и глушение по расстоянию, панорама, эхо, фон мира (ветер, гул, вода, дальние звуки).
// Игра только ставит звуки в очередь и сообщает, где слушатель; поток игры не ждёт микшер.
// Что какой звук значит — docs/sound.md
#ifndef ANA_AUDIO_H
#define ANA_AUDIO_H

#include "../core/arena.h"
#include "../core/math3d.h"

#define AUDIO_RATE 48000

typedef enum Sfx {
    // Шаги: по покрытию под ногой
    SFX_STEP_SAND, SFX_STEP_CONCRETE, SFX_STEP_GRASS, SFX_STEP_WATER,
    // Бой
    SFX_SWISH,        // замах лёгким оружием: свист воздуха
    SFX_SWISH_HEAVY,  // замах тяжёлым: низкий гул
    SFX_CLASH,        // блок: клинок о клинок
    SFX_CLASH_HEAVY,  // блок тяжёлым или о тяжёлое: лязг железа
    SFX_HIT_CUT,      // попадание режущим
    SFX_HIT_BLUNT,    // попадание дробящим
    SFX_HIT_PIERCE,   // укол
    SFX_HIT_ARMOR,    // удар пришёлся в железную броню
    SFX_CRACK,        // перелом
    SFX_BODYFALL,     // тело упало
    SFX_DROP_METAL,   // оружие выпало на землю
    SFX_DRAW,         // достал оружие
    SFX_SHEATH,       // убрал оружие
    SFX_SCUFF,        // уворот, подъём: шарканье и ткань
    // Интерфейс
    SFX_UI_TICK, SFX_UI_ORDER,
    // Дальние звуки мира: стон металла, удар, обрывок радио
    SFX_FAR_GROAN, SFX_FAR_BANG, SFX_FAR_RADIO,
    SFX_COUNT
} Sfx;

// Фон под слушателем; меняется плавно
typedef struct AudioScene {
    f32  wind;    // 0..1+ сила ветра
    f32  hum;     // гул мегаструктур
    f32  room;    // доля эха
    f32  far;     // как часто и громко звучит даль
    bool river;   // рядом вода: river_pos — ближайшая точка русла
    V3   river_pos;
} AudioScene;

// Синтез банка в постоянную арену (~6 МБ, десятки мс)
bool audio_init(Arena *perm);
// Открыть устройство. false — нет звука, остальное работает вхолостую
bool audio_start(void);
void audio_stop(void);
// Звук в мире: gain 0..1+ сверх громкости из таблицы, pitch — множитель высоты, delay — с
void audio_play(u8 sfx, V3 pos, f32 gain, f32 pitch, f32 delay);
// Звук интерфейса: без места, без эха
void audio_play_ui(u8 sfx, f32 gain);
// Раз в кадр: слушатель (позиция, вектор вправо — для панорамы) и фон под ним
void audio_listener(V3 pos, V3 right, const AudioScene *scene);
void audio_volume(f32 master, bool on);
// Дальше звуков с этим номером уже не слышно: не ставить в очередь (дешевле)
f32  audio_max_dist(u8 sfx);
// Вызывает платформа из своего потока; out — frames кадров стерео
void audio_mix(f32 *out, u32 frames);
// Проверка без окна: сценка (ветер, шаги вблизи и вдали, бой, дальние звуки) в WAV и замер микшера
int  audio_demo(const char *wav_path);

#endif
