# Anathemyre v2 — чистый старт

Anathemyre («Анатемайр») — open-world sandbox на C++20 без движка. Устройство мира в духе Kenshi, картинка
в духе Peripeteia, бой с прицелом на уровень Dragon's Dogma 2. Сеттинг, фракции и контент свои.

Это ветка чистого старта. Запуск: `./tools/run.sh` (`docs/build.md`). Сейчас в игре каркас этапа 0: окно Vulkan,
шаг мира с повтором бит в бит (`docs/sim.md`).

| Файл | О чём |
|---|---|
| [AGENTS.md](AGENTS.md) | правила для нейросети: git, порядок работы, код |
| [docs/vision.md](docs/vision.md) | атмосфера, сеттинг, цель по качеству |
| [docs/stack.md](docs/stack.md) | стек: язык, сборка, графика, ассеты, инструменты |
| [docs/invariants.md](docs/invariants.md) | швы архитектуры и запреты (меняет только пользователь) |
| [docs/architecture.md](docs/architecture.md) | папки, модули, поток данных кадра |
| [docs/roadmap.md](docs/roadmap.md) | этапы вертикального среза и где мы сейчас |
| [docs/decisions.md](docs/decisions.md) | журнал решений одной строкой |
| [docs/legacy.md](docs/legacy.md) | что брать со склада (`stage0/test-room`) и чего не повторять |
| [docs/setup-new-pc.md](docs/setup-new-pc.md) | новый компьютер: клон, ветка, доступ на push, зависимости |
| [docs/build.md](docs/build.md) | сборка, компиляторы, сторонние библиотеки, CI |
| [docs/sim.md](docs/sim.md) | хребет симуляции: таблицы, шаг, выписка, хеш, `--sim`, `--replay`, формат записи |
