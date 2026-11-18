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
    GaitClip idle, walk, jog, run;
    f32 len[3];      // длина цикла шага, трусцы и бега по корню клипа, м (рост сетки)
};

// Доли: move — от покоя к ходьбе, jog — от шага к трусце, run — от трусцы к бегу (по скорости, м/с)
struct GaitMix {
    f32 move, jog, run;
};

// Привязать клипы к скелету. idle — петля покоя (стоит, дышит, переносит вес); walk, jog и run — петли по одному
// циклу с метками стоп, один актёр. false — клипа нет или скелет не тот
bool gait_bind(GaitSet &g, const Skel &s, const AnimClip *idle, const AnimClip *walk, const AnimLabels *walk_l,
               const AnimClip *jog, const AnimLabels *jog_l, const AnimClip *run, const AnimLabels *run_l);
GaitMix gait_mix(f32 speed);
// Естественная длина цикла смеси клипов при долях m, м (рост сетки)
f32 gait_natural_len(const GaitSet &g, GaitMix m);
// Поза: повороты rot (nbones), таз trans; tmp и tmp2 — ещё по nbones поворотов под другие клипы. idle_t — время
// покоя человека, с (у каждого своё). contact — касание стоп (0 — в воздухе, 1 — на опоре) по меткам главного клипа
void gait_pose(const GaitSet &g, u32 nbones, GaitMix m, u32 phase, f32 idle_t, Quat *rot, Vec3 *trans, Quat *tmp,
               Quat *tmp2, f32 contact[2]);
