// Фоновая музыка: процедурный тёмный эмбиент в потоке звука. Пьеса 2–3 минуты, потом 20–40 с тишины.
// Низкий пэд по аккордам лада, суб-бас, редкие ноты в эхо, плёнка плывёт и шипит.
// Что и зачем — docs/sound.md
#ifndef ANA_MUSIC_H
#define ANA_MUSIC_H

#include "../core/types.h"

void music_init(u32 seed);
// Добавить n кадров в l, r и в посыл эха; green — мягкий лад долины, level — громкость 0..1
void music_mix(f32 *l, f32 *r, f32 *send, u32 n, bool green, f32 level);
// Играет ли сейчас пьеса (или пауза между ними)
bool music_playing(void);
// Начать пьесу через sec секунд (проверка без окна)
void music_cue(f32 sec);

#endif
