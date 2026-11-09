# Сборка Anathemyre (docs/build.md).
#   make                       Linux, релиз          → build/linux-release/anathemyre
#   make TARGET=windows        Windows .exe          → build/windows-release/anathemyre.exe
#   make MODE=debug            отладка (Linux: ASan + UBSan)
#   make NODEBUG=1             без src/debug (так собирается игра)
#   make COMPILER=gcc          Linux, gcc: только предупреждения и тесты
#   make WERROR=1              предупреждения — ошибки (CI)
#   make check                 слои и запреты; make sim-check — эталонные хеши --sim; make sim-ref — обновить их
#   make compdb                compile_commands.json по исходным файлам
#   make anims                 дорожки клипов из .anim (CI сверяет); make retarget — ретаргет BVH/FBX (bpy)
# Компиляторы и SDL3 скачиваются и собираются сами при первой сборке (tools/toolchain.sh, tools/third_party.sh).

include tools/versions.sh

TARGET   ?= linux
MODE     ?= release
NODEBUG  ?= 0
COMPILER ?= clang
WERROR   ?= 0
ANA_CACHE ?= $(HOME)/.cache/anathemyre

ifeq ($(filter $(TARGET),linux windows),)
$(error TARGET должен быть linux или windows)
endif
ifeq ($(filter $(MODE),release debug),)
$(error MODE должен быть release или debug)
endif

OUT := build/$(TARGET)-$(MODE)$(if $(filter gcc,$(COMPILER)),-gcc)$(if $(filter 1,$(NODEBUG)),-nodebug)
SDL := build/third_party/sdl3-$(TARGET)
# Метка готовой SDL3 с версией в имени: смена версии в tools/versions.sh — новая сборка
SDL_STAMP := $(SDL)/.ok-$(SDL3_VER)

# --- компилятор ---
ifeq ($(TARGET),windows)
  TC_DIR := $(ANA_CACHE)/llvm-mingw-$(LLVM_MINGW_VER)
  CXX := $(TC_DIR)/bin/x86_64-w64-mingw32-clang++
  CC  := $(TC_DIR)/bin/x86_64-w64-mingw32-clang
  EXE := .exe
else ifeq ($(COMPILER),gcc)
  TC_DIR :=
  CXX := g++
  CC  := gcc
  EXE :=
else
  TC_DIR := $(ANA_CACHE)/llvm-$(LLVM_VER)
  CXX := $(TC_DIR)/bin/clang++
  CC  := $(TC_DIR)/bin/clang
  EXE :=
endif
TC_STAMP := $(if $(TC_DIR),$(TC_DIR)/.ok)

# --- флаги ---
# Детерминизм float (docs/invariants.md): одна цель x86-64-v3, без сжатия в FMA, без -ffast-math
ARCH := -march=x86-64-v3 -ffp-contract=off -fno-math-errno
WARN := -Wall -Wextra -Wpedantic -Wshadow $(if $(filter 1,$(WERROR)),-Werror)
# Чужие заголовки — -isystem: их предупреждения не наши
INC  := -isystem third_party -isystem third_party/vulkan/include -isystem $(SDL)/include
DEFS := -DVK_NO_PROTOTYPES $(if $(filter 1,$(NODEBUG)),-DANA_NODEBUG)
CXXFLAGS := -std=c++20 -fno-exceptions -fno-rtti $(ARCH) $(WARN) $(INC) $(DEFS) -MMD -MP
CFLAGS   := -std=c11 $(ARCH) -w $(INC) $(DEFS) -MMD -MP
LDFLAGS  :=
LIBS     := $(SDL)/lib/libSDL3.a

ifeq ($(MODE),debug)
  OPT := -O0 -g3 -DANA_DEBUG
  OPT_TP := -O1 -g
else
  OPT := -O2
  OPT_TP := -O2
endif

ifeq ($(TARGET),windows)
  # Статически: без libc++.dll и libunwind.dll рядом; консоль — журнал виден в терминале WSL
  LDFLAGS += -static
  LIBS += -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 \
          -lsetupapi -lshell32 -ldinput8
  ifeq ($(MODE),debug)
    OPT += -gcodeview
    LDFLAGS += -Wl,--pdb=$(OUT)/anathemyre.pdb
  endif
else
  ifneq ($(COMPILER),gcc)
    # libc++ той же версии LLVM, что и в llvm-mingw, статически
    CXXFLAGS += -stdlib=libc++
    LDFLAGS += -fuse-ld=lld -stdlib=libc++ -static-libstdc++ -l:libc++abi.a
  endif
  LIBS += -lm -ldl -lpthread
  ifeq ($(MODE),debug)
    SAN := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
    OPT += $(SAN)
    LDFLAGS += $(SAN)
  endif
endif

# --- unity по модулю: build/<цель>/unity_<модуль>.cpp включает все .cpp модуля ---
MODULES := core platform asset anim sim view render ui app $(if $(filter 1,$(NODEBUG)),,debug)
$(shell mkdir -p $(OUT)/shaders/gen $(OUT)/data/meshes)
define unity_file
$(shell printf '%s\n' '// создано Makefile, не править' $(foreach f,$(sort $(wildcard src/$(1)/*.cpp)),'#include "$(CURDIR)/$(f)"') > $(OUT)/unity_$(1).tmp; \
  cmp -s $(OUT)/unity_$(1).tmp $(OUT)/unity_$(1).cpp || mv $(OUT)/unity_$(1).tmp $(OUT)/unity_$(1).cpp; rm -f $(OUT)/unity_$(1).tmp)
endef
$(foreach m,$(MODULES),$(eval $(call unity_file,$(m))))

GAME_OBJS := $(patsubst %,$(OUT)/unity_%.o,$(MODULES))
TP_OBJS   := $(OUT)/tp_volk.o $(OUT)/tp_vma.o $(OUT)/tp_vkb.o
BIN       := $(OUT)/anathemyre$(EXE)

SHADER_SRC := $(sort $(wildcard data/shaders/*.vert data/shaders/*.frag data/shaders/*.comp))
SHADER_SPV := $(patsubst data/shaders/%,$(OUT)/shaders/%.spv,$(SHADER_SRC))

# Виды материалов: switch в шейдере и номера видов для игры из одного списка файлов (tools/matgen.sh)
$(shell ./tools/matgen.sh $(OUT))

# Данные рядом с исполняемым файлом: описания и сетки (.mesh пишет tools/meshgen.py, лежат в data/meshes)
DATA_SRC := data/materials.txt data/scene.txt data/sky.txt data/terrain.txt data/ui/plex.font $(sort $(wildcard data/meshes/*.mesh)) \
            $(sort $(wildcard data/dunes/*.dun)) $(sort $(wildcard data/prefabs/*.txt)) $(sort $(wildcard data/people/*.skel)) \
            $(sort $(wildcard data/anims/*.anim data/anims/*.trk data/anims/*.txt))
DATA_OUT := $(patsubst data/%,$(OUT)/data/%,$(DATA_SRC))

.PHONY: all clean check sim-check sim-ref compdb toolchain third_party meshes anims retarget
all: $(BIN) $(SHADER_SPV) $(DATA_OUT)

$(OUT)/data/%: data/%
	@mkdir -p $(dir $@)
	cp $< $@

# Пересобрать детали .mesh (результат лежит в git; CI проверяет, что генератор даёт те же байты).
# meshgen.py пишет сетку одним LOD, meshpack (meshoptimizer) строит LOD и кластеры. Инструмент собирается
# закреплённым clang под Linux при любой TARGET: байты не зависят от того, для какой цели собирается игра
MESHPACK := build/tools/meshpack
MO_SRC := $(sort $(wildcard third_party/meshoptimizer/*.cpp))
HOST_TC := $(ANA_CACHE)/llvm-$(LLVM_VER)
$(MESHPACK): tools/meshpack/meshpack.cpp $(MO_SRC) third_party/meshoptimizer/meshoptimizer.h | $(HOST_TC)/.ok
	@mkdir -p $(dir $@)
	$(HOST_TC)/bin/clang++ -std=c++20 -O2 -march=x86-64-v3 -ffp-contract=off -fno-math-errno -w -isystem third_party \
	  -stdlib=libc++ -fuse-ld=lld -static-libstdc++ -l:libc++abi.a tools/meshpack/meshpack.cpp $(MO_SRC) -o $@
ifneq ($(HOST_TC)/.ok,$(TC_STAMP))
$(HOST_TC)/.ok:
	./tools/toolchain.sh linux
endif
meshes: $(MESHPACK)
	python3 tools/meshgen.py data/meshes $(MESHPACK)
	./tools/humanconv/fetch.sh $(MESHPACK)

# Клипы людей (docs/anim.md). anims — запечённые дорожки .trk и касания стоп из .anim (numpy; CI сверяет байты).
# retarget — сырые BVH CMU в downloads/anims (fetch.sh), ретаргет на оба скелета (нужен pip install bpy), потом anims
anims:
	python3 tools/retarget/bake.py
retarget:
	./tools/retarget/fetch.sh
	python3 tools/retarget/retarget.py
	python3 tools/retarget/bake.py

# Компилятор и SDL3 — до любой компиляции
$(TC_STAMP):
	./tools/toolchain.sh $(TARGET)
$(SDL_STAMP): | $(TC_STAMP)
	./tools/third_party.sh $(TARGET)
toolchain: $(TC_STAMP)
third_party: $(SDL_STAMP)

$(OUT)/unity_%.o: $(OUT)/unity_%.cpp | $(SDL_STAMP)
	$(CXX) $(CXXFLAGS) $(OPT) -c $< -o $@

$(OUT)/tp_volk.o: third_party/volk/volk.c | $(SDL_STAMP)
	$(CC) $(CFLAGS) $(OPT_TP) -c $< -o $@
$(OUT)/tp_vma.o: third_party/vma_impl.cpp | $(SDL_STAMP)
	$(CXX) -std=c++20 -fno-exceptions -fno-rtti $(ARCH) -w $(INC) $(DEFS) -MMD -MP $(OPT_TP) $(filter -stdlib=libc++,$(CXXFLAGS)) -c $< -o $@
$(OUT)/tp_vkb.o: third_party/vk-bootstrap/VkBootstrap.cpp | $(SDL_STAMP)
	$(CXX) -std=c++20 -fno-exceptions -fno-rtti $(ARCH) -w $(INC) $(DEFS) -MMD -MP $(OPT_TP) $(filter -stdlib=libc++,$(CXXFLAGS)) -c $< -o $@

$(BIN): $(GAME_OBJS) $(TP_OBJS) $(SDL_STAMP)
	$(CXX) $(OPT) $(LDFLAGS) $(filter %.o,$^) $(LIBS) -o $@

# Шейдеры: GLSL → SPIR-V для Vulkan 1.3, проверка spirv-val
$(OUT)/shaders/%.spv: data/shaders/%
	glslangValidator -s --target-env vulkan1.3 -I$(OUT)/shaders/gen -Idata/shaders --depfile $@.d -o $@ $<
	@# Пустое правило на каждый заголовок (как -MP у компилятора): удалённый или переименованный .glsl не ломает сборку
	@tr ' ' '\n' < $@.d | sed -n '2,$$s/$$/:/p' >> $@.d
	spirv-val --target-env vulkan1.3 --scalar-block-layout $@

-include $(GAME_OBJS:.o=.d) $(TP_OBJS:.o=.d) $(SHADER_SPV:=.d)

check:
	./tools/layers.sh
	./tools/forbid.sh

# Эталонные хеши --sim (tests/sim_hashes.txt). Правка шага мира, меняющая их, обновляет файл через sim-ref
sim-check: $(BIN) $(DATA_OUT)
	$(BIN) --sim 10000 --seed 1 --hashes $(OUT)/sim_hashes.txt
	diff -u tests/sim_hashes.txt $(OUT)/sim_hashes.txt && echo "sim-check: хеши совпали с эталоном"
sim-ref: $(BIN) $(DATA_OUT)
	$(BIN) --sim 10000 --seed 1 --hashes tests/sim_hashes.txt

compdb:
	./tools/compdb.sh "$(CXX)" "$(CXXFLAGS) $(OPT)"

clean:
	rm -rf $(OUT)
