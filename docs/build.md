# Сборка, компиляторы, CI

## Как запускать
Пользователь: `./tools/run.sh` (подтянуть код, собрать, запустить). В WSL собирается и запускается Windows `.exe`
на родном драйвере видеокарты, на чистом Linux — Linux-версия. Первый запуск качает компилятор (~0.1 ГБ для
Windows, ~2 ГБ архив LLVM для Linux, распаковывается только нужное) и собирает SDL3 — несколько минут.

Нейросеть и CI — через `make` (полный список целей — в шапке `Makefile`):

| Команда | Что |
|---|---|
| `make` / `make TARGET=windows` | релиз Linux / Windows `.exe` в `build/<цель>-<режим>/` |
| `make MODE=debug` | отладка: `-O0 -g3`, на Linux ASan + UBSan, в окне — слои проверки Vulkan |
| `make COMPILER=gcc` | gcc — второй компилятор, только предупреждения и тесты |
| `make NODEBUG=1` | без `src/debug` (так собирается игра, шов 7) |
| `make check` | слои (`tools/layers.sh`) и запреты (`tools/forbid.sh`) |
| `make sim-check` / `make sim-ref` | сверить / обновить эталонные хеши `--sim` в `tests/sim_hashes.txt` |
| `make compdb` | `compile_commands.json` по исходным файлам для clangd |

## Если игра не запускается
- `tools/run.sh` печатает код выхода, если он не 0, и путь к журналу. Журнал — `anathemyre.log` рядом с игрой
  (`build/windows-release/` или `build/linux-release/`), он перезаписывается каждым запуском.
- Падение ловит обработчик (`platform_sdl.cpp`, «Падение»): дописывает строки, которые поток журнала не успел
  записать, строку `падение: что, код, адрес` и стек адресами `модуль+смещение` — в терминал и в журнал. На Windows
  ещё окно с сообщением (кроме CI: там задано `CI`). Под ASan обработчик не ставится, у ASan отчёт подробнее.
- Смещение в `anathemyre.exe` переводится в функцию по сборке того же коммита: `make TARGET=windows MODE=debug`
  и `llvm-addr2line -fe build/windows-debug/anathemyre.exe 0x<смещение + 0x140000000>` (у релиза без `-g` —
  `llvm-objdump -d --start-address=…`, чтобы хотя бы найти функцию).
- Ошибки Vulkan — запуск с `--validation`.
- Стек: главный поток Windows по умолчанию 1 МБ, Linux 8 МБ. После 6а `g_vk = VkState{}` клал на стек временный
  объект ~1 МБ, и игра падала только на Windows («переполнение стека»). Теперь состояние сбрасывается на месте,
  `.exe` получает стек 8 МБ (`-Wl,--stack`), а кадр функции больше 128 КБ — предупреждение сборки
  (`-Wframe-larger-than`). Большое — в арену или в статическую память, не на стек.
- Шейдерам Makefile дописывает пустые правила на каждый включённый `.glsl` (как `-MP` у компилятора): после
  переключения ветки, где заголовка нет, сборка не падает с `No rule to make target …glsl`. Старые ветки без этой
  правки: `rm -f build/*/shaders/*.d` перед сборкой.

## Компиляторы — одна версия LLVM
- Windows: llvm-mingw (clang + libc++ + lld), `.exe` статический, консольный (журнал виден в терминале WSL).
- Linux: официальный выпуск LLVM той же версии, libc++ статически, lld. clang из apt не используется.
- Версии и sha256 — `tools/versions.sh`, качает `tools/toolchain.sh` в `~/.cache/anathemyre` (или `$ANA_CACHE`).
  CI сверяет `clang --version` обеих целей.
- Флаги детерминизма для всего кода: `-march=x86-64-v3 -ffp-contract=off -fno-math-errno`, без `-ffast-math`.
  `-fno-math-errno` только убирает запись `errno`, значения не меняет.

## Сторонние библиотеки
- `third_party/`: Vulkan-Headers (только C-заголовки), volk, VMA, vk-bootstrap — версии в `third_party/VERSIONS.md`,
  собираются нашим Makefile с `-w` (их предупреждения не наши, заголовки подключены через `-isystem`).
- SDL3 — исключение: собирается своим CMake (`tools/third_party.sh`) статически в `build/third_party/sdl3-<цель>/`.
  На Linux только X11, звук ALSA и PulseAudio (грузятся при запуске), остальное выключено, иначе CMake SDL3
  требует dev-пакет на каждую возможность. Метка `.ok-<версия>`: смена версии пересобирает SDL3 и игру.

## Unity по модулю
`build/<цель>/unity_<модуль>.cpp` создаёт Makefile: он включает все `.cpp` модуля (`core`, `platform`, `sim`,
`view`, `render`, `app`, `debug`), `make -j` собирает модули параллельно, зависимости — `-MMD -MP`.
Следствие: `static`-имена внутри модуля должны быть разными. Свои заголовки включаются только относительным
путём (`"../core/mem.h"`): `-Isrc` нет, иначе `src/debug/` перекрывает `<debug/...>` из libstdc++.
Шейдеры `data/shaders/*.vert|frag|comp` → `build/<цель>/shaders/*.spv` (`glslangValidator --depfile`,
`spirv-val --scalar-block-layout`), игра ищет их рядом с исполняемым файлом. Пути `#include`: `data/shaders/` и
`build/<цель>/shaders/gen/` (там `materials.glsl` от `tools/matgen.sh`, `docs/materials.md`).
Данные (`data/materials.txt`, `data/scene.txt`, `data/sky.txt`, `data/terrain.txt`, `data/meshes/*.mesh`, `data/people/*.skel`, `data/prefabs/*.txt`) копируются в `build/<цель>/data/`; туда же
`matgen.sh` пишет `material_kinds.txt`. Без них игра не стартует, и `--sim` тоже (сцена — часть мира).
`make meshes` собирает `build/tools/meshpack` (meshoptimizer, тем же clang под Linux) и пересобирает все `.mesh`
(`tools/meshgen.py` пишет сырую сетку, `meshpack` — сварка, порядок вершин, LOD и кластеры; `docs/mesh.md`).
Людей пересобирает `tools/humanconv/fetch.sh`: качает исходники MakeHuman (~38 МБ, sha256 закреплены) в
`~/.cache/anathemyre/makehuman`, нужен Python 3 с numpy (`apt install python3-numpy`), пишет `data/meshes/human_*.mesh`
и `data/people/*.skel` (`docs/people.md`). Байты одни на numpy 1.26 и 2.x.

## CI (`.github/workflows/ci.yml`)
- **linux** (ubuntu-24.04): слои и запреты; сетки в git совпадают с генератором; сборки релиз, Windows, отладка, gcc, без debug — все с `-Werror`;
  эталонные хеши `--sim` у clang `-O2`, clang `-O0` с ASan и gcc; запись 10 000 шагов и её повтор отладочной
  сборкой. Кешируются компиляторы и SDL3.
- **window** (ubuntu-24.04, параллельно linux): свои сборки релиз и отладка; окно под Xvfb + lavapipe со слоями
  проверки Vulkan: отладка с ASan 30 кадров в окне 640×360 (`--window W H`), GPU-AV 4 кадра, релиз 10 кадров 1600×900 и снимок
  в артефакт `frame`. Ошибка слоя — код выхода 1, `pipefail`, чтобы `tail` его не съедал. Отдельно, потому что
  lavapipe рисует ландшафт около секунды на кадр (300 кадров дважды стоили 10 минут). Ключ кеша свой
  (`tc-window-`): здесь в кеше только Linux-половина.
- **windows** (windows-latest, после linux, не ждёт window): `.exe` (с `shaders/` и `data/`) повторяет запись Linux (`--replay`) и сверяет свои хеши
  `--sim` с эталоном. Wine — только для быстрой проверки у нейросети в контейнере.
- Mesa для сравнения кадров закрепляется образом контейнера по дайджесту, когда появятся кадры в CI (этап 5).
