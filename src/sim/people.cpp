// Люди: рождение, приказ, путь, руление, расталкивание, опора и фаза шага. Числа руления — из беты (инерция
// старта и остановки нравилась пользователю): разгон 3, торможение 6, мягкое торможение к цели 2.5 м/с²
#include "people.h"
#include "../core/hash.h"
#include "ground.h"
#include "people_grid.h"
#include "step.h"

static constexpr f32 CAPSULE_R = 0.3f;
static constexpr f32 CAPSULE_H = 1.8f;
static constexpr f32 ARRIVE_DIST = 0.15f;
static constexpr f32 WAYPOINT_DIST = 0.6f;   // ближе — следующий узел пути
static constexpr f32 ACCEL = 3.0f;           // разгон, м/с²
static constexpr f32 BRAKE = 6.0f;           // торможение, м/с²
static constexpr f32 ARRIVE_DEC = 2.5f;      // мягкое торможение к цели, м/с²
static constexpr f32 LAT_ACC = 4.5f;         // предел бокового ускорения в повороте, м/с²
static constexpr f32 TURN_K = 3.0f;          // руление: рад/с на радиан отклонения
static constexpr f32 TURN_MOVE = 6.0f;       // предел поворота скорости на малом ходу, рад/с
static constexpr f32 TURN_BODY = 5.5f;       // разворот тела на месте, рад/с
static constexpr f32 TURN_TRACK = 10.0f;     // на ходу тело следует за направлением шага, рад/с
static constexpr f32 PIVOT_SPEED = 1.3f;     // темп перед крутым разворотом, м/с
static constexpr f32 GRAVITY = 9.81f;
static constexpr u16 STUCK_REPLAN = 90;      // тиков почти без хода — новый путь
static constexpr u16 STUCK_ARRIVE = 60;      // тиков у самой цели в толпе — дошёл
static constexpr f32 PUSH_SOFT = 0.5f;       // доля перекрытия капсул, которую снимает один шаг

// Капсула против коробок: выше шага до колена — только стопа (круг FOOT_R: ступень впереди не держит, край
// цоколя держит), выше колена — тело r 0.3. Ниже шага коробка — опора, её решает support_at()
static void push_capsule(const Colliders &c, People &p, u32 s) {
    coll_push_circle(c, p.cx[s], p.cz[s], p.x[s], p.z[s], WALK_FOOT_R_M, p.y[s] + STEP_UP_M, p.y[s] + WALK_KNEE_M);
    coll_push_circle(c, p.cx[s], p.cz[s], p.x[s], p.z[s], CAPSULE_R, p.y[s] + WALK_KNEE_M, p.y[s] + CAPSULE_H);
    wpos_rebase(p.cx[s], p.x[s]);
    wpos_rebase(p.cz[s], p.z[s]);
}

void people_push_capsule(World &w, u32 s) { push_capsule(w.coll, w.people, s); }

// Столбцы таблицы на место (сколько на человека) — один список для выделения и для хеша и записи повтора.
// Порядок — порядок столбцов в записи: новый столбец — в конец группы, с новым SIM_VERSION
#define PEOPLE_SLOT_COLUMNS(X)                                                                                     \
    X(gen, 1) X(alive, 1) X(support, 1) X(cx, 1) X(cz, 1) X(x, 1) X(z, 1) X(y, 1) X(vy, 1) X(heading, 1)           \
    X(move_yaw, 1) X(speed, 1) X(gait, 1) X(phase, 1) X(state, 1) X(stuck, 1) X(gcx, 1) X(gcz, 1) X(gx, 1)         \
    X(gz, 1) X(gy, 1) X(path, PATH_MAX) X(path_n, 1) X(path_i, 1) X(path_area, 1) X(path_more, 1) X(queued, 1)     \
    X(seed, 1) X(target, 1) X(act, 1) X(act_t, 1) X(act_yaw, 1) X(act_hit, 1) X(freeze, 1) X(react_t, 1)          \
    X(react_yaw, 1) X(react_part, 1) X(hits, 1) X(react_len, 1) X(react_push, 1) X(skill, 1) X(want, 1)          \
    X(last, 1) X(side, 1) X(place, 1) X(struck, 1) X(free, 1)

template <typename T>
static void people_alloc(Arena &a, T *&col, usize n) { col = arena_push<T>(a, n); }

void people_init(People &p, Arena &arena) {
    const u32 n = PEOPLE_CAPACITY;
#define X(name, per) people_alloc(arena, p.name, n * (per));
    PEOPLE_SLOT_COLUMNS(X)
#undef X
    p.queue = arena_push<u16>(arena, n);
    p.free_count = n;
    for (u32 i = 0; i < n; ++i) p.free[i] = (u16)(n - 1 - i);
    p.live = 0;
    p.q_head = p.q_count = 0;
}

bool people_spawn(World &w, const WPos &at, f32 radius, u32 side) {
    People &p = w.people;
    if (p.free_count == 0) return false;
    // Место: точка в круге; в сетке ходимости — ближайшая ходимая клетка (уровень — самый нижний)
    WPos q = at;
    f32 ox, oz;
    do {
        ox = rng_range(w.rng, -1.0f, 1.0f);
        oz = rng_range(w.rng, -1.0f, 1.0f);
    } while (ox * ox + oz * oz > 1.0f);
    q.x += ox * radius;
    q.z += oz * radius;
    wpos_rebase(q);
    q.y = ground_at(w.terrain, q).height;
    u32 i, j, node;
    i32 ar = walk_area_of(w.walk, q.cx, q.cz, q.x, q.z, &i, &j);
    if (ar >= 0 && walk_node_near(w.walk.area[ar], i, j, q.y, 8, &node)) {
        WPos c = walk_node_pos(w.walk.area[ar], node & ~3u);
        q.cx = c.cx; q.cz = c.cz; q.x = c.x; q.z = c.z;
        q.y = c.y;
    }
    u32 s = p.free[--p.free_count];
    p.gen[s] = (u16)(p.gen[s] + 1);
    if (p.gen[s] == 0) p.gen[s] = 1;
    p.alive[s] = 1;
    Support sup = support_at(w.terrain, w.coll, q.cx, q.cz, q.x, q.z, q.y);
    p.support[s] = sup.kind;
    p.cx[s] = q.cx;
    p.cz[s] = q.cz;
    p.x[s] = q.x;
    p.z[s] = q.z;
    p.y[s] = sup.height;
    p.vy[s] = 0.0f;
    p.heading[s] = p.move_yaw[s] = rng_u32(w.rng);
    p.speed[s] = 0.0f;
    p.gait[s] = GAIT_WALK;
    p.phase[s] = 0;
    p.state[s] = MOVE_STAND;
    p.stuck[s] = 0;
    p.gcx[s] = q.cx; p.gcz[s] = q.cz; p.gx[s] = q.x; p.gz[s] = q.z; p.gy[s] = p.y[s];
    p.path_n[s] = p.path_i[s] = p.path_more[s] = 0;
    p.path_area[s] = PATH_DIRECT;
    p.queued[s] = 0;
    p.seed[s] = rng_u32(w.rng);
    p.target[s] = 0;
    p.act[s] = p.act_hit[s] = p.freeze[s] = p.react_part[s] = 0;
    p.act_t[s] = p.react_t[s] = p.hits[s] = 0;
    p.act_yaw[s] = p.react_yaw[s] = 0;
    p.react_len[s] = 0;
    p.react_push[s] = 0.0f;
    // Навык — из зерна внешности, без лишнего числа RNG: 20–82, у большинства около середины (сумма двух)
    u64 h = hash_mix(p.seed[s] ^ 0x5ce11ull);
    p.skill[s] = (u8)(20 + (h & 31) + ((h >> 8) & 31));
    p.want[s] = p.last[s] = 0;
    p.side[s] = (u8)side;
    p.place[s] = 0;
    p.struck[s] = (u32)w.step;
    p.live++;
    return true;
}

void people_kill(World &w, u32 slot) {
    People &p = w.people;
    ANA_ASSERT(slot < PEOPLE_CAPACITY && p.alive[slot]);
    p.alive[slot] = 0;
    p.free[p.free_count++] = (u16)slot;
    p.live--;
}

static void queue_path(People &p, u32 s) {
    if (p.queued[s]) return;
    if (p.q_count >= PEOPLE_CAPACITY) return;
    p.queue[(p.q_head + p.q_count) % PEOPLE_CAPACITY] = (u16)s;
    p.q_count++;
    p.queued[s] = 1;
}

bool people_valid(const People &p, u32 id, u32 *slot) {
    u32 s = id & 0xFFFFu;
    if (s >= PEOPLE_CAPACITY || !p.alive[s] || p.gen[s] != (u16)(id >> 16)) return false;
    *slot = s;
    return true;
}

void people_order_attack(World &w, u32 id, u32 target_id) {
    People &p = w.people;
    u32 s, t;
    if (!people_valid(p, id, &s) || !people_valid(p, target_id, &t) || s == t) return;
    if (p.target[s] != target_id) people_set_target(w, s, target_id);
}

void people_set_target(World &w, u32 s, u32 target_id) {
    People &p = w.people;
    p.target[s] = target_id;
    p.place[s] = 0;
    p.struck[s] = (u32)w.step;
}

void people_order_place(World &w, u32 id, u32 place) {
    People &p = w.people;
    u32 s;
    if (!people_valid(p, id, &s) || !p.target[s]) return;
    p.place[s] = (u16)(place & (PLACE_RING | PLACE_ANGLE | PLACE_BACK));
}

void people_order_act(World &w, u32 id, u32 act) {
    People &p = w.people;
    u32 s;
    if (!people_valid(p, id, &s) || act > w.actions.count) return;
    p.want[s] = (u8)act;
}

void people_order_go(World &w, u32 id, const WPos &target, u32 gait, bool player) {
    People &p = w.people;
    u32 s;
    if (!people_valid(p, id, &s)) return;
    if (player) {
        p.target[s] = 0;
        p.want[s] = 0;
        p.place[s] = 0;
    }
    WPos t = target;
    wpos_rebase(t);
    p.gcx[s] = t.cx; p.gcz[s] = t.cz; p.gx[s] = t.x; p.gz[s] = t.z; p.gy[s] = t.y;
    p.gait[s] = (u8)(gait < (u32)GAIT_COUNT ? gait : (u32)GAIT_WALK);
    p.state[s] = MOVE_WAIT_PATH;
    p.stuck[s] = 0;
    queue_path(p, s);
}

// Путь человека s к его цели: узлы сетки, если и он, и цель в одной области; иначе — прямо
static u32 plan(World &w, u32 s, StepCtx &ctx) {
    People &p = w.people;
    p.path_n[s] = p.path_i[s] = p.path_more[s] = 0;
    p.path_area[s] = PATH_DIRECT;
    p.state[s] = MOVE_FOLLOW;
    u32 i0, j0, i1, j1, from, to;
    i32 a0 = walk_area_of(w.walk, p.cx[s], p.cz[s], p.x[s], p.z[s], &i0, &j0);
    i32 a1 = walk_area_of(w.walk, p.gcx[s], p.gcz[s], p.gx[s], p.gz[s], &i1, &j1);
    if (a0 < 0 || a0 != a1 || !ctx.step_arena) return 0;
    const WalkArea &a = w.walk.area[a0];
    if (!walk_node_near(a, i0, j0, p.y[s], 6, &from) || !walk_node_near(a, i1, j1, p.gy[s], 16, &to)) return 0;
    WalkSearch ws;
    u32 n = walk_find(a, from, to, *ctx.step_arena, ws, p.path + s * PATH_MAX, PATH_MAX);
    if (ctx.stats) { ctx.stats->paths++; ctx.stats->expanded += ws.expanded; }
    p.path_n[s] = (u8)n;
    p.path_area[s] = (u8)a0;
    // Узел под ногами без выходов (отброс или толпа задвинули на крутое) — прямо к цели, иначе стоял бы там вечно
    if (!n && !ws.reached && ws.expanded <= 1) { p.path_area[s] = PATH_DIRECT; return ws.expanded; }
    if (!n && !ws.reached) { p.state[s] = MOVE_STAND; return ws.expanded; }  // идти некуда: стоит
    // Не дошёл до цели (обрезан или предел поиска) — у последнего узла ищет дальше
    p.path_more[s] = (u8)(!ws.reached || (n == PATH_MAX && p.path[s * PATH_MAX + n - 1] != to));
    return ws.expanded;
}

// Читает: очередь, место и цели людей, сетку ходимости. Пишет: пути, state, очередь
void people_paths(World &w, StepCtx &ctx) {
    People &p = w.people;
    u32 budget = 0;  // раскрыто узлов в этом шаге: больше PATH_STEP_BUDGET — остальные ждут следующего шага
    for (u32 k = 0; k < PATHS_PER_STEP && p.q_count && budget < PATH_STEP_BUDGET; ++k) {
        u32 s = p.queue[p.q_head];
        p.q_head = (p.q_head + 1) % PEOPLE_CAPACITY;
        p.q_count--;
        p.queued[s] = 0;
        if (p.alive[s] && p.state[s] == MOVE_WAIT_PATH) budget += plan(w, s, ctx);
    }
}

static f32 smooth01(f32 x) {
    x = f_clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// Куда идёт человек сейчас: узел пути или сама цель. last — это конец пути
static void current_target(const World &w, u32 s, Vec2 *to, bool *last) {
    const People &p = w.people;
    Vec2 goal = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.gcx[s], p.gcz[s], p.gx[s], p.gz[s]);
    if (p.path_area[s] == PATH_DIRECT || p.path_i[s] >= p.path_n[s]) { *to = goal; *last = !p.path_more[s]; return; }
    WPos n = walk_node_pos(w.walk.area[p.path_area[s]], p.path[s * PATH_MAX + p.path_i[s]]);
    *to = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], n.cx, n.cz, n.x, n.z);
    *last = false;
    if (p.path_i[s] + 1 == p.path_n[s] && !p.path_more[s]) {
        // Последний узел — клетка цели: идти в саму точку приказа, если она рядом с узлом
        Vec2 d = goal - *to;
        if (dot(d, d) < 0.5f) *to = goal;
        *last = true;
    }
}

// Читает: путь, место, курсы, скорость. Пишет: курсы, скорость, место (с выталкиванием из стен), state, stuck
void people_steer(World &w, StepCtx &) {
    People &p = w.people;
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!p.alive[s]) continue;
        if (people_busy(p, s)) {  // действие, стоп или реакция ведут корень сами (action.cpp)
            p.speed[s] = 0.0f;
            continue;
        }
        f32 goal_v = 0.0f;
        if (p.state[s] == MOVE_FOLLOW) {
            Vec2 to;
            bool last;
            current_target(w, s, &to, &last);
            f32 d = length(to);
            // Узел пройден — следующий (у последнего узла пути, который обрезан, — новый путь)
            while (!last && d < WAYPOINT_DIST) {
                if (p.path_i[s] < p.path_n[s]) p.path_i[s]++;
                else { p.state[s] = MOVE_WAIT_PATH; queue_path(p, s); break; }
                current_target(w, s, &to, &last);
                d = length(to);
            }
            if (p.state[s] == MOVE_FOLLOW) {
                if (last && (d < ARRIVE_DIST || (p.stuck[s] > STUCK_ARRIVE && d < 1.5f))) {
                    p.state[s] = MOVE_STAND;
                } else {
                    Turn want = turn_of_dir(to.x, to.y);
                    if (p.speed[s] < 0.3f) p.move_yaw[s] = want;
                    f32 err = turn_diff_rad(p.move_yaw[s], want);
                    f32 off = f_abs(err);
                    f32 vmax = w.gait.speed[p.gait[s]];
                    goal_v = last ? f_min(vmax, f_max(0.4f, f_sqrt(2.0f * ARRIVE_DEC * d))) : vmax;
                    f32 pivot = PIVOT_SPEED * f_clamp(d, 0.3f, 1.0f);
                    f32 t = smooth01((off - 0.4f) / 1.4f);
                    goal_v = goal_v + (f_min(goal_v, pivot) - goal_v) * t;
                    f32 face = f_abs(turn_diff_rad(p.heading[s], p.move_yaw[s]));
                    goal_v *= 1.0f - smooth01((face - 0.6f) / 1.1f);
                    f32 wmax = f_min(TURN_MOVE, LAT_ACC / f_max(p.speed[s], 0.1f));
                    f32 step = f_clamp(err * TURN_K, -wmax, wmax) * SIM_DT;
                    if (f_abs(step) > off) step = err;
                    p.move_yaw[s] = turn_add_rad(p.move_yaw[s], step);
                    // Почти стоит, хотя должен идти, — счётчик; долго — новый путь
                    if (p.speed[s] < 0.2f && face < 0.6f) {
                        if (++p.stuck[s] > STUCK_REPLAN && !last) { p.stuck[s] = 0; p.state[s] = MOVE_WAIT_PATH; queue_path(p, s); }
                    } else if (p.stuck[s]) {
                        p.stuck[s]--;
                    }
                }
            }
        }
        p.speed[s] += f_clamp(goal_v - p.speed[s], -BRAKE * SIM_DT, ACCEL * SIM_DT);
        f32 sn, cs;
        sincos_turn(p.move_yaw[s], &sn, &cs);
        p.x[s] += sn * p.speed[s] * SIM_DT;
        p.z[s] += cs * p.speed[s] * SIM_DT;
        push_capsule(w.coll, p, s);
        // Тело: на ходу смотрит по шагу, на месте разворачивается к цели
        f32 tw = (p.speed[s] < 0.5f ? TURN_BODY : TURN_TRACK) * SIM_DT;
        p.heading[s] = turn_add_rad(p.heading[s], f_clamp(turn_diff_rad(p.heading[s], p.move_yaw[s]), -tw, tw));
    }
}

// Расталкивание капсул: пары по сетке людей 2 м, сдвиги считаются по положению до шага и складываются в порядке
// записей сетки (ни у кого нет форы от номера: оба в паре сдвигаются поровну)
void people_separate(World &w, StepCtx &ctx) {
    People &p = w.people;
    if (!ctx.step_arena || p.live < 2) return;
    Arena &ar = *ctx.step_arena;
    usize mark = arena_mark(ar);
    PeopleGrid &g = *arena_push<PeopleGrid>(ar, 1);
    people_grid_build(p, PEOPLE_GRID_FINE, g, arena_push<PeopleGridEntry>(ar, PEOPLE_CAPACITY));
    f32 *px = arena_push<f32>(ar, PEOPLE_CAPACITY), *pz = arena_push<f32>(ar, PEOPLE_CAPACITY);
    const f32 dmin = 2.0f * CAPSULE_R;
    for (u32 k = 0; k < g.n; ++k) {
        u32 a = g.e[k].slot;
        // Три клетки ряда — ключи подряд: один поиск на ряд, порядок тот же, что клетка за клеткой
        for (i32 dz = -1; dz <= 1; ++dz) {
            const u64 hi = people_grid_key(g, p.cx[a], p.cz[a], p.x[a], p.z[a], 1, dz);
            u32 q = people_grid_find(g, people_grid_key(g, p.cx[a], p.cz[a], p.x[a], p.z[a], -1, dz));
            for (; q < g.n && g.e[q].key <= hi; ++q) {
                u32 b = g.e[q].slot;
                if (b <= a) continue;
                if (f_abs(p.y[a] - p.y[b]) > CAPSULE_H * 0.8f) continue;
                Vec2 d = wpos_delta_xz(p.cx[a], p.cz[a], p.x[a], p.z[a], p.cx[b], p.cz[b], p.x[b], p.z[b]);
                f32 d2 = dot(d, d);
                if (d2 >= dmin * dmin) continue;
                f32 len = f_sqrt(d2);
                Vec2 dir = len > 1e-4f ? d * (1.0f / len) : Vec2{1.0f, 0.0f};
                f32 m = (dmin - len) * 0.5f * PUSH_SOFT;
                px[a] -= dir.x * m; pz[a] -= dir.y * m;
                px[b] += dir.x * m; pz[b] += dir.y * m;
            }
        }
    }
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!p.alive[s] || (px[s] == 0.0f && pz[s] == 0.0f)) continue;
        p.x[s] += px[s];
        p.z[s] += pz[s];
        push_capsule(w.coll, p, s);
    }
    arena_restore(ar, mark);
}

// Читает: место, коллайдеры, землю, скорость. Пишет: высоту, опору, vy, фазу шага
void people_settle(World &w, StepCtx &) {
    People &p = w.people;
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!p.alive[s]) continue;
        Support sup = support_at(w.terrain, w.coll, p.cx[s], p.cz[s], p.x[s], p.z[s], p.y[s]);
        p.support[s] = sup.kind;
        if (p.y[s] - sup.height > STEP_UP_M || p.vy[s] < 0.0f) {
            // Падает: опора ниже шага вниз
            p.vy[s] -= GRAVITY * SIM_DT;
            p.y[s] += p.vy[s] * SIM_DT;
            if (p.y[s] <= sup.height) { p.y[s] = sup.height; p.vy[s] = 0.0f; }
        } else {
            p.y[s] = sup.height;
        }
        f32 v = p.speed[s];
        if (v > 0.01f) {
            f32 frac = v * SIM_DT / gait_cycle_len(w.gait, v);
            p.phase[s] += (u32)(i64)(frac * 4294967296.0f);
        }
    }
}

u32 people_columns(const People &p, Column *out) {
    const u32 n = PEOPLE_CAPACITY;
    u32 k = 0;
#define X(name, per) out[k++] = column("people." #name, p.name, n * (per));
    PEOPLE_SLOT_COLUMNS(X)
#undef X
    out[k++] = column("people.free_count", &p.free_count, 1);
    out[k++] = column("people.live", &p.live, 1);
    out[k++] = column("people.queue", p.queue, n);
    out[k++] = column("people.q_head", &p.q_head, 1);
    out[k++] = column("people.q_count", &p.q_count, 1);
    return k;
}
