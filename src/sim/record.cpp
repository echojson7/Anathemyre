#include "record.h"

static const char TAG_HEAD[4] = {'H', 'E', 'A', 'D'};
static const char TAG_COLS[4] = {'C', 'O', 'L', 'S'};
static const char TAG_INTN[4] = {'I', 'N', 'T', 'N'};
static const char TAG_HASH[4] = {'H', 'A', 'S', 'H'};
static const u8 MAGIC[8] = {'A', 'N', 'A', 'R', 'E', 'C', '0', '1'};

void recorder_init(Recorder &r, Arena &arena, const World &w, u64 seed, u64 max_steps, u32 max_intents) {
    r = Recorder{};
    r.seed = seed;
    Column cols[MAX_COLUMNS];
    r.ncols = world_columns(w, cols);
    for (u32 i = 0; i < r.ncols; ++i) r.names[i] = cols[i].name;
    r.intents.init(arena, max_intents);
    r.hashes = arena_push<u64>(arena, max_steps * r.ncols);
    r.max_steps = max_steps;
}

void recorder_step(Recorder &r, const World &w, Span<const Intent> step_intents) {
    if (r.full) return;
    if (r.steps >= r.max_steps || r.intents.count + step_intents.count > r.intents.capacity) {
        r.full = true;
        return;
    }
    for (const Intent &it : step_intents) r.intents.push_back(it);
    world_column_hashes(w, r.hashes + r.steps * r.ncols, r.ncols);
    r.steps++;
}

usize recorder_bytes(const Recorder &r) {
    return 8 + 4 * 16 + 64 + r.ncols * 64 + r.intents.count * 32 + r.steps * r.ncols * 8;
}

void recorder_save(const Recorder &r, Writer &w) {
    put_bytes(w, MAGIC, sizeof MAGIC);
    usize at = chunk_begin(w, TAG_HEAD, 1);
    put_u32(w, SIM_VERSION);
    put_u64(w, r.seed);
    put_u64(w, r.desc_hash);
    put_u64(w, r.steps);
    chunk_end(w, at);

    at = chunk_begin(w, TAG_COLS, 1);
    put_u32(w, r.ncols);
    for (u32 i = 0; i < r.ncols; ++i) {
        usize n = strlen(r.names[i]);
        put_u8(w, (u8)n);
        put_bytes(w, r.names[i], n);
    }
    chunk_end(w, at);

    at = chunk_begin(w, TAG_INTN, 2);
    put_u32(w, (u32)r.intents.count);
    for (usize i = 0; i < r.intents.count; ++i) {
        const Intent &it = r.intents.data[i];
        put_u64(w, it.step);
        put_u32(w, it.kind);
        put_i32(w, it.target.cx);
        put_i32(w, it.target.cz);
        put_f32(w, it.target.x);
        put_f32(w, it.target.y);
        put_f32(w, it.target.z);
        put_u64(w, it.arg);
    }
    chunk_end(w, at);

    at = chunk_begin(w, TAG_HASH, 1);
    put_u64(w, r.steps);
    put_u32(w, r.ncols);
    for (u64 i = 0; i < r.steps * r.ncols; ++i) put_u64(w, r.hashes[i]);
    chunk_end(w, at);
}

bool replay_parse(const u8 *data, usize size, Arena &arena, Replay &out, const char **err) {
    out = Replay{};
    if (size < sizeof MAGIC || memcmp(data, MAGIC, sizeof MAGIC) != 0) { *err = "не файл записи"; return false; }
    Reader r{data + sizeof MAGIC, size - sizeof MAGIC, 0, false};
    bool head = false, cols = false, hashes = false;
    Chunk c;
    while (next_chunk(r, c)) {
        Reader &b = c.body;
        if (chunk_is(c, TAG_HEAD) && c.version == 1) {
            out.sim_version = get_u32(b);
            out.seed = get_u64(b);
            out.desc_hash = get_u64(b);
            out.steps = get_u64(b);
            head = !b.bad;
        } else if (chunk_is(c, TAG_COLS) && c.version == 1) {
            out.ncols = get_u32(b);
            if (out.ncols > MAX_COLUMNS) { *err = "слишком много столбцов"; return false; }
            for (u32 i = 0; i < out.ncols; ++i) {
                u8 n = get_u8(b);
                char *s = arena_push<char>(arena, (usize)n + 1);
                get_bytes(b, s, n);
                out.names[i] = s;
            }
            cols = !b.bad;
        } else if (chunk_is(c, TAG_INTN) && (c.version == 1 || c.version == 2)) {
            // Версия 2 — с числом намерения arg (часы)
            u32 n = get_u32(b);
            if ((u64)n * (c.version == 2 ? 40 : 32) > b.size) { *err = "кусок намерений испорчен"; return false; }
            out.intents = {arena_push<Intent>(arena, n), n};
            for (u32 i = 0; i < n; ++i) {
                Intent &it = out.intents.data[i];
                it.step = get_u64(b);
                it.kind = get_u32(b);
                it.target.cx = get_i32(b);
                it.target.cz = get_i32(b);
                it.target.x = get_f32(b);
                it.target.y = get_f32(b);
                it.target.z = get_f32(b);
                it.arg = c.version == 2 ? get_u64(b) : 0;
            }
            if (b.bad) { *err = "кусок намерений испорчен"; return false; }
        } else if (chunk_is(c, TAG_HASH) && c.version == 1) {
            u64 steps = get_u64(b);
            u32 n = get_u32(b);
            if (n != out.ncols || steps != out.steps || steps * n * 8 > b.size - b.pos) {
                *err = "кусок хешей не сходится с заголовком";
                return false;
            }
            u64 *h = arena_push<u64>(arena, steps * n);
            get_bytes(b, h, steps * n * 8);
            out.hashes = h;
            hashes = !b.bad;
        }
        // незнакомый кусок пропускается
    }
    if (r.bad) { *err = "файл обрезан"; return false; }
    if (!head || !cols || !hashes) { *err = "нет заголовка, столбцов или хешей"; return false; }
    return true;
}
