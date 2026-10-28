# Anathemyre — сборка
# make            релиз (-O2)
# make debug      отладка с ASan/UBSan
# make run        собрать релиз и запустить
# make shot      снять кадр без окна на экране (нужен Xvfb)
# make clean
# WERROR=1       превратить предупреждения в ошибки (так собирает CI)

CC      ?= cc
TARGET  := anathemyre
SRC     := $(shell find src -name '*.c')
WARN    := -Wall -Wextra -Wpedantic -Wshadow
CSTD    := -std=c11
ifeq ($(WERROR),1)
WARN    += -Werror
endif
LDLIBS  := -lX11 -lm -ldl

REL_DIR := build/release
DBG_DIR := build/debug
REL_OBJ := $(SRC:%.c=$(REL_DIR)/%.o)
DBG_OBJ := $(SRC:%.c=$(DBG_DIR)/%.o)

REL_FLAGS := $(CSTD) $(WARN) -O2 -DNDEBUG -MMD -MP
DBG_FLAGS := $(CSTD) $(WARN) -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer -MMD -MP

.PHONY: all release debug run run-debug shot clean

all: release

release: build/$(TARGET)
debug: build/$(TARGET)_debug

build/$(TARGET): $(REL_OBJ)
	$(CC) $(REL_FLAGS) -o $@ $^ $(LDLIBS)

build/$(TARGET)_debug: $(DBG_OBJ)
	$(CC) $(DBG_FLAGS) -o $@ $^ $(LDLIBS)

$(REL_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(REL_FLAGS) -c $< -o $@

$(DBG_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(DBG_FLAGS) -c $< -o $@

run: release
	./build/$(TARGET)

# Утечки внутри GL-драйвера подавлены, наши видны
run-debug: debug
	LSAN_OPTIONS=suppressions=tools/lsan.supp ./build/$(TARGET)_debug

shot: release
	xvfb-run -a -s "-screen 0 1280x720x24" ./build/$(TARGET) --shot build/shot.ppm --frames 120

clean:
	rm -rf build

-include $(REL_OBJ:.o=.d) $(DBG_OBJ:.o=.d)
