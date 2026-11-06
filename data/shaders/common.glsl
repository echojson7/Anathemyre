// Общее для шейдеров: данные кадра (FrameGpu в src/render/vk_state.h), вершина детали, экземпляр, материал.
// Всё читается по адресам буферов (buffer_reference), раскладка scalar — как структуры C++ без дыр
#ifndef COMMON_GLSL
#define COMMON_GLSL
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// Вершина детали — MeshVertex (src/asset/mesh.h), 24 байта
struct Vertex { float px, py, pz; uint normal; uint mats; uint misc; };
// Экземпляр — RenderInstance (src/render/render.h), 48 байт
struct Instance { int cx, cz; float x, y, z; uint rot_xy, rot_zw; float scale; uint mesh, material, seed, pad; };
// Материал — RenderMaterial, 64 байта: p0 = color.rgb + rough, p1 = color2.rgb + texel, p2 = extra
struct Material { uint kind, pad0, pad1, pad2; vec4 p0, p1, p2; };
struct LineVtx { float x, y, z; uint rgba; };
struct PointVtx { float x, y, z, size; uint rgba, pad0, pad1, pad2; };

layout(buffer_reference, scalar) readonly buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) readonly buffer Instances { Instance v[]; };
layout(buffer_reference, scalar) readonly buffer Materials { Material v[]; };
layout(buffer_reference, scalar) readonly buffer Lines { LineVtx v[]; };
layout(buffer_reference, scalar) readonly buffer Points { PointVtx v[]; };
layout(buffer_reference, scalar) readonly buffer Words { uint v[]; };

layout(buffer_reference, scalar) readonly buffer FrameData {
    Vertices vertices;
    Instances instances;
    Materials materials;
    Lines lines;
    Points points;
    Words t_height;     // сетка высот земли, u16 парами (docs/terrain.md)
    Words t_mask;       // маски 32 м, u32: шероховатость, песок, трава, сырость по байту
    Words t_water;      // уровень воды 32 м, u16 парами, 5 см; 0 — воды нет
    ivec2 cam_chunk;
    vec3 cam_off;       // камера внутри своего чанка: x, z в [0, 256), y — высота
    float tan_half_fov;
    vec4 right, up, forward;
    float proj_x, proj_y, near_m, pixel_angle;
    float cell_min, cell_pixels;
    uint pixel, pad1;
    vec4 sun_dir, sun_color, sky_color;
    vec2 viewport, ndc_per_pixel;
    // Земля: как Terrain в src/sim/terrain.h
    int t_chunk0, t_size, t_n, t_nm;
    int t_oct_lp[5];
    int t_oct_amp[5];
    uint t_oct_seed[5];
    uint t_on;
    // Clipmap: кольцо L — сетка (2n+1)² вершин с шагом 0.5·2^L м, начало в полуметрах от угла карты
    int clip_n, clip_levels, clip_morph, clip_pad;
    ivec4 clip[12];
    uint ground_layers[8];  // материалы слоёв: песок, сухая земля, щебень, камень, трава, вода
    vec4 fog;             // временная дымка: цвет (линейный) и 1/расстояние
    int t_mip[8];         // начало уровня k сглаженной сетки высот (шаг 8·2^k м) в буфере высот, номера u16
};

layout(push_constant, scalar) uniform Push {
    FrameData frame;
    uint a, b;
} pc;

const float CHUNK_M = 256.0;

// Точка относительно камеры → клип. Обратная глубина до бесконечности: z = near, w = глубина вдоль взгляда
vec4 to_clip(vec3 rel) {
    FrameData f = pc.frame;
    float vx = dot(rel, f.right.xyz), vy = dot(rel, f.up.xyz), vz = dot(rel, f.forward.xyz);
    return vec4(vx * f.proj_x, -vy * f.proj_y, f.near_m, vz);
}

// Чанк + смещение → метры относительно камеры: сначала целая разность чанков, потом смещения
vec3 rel_to_camera(ivec2 chunk, vec3 off) {
    FrameData f = pc.frame;
    vec2 dc = vec2(chunk - f.cam_chunk) * CHUNK_M;
    return vec3(dc.x + (off.x - f.cam_off.x), off.y - f.cam_off.y, dc.y + (off.z - f.cam_off.z));
}

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Октаэдр (два snorm16) → единичная нормаль
vec3 oct_decode(uint packed) {
    vec2 e = unpackSnorm2x16(packed);
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0.0) n.xz = (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

// sRGB 8 бит → линейный цвет (картинка swapchain *_SRGB сама кодирует обратно)
vec4 unpack_srgb(uint rgba) {
    vec4 c = unpackUnorm4x8(rgba);
    vec3 lo = c.rgb / 12.92;
    vec3 hi = pow((c.rgb + 0.055) / 1.055, vec3(2.4));
    return vec4(mix(hi, lo, lessThanEqual(c.rgb, vec3(0.04045))), c.a);
}
#endif
