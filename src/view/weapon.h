// Оружие вида (docs/weapons.md): описания data/weapons.txt (пишет tools/weapons/build.py), сетка на вид и выбор
// оружия человеку по зерну внешности. Только кадр: шаг мира оружия пока не знает, повтор от него не зависит
#pragma once
#include "../core/types.h"

constexpr u32 WEAPONS_MAX = 32;
enum WeaponHands : u8 { WEAPON_ONE = 0, WEAPON_TWO = 1, WEAPON_POLE = 2 };
enum WeaponCarry : u8 { CARRY_HIP_L = 0, CARRY_BACK = 1, CARRY_BACK_POLE = 2 };

// Все длины — по оси сетки (остриё +Y) от кулака верхней руки, м
struct WeaponDesc {
    char name[24];
    u64 hash;       // хеш имени
    u32 mesh;       // сетка weapon_<имя> в рендере (0 — нет файла)
    u8 hands;       // WeaponHands
    u8 carry;       // WeaponCarry
    f32 grip2;      // вторая кисть (0 — одна рука)
    f32 carry_at;   // точка подвеса на теле
    f32 tip, butt;  // остриё (верх) и торец рукояти
    f32 issue;      // вес в выборе по зерну, 0 — не выдаётся
};

struct WeaponTable {
    WeaponDesc w[WEAPONS_MAX];
    u32 count = 0;
    f32 issue_sum = 0.0f;
};

bool weapon_parse(WeaponTable &t, const u8 *text, usize size, char *err, usize err_cap);
// Оружие человека по зерну внешности: номер в таблице, −1 — без оружия (пустая таблица или нет сеток)
i32 weapon_pick(const WeaponTable &t, u32 seed);
