// Константы кадра (render.c, GpuFrame): один блок на кадр, у портрета — свой с его камерой
layout(std140, binding = 1) uniform Frame {
    mat4  u_light_vp;
    mat4  u_inv_vp;
    vec3  u_cam_pos;    float u_time;
    vec3  u_sun_dir;    float u_texel_on;    // 0 — пиксельность выключена (RenderSettings.pixel)
    vec3  u_sun_color;  float u_proj_scale;  // пикселей на метр на единичной дальности
    vec3  u_sky_amb;    int   u_frame;
    vec3  u_ground_amb; int   u_noise_tex;   // 1 — шум из текстуры (noise3)
    vec3  u_haze;       int   u_shadow_on;   // 0 — тени выключены (ЭТАП СЦЕНЫ): карта теней не читается
    vec3  u_zenith;     int   u_unlit;       // портреты интерфейса: свой неподвижный свет
    vec4  u_pix;        // размер пикселя k = x * (1/d)^y; m10, m14 проекции (pixel.glsl)
    int   u_post;       // фильтр кадра
    int   u_ao_half;    // AO из половинного разрешения
    int   u_parts;      // бит 0 — объёмные лучи, бит 1 — AO (ЭТАП СЦЕНЫ)
    int   u_frame_pad;
    ivec4 u_tile;       // xy — клетка карты частоты в блоках 4x4 (vrs.frag)
};

// Константы вызова сцены и теней (render.c, ScenePush)
#define u_view_proj mat4(uintBitsToFloat(u_push[0]), uintBitsToFloat(u_push[1]), uintBitsToFloat(u_push[2]), uintBitsToFloat(u_push[3]))
#define u_skinned int(u_push[4].x)    // 0 — статика, 1 — кости, 2 — экземпляры статики, 3 — рельеф, 4 — кости из общего буфера,
                                      // 5 — то же, поза уже в кеше (skin.comp)
#define u_inst_base int(u_push[4].y)
#define u_morphs int(u_push[4].z)     // число морфов сетки
#define u_fade uintBitsToFloat(u_push[5].xy)  // экземпляры редеют с дальности x и пропадают к y (0 — нет)
#define u_terr_grid ivec4(u_push[6])  // xy — узел (0,0) уровня в узлах уровня, z — уровень
#define u_terr_view uintBitsToFloat(u_push[7])  // x — шаг узлов, м; yz — центр колец
