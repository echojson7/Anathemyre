# Склад: старый проект

Старый проект — ветка `stage0/test-room` (на 2026-11-04 последний коммит `2449bca`). Он собирается, работает и
содержит много проверенных решений. Мы его не продолжаем, а берём из него куски.

Смотреть: `git fetch origin stage0/test-room`, затем `git show origin/stage0/test-room:путь` или папка
`../anathemyre-old` (worktree, `docs/setup-new-pc.md`). В складе ничего не менять.

## Брать (с проверкой и уборкой при переносе)
| Что | Где в складе | Зачем в v2 |
|---|---|---|
| Окно и ввод Win32 / X11 | `src/platform/platform_win32.c`, `platform_linux.c`, `platform.h` | этап 0 |
| Звук Win32 / ALSA | `src/platform/audio_*.c` | позже |
| Сборка Windows из WSL, запуск | `Makefile`, `tools/run.sh` | этап 0 |
| Свой `vk.h` из `vk.xml` | `tools/vkgen/` | этап 0 |
| Шейдеры в SPIR-V и сверка | `tools/spv/` | этап 0 |
| Vulkan-бэкенд | `src/render/rb_vk.c`, `rb.c`, `rb_backend.h` | этапы 0–1, как образец, не целиком |
| Арены, RNG, журнал, замер, сериализация, сетка, jobs | `src/core/*` | этап 0 |
| Формат `.mesh` и чтение | `src/core/mesh.c/.h` | этап 1 |
| Люди MakeHuman, 5 LOD, морфы, причёски | `tools/humanconv/` (`fetch.sh`, `humanconv.py`) | этап 1, скелет меняется |
| Пиксельность и стиль кадра | `data/shaders/pixel.glsl`, `style.frag`, `docs/pixels.md` | после этапа 1 |
| Шрифт и его конвертер | `tools/fontconv/`, `data/ui/plex.font` | HUD |
| Реестр людей SoA с поколениями | `src/sim/persons.c/.h`, `docs/people.md` | этап 1–4, как образец |
| Проверка слоёв в CI | `tools/layers.sh`, `docs/code.md` | этап 0 |
| CI | `.github/workflows/build.yml` | этап 0, урезанный |

## Не повторять
- Позы и клипы формулами (`src/sim/anim.c`, `anim_keys.*`, `tools/animbake/keys.c`): результат любительский.
- Бой как отдельный мир арены рядом с общим миром; пять разных бенчей вместо одного `--bench`.
- Сложный реализм раньше играбельного боя: трение поверхностей для ragdoll, лишние режимы света, переключатели-сравнения.
- Файлы по 1500–2100 строк (anim.c, render.c, hud.c): нейросеть читает их кусками и теряет связи.
- Подобие Lumen: лагало и не дало нужной картинки. Свет — по решению пользователя, позже.
- Архитектура, которую каждая сессия достраивает заново: в v2 швы в `docs/invariants.md`, решения в `docs/decisions.md`.

## Документы склада, полезные для чтения
`docs/plan/architecture.md` (разбор и раздел 9 «Решения пользователя»), `docs/performance.md`, `docs/pixels.md`,
`docs/people.md`, `docs/libraries.md`, `docs/vulkan.md`, `docs/combat.md` (что было, а не как надо).
