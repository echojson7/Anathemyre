// Поза и морфы вершины человека: один и тот же счёт в scene.vert (режимы 1 и 4) и в skin.comp (кеш поз)

// Наборы костей кадра по 24 матрицы; последний слот — внешность (render.h, RENDER_LOOK_SLOT)
layout(std430, binding = 2) readonly buffer BoneBuf { mat4 bones[]; };
// Сдвиги морфов: на вершину и морф три uint — snorm16 пары (dx dy) (dz nx) (ny nz)
layout(std430, binding = 4) readonly buffer MorphBuf { uint morph[]; };
const float MORPH_POS = 0.25, MORPH_NRM = 2.0;  // как в tools/humanconv

// base — первая матрица набора, vid — номер вершины в сетке, moff — начало морфов сетки (в uint);
// bi, bw — кости и веса (вес первой 0 — жёсткая вершина на кости rigid). p — положение с w = 1
void skin_apply(uint base, int morphs, uint vid, uint moff, uvec4 bi, vec4 bw, uint rigid, inout vec4 p, inout vec3 n)
{
    mat4 look = bones[base + 23u];
    for (int k = 0; k < morphs; k++) {
        float w = k < 8 ? look[k >> 2][k & 3] : look[k - 6][3];  // 9-й и 10-й — в свободных look[2].w, look[3].w
        if (w == 0.0) continue;
        uint o = moff + (vid * uint(morphs) + uint(k)) * 3u;
        vec2 a = unpackSnorm2x16(morph[o]), b = unpackSnorm2x16(morph[o + 1u]), c = unpackSnorm2x16(morph[o + 2u]);
        p.xyz += w * MORPH_POS * vec3(a, b.x);
        n += w * MORPH_NRM * vec3(b.y, c);
    }
    mat4 b;
    if (bw.x > 0.0) {
        // Мягкий скиннинг: смесь матриц до 4 костей
        uvec4 k = min(bi, uvec4(23u));
        b = bones[base + k.x] * bw.x + bones[base + k.y] * bw.y + bones[base + k.z] * bw.z + bones[base + k.w] * bw.w;
    } else {
        // Жёсткий: одна кость на вершину
        b = bones[base + min(rigid, 23u)];
    }
    p = b * p;
    n = mat3(b) * n;
}

// Главная кость вершины: от неё поворот квадратов материала
uint skin_main_bone(uvec4 bi, vec4 bw, uint rigid)
{
    return min(bw.x > 0.0 ? bi.x : rigid, 23u);
}
