// Ходьба для вида (docs/people.md, «Ходьба»): поза из клипов покоя, шага и бега по скорости и фазе шага из шага
// мира. Клипы шага и бега — по одному циклу, синхронно по фазе: фаза 0 — касание левой стопы (метки foot)
#pragma once
#include "clip.h"

struct GaitClip {
    const AnimClip *clip;
    i16 map[SKEL_MAX_BONES];
    f32 off;        // доля цикла клипа, где касается левая стопа (фаза 0)
    const AnimLabels *labels;
};

struct GaitSet {
    bool ok;
    i32 trans_bone;  // таз в скелете
    GaitClip idle, walk, run;
    f32 idle_t;      // момент клипа покоя, с
};

// Доли: move — от покоя к ходьбе, run — от шага к бегу (по скорости, м/с)
struct GaitMix {
    f32 move, run;
};

// Привязать клипы к скелету. idle — клип, из которого берётся поза покоя в момент idle_t; walk и run — петли
// с метками стоп. false — клипа нет или скелет не тот
bool gait_bind(GaitSet &g, const Skel &s, const AnimClip *idle, f32 idle_t, const AnimClip *walk, const AnimLabels *walk_l,
               const AnimClip *run, const AnimLabels *run_l);
GaitMix gait_mix(f32 speed);
// Поза: повороты rot (nbones), таз trans; tmp — ещё nbones поворотов под второй клип. contact — касание стоп
// (0 — в воздухе, 1 — на опоре) по меткам главного клипа
void gait_pose(const GaitSet &g, u32 nbones, GaitMix m, u32 phase, Quat *rot, Vec3 *trans, Quat *tmp, f32 contact[2]);
