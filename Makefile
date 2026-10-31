# Anathemyre — сборка
# make            релиз (-O2)
# make debug      отладка (на Linux с ASan/UBSan)
# make run        собрать релиз и запустить
# make shot       снять кадр без окна на экране (Linux, нужен Xvfb)
# make vkcheck    проверка Vulkan-бэкенда отдельной программой (build/vkcheck, на Linux отладочная с ASan)
# make clean
#
# TARGET_OS=windows  собрать .exe (MinGW-w64): на Windows в MSYS2 выбирается сам,
#                    из Linux/WSL: make TARGET_OS=windows
# WERROR=1           превратить предупреждения в ошибки (так собирает CI)

ifeq ($(OS),Windows_NT)
TARGET_OS ?= windows
else
TARGET_OS ?= linux
endif

TARGET  := anathemyre
# errno математика не ставит: sqrtf и подобные встраиваются одной инструкцией без запасной ветки
CSTD    := -std=c11 -fno-math-errno
WARN    := -Wall -Wextra -Wpedantic -Wshadow
ifeq ($(WERROR),1)
WARN    += -Werror
endif

SRC_COMMON := $(wildcard src/core/*.c src/defs/*.c src/sim/*.c src/ai/*.c src/world/*.c src/io/*.c src/content/*.c src/view/*.c src/app/*.c src/render/*.c src/audio/*.c)

ifeq ($(TARGET_OS),windows)
EXE      := .exe
PLATFORM := src/platform/platform_win32.c
SRC      := $(SRC_COMMON) $(PLATFORM) src/platform/audio_win32.c
LDLIBS   := -lgdi32 -luser32 -lkernel32 -lm
# Стандартный printf с %zu и %llu вместо старого msvcrt
CSTD     += -D__USE_MINGW_ANSI_STDIO=1
SANITIZE :=
ifneq ($(OS),Windows_NT)
CC       := x86_64-w64-mingw32-gcc
endif
else
EXE      :=
PLATFORM := src/platform/platform_linux.c
SRC      := $(SRC_COMMON) $(PLATFORM) src/platform/audio_linux.c
LDLIBS   := -lX11 -lm -ldl -pthread
SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer
endif

REL_DIR := build/obj/$(TARGET_OS)-release
DBG_DIR := build/obj/$(TARGET_OS)-debug
REL_OBJ := $(SRC:%.c=$(REL_DIR)/%.o)
DBG_OBJ := $(SRC:%.c=$(DBG_DIR)/%.o)
REL_BIN := build/$(TARGET)$(EXE)
DBG_BIN := build/$(TARGET)_debug$(EXE)

REL_FLAGS := $(CSTD) $(WARN) -O2 -DNDEBUG -MMD -MP
DBG_FLAGS := $(CSTD) $(WARN) -O0 -g3 $(SANITIZE) -MMD -MP

.PHONY: all release debug run run-debug shot vkcheck clean

all: release

release: $(REL_BIN)
debug: $(DBG_BIN)

$(REL_BIN): $(REL_OBJ)
	$(CC) $(REL_FLAGS) -o $@ $^ $(LDLIBS)

$(DBG_BIN): $(DBG_OBJ)
	$(CC) $(DBG_FLAGS) -o $@ $^ $(LDLIBS)

$(REL_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(REL_FLAGS) -c $< -o $@

$(DBG_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(DBG_FLAGS) -c $< -o $@

run: release
	./$(REL_BIN)

# Утечки внутри GL-драйвера подавлены, наши видны
run-debug: debug
	LSAN_OPTIONS=suppressions=tools/lsan.supp ./$(DBG_BIN)

shot: release
	xvfb-run -a -s "-screen 0 1280x720x24" ./$(REL_BIN) --shot build/shot.ppm --frames 120

VK_SRC := tools/vkcheck/vkcheck.c src/render/rb.c src/render/rb_vk.c src/core/arena.c src/core/clock.c src/core/log.c $(PLATFORM)
VK_OBJ := $(VK_SRC:%.c=$(DBG_DIR)/%.o)
VK_BIN := build/vkcheck$(EXE)

vkcheck: $(VK_BIN)

$(VK_BIN): $(VK_OBJ)
	$(CC) $(DBG_FLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf build

-include $(REL_OBJ:.o=.d) $(DBG_OBJ:.o=.d) $(VK_OBJ:.o=.d)
