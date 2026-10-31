---
name: anathemyre-materials-impact
description: Шаг 2а (PR #111 влит 2026-10-31): impact у материала, удары физики в кольцо Phys.hit, одно трение/отскок на мир — что нужно 2б и 2в
metadata:
  type: project
  modified: 2026-10-31T15:05:00.693Z
---
Шаг 2а мастер-плана закрыт: PR #111 влит в stage0/test-room 2026-10-31 15:05 (0c5097b).

- Ключ `impact metal|wood|stone|soft` у материала (data/defs/materials.txt), обязателен, наследуется через parent. `MatDef.impact`, `enum Impact { IMP_METAL, IMP_WOOD, IMP_STONE, IMP_SOFT }` и `mat_impact(const MatDef*)` в src/defs/material.h. Звук по классу — `impact_sfx(u8)` в src/view/sfx.c (SFX_IMPACT_METAL/WOOD/STONE/SOFT в конце enum Sfx).
- Материал вещи для звука сейчас: лом — `scrap_mat(kind)` (src/sim/scrap.c); оружие на земле — g_mat.iron, остальные вещи — g_mat.cloth (функция body_mat в view/sfx.c). Шаг 2в (одна запись item) должен заменить body_mat на материал из описания item и перевести ArmorDef.metal / WeaponDef.voice на материал.
- Физика: `PHYS_FRICTION 0.6`, `PHYS_BOUNCE 0.15` в src/sim/phys.h; PhysDesc без friction/bounce. Удары: кольцо `Phys.hit[PHYS_HITS=64]`, `hit_seq`, PhysHit{pos, speed, user, other|PHYS_GROUND}, порог PHYS_HIT_V 0.4 м/с; не сериализуется. Вид читает в `sfx_phys` (вызов в app/main.c).
- SAVE_VERSION поднят до 6.
- Стекла и плоти в классах нет (плоть — вместе с телами в 3.1).

См. [[anathemyre-master-plan]], [[anathemyre-reuse]], [[anathemyre-sound]], [[anathemyre-physics]].
