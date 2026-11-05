# Новый компьютер: клон, ветка, доступ на push, зависимости

Ветка v2: `claude/project-v2`. Репозиторий: `github.com/echojson7/Anathemyre` (приватный).

Схема: на Windows всё делается внутри WSL2 (Ubuntu). Там живут git, ключ, сборка и нейросеть. Игра
собирается как Windows `.exe` (MinGW) и запускается из WSL на обычном драйвере видеокарты.
На чистом Linux — те же шаги без пункта 1.

## 1. WSL2 (только Windows)
PowerShell от администратора:

    wsl --install -d Ubuntu-24.04

Перезагрузка, задать логин и пароль Ubuntu. Дальше все команды — в окне Ubuntu.
Драйвер видеокарты ставится в самой Windows (свежий NVIDIA/AMD/Intel), в Ubuntu его ставить не нужно.

## 2. Зависимости

    sudo apt update
    sudo apt install -y build-essential clang git make pkg-config \
        libx11-dev libvulkan-dev libvulkan1 vulkan-tools mesa-vulkan-drivers \
        glslang-tools spirv-tools libasound2-dev \
        mingw-w64 python3 python3-numpy xvfb curl \
        cmake ninja-build libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev \
        libxfixes-dev libpulse-dev vulkan-validationlayers

Что зачем: `build-essential`/`clang` — компиляторы и make; `mingw-w64` — запасная сборка Windows `.exe` (основная — llvm-mingw закреплённой версии, его скачивает `tools/run.sh` на этапе 0); `libx11-dev`,
`libvulkan*`, `libasound2-dev` — Linux-версия; `glslang-tools`, `spirv-tools` — шейдеры в SPIR-V и проверка;
`mesa-vulkan-drivers` + `xvfb` — запуск и кадры без окна (lavapipe); `python3-numpy` — конвертер людей;
`cmake`, `ninja-build` и заголовки X11 — только для сборки SDL3 (`tools/third_party.sh`). Компилятор игры —
не `clang` из apt, а закреплённый LLVM и llvm-mingw одной версии, их скачивает `tools/run.sh` (`docs/stack.md`).
Позже для ретаргета захвата движения понадобится Blender 4.x (`sudo apt install -y blender` или с blender.org).
`vulkan-validationlayers` — слои проверки Vulkan для отладочной сборки и `--validation`. По желанию `wine64` —
быстро проверить Windows `.exe` из Linux (`docs/build.md`). Первый `./tools/run.sh` сам скачает компилятор и
соберёт SDL3: несколько минут, дальше — секунды.

## 3. Ключ для push (deploy key — доступ только к этому репозиторию)

    ssh-keygen -t ed25519 -C "anathemyre-pc2" -f ~/.ssh/anathemyre -N ""
    cat ~/.ssh/anathemyre.pub

Скопировать выведенную строку. На GitHub: репозиторий **Anathemyre → Settings → Deploy keys → Add deploy key**,
название `pc2`, вставить ключ, **поставить галочку Allow write access**, Add key.
Затем сказать ssh, каким ключом ходить на GitHub:

    cat >> ~/.ssh/config <<'EOF'
    Host github.com
        IdentityFile ~/.ssh/anathemyre
        IdentitiesOnly yes
    EOF
    chmod 600 ~/.ssh/config
    ssh -T git@github.com

Последняя команда спросит про отпечаток (ответить `yes`) и скажет «Hi echojson7/Anathemyre! You've successfully
authenticated». Ключ даёт доступ только к этому репозиторию; отозвать — удалить его в Deploy keys.

Вместо ключа можно токен: github.com → Settings → Developer settings → Fine-grained tokens → Generate,
Repository access: Only select repositories → Anathemyre, Permissions → Contents: Read and write. Клонировать
тогда по `https://`, токен ввести вместо пароля. Ключ проще и надёжнее, поэтому основной путь — ключ.

## 4. Клон и ветка

    cd ~
    git clone git@github.com:echojson7/Anathemyre.git anathemyre-v2
    cd anathemyre-v2
    git checkout claude/project-v2

Клонировать в домашнюю папку Linux (`~`), а не в `/mnt/c/...`: там медленно и ломаются права и концы строк.

## 5. Настройка git в папке проекта

    git config user.name "lakinsbeast"
    git config user.email "86934594+echojson7@users.noreply.github.com"
    git config pull.ff only
    git config core.hooksPath tools/git-hooks
    git config anathemyre.branch claude/project-v2

`core.hooksPath` включает хук `tools/git-hooks/pre-push`: он пускает push только в ветку v2, без force и
удаления веток. Это защита от ошибки нейросети, а не от злого умысла: хук можно обойти `--no-verify`, и
`AGENTS.md` это запрещает. Почта `...@users.noreply.github.com` — служебная почта GitHub вместо настоящей.

Проверка, что хук работает (должен отказать):

    git push --dry-run origin HEAD:refs/heads/main

## 6. Склад рядом (по желанию, удобно для нейросети)

    git fetch origin stage0/test-room
    git worktree add ../anathemyre-old origin/stage0/test-room

Папка `~/anathemyre-old` — старый проект только для чтения, нейросеть смотрит туда за образцами.

## 7. Нейросеть
Запускать её в терминале Ubuntu в папке `~/anathemyre-v2` (или открыть эту папку в редакторе через WSL), чтобы
она пользовалась этим git, ключом и хуком. Правила она берёт из `AGENTS.md` (Codex, Cursor и большинство
других читают его сами; Claude Code читает `CLAUDE.md`, который отсылает туда же). Если инструмент сам файл не
подхватил, первым сообщением написать: «Прочитай AGENTS.md и следуй ему».

Первое сообщение нейросети, если нужно с чего-то начать:

> Прочитай AGENTS.md, docs/invariants.md, docs/roadmap.md и docs/stack.md. Предложи план этапа 0: какие файлы
> создашь, что возьмёшь из склада (docs/legacy.md), какие решения нужны от меня. Код пока не пиши.

## 8. Запуск игры (с этапа 0)

    cd ~/anathemyre-v2 && ./tools/run.sh
