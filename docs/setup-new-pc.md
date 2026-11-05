# Новый компьютер: клон, ветка, доступ на push, зависимости

Ветка v2: `claude/project-thread-vwjaxp`. Репозиторий: `github.com/echojson7/Anathemyre` (приватный).

Схема: на Windows всё делается внутри WSL2 (Ubuntu). Там живут git, ключ, сборка и нейросеть. Игра
собирается как Windows `.exe` (llvm-mingw) и запускается из WSL на обычном драйвере видеокарты.
На чистом Linux — те же шаги без пунктов 0 и 1.

## 0. Переустановка WSL с нуля (если старая Ubuntu засорилась)
Удаление дистрибутива **стирает весь его диск без возврата**. Сначала сохранить нужное.

В старой Ubuntu проверить, что всё отправлено на GitHub и что ещё лежит в домашней папке:

    cd ~/anathemyre-dev && git status --short && git log --oneline @{u}..HEAD
    du -sh ~/* ~/.[!.]* 2>/dev/null | sort -h | tail -20

Пустой вывод первых двух команд — несохранённой работы нет. Нужные файлы (скачанный захват движения, файлы
Blender, свои заметки) скопировать в Windows: `cp -r ~/папка /mnt/c/Users/<имя>/wsl-backup/`.

Страховка на всякий случай — снимок всего дистрибутива в один файл (занимает место, удалить через неделю):

    wsl --export <Имя> D:\wsl-old.tar

Удаление и установка заново — PowerShell от администратора:

    wsl -l -v
    wsl --shutdown
    wsl --unregister <Имя>
    wsl --update
    wsl --install -d Ubuntu-24.04

`<Имя>` — из первой колонки `wsl -l -v` (обычно `Ubuntu` или `Ubuntu-24.04`). Дальше — с пункта 1, кроме самой
установки. Старый ключ из Deploy keys на GitHub удалить, когда новый заработает (пункт 3).

## 1. WSL2 (только Windows)
PowerShell от администратора:

    wsl --install -d Ubuntu-24.04

Перезагрузка, задать логин и пароль Ubuntu. Дальше все команды — в окне Ubuntu.
Драйвер видеокарты ставится в самой Windows (свежий NVIDIA/AMD/Intel), в Ubuntu его ставить не нужно:
игра идёт как Windows `.exe` на обычном драйвере, а в Ubuntu Vulkan нужен только для lavapipe.

## 2. Зависимости

    sudo apt update && sudo apt upgrade -y
    sudo apt install -y build-essential clang lld llvm git make pkg-config curl unzip gh \
        libvulkan-dev libvulkan1 vulkan-tools vulkan-validationlayers mesa-vulkan-drivers \
        glslang-tools spirv-tools \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
        libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
        mingw-w64 python3 python3-numpy xvfb

Что зачем:
- `clang`, `lld`, `llvm` — основной компилятор, линкер и утилиты; `build-essential` даёт gcc (второй компилятор
  в CI) и make. Закреплённую версию clang и llvm-mingw для Windows `.exe` скачивает `tools/run.sh` на этапе 0;
  `mingw-w64` — запасная сборка `.exe`.
- `libvulkan-dev`, `vulkan-validationlayers` — заголовки, загрузчик и слои проверки Vulkan 1.3 (в Ubuntu 24.04 это
  1.3.275, отдельный Vulkan SDK от LunarG не нужен). VMA и vk-bootstrap — исходники в `third_party/`, не пакеты.
- `glslang-tools`, `spirv-tools` — GLSL → SPIR-V (`glslangValidator`) и проверка `spirv-val`.
- `mesa-vulkan-drivers` + `xvfb` — запуск и кадры без окна на lavapipe; `vulkaninfo --summary` должен
  показать `llvmpipe`.
- `libx11…`, `libwayland…`, `libxkbcommon`, `libdecor`, `libasound2`, `libpulse`, `libpipewire`, `libudev`,
  `libdbus` — заголовки, без которых SDL3 не соберёт окно, ввод, геймпад и звук для Linux-версии.
  SDL3 в Ubuntu 24.04 нет, он собирается из `third_party/`.
- `python3-numpy` — конвертеры; `gh` — смотреть результаты CI из терминала (пункт 3а).

Позже для ретаргета захвата движения понадобится Blender 4.x (с blender.org: в apt Ubuntu 24.04 версия 4.0).
Проверка, что всё на месте:

    clang --version && glslangValidator --version | head -1 && vulkaninfo --summary | grep -i devicename

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

## 3а. gh для CI (по желанию)
Бенчи, повтор бит в бит и кадры гоняет GitHub Actions, их результат удобно смотреть `gh run list` / `gh run view`.
Чтобы не давать gh доступ ко всему аккаунту, вход по ограниченному токену: github.com → Settings → Developer
settings → Fine-grained tokens → Generate, Only select repositories → Anathemyre, Permissions: Actions — Read,
Contents — Read, Pull requests — Read. Затем:

    gh auth login --with-token     # вставить токен, Enter, Ctrl+D
    gh run list -R echojson7/Anathemyre -L 5

## 4. Клон и ветка

    cd ~
    git clone git@github.com:echojson7/Anathemyre.git anathemyre-v2
    cd anathemyre-v2
    git checkout claude/project-thread-vwjaxp

Клонировать в домашнюю папку Linux (`~`), а не в `/mnt/c/...`: там медленно и ломаются права и концы строк.

## 5. Настройка git в папке проекта

    git config user.name "lakinsbeast"
    git config user.email "86934594+echojson7@users.noreply.github.com"
    git config pull.ff only
    git config core.hooksPath tools/git-hooks
    git config anathemyre.branch claude/project-thread-vwjaxp

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
